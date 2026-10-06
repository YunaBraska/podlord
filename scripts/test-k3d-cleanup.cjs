#!/usr/bin/env node
'use strict';
// Only the Docker Engine boundary is simulated; the real shell, Docker CLI and
// k3d executable remain the public entrypoint of the cleanup workflow.
const { test } = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');
const owner = 'private-test-owner';
const cluster = 'podlord-it-abcdef123456';
const root = path.resolve(__dirname, '..');

for (const scenario of ['empty', 'foreign', 'mixed', 'list_failure', 'inspection_failure', 'remote', 'invalid', 'tools_unrecorded', 'tools_recorded', 'tools_wrong_id', 'tools_wrong_role', 'tools_foreign_owner', 'tools_invalid_id']) {
    test(`k3d cleanup ownership: ${scenario}`, { timeout: 15000 }, async () => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-k3d-cleanup-test-'));
        const socket = path.join(directory, 'docker.sock'), trace = [];
        const server = http.createServer((request, response) => {
            const url = new URL(request.url, 'http://localhost');
            const route = url.pathname.replace(/^\/v\d+\.\d+/, '');
            trace.push({ method: request.method, route });
            const json = (status, value) => { response.writeHead(status, { 'Content-Type': 'application/json' }); response.end(JSON.stringify(value)); };
            if (route === '/_ping') { response.writeHead(200, { 'API-Version': '1.51', OSType: 'linux' }); response.end('OK'); return; }
            if (route === '/version') { json(200, { ApiVersion: '1.51', MinAPIVersion: '1.24' }); return; }
            if (route === '/containers/json') {
                if (scenario === 'list_failure') { json(500, { message: 'Explicit boundary failure.' }); return; }
                const filters = JSON.parse(url.searchParams.get('filters') || '{}');
                const labels = Array.isArray(filters.label) ? filters.label : Object.keys(filters.label || {});
                const ownedOnly = labels.some(label => label.startsWith('podlord.test.run='));
                const ownedNode = { Id: 'a'.repeat(64), Names: ['/k3d-' + cluster + '-server-0'], Labels: { 'k3d.cluster': cluster, 'podlord.test.run': owner } };
                const foreignNode = { Id: 'b'.repeat(64), Names: ['/k3d-' + cluster + '-serverlb'], Labels: { 'k3d.cluster': cluster } };
                if (scenario.startsWith('tools_')) {
                    foreignNode.Names = ['/k3d-' + cluster + '-tools'];
                    foreignNode.Labels.app = 'k3d'; foreignNode.Labels['k3d.role'] = scenario === 'tools_wrong_role' ? 'server' : 'noRole';
                    if (scenario === 'tools_foreign_owner') foreignNode.Labels['podlord.test.run'] = 'other-owner';
                    if (scenario === 'tools_recorded' && trace.filter(call => call.route === '/containers/json').length > 2) {
                        json(500, { message: 'External deletion unavailable.' }); return;
                    }
                }
                const values = scenario.startsWith('tools_') || scenario === 'mixed' || scenario === 'inspection_failure'
                    ? ownedOnly ? [ownedNode] : [ownedNode, foreignNode]
                    : scenario === 'foreign' && !ownedOnly ? [foreignNode] : [];
                json(200, values); return;
            }
            if (/^\/containers\/[ab]{12,64}\/json$/.test(route)) {
                if (scenario === 'inspection_failure') { json(500, { message: 'Ownership lookup failed.' }); return; }
                const own = route.includes('a'.repeat(12));
                json(200, { Id: (own ? 'a' : 'b').repeat(64), Name: '/k3d-' + cluster + (own ? '-server-0' : '-tools'), Config: { Labels: { 'k3d.cluster': cluster,
                    ...(own ? { 'podlord.test.run': owner } : scenario === 'tools_foreign_owner' ? { 'podlord.test.run': 'other-owner' } : {}),
                    ...(scenario.startsWith('tools_') ? { app: 'k3d', 'k3d.role': scenario === 'tools_wrong_role' ? 'server' : 'noRole' } : {})
                } }, Mounts: [] }); return;
            }
            json(500, { message: 'Unexpected external request: ' + route });
        });
        try {
            await new Promise((resolve, reject) => { server.once('error', reject); server.listen(socket, resolve); });
            const result = await new Promise((resolve, reject) => {
                const args = [path.join(root, 'scripts/cleanup-k3d-test-run.sh'), scenario === 'invalid' ? '../invalid' : owner];
                if (scenario.startsWith('tools_') && scenario !== 'tools_unrecorded') args.push(cluster, scenario === 'tools_invalid_id' ? '../invalid' : (scenario === 'tools_wrong_id' ? 'c' : 'b').repeat(64));
                const child = spawn('/bin/sh', args, {
                    cwd: root, env: { ...process.env, PATH: '/opt/homebrew/bin:' + process.env.PATH, DOCKER_CONTEXT: '', DOCKER_HOST: scenario === 'remote' ? 'tcp://127.0.0.1:1' : 'unix://' + socket },
                    stdio: ['ignore', 'pipe', 'pipe']
                });
                let output = ''; child.stdout.on('data', value => { output += value; }); child.stderr.on('data', value => { output += value; });
                const timeout = setTimeout(() => child.kill('SIGTERM'), 10000);
                child.once('error', error => { clearTimeout(timeout); reject(error); });
                child.once('close', (code, signal) => { clearTimeout(timeout); resolve({ code, signal, output }); });
            });
            assert.equal(result.signal, null, result.output);
            assert.equal(result.code, ['empty', 'foreign'].includes(scenario) ? 0 : ['invalid', 'tools_invalid_id'].includes(scenario) ? 2 : 1, result.output);
            assert.equal(trace.some(call => call.method === 'DELETE' || call.route.includes('/prune')), false, 'Unowned containers, volumes and images must remain untouched.');
            if (scenario === 'mixed') assert.match(result.output, /contains an unowned container/);
            if (['tools_unrecorded', 'tools_wrong_id', 'tools_wrong_role', 'tools_foreign_owner'].includes(scenario)) assert.match(result.output, /contains an unowned container/);
            if (scenario === 'tools_recorded') { assert.doesNotMatch(result.output, /contains an unowned container/); assert.match(result.output, /External deletion unavailable/); }
            if (scenario === 'remote' || scenario === 'invalid') assert.deepEqual(trace, [], 'Invalid or remote invocations must fail before reaching the daemon.');
        } finally {
            server.closeAllConnections(); await new Promise(resolve => server.close(resolve));
            await fs.rm(directory, { recursive: true });
        }
    });
}
