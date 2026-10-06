'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const https = require('node:https');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const execute = promisify(execFile);
const [driver, openssl, scenario, bundle] = process.argv.slice(2);
assert.ok(driver && openssl && ['trusted', 'untrusted_ca', 'untrusted_client'].includes(scenario), 'Expected real driver, OpenSSL and TLS scenario.');
async function run() {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-mutual-tls-'));
    let server;
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
        await fs.writeFile(path.join(directory, 'server.ext'), 'subjectAltName=IP:127.0.0.1\nextendedKeyUsage=serverAuth\n', { mode: 0o600 });
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
        await fs.writeFile(input, `apiVersion: v1\nkind: Config\nclusters:\n- name: local\n  cluster:\n    server: https://127.0.0.1:${server.address().port}\n    certificate-authority-data: ${await base64(scenario === 'untrusted_ca' ? 'other.pem' : 'root.pem')}\nusers:\n- name: local\n  user:\n    client-certificate-data: ${await base64('client.pem')}\n    client-key-data: ${await base64('client.key')}\ncontexts:\n- name: local\n  context:\n    cluster: local\n    user: local\ncurrent-context: local\n`, { mode: 0o600 });
        const env = { ...process.env };
        let executable = driver;
        if (bundle) {
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
        if (scenario === 'trusted') {
            assert.ok(requests >= 4, 'The owned cache did not read the real HTTPS boundary.');
            for (const required of ['/api', '/apis', '/api/v1', '/api/v1/pods']) assert.ok(paths.has(required), `Missing public discovery/list request: ${required}`);
        } else assert.equal(requests, 0, 'An untrusted TLS peer or client reached the API.');
        process.stdout.write(result.stdout);
    } finally {
        if (server) {
            server.closeAllConnections();
            await new Promise(resolve => server.close(resolve));
        }
        await fs.rm(directory, { recursive: true, force: true });
    }
}
run().catch(error => { process.stderr.write(`${error.stderr || error.message}\n`); process.exitCode = 1; });
