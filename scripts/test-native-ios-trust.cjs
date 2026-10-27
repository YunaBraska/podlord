'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const https = require('node:https');
const crypto = require('node:crypto');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const execute = promisify(execFile);
const [app, openssl, evidence] = process.argv.slice(2);
assert.equal(process.platform, 'darwin');
assert.ok(app && openssl && evidence && path.isAbsolute(app) && path.isAbsolute(evidence));
async function run() {
    const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-ios-trust-'));
    const servers = [];
    const evaluations = new Map();
    const apiRequests = [];
    const socketRequests = [];
    const connections = new Set();
    let device;
    const command = args => execute(openssl, args, { cwd: temporary, timeout: 15000, env: { ...process.env, OPENSSL_CONF: path.join(temporary, 'openssl.cnf') } });
    const read = name => fs.readFile(path.join(temporary, name));
    const simctl = (args, env = {}) => execute('xcrun', ['simctl', ...args], { timeout: 120000, env: { ...process.env, ...env } });
    const listen = async server => {
        servers.push(server);
        server.on('connection', socket => { connections.add(socket); socket.on('close', () => connections.delete(socket)); });
        await new Promise(resolve => server.listen(0, '127.0.0.1', resolve)); return server.address().port;
    };
    try {
        await fs.mkdir(evidence);
        const identity = await execute('/usr/bin/plutil', ['-extract', 'CFBundleIdentifier', 'raw', path.join(app, 'Info.plist')]);
        assert.equal(identity.stdout.trim(), 'berlin.yuna.podlord.tests', 'Only the isolated UI test application may be installed.');
        await fs.writeFile(path.join(temporary, 'openssl.cnf'), '[req]\ndistinguished_name=subject\n[subject]\n[ca]\nbasicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n');
        for (const root of ['root', 'other']) {
            await command(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', `${root}.key`]);
            await command(['req', '-new', '-x509', '-key', `${root}.key`, '-out', `${root}.pem`, '-days', '1', '-subj', `/CN=Podlord isolated ${root}`, '-extensions', 'ca']);
        }
        const aiaPort = await listen(http.createServer((request, response) => {
            const id = request.url.slice(1);
            const item = evaluations.get(id);
            if (!item) { response.writeHead(404); response.end(); return; }
            item.start = Date.now();
            const timer = setTimeout(() => {
                item.end = Date.now();
                response.writeHead(200, { 'Content-Type': 'application/pkix-cert' }); response.end(item.der);
            }, item.delay);
            response.on('close', () => clearTimeout(timer));
        }));
        for (const id of ['direct', 'delayed', 'canceled', 'socket-direct', 'socket-delayed', 'socket-canceled']) {
            const direct = id.endsWith('direct');
            let issuer = 'root';
            if (!direct) {
                issuer = `${id}-issuer`;
                await command(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', `${issuer}.key`]);
                await command(['req', '-new', '-key', `${issuer}.key`, '-out', `${issuer}.csr`, '-subj', `/CN=Podlord ${id} intermediate`]);
                await fs.writeFile(path.join(temporary, 'issuer.ext'), 'basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\n');
                await command(['x509', '-req', '-in', `${issuer}.csr`, '-CA', 'root.pem', '-CAkey', 'root.key', '-CAcreateserial', '-out', `${issuer}.pem`, '-days', '1', '-extfile', 'issuer.ext']);
                await command(['x509', '-in', `${issuer}.pem`, '-outform', 'DER', '-out', `${issuer}.der`]);
                evaluations.set(id, { der: await read(`${issuer}.der`), delay: id.endsWith('canceled') ? 3000 : 500, start: 0, end: 0 });
            }
            await command(['genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-out', `${id}.key`]);
            await command(['req', '-new', '-key', `${id}.key`, '-out', `${id}.csr`, '-subj', `/CN=Podlord ${id} HTTPS`]);
            await fs.writeFile(path.join(temporary, `${id}.ext`), 'subjectAltName=IP:127.0.0.1\nextendedKeyUsage=serverAuth\n'
                + (direct ? '' : `authorityInfoAccess=caIssuers;URI:http://127.0.0.1:${aiaPort}/${id}\n`));
            await command(['x509', '-req', '-in', `${id}.csr`, '-CA', `${issuer}.pem`, '-CAkey', `${issuer}.key`, '-CAcreateserial', '-out', `${id}.pem`, '-days', '1', '-extfile', `${id}.ext`]);
            const server = https.createServer({ key: await read(`${id}.key`), cert: await read(`${id}.pem`) }, (_request, response) => {
                apiRequests.push(id);
                const timing = evaluations.get(id);
                response.writeHead(200, { 'X-Podlord-CA-Start': String(timing?.start || 0), 'X-Podlord-CA-End': String(timing?.end || 0) });
                response.end('Real HTTPS response.');
            });
            server.on('upgrade', (request, socket) => {
                socketRequests.push(id);
                const accept = crypto.createHash('sha1').update(request.headers['sec-websocket-key'] + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
                socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
                const timing = evaluations.get(id);
                const payload = Buffer.from(JSON.stringify({ text: 'Podlord isolated WebSocket', start: timing?.start || 0, end: timing?.end || 0 }));
                assert.ok(payload.length < 126);
                socket.write(Buffer.concat([Buffer.from([0x81, payload.length]), payload]));
                socket.on('data', () => socket.end());
                socket.on('error', () => {});
            });
            const port = await listen(server);
            if (direct) evaluations.set(id, { port });
            else evaluations.get(id).port = port;
        }
        device = (await simctl(['create', 'Podlord Native Trust Fixture', 'com.apple.CoreSimulator.SimDeviceType.iPhone-17-Pro', 'com.apple.CoreSimulator.SimRuntime.iOS-26-4'])).stdout.trim();
        await simctl(['boot', device, '--arch=arm64']);
        const boot = await simctl(['bootstatus', device, '-b']);
        await fs.writeFile(path.join(evidence, 'boot.log'), boot.stdout + boot.stderr);
        await simctl(['install', device, app]);
        const data = (await simctl(['get_app_container', device, 'berlin.yuna.podlord.tests', 'data'])).stdout.trim();
        const wrongCa = path.join(data, 'Documents', 'other.pem');
        await fs.copyFile(path.join(temporary, 'other.pem'), wrongCa);
        const probe = async (name, mode, id, ca = '', transport = 'https') => {
            const result = await simctl(['launch', '--console', device, 'berlin.yuna.podlord.tests', `system_${transport}_${mode}`, `${transport}://127.0.0.1:${evaluations.get(id).port}`], { SIMCTL_CHILD_PODLORD_TEST_HTTPS_CA_FILE: ca });
            const output = result.stdout + result.stderr;
            await fs.writeFile(path.join(evidence, `${name}.log`), output);
            assert.doesNotMatch(output, /UI scenario failed/, output);
            assert.match(output, mode === 'trusted' ? /expected=1 actual=1/ : mode === 'canceled' ? /expected=0 actual=0/ : /expected=0 actual=0 .*error=6|expected=0 actual=0 .*error=13/);
            process.stdout.write(`${name}: ${output.split('\n').filter(line => line.startsWith('System ')).join(' | ')}\n`);
        };
        await probe('untrusted-before-install', 'untrusted', 'direct');
        await probe('wss-untrusted-before-install', 'untrusted', 'socket-direct', '', 'wss');
        assert.equal(apiRequests.length, 0);
        assert.equal(socketRequests.length, 0);
        await simctl(['keychain', device, 'add-root-cert', path.join(temporary, 'root.pem')]);
        await probe('installed-system-root', 'trusted', 'direct');
        await probe('explicit-wrong-ca', 'untrusted', 'direct', wrongCa);
        assert.deepEqual(apiRequests, ['direct']);
        await probe('wss-installed-system-root', 'trusted', 'socket-direct', '', 'wss');
        await probe('wss-explicit-wrong-ca', 'untrusted', 'socket-direct', wrongCa, 'wss');
        assert.deepEqual(socketRequests, ['socket-direct']);
        await probe('delayed-native-validation', 'trusted', 'delayed');
        assert.ok(evaluations.get('delayed').start > 0 && evaluations.get('delayed').end > 0, 'Native trust never fetched the missing intermediate.');
        await probe('cancel-pending-native-validation', 'canceled', 'canceled');
        assert.ok(evaluations.get('canceled').start > 0, 'Cancellation did not overlap real native validation.');
        assert.deepEqual(apiRequests, ['direct', 'delayed'], 'An unvalidated or canceled request reached the HTTPS application.');
        await probe('wss-delayed-native-validation', 'trusted', 'socket-delayed', '', 'wss');
        assert.ok(evaluations.get('socket-delayed').start > 0 && evaluations.get('socket-delayed').end > 0);
        await probe('wss-cancel-pending-native-validation', 'canceled', 'socket-canceled', '', 'wss');
        assert.ok(evaluations.get('socket-canceled').start > 0);
        assert.deepEqual(socketRequests, ['socket-direct', 'socket-delayed'], 'An unvalidated or canceled WebSocket reached the application.');
        await fs.writeFile(path.join(evidence, 'result.json'), JSON.stringify({ platform: 'iOS ARM64 simulator', apiRequests, socketRequests,
            nativeValidation: [...evaluations.entries()].map(([name, item]) => ({ name, start: item.start, end: item.end })) }, null, 2));
    } finally {
        try {
            if (device) {
                await simctl(['terminate', device, 'berlin.yuna.podlord.tests']).catch(() => {});
                await simctl(['shutdown', device]).catch(() => {});
                await simctl(['delete', device]);
            }
        } finally {
            try {
                for (const socket of connections) socket.destroy();
                for (const server of servers) { server.closeAllConnections(); await new Promise(resolve => server.close(resolve)); }
            } finally {
                await fs.rm(temporary, { recursive: true, force: true });
            }
        }
    }
}
run().catch(error => { process.stderr.write(`${error.stdout || ''}${error.stderr || error.message}\n`); process.exitCode = 1; });
