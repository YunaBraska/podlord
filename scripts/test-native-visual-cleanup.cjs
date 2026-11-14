#!/usr/bin/env node
'use strict';

// Only the external Docker Engine API is simulated. The real Docker CLI and
// visual Kubernetes runner execute unchanged against a private Unix socket.
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');
const { test } = require('node:test');

const root = path.resolve(__dirname, '..');
const build = process.env.PODLORD_NATIVE_BUILD_DIR;
if (!build) throw new Error('PODLORD_NATIVE_BUILD_DIR must identify the real native test build.');

async function exercise(scenario, mode = 'native-e2e', metadataAudit = false) {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-cleanup-'));
    const socket = path.join(directory, 'docker.sock');
    const trace = [];
    let name = '', exists = false, owned = false, running = false, runner, execAttempts = 0, boundaryFailure, applicationLog = '';
    const server = http.createServer(async (request, response) => {
        try {
        const url = new URL(request.url, 'http://localhost');
        const route = url.pathname.replace(/^\/v\d+\.\d+/, '');
        const json = (status, value) => { response.writeHead(status, { 'Content-Type': 'application/json' }); response.end(JSON.stringify(value)); };
        trace.push({ method: request.method, route });
        if (route === '/_ping') { response.writeHead(200, { 'API-Version': '1.51', 'Docker-Experimental': 'false', 'OSType': 'linux' }); response.end('OK'); return; }
        if (route === '/version') { json(200, { ApiVersion: '1.51', MinAPIVersion: '1.24', Version: 'test-boundary' }); return; }
        if (route === '/containers/json') {
            const filters = JSON.parse(url.searchParams.get('filters') || '{}');
            if (filters.name) {
                name = Object.keys(filters.name)[0].replace(/^\^\//, '').replace(/\$$/, '');
                exists = scenario === 'preexisting';
            }
            json(200, exists ? [{ Id: 'a'.repeat(64), Names: ['/' + name], Labels: owned ? { 'podlord.native.e2e': name } : {} }] : []); return;
        }
        if (route === '/containers/create') {
            const chunks=[];
            for await (const chunk of request) chunks.push(chunk);
            const command=JSON.parse(Buffer.concat(chunks).toString('utf8')).Cmd;
            const audit=command.filter(arg=>arg.startsWith('--kube-apiserver-arg=audit-'));
            assert.deepEqual(audit,metadataAudit?[
                '--kube-apiserver-arg=audit-policy-file=/etc/podlord-metadata-audit-policy.json',
                '--kube-apiserver-arg=audit-log-path=/var/log/podlord-metadata-audit.jsonl',
                '--kube-apiserver-arg=audit-log-mode=blocking',
            ]:[],'Audit must be explicit, owned and Metadata-only');
            name = url.searchParams.get('name'); exists = true;
            owned = scenario !== 'collision';
            if (scenario === 'collision') { json(409, { message: 'The container name is already in use.' }); return; }
            if (scenario === 'lost_reply') { json(500, { message: 'Creation response failed after creation.' }); return; }
            json(201, { Id: 'a'.repeat(64), Warnings: [] }); return;
        }
        if (route === '/images/get') {
            if(metadataAudit){
                const entries=await fs.readdir(path.join(build,'e2e'));
                const run=entries.find(entry=>'podlord-'+entry.toLowerCase().replaceAll('.','-')===name);
                const policy=JSON.parse(await fs.readFile(path.join(build,'e2e',run,'metadata-audit-policy.json'),'utf8'));
                assert.deepEqual(policy,{apiVersion:'audit.k8s.io/v1',kind:'Policy',omitStages:['ResponseStarted','ResponseComplete'],rules:[{level:'Metadata',users:['system:admin']},{level:'None'}]});
            }
            if(scenario==='audit_export_failure'){
                response.writeHead(200,{'Content-Type':'application/x-tar'});
                response.end(Buffer.alloc(1024)); return;
            }
            if (scenario === 'coverage' || scenario === 'application_log') {
                const entries = await fs.readdir(path.join(build, 'e2e'));
                const run = entries.find(entry => 'podlord-' + entry.toLowerCase().replaceAll('.', '-') === name);
                assert.ok(run, 'The real runner must have created its private run directory.');
                const file = path.join(build, 'e2e', run, 'help.profraw');
                const native = process.env.PODLORD_NATIVE_APP || path.join(build, 'podlord-native.app/Contents/MacOS/podlord-native');
                await new Promise((resolve, reject) => {
                    const child = spawn(native, ['--help'], { env: { ...process.env, QT_QPA_PLATFORM: 'offscreen', LLVM_PROFILE_FILE: file }, stdio: ['ignore', 'pipe', 'ignore'] });
                    child.stdout.on('data', bytes => { applicationLog += bytes; });
                    child.once('error', reject); child.once('close', code => code === 0 ? resolve() : reject(new Error('Real application help failed.')));
                });
                assert.ok((await fs.stat(file)).size > 0, 'Real application execution must produce coverage evidence.');
                if (scenario === 'application_log') {
                    assert.ok(applicationLog.length > 0, 'Real application help must produce its own log content.');
                    await fs.writeFile(path.join(build, 'e2e', run, 'native.log'), applicationLog);
                }
            }
            json(500, { message: 'Explicit test failure after successful creation.' }); return;
        }
        if (route === `/containers/${name}/json`) {
            if (scenario === 'inspect_error') { json(500, { message: 'Ownership inspection unavailable.' }); return; }
            json(exists ? 200 : 404, exists ? {
                Id: 'a'.repeat(64), Name: '/' + name,
                Config: { Labels: owned ? { 'podlord.native.e2e': name } : {}, Tty: false },
                State: { Status: running ? 'running' : 'created', Running: running },
                NetworkSettings: { Ports: { '6443/tcp': [{ HostIp: '127.0.0.1', HostPort: '16443' }] } },
                Mounts: [{ Type: 'volume', Name: 'private-test-volume' }]
            } : { message: 'No such container.' }); return;
        }
        if(scenario==='audit_export_failure'&&route===`/containers/${name}/archive`){
            if(request.method==='HEAD'){
                response.writeHead(200,{'X-Docker-Container-Path-Stat':Buffer.from(JSON.stringify({name:path.basename(url.searchParams.get('path')),size:0,mode:2147484141,mtime:'2026-01-01T00:00:00Z',linkTarget:''})).toString('base64')});
                response.end(); return;
            }
            if(request.method==='PUT'){request.resume();request.once('end',()=>{response.writeHead(200);response.end();});return;}
        }
        if(scenario==='audit_export_failure'&&route===`/containers/${name}/start`){running=true;response.writeHead(204);response.end();return;}
        if(scenario==='audit_export_failure'&&route===`/containers/${name}/exec`){
            execAttempts++;
            if(execAttempts===1)response.once('finish',()=>runner.kill('SIGTERM'));
            json(500,{message:'Deliberate owned fixture exec/export failure'});return;
        }
        if (route === `/containers/${name}/logs` || route === `/containers/${'a'.repeat(64)}/logs`) { response.writeHead(200, { 'Content-Type': 'application/vnd.docker.raw-stream' }); response.end(); return; }
        if (request.method === 'DELETE' && route === `/containers/${name}`) {
            if (scenario === 'remove_failure') { json(500, { message: 'Removal unavailable.' }); return; }
            exists = false; response.writeHead(204); response.end(); return;
        }
        if (route === '/volumes') { json(200, { Volumes: [], Warnings: [] }); return; }
        json(500, { message: `Unexpected Docker boundary request: ${request.method} ${route}` });
        } catch (error) {
            boundaryFailure = error;
            response.writeHead(500, { 'Content-Type': 'application/json' });
            response.end(JSON.stringify({ message: error.message }));
        }
    });
    try {
        await new Promise((resolve, reject) => { server.once('error', reject); server.listen(socket, resolve); });
        const result = await new Promise((resolve, reject) => {
            const child = spawn('/bin/sh', [path.join(root, 'scripts/test-native-visual-kubernetes.sh'), mode], {
                cwd: root, env: { ...process.env, DOCKER_CONTEXT: '', DOCKER_HOST: `unix://${socket}`, PODLORD_KUBERNETES_METADATA_AUDIT: metadataAudit?'1':'0', PODLORD_NATIVE_BUILD_DIR: build, PODLORD_VISUAL_EVIDENCE_DIR: path.join(directory, 'evidence'), ...(mode === 'mobile' ? { PODLORD_NATIVE_APP: '/missing/native-app', PODLORD_LEGACY_APP: '/missing/reference-app' } : {}) },
                stdio: ['ignore', 'pipe', 'pipe']
            });
            runner=child;
            let output = '';
            child.stdout.on('data', bytes => { output += bytes; });
            child.stderr.on('data', bytes => { output += bytes; });
            const timeout = setTimeout(() => child.kill('SIGTERM'), 15000);
            child.once('error', error => { clearTimeout(timeout); reject(error); });
            child.once('close', (code, signal) => { clearTimeout(timeout); resolve({ code, signal, output }); });
        });
        assert.ifError(boundaryFailure);
        assert.notEqual(result.code, 0, `The deliberate external failure must be reported: ${result.output}`);
        assert.equal(result.signal, null, `The runner must finish without timeout: ${result.output}`);
        const deletes = trace.filter(call => call.method === 'DELETE');
        if (scenario === 'preexisting' || scenario === 'collision' || scenario === 'inspect_error') {
            assert.deepEqual(deletes, [], `An unrelated container must survive: ${result.output}`);
            assert.equal(exists, true, `Protected container state was not reached: ${result.output}; ${JSON.stringify(trace)}`);
            assert.equal(trace.some(call => call.route.endsWith('/logs')), false, 'Unowned logs must not be archived.');
        } else {
            assert.deepEqual(deletes, [{ method: 'DELETE', route: `/containers/${name}` }], result.output);
            assert.equal(exists, scenario === 'remove_failure', 'Owned cleanup must not report a removed container when removal failed.');
            if (scenario === 'remove_failure') assert.match(result.output, /Owned containers remain:/, 'Remaining resources must be reported explicitly.');
        }
        assert.equal(trace.some(call => call.route.includes('/prune')), false, 'No global resource prune is permitted.');
        if(scenario==='audit_export_failure'){
            assert.equal(running,true,'The real runner must reach started/audited ownership before failure');
            assert.ok(execAttempts>=2,'Audit export must fail after the original readiness failure');
            assert.match(result.output,/Owned fixture Metadata audit capture failed\./,'Audit failure must be explicit while owned deletion still runs');
        }
        if (scenario === 'coverage') {
            const retained = path.join(directory, 'evidence', name + '-profiles', 'help.profraw');
            assert.ok((await fs.stat(retained)).size > 0, 'Cleanup must retain real execution evidence, not its credentials or profile.');
        }
        if (scenario === 'application_log') {
            const retained = path.join(directory, 'evidence', name + '-native.log');
            assert.equal(await fs.readFile(retained, 'utf8'), applicationLog, 'Cleanup must retain the real application log before removing its private run.');
        }
    } finally {
        server.closeAllConnections();
        await new Promise(resolve => server.close(resolve));
        await fs.rm(directory, { recursive: true });
    }
}

for (const scenario of ['preexisting', 'collision', 'lost_reply', 'after_create', 'inspect_error', 'remove_failure', 'coverage', 'application_log']) {
    test(`visual runner cleanup: ${scenario}`, { timeout: 20000 }, () => exercise(scenario));
}

test('mobile review setup and owned cleanup need no desktop applications', { timeout: 20000 }, () => exercise('after_create', 'mobile'));
test('mobile fixture Metadata audit is opt-in and preserves owned cleanup', { timeout: 20000 }, () => exercise('after_create', 'mobile', true));
test('failed Metadata audit export still removes the started owned fixture and volumes', { timeout: 20000 }, () => exercise('audit_export_failure', 'mobile', true));

for (const mode of ['ios-themes', 'ios-input-e2e']) {
test(`${mode} rejects a missing simulator app before Docker access`, { timeout: 5000, skip: process.platform !== 'darwin' }, async () => {
    const result = await new Promise((resolve, reject) => {
        const child = spawn('/bin/sh', [path.join(root, 'scripts/test-native-visual-kubernetes.sh'), mode], {
            env: { ...process.env, PODLORD_NATIVE_BUILD_DIR: build, PODLORD_IOS_BUILD_DIR: '/missing/podlord-ios-build',
                DOCKER_CONTEXT: '', DOCKER_HOST: 'tcp://127.0.0.1:1' }, stdio: ['ignore', 'pipe', 'pipe']
        });
        let output = '';
        child.stdout.on('data', bytes => { output += bytes; }); child.stderr.on('data', bytes => { output += bytes; });
        child.once('error', reject); child.once('close', (code, signal) => resolve({ code, signal, output }));
    });
    assert.equal(result.code, 1); assert.equal(result.signal, null);
    assert.match(result.output, /Required iOS simulator UI test application is missing/);
    assert.doesNotMatch(result.output, /Only a local Docker daemon/);
});
}

async function iosCompletion(scenario) {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-ios-completion-'));
    const app = path.join(directory, 'launcher-input.app');
    const bin = path.join(directory, 'bin');
    const data = path.join(directory, 'data');
    const evidence = path.join(directory, 'evidence');
    const trace = path.join(directory, 'simctl.jsonl');
    const device = '00000000-0000-4000-8000-000000000001';
    try {
        await fs.mkdir(app);
        await fs.mkdir(bin);
        await fs.mkdir(path.join(data, 'Documents'), { recursive: true });
        // Bundle metadata is CLI input only. No substitute product executable is created or run.
        await fs.writeFile(path.join(app, 'Info.plist'), '<?xml version="1.0"?><plist version="1.0"><dict><key>CFBundleIdentifier</key><string>berlin.yuna.podlord.tests</string></dict></plist>');
        await fs.writeFile(path.join(bin, 'xcrun'), `#!/usr/bin/env node
const fs = require('node:fs');
const args = process.argv.slice(2);
if (args.shift() !== 'simctl') process.exit(99);
fs.appendFileSync(process.env.PODLORD_SIMCTL_TRACE, JSON.stringify(args) + '\\n');
const action = args[0];
if (action === 'create') { console.log('${device}'); process.exit(0); }
if (action === 'get_app_container') { console.log(process.env.PODLORD_SIMCTL_DATA); process.exit(0); }
if (['boot', 'bootstatus', 'install', 'terminate', 'shutdown', 'delete'].includes(action)) process.exit(0);
if (action !== 'launch') process.exit(99);
const launches = fs.readFileSync(process.env.PODLORD_SIMCTL_TRACE, 'utf8').trim().split('\\n').map(JSON.parse).filter(call => call[0] === 'launch').length;
if (launches > 1) { console.error('Controlled second launch transport failure'); process.exit(9); }
const name = args[4], expected = 'UI scenario passed: ' + name;
console.log('System native HTTPS trust expected=0 actual=0 elapsed=0 error=6');
const scenario = process.env.PODLORD_SIMCTL_COMPLETION_CASE;
if (scenario === 'launch_only') console.log('berlin.yuna.podlord.tests: 123');
else if (scenario === 'wrong_scenario') console.log('UI scenario passed: different_case');
else if (scenario === 'duplicate') { console.log(expected); console.log(expected); }
else if (scenario === 'extra_marker') { console.log(expected); console.log('UI scenario passed: different_case'); }
else if (scenario === 'stderr_only') console.error(expected);
else {
    console.log(expected);
    if (scenario === 'explicit_failure') console.error('UI scenario failed: ' + name);
    if (scenario === 'binding_loop') console.error('qrc:/podlord/ResourceRadar.qml: QML Pane: Binding loop detected for property "hasDetails"');
    if (scenario === 'reference_error') console.error('ReferenceError: missing value');
    if (scenario === 'type_error') console.error('TypeError: invalid value');
    if (scenario === 'transport_failure') { console.error('Controlled first launch transport failure'); process.exit(7); }
    if (scenario === 'clean') console.error('Benign simulator console output');
}
`, { mode: 0o700 });
        const result = await new Promise((resolve, reject) => {
            const script = process.env.PODLORD_IOS_COMPLETION_RUNNER || path.join(root, 'scripts/test-native-ios-trust.cjs');
            const child = spawn(process.execPath, [script, app, 'openssl', evidence], {
                env: { ...process.env, PATH: bin + path.delimiter + process.env.PATH, PODLORD_SIMCTL_TRACE: trace,
                    PODLORD_SIMCTL_DATA: data, PODLORD_SIMCTL_COMPLETION_CASE: scenario },
                stdio: ['ignore', 'pipe', 'pipe']
            });
            let output = '';
            child.stdout.on('data', bytes => { output += bytes; });
            child.stderr.on('data', bytes => { output += bytes; });
            const timeout = setTimeout(() => child.kill('SIGTERM'), 15000);
            child.once('error', error => { clearTimeout(timeout); reject(error); });
            child.once('close', (code, signal) => { clearTimeout(timeout); resolve({ code, signal, output }); });
        });
        assert.equal(result.code, 1, result.output);
        assert.equal(result.signal, null, result.output);
        const calls = (await fs.readFile(trace, 'utf8')).trim().split('\n').map(JSON.parse);
        assert.equal(calls.filter(call => call[0] === 'launch').length, scenario === 'clean' ? 2 : 1,
            'Reject invalid child evidence before another launch, but accept one clean child before the controlled external failure: ' + result.output);
        assert.deepEqual(calls.filter(call => ['terminate', 'shutdown', 'delete'].includes(call[0])), [
            ['terminate', device, 'berlin.yuna.podlord.tests'], ['shutdown', device], ['delete', device]
        ], 'Every result must clean only its own simulator.');
        const stdout = await fs.readFile(path.join(evidence, 'untrusted-before-install.stdout.log'), 'utf8');
        const stderr = await fs.readFile(path.join(evidence, 'untrusted-before-install.stderr.log'), 'utf8');
        assert.equal(await fs.readFile(path.join(evidence, 'untrusted-before-install.log'), 'utf8'), stdout + stderr);
        assert.match(stdout, /expected=0 actual=0/);
        if (scenario === 'clean') {
            assert.equal(stdout.split('\n').filter(line => line === 'UI scenario passed: system_https_untrusted').length, 1);
            assert.match(stderr, /Benign simulator console output/);
            assert.match(result.output, /Controlled second launch transport failure/);
            assert.match(await fs.readFile(path.join(evidence, 'wss-untrusted-before-install.stderr.log'), 'utf8'), /Controlled second launch transport failure/);
        }
        if (scenario === 'transport_failure') assert.match(stderr, /Controlled first launch transport failure/);
    } finally {
        await fs.rm(directory, { recursive: true, force: true });
    }
}

for (const scenario of ['launch_only', 'wrong_scenario', 'duplicate', 'extra_marker', 'stderr_only', 'explicit_failure', 'binding_loop', 'reference_error', 'type_error', 'transport_failure', 'clean']) {
    test(`iOS trust launch completion: ${scenario}`, { timeout: 20000, skip: process.platform !== 'darwin' }, () => iosCompletion(scenario));
}

for (const missing of ['metric-filter-ui-test', 'workspace_ui_test']) {
test(`visual runner rejects missing ${missing} before Docker access`, { timeout: 5000 }, async () => {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-incomplete-build-'));
    try {
        await fs.symlink(path.join(build, 'CMakeCache.txt'), path.join(directory, 'CMakeCache.txt'));
        await fs.symlink(path.join(build, 'window-host-test'), path.join(directory, 'window-host-test'));
        await fs.symlink(path.join(build, 'alert_ui_test'), path.join(directory, 'alert_ui_test'));
        await fs.symlink(path.join(build, 'container-terminal-ui-test'), path.join(directory, 'container-terminal-ui-test'));
        if (missing === 'workspace_ui_test') await fs.symlink(path.join(build, 'metric-filter-ui-test'), path.join(directory, 'metric-filter-ui-test'));
        const result = await new Promise((resolve, reject) => {
            const child = spawn('/bin/sh', [path.join(root, 'scripts/test-native-visual-kubernetes.sh'), 'native-e2e'], {
                env: { ...process.env, PODLORD_NATIVE_BUILD_DIR: directory, DOCKER_CONTEXT: '', DOCKER_HOST: 'tcp://127.0.0.1:1' }, stdio: ['ignore', 'pipe', 'pipe']
            });
            let output = '';
            child.stdout.on('data', bytes => { output += bytes; }); child.stderr.on('data', bytes => { output += bytes; });
            child.once('error', reject); child.once('close', code => resolve({ code, output }));
        });
        assert.equal(result.code, 1);
        assert.match(result.output, missing === 'metric-filter-ui-test'
            ? /Required native field filter UI test executable is missing\./
            : /Required native UI test executable is missing: .*workspace_ui_test/);
        assert.doesNotMatch(result.output, /Only a local Docker daemon/);
    } finally { await fs.rm(directory, { recursive: true }); }
});
}

for (const value of ['0', '-1', 'nope', '21601', '999999999999999999999', '060']) {
    test(`visual runner rejects desktop timeout ${value}`, { timeout: 5000 }, async () => {
        const result = await new Promise((resolve, reject) => {
            const child = spawn('/bin/sh', [path.join(root, 'scripts/test-native-visual-kubernetes.sh'), 'desktop'], {
                env: { ...process.env, PODLORD_DESKTOP_TIMEOUT_SECONDS: value, PODLORD_NATIVE_BUILD_DIR: build }, stdio: ['ignore', 'pipe', 'pipe']
            });
            let output = ''; child.stdout.on('data', bytes => { output += bytes; }); child.stderr.on('data', bytes => { output += bytes; });
            child.once('error', reject); child.once('close', (code, signal) => resolve({ code, signal, output }));
        });
        assert.equal(result.code, 1); assert.equal(result.signal, null);
        assert.match(result.output, /Desktop timeout must be a whole number from 60 to 21600 seconds/);
    });
}
