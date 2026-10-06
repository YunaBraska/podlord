import http from 'node:http';
import fs from 'node:fs';

// The Kubernetes HTTP boundary alone is simulated, outside the measured process.
const directory = process.argv[2];
if (!directory) throw new Error('A private output directory is required.');
const started = new Date().toISOString();
const servers = [];
const counts = [1667, 1667, 1666];
let largeLogSent = false;
let sequence = 0;
let requests = 0;
const resources = [
  {name: 'pods', kind: 'Pod', namespaced: true, verbs: ['get', 'list']},
  {name: 'configmaps', kind: 'ConfigMap', namespaced: true, verbs: ['get', 'list']},
  {name: 'secrets', kind: 'Secret', namespaced: true, verbs: ['get', 'list']}
];
const addresses = await Promise.all(counts.map(async (count, partition) => {
  const pod = index => ({apiVersion: 'v1', kind: 'Pod',
    metadata: {name: `load-${index}`, namespace: 'benchmark', uid: `pod-${partition}-${index}`, resourceVersion: '1', creationTimestamp: started},
    spec: {containers: [{name: 'worker', image: 'registry.example.invalid/performance/worker:1'}]},
    status: {phase: 'Running', containerStatuses: [{name: 'worker', ready: true, restartCount: 0, state: {running: {}}}]}});
  const objects = {
    pods: Array.from({length: 500}, (_, index) => pod(index)),
    configmaps: partition < 2 ? Array.from({length: count - 500}, (_, index) => ({apiVersion: 'v1', kind: 'ConfigMap',
      metadata: {name: `config-${index}`, namespace: 'benchmark', uid: `config-${partition}-${index}`, resourceVersion: '1'}, data: {setting: 'benchmark'}})) : [],
    secrets: partition === 2 ? Array.from({length: count - 500}, (_, index) => ({apiVersion: 'v1', kind: 'Secret',
      metadata: {name: `secret-${index}`, namespace: 'benchmark', uid: `secret-${partition}-${index}`, resourceVersion: '1'}, type: 'Opaque', data: {setting: 'YmVuY2htYXJr'}})) : []
  };
  const reply = (response, status, body, contentType = 'application/json') => {
    const bytes = contentType === 'application/json' ? JSON.stringify(body) : body;
    response.writeHead(status, {'Content-Type': contentType, 'Content-Length': Buffer.byteLength(bytes), 'Connection': 'close'});
    response.end(bytes);
  };
  const server = http.createServer((request, response) => {
    requests++;
    process.stdout.write(JSON.stringify({at: new Date().toISOString(), partition, method: request.method, path: request.url}) + '\n');
    if (request.method !== 'GET' || request.headers.authorization !== 'Bearer local-test') {
      reply(response, 401, {kind: 'Status', apiVersion: 'v1', status: 'Failure', reason: 'Unauthorized', code: 401}); return;
    }
    const url = new URL(request.url, 'http://127.0.0.1');
    if (url.pathname === '/api') { reply(response, 200, {versions: ['v1']}); return; }
    if (url.pathname === '/apis') { reply(response, 200, {groups: []}); return; }
    if (url.pathname === '/api/v1') { reply(response, 200, {resources}); return; }
    const match = /^\/api\/v1\/(?:namespaces\/benchmark\/)?(pods|configmaps|secrets)(?:\/([^/]+))?(\/log)?$/.exec(url.pathname);
    if (!match) { reply(response, 404, {kind: 'Status', apiVersion: 'v1', status: 'Failure', reason: 'NotFound', code: 404}); return; }
    const [, collection, name, log] = match;
    if (log && collection === 'pods' && name === 'load-0') {
      const size = largeLogSent ? 32 : 51200;
      largeLogSent = true;
      const lines = Array.from({length: 100}, () => `${new Date(Date.now() + sequence++).toISOString()} ${'x'.repeat(size)}\n`).join('');
      reply(response, 200, lines, 'text/plain'); return;
    }
    if (name) {
      const object = objects[collection].find(value => value.metadata.name === name);
      if (!object) { reply(response, 404, {kind: 'Status', apiVersion: 'v1', status: 'Failure', reason: 'NotFound', code: 404}); return; }
      // Preserve a real cache-first interval; the app must not await this reply to paint.
      setTimeout(() => { if (!response.destroyed) reply(response, 200, object); }, 750); return;
    }
    const offset = Number(url.searchParams.get('continue') || '0');
    const limit = Number(url.searchParams.get('limit') || 500);
    if (!Number.isSafeInteger(offset) || offset < 0 || !Number.isSafeInteger(limit) || limit < 1 || limit > 10000) {
      reply(response, 400, {kind: 'Status', apiVersion: 'v1', status: 'Failure', reason: 'BadRequest', code: 400}); return;
    }
    const items = objects[collection].slice(offset, offset + limit);
    const next = offset + items.length;
    reply(response, 200, {apiVersion: 'v1', kind: `${resources.find(value => value.name === collection).kind}List`,
      metadata: {resourceVersion: '1', continue: next < objects[collection].length ? String(next) : ''}, items});
  });
  servers.push(server);
  await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  return `http://127.0.0.1:${server.address().port}`;
}));
const config = ['apiVersion: v1', 'kind: Config', 'clusters:', ...addresses.flatMap((server, index) => [
  `- name: cluster-${index}`, `  cluster: {server: '${server}'}`]), 'users:', '- name: local', '  user: {token: local-test}',
  'contexts:', ...addresses.flatMap((_, index) => [`- name: session-${index}`, `  context: {cluster: cluster-${index}, user: local, namespace: benchmark}`])].join('\n') + '\n';
fs.writeFileSync(`${directory}/kubeconfig`, config, {mode: 0o600});
fs.writeFileSync(`${directory}/ready`, 'ready\n', {mode: 0o600});
process.once('SIGTERM', () => {
  fs.writeFileSync(`${directory}/api-summary.json`, JSON.stringify({requests, largeLogSent}) + '\n', {mode: 0o600});
  for (const server of servers) server.closeAllConnections();
  Promise.all(servers.map(server => new Promise(resolve => server.close(resolve)))).then(() => process.exit(0));
});
