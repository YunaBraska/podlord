'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const https = require('node:https');
const { X509Certificate } = require('node:crypto');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const execute = promisify(execFile);
const [driver, openssl, inputScenario, bundle] = process.argv.slice(2);
const storeOnly = driver === '--check-system-store';
const scenario = storeOnly ? 'system_trusted' : inputScenario;
assert.ok(driver && openssl && ['trusted', 'untrusted_ca', 'untrusted_client', 'untrusted_host', 'system_trusted', 'system_untrusted'].includes(scenario)
    && (!storeOnly || (!inputScenario && !bundle)), 'Expected real driver, OpenSSL and TLS scenario, or --check-system-store OPENSSL.');
const systemTrust = scenario.startsWith('system_');
assert.ok(!systemTrust || (process.platform === 'win32' && process.env.GITHUB_ACTIONS === 'true'
    && process.env.RUNNER_ENVIRONMENT === 'github-hosted' && process.env.RUNNER_TEMP && (bundle || storeOnly)),
    'System-store fixtures require the disposable Windows CI account and extracted package.');
async function run() {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-mutual-tls-'));
    let server;
    let importedRoot;
    const certutil = systemTrust ? path.join(process.env.SystemRoot, 'System32', 'certutil.exe') : undefined;
    const powershell = systemTrust ? path.join(process.env.SystemRoot, 'System32', 'WindowsPowerShell', 'v1.0', 'powershell.exe') : undefined;
    const rootMatches = async fingerprint => {
        assert.match(fingerprint, /^[0-9A-F]{40}$/i);
        const query = "$ErrorActionPreference = 'Stop'; "
            + '$store = [System.Security.Cryptography.X509Certificates.X509Store]::new('
            + '[System.Security.Cryptography.X509Certificates.StoreName]::Root, '
            + '[System.Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser); '
            + 'try { $store.Open([System.Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly '
            + '-bor [System.Security.Cryptography.X509Certificates.OpenFlags]::OpenExistingOnly); '
            + '[Console]::Write($store.Certificates.Find('
            + '[System.Security.Cryptography.X509Certificates.X509FindType]::FindByThumbprint, '
            + `'${fingerprint}', $false).Count) } finally { $store.Close() }`;
        const result = await execute(powershell, ['-NoLogo', '-NoProfile', '-NonInteractive', '-Command', query],
            { timeout: 15000, windowsHide: true });
        return result.stdout.trim();
    };
    let requests = 0;
    const paths = new Set();
    const config = path.join(directory, 'openssl.cnf');
    const command = args => execute(openssl, args, { cwd: directory, timeout: 15000, env: { ...process.env, OPENSSL_CONF: config } });
    const read = name => fs.readFile(path.join(directory, name));
    try {
        await fs.writeFile(config, '[req]\ndistinguished_name=subject\n[subject]\n[ca]\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n', { mode: 0o600 });
        for (const ca of ['root', 'other']) {
            await command(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', `${ca}.key`]);
            await command(['req', '-new', '-x509', '-key', `${ca}.key`, '-out', `${ca}.pem`, '-days', '1', '-subj', `/CN=Podlord TLS ${ca}`, '-extensions', 'ca']);
        }
        if (scenario === 'system_trusted') {
            const fingerprint = new X509Certificate(await read('root.pem')).fingerprint.replaceAll(':', '');
            assert.match(fingerprint, /^[0-9A-F]{40}$/i);
            // Never replace an existing account certificate, even in this ephemeral runner.
            assert.equal(await rootMatches(fingerprint), '0', 'The fixture CA must be absent before import.');
            importedRoot = fingerprint;
            await execute(certutil, ['-user', '-f', '-addstore', 'Root', path.join(directory, 'root.pem')], { timeout: 15000 });
            assert.equal(await rootMatches(fingerprint), '1', 'The owned fixture CA must be present after import.');
        }
        if (storeOnly) return;
        await fs.writeFile(path.join(directory, 'server.ext'), `subjectAltName=IP:${scenario === 'untrusted_host' ? '192.0.2.1' : '127.0.0.1'}\nextendedKeyUsage=serverAuth\n`, { mode: 0o600 });
        await fs.writeFile(path.join(directory, 'client.ext'), 'extendedKeyUsage=clientAuth\n', { mode: 0o600 });
        for (const name of ['server', 'client']) {
            await command(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', `${name}.key`]);
            await command(['req', '-new', '-key', `${name}.key`, '-out', `${name}.csr`, '-subj', `/CN=Podlord TLS ${name}`]);
            const ca = name === 'client' && scenario === 'untrusted_client' ? 'other' : 'root';
            await command(['x509', '-req', '-in', `${name}.csr`, '-CA', `${ca}.pem`, '-CAkey', `${ca}.key`, '-CAcreateserial', '-out', `${name}.pem`, '-days', '1', '-extfile', `${name}.ext`]);
        }
        // Only the external Kubernetes HTTPS boundary is simulated. Mutual TLS is real.
        server = https.createServer({ key: await read('server.key'), cert: await read('server.pem'), ca: await read('root.pem'), requestCert: true, rejectUnauthorized: true }, (request, response) => {
            requests++;
            const pathname = new URL(request.url, 'https://127.0.0.1').pathname;
            paths.add(pathname);
            let body;
            if (pathname === '/api') body = { versions: ['v1'] };
            else if (pathname === '/apis') body = { groups: [] };
            else if (pathname === '/api/v1') body = { resources: [{ name: 'pods', kind: 'Pod', namespaced: true, verbs: ['get', 'list'] }] };
            else if (pathname === '/api/v1/pods') body = { apiVersion: 'v1', kind: 'PodList', metadata: {}, items: [{ apiVersion: 'v1', kind: 'Pod', metadata: { uid: 'tls-pod', name: 'tls-pod', namespace: 'default', resourceVersion: '1' }, status: { phase: 'Running' } }] };
            else { response.writeHead(404); response.end(); return; }
            response.writeHead(200, { 'Content-Type': 'application/json' });
            response.end(JSON.stringify(body));
        });
        await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
        const base64 = async name => (await read(name)).toString('base64');
        const input = path.join(directory, 'kubeconfig');
        const ca = systemTrust ? '' : `    certificate-authority-data: ${await base64(scenario === 'untrusted_ca' ? 'other.pem' : 'root.pem')}\n`;
        await fs.writeFile(input, `apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: https://127.0.0.1:${server.address().port}\n${ca}users:\n- name: local\n  user:\n    client-certificate-data: ${await base64('client.pem')}\n    client-key-data: ${await base64('client.key')}\ncontexts:\n- name: local\n  context:\n    cluster: local\n    user: local\ncurrent-context: local\n`, { mode: 0o600 });
        const env = { ...process.env };
        let executable = driver;
        if (bundle && process.platform === 'win32') {
            assert.ok(path.isAbsolute(bundle));
            const runtime = path.join(directory, 'packaged-runtime');
            await fs.cp(bundle, runtime, { recursive: true, dereference: false, verbatimSymlinks: true });
            executable = path.join(runtime, 'tls-connection-test.exe');
            await fs.copyFile(driver, executable);
            // Fresh child process, app-local runtime and ordinary OS paths only.
            for (const key of Object.keys(env)) delete env[key];
            for (const key of ['SystemRoot', 'WINDIR', 'SystemDrive', 'ProgramData']) {
                if (process.env[key]) env[key] = process.env[key];
            }
            const system = path.join(process.env.SystemRoot, 'System32');
            env.PATH = [system, process.env.SystemRoot, path.join(system, 'Wbem')].join(path.delimiter);
            for (const key of ['USERPROFILE', 'APPDATA', 'LOCALAPPDATA', 'TEMP', 'TMP']) env[key] = directory;
            env.KUBECONFIG = path.join(directory, 'absent-kubeconfig');
            env.PODLORD_DISABLE_AUDIO = '1';
            env.PODLORD_DISABLE_UPDATE_CHECK = '1';
            env.OPENSSL_CONF = config;
            env.OPENSSL_MODULES = path.join(directory, 'absent-openssl-modules');
            env.QT_QPA_PLATFORM = 'windows';
            env.QT_QUICK_BACKEND = 'software';
            env.QT_PLUGIN_PATH = runtime;
            env.QT_QPA_PLATFORM_PLUGIN_PATH = path.join(runtime, 'platforms');
            env.QML_IMPORT_PATH = path.join(runtime, 'qml');
            env.QT_FORCE_STDERR_LOGGING = '1';
        } else if (bundle) {
            assert.equal(process.platform, 'darwin', 'Packaged TLS inspection currently uses a real macOS app.');
            assert.ok(path.isAbsolute(bundle));
            const app = path.join(directory, 'podlord-native.app');
            await fs.cp(bundle, app, { recursive: true, dereference: false, verbatimSymlinks: true });
            executable = path.join(app, 'Contents/MacOS/tls-connection-test');
            await fs.copyFile(driver, executable);
            const libraries = await execute('/usr/bin/otool', ['-L', executable]);
            for (const match of libraries.stdout.matchAll(/^\s+(\/.*?)\s+\(compatibility version/gm)) {
                const library = match[1];
                if (library.startsWith('/usr/lib/') || library.startsWith('/System/Library/')) continue;
                const filename = path.basename(library);
                assert.ok(filename.endsWith('.dylib'), 'Unexpected external helper dependency.');
                await fs.access(path.join(app, 'Contents/Frameworks', filename));
                await execute('/usr/bin/install_name_tool', ['-change', library, `@executable_path/../Frameworks/${filename}`, executable]);
            }
            await execute('/usr/libexec/PlistBuddy', ['-c', 'Set :CFBundleExecutable tls-connection-test', path.join(app, 'Contents/Info.plist')]);
            await execute('/usr/bin/codesign', ['--force', '--sign', '-', executable]);
            await execute('/usr/bin/codesign', ['--force', '--sign', '-', app]);
            delete env.DYLD_LIBRARY_PATH;
            delete env.DYLD_FALLBACK_LIBRARY_PATH;
            env.DYLD_FRAMEWORK_PATH = path.join(app, 'Contents/Frameworks');
            env.QT_PLUGIN_PATH = path.join(app, 'Contents/PlugIns');
            env.QT_QPA_PLATFORM_PLUGIN_PATH = path.join(app, 'Contents/PlugIns/platforms');
            env.QT_QPA_PLATFORM = 'cocoa';
        } else if (process.platform === 'darwin') env.DYLD_LIBRARY_PATH = path.resolve(path.dirname(openssl), '../lib')
            + (env.DYLD_LIBRARY_PATH ? path.delimiter + env.DYLD_LIBRARY_PATH : '');
        const result = await execute(executable, [input, scenario], { timeout: 25000, env });
        if (bundle && process.platform === 'win32') assert.match(result.stdout, /backend: openssl\b/, 'The Windows package must use its embedded OpenSSL backend.');
        if (scenario === 'trusted' || scenario === 'system_trusted') {
            assert.ok(requests >= 4, 'The owned cache did not read the real HTTPS boundary.');
            for (const required of ['/api', '/apis', '/api/v1', '/api/v1/pods']) assert.ok(paths.has(required), `Missing public discovery/list request: ${required}`);
        } else assert.equal(requests, 0, 'An untrusted TLS peer or client reached the API.');
        process.stdout.write(result.stdout);
    } finally {
        try {
            if (server) {
                server.closeAllConnections();
                await new Promise(resolve => server.close(resolve));
            }
        } finally {
            try {
                if (importedRoot) {
                    await execute(certutil, ['-user', '-delstore', 'Root', importedRoot], { timeout: 15000 });
                    assert.equal(await rootMatches(importedRoot), '0', 'The fixture CA must be absent after cleanup.');
                }
            } finally {
                await fs.rm(directory, { recursive: true, force: true });
            }
        }
    }
}
run().then(() => {
    if (storeOnly) process.stdout.write('Disposable Windows certificate-store fixture passed; native TLS was not exercised.\n');
}).catch(error => {
    process.stderr.write(`${[error.message, `exit=${error.code ?? 'unknown'} signal=${error.signal ?? 'none'} killed=${error.killed === true}`,
        error.stdout, error.stderr].filter(Boolean).join('\n')}\n`);
    process.exitCode = 1;
});
