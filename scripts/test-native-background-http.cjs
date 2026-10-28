'use strict';
const assert = require('node:assert/strict');
const http = require('node:http');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const execute = promisify(execFile);
const [driver, scenario] = process.argv.slice(2);
assert.ok(driver && ['body', 'post', 'patch', 'bounded', 'paused_reader', 'parallel', 'repeat', 'cancel_before', 'cancel_queued', 'cancel_active', 'cancel_unread', 'cancel_metadata', 'destroy', 'not_found', 'redirect', 'disconnect'].includes(scenario));
const payload = size => Buffer.from(Array.from({ length: size }, (_, i) => i % 251));
async function run() {
    const methods = [];
    const server = http.createServer((request, response) => {
        methods.push(request.method);
        if (request.url === '/connections') {
            server.getConnections((error, count) => {
                response.writeHead(error ? 500 : 200);
                response.end(error ? 'unavailable' : String(count - 1));
            });
            return;
        }
        if (request.url === '/empty') { response.writeHead(200, { 'Content-Length': 0 }); response.end(); return; }
        if (request.url === '/disconnect') { request.socket.destroy(); return; }
        if (request.url === '/missing') { response.writeHead(404); response.end('missing'); return; }
        if (request.url === '/redirect') { response.writeHead(302, { Location: '/body' }); response.end(); return; }
        if (request.url === '/echo') {
            const chunks = [];
            request.on('data', chunk => chunks.push(chunk));
            request.on('end', () => { const body = Buffer.concat(chunks); response.writeHead(200, { 'Content-Length': body.length }); response.end(body); });
            return;
        }
        const body = payload(request.url === '/chunks' ? 196619 : 262175);
        response.writeHead(200, { 'Content-Length': body.length, 'Content-Type': 'application/octet-stream', 'X-Podlord-Test': request.headers['x-podlord-test'] || 'parallel' });
        if (request.url !== '/chunks') { response.end(body); return; }
        let offset = 0;
        const timer = setInterval(() => {
            if (offset >= body.length) { clearInterval(timer); response.end(); return; }
            response.write(body.subarray(offset, offset + 16384)); offset += 16384;
        }, 2);
        response.on('close', () => clearInterval(timer));
    });
    try {
        await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
        const result = await execute(driver, [`http://127.0.0.1:${server.address().port}`, scenario], { timeout: 15000, env: process.env });
        if (scenario === 'cancel_before') assert.equal(methods.length, 0, 'A canceled queued request reached the server.');
        else if (scenario === 'cancel_queued') assert.deepEqual(methods, ['GET'], 'Only the first request may reach the server.');
        else if (scenario === 'parallel') assert.equal(methods.length, 8);
        else if (scenario === 'repeat') assert.equal(methods.length, 3);
        else if (scenario === 'destroy') assert.deepEqual(methods, ['GET', 'GET']);
        else if (scenario === 'post' || scenario === 'patch') assert.deepEqual(methods, [scenario.toUpperCase()]);
        process.stdout.write(result.stdout);
    } finally {
        server.closeAllConnections();
        await new Promise(resolve => server.close(resolve));
    }
}
run().catch(error => { process.stderr.write(`${error.stdout || ''}${error.stderr || error.message}\n`); process.exitCode = 1; });
