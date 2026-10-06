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

async function exercise(scenario) {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-cleanup-'));
    const socket = path.join(directory, 'docker.sock');
    const trace = [];
    let name = '', exists = false, owned = false;
    const server = http.createServer(async (request, response) => {
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
            name = url.searchParams.get('name'); exists = true;
            owned = scenario !== 'collision';
            if (scenario === 'collision') { json(409, { message: 'The container name is already in use.' }); return; }
            if (scenario === 'lost_reply') { json(500, { message: 'Creation response failed after creation.' }); return; }
            json(201, { Id: 'a'.repeat(64), Warnings: [] }); return;
        }
        if (route === '/images/get') {
            if (scenario === 'coverage') {
                const entries = await fs.readdir(path.join(build, 'e2e'));
                const run = entries.find(entry => 'podlord-' + entry.toLowerCase().replaceAll('.', '-') === name);
                assert.ok(run, 'The real runner must have created its private run directory.');
                const file = path.join(build, 'e2e', run, 'help.profraw');
                const native = process.env.PODLORD_NATIVE_APP || path.join(build, 'podlord-native.app/Contents/MacOS/podlord-native');
                await new Promise((resolve, reject) => {
                    const child = spawn(native, ['--help'], { env: { ...process.env, QT_QPA_PLATFORM: 'offscreen', LLVM_PROFILE_FILE: file }, stdio: 'ignore' });
                    child.once('error', reject); child.once('close', code => code === 0 ? resolve() : reject(new Error('Real application help failed.')));
                });
                assert.ok((await fs.stat(file)).size > 0, 'Real application execution must produce coverage evidence.');
            }
            json(500, { message: 'Explicit test failure after successful creation.' }); return;
        }
        if (route === `/containers/${name}/json`) {
            if (scenario === 'inspect_error') { json(500, { message: 'Ownership inspection unavailable.' }); return; }
            json(exists ? 200 : 404, exists ? {
                Id: 'a'.repeat(64), Name: '/' + name,
                Config: { Labels: owned ? { 'podlord.native.e2e': name } : {}, Tty: false },
                State: { Status: 'created', Running: false },
                Mounts: [{ Type: 'volume', Name: 'private-test-volume' }]
            } : { message: 'No such container.' }); return;
        }
        if (route === `/containers/${name}/logs` || route === `/containers/${'a'.repeat(64)}/logs`) { response.writeHead(200, { 'Content-Type': 'application/vnd.docker.raw-stream' }); response.end(); return; }
        if (request.method === 'DELETE' && route === `/containers/${name}`) {
            if (scenario === 'remove_failure') { json(500, { message: 'Removal unavailable.' }); return; }
            exists = false; response.writeHead(204); response.end(); return;
        }
        if (route === '/volumes') { json(200, { Volumes: [], Warnings: [] }); return; }
        json(500, { message: `Unexpected Docker boundary request: ${request.method} ${route}` });
    });
    try {
        await new Promise((resolve, reject) => { server.once('error', reject); server.listen(socket, resolve); });
        const result = await new Promise((resolve, reject) => {
            const child = spawn('/bin/sh', [path.join(root, 'scripts/test-native-visual-kubernetes.sh'), 'native-e2e'], {
                cwd: root, env: { ...process.env, DOCKER_CONTEXT: '', DOCKER_HOST: `unix://${socket}`, PODLORD_NATIVE_BUILD_DIR: build, PODLORD_VISUAL_EVIDENCE_DIR: path.join(directory, 'evidence') },
                stdio: ['ignore', 'pipe', 'pipe']
            });
            let output = '';
            child.stdout.on('data', bytes => { output += bytes; });
            child.stderr.on('data', bytes => { output += bytes; });
            const timeout = setTimeout(() => child.kill('SIGTERM'), 15000);
            child.once('error', error => { clearTimeout(timeout); reject(error); });
            child.once('close', (code, signal) => { clearTimeout(timeout); resolve({ code, signal, output }); });
        });
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
        if (scenario === 'coverage') {
            const retained = path.join(directory, 'evidence', name + '-profiles', 'help.profraw');
            assert.ok((await fs.stat(retained)).size > 0, 'Cleanup must retain real execution evidence, not its credentials or profile.');
        }
    } finally {
        server.closeAllConnections();
        await new Promise(resolve => server.close(resolve));
        await fs.rm(directory, { recursive: true });
    }
}

for (const scenario of ['preexisting', 'collision', 'lost_reply', 'after_create', 'inspect_error', 'remove_failure', 'coverage']) {
    test(`visual runner cleanup: ${scenario}`, { timeout: 20000 }, () => exercise(scenario));
}

for (const missing of ['metric-filter-ui-test', 'workspace_ui_test']) {
test(`visual runner rejects missing ${missing} before Docker access`, { timeout: 5000 }, async () => {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-incomplete-build-'));
    try {
        await fs.symlink(path.join(build, 'alert_ui_test'), path.join(directory, 'alert_ui_test'));
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
