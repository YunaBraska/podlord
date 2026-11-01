import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawn, execFile} from 'node:child_process';
import {once} from 'node:events';
import {promisify} from 'node:util';
import {setTimeout as delay} from 'node:timers/promises';

const api = fileURLToPath(new URL('./performance_api.mjs',import.meta.url));
const execute = promisify(execFile);

function owned(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(),'podlord-fixture-check-'));
  const children = [];
  t.after(async () => {
    try {
      for (const child of children) {
        if (child.exitCode !== null || child.signalCode !== null) continue;
        const ended = once(child,'exit');
        const watchdog = setTimeout(() => child.kill('SIGKILL'),3000);
        try { child.kill('SIGTERM'); await ended; } finally { clearTimeout(watchdog); }
      }
    } finally { fs.rmSync(root,{recursive:true,force:true}); }
  });
  return {root,children};
}

async function start(state,name,replay = '') {
  const directory = path.join(state.root,name);
  fs.mkdirSync(directory);
  const child = spawn(process.execPath,[api,directory,replay],{stdio:['ignore','ignore','pipe']});
  state.children.push(child);
  let error = '';
  child.stderr.on('data',chunk => { error += chunk; });
  const deadline = Date.now()+5000;
  while (!fs.existsSync(path.join(directory,'ready'))) {
    if (child.exitCode !== null || child.signalCode !== null || Date.now() >= deadline) throw new Error('Fixture failed to start: '+error);
    await delay(10);
  }
  const fixture = JSON.parse(fs.readFileSync(path.join(directory,'kubeconfig.fixture.json'),'utf8'));
  return {child,directory,fixture};
}

async function get(port,route) {
  const response = await fetch(`http://127.0.0.1:${port}${route}`,{
    headers:{Authorization:'Bearer local-test'},signal:AbortSignal.timeout(5000)});
  assert.equal(response.status,200);
  return response;
}

test('replay preserves the input identity and resets large-log state',{timeout:15000},async t => {
  const state = owned(t), first = await start(state,'first');
  const large = await (await get(first.fixture.ports[0],'/api/v1/namespaces/benchmark/pods/load-0/log')).text();
  const small = await (await get(first.fixture.ports[0],'/api/v1/namespaces/benchmark/pods/load-0/log')).text();
  assert.ok(large.length > 5000000 && small.length < 10000);
  const manifest = path.join(state.root,'replay.json');
  fs.writeFileSync(manifest,JSON.stringify(first.fixture));
  const ended = once(first.child,'exit'); first.child.kill('SIGTERM'); await ended;
  const replay = await start(state,'replayed',manifest);
  assert.equal(replay.fixture.origin,first.fixture.origin);
  assert.deepEqual(replay.fixture.ports,first.fixture.ports);
  assert.equal(replay.fixture.configSha256,first.fixture.configSha256);
  assert.equal(fs.readFileSync(path.join(replay.directory,'kubeconfig'),'utf8'),fs.readFileSync(path.join(first.directory,'kubeconfig'),'utf8'));
  const again = await (await get(replay.fixture.ports[0],'/api/v1/namespaces/benchmark/pods/load-0/log')).text();
  assert.equal(again.length,large.length);
});

test('occupied recorded ports are rejected without replacing the running fixture',{timeout:15000},async t => {
  const state = owned(t), first = await start(state,'first');
  const next = path.join(state.root,'occupied'); fs.mkdirSync(next);
  await assert.rejects(execute(process.execPath,[api,next,path.join(first.directory,'kubeconfig.fixture.json')],{timeout:5000}),error => error.code !== 0 && /EADDRINUSE/.test(error.stderr));
  assert.equal(fs.existsSync(path.join(next,'ready')),false);
  assert.deepEqual(await (await get(first.fixture.ports[0],'/api')).json(),{versions:['v1']});
});

test('malformed replay metadata is rejected before readiness',{timeout:10000},async t => {
  const state = owned(t), directory = path.join(state.root,'bad'); fs.mkdirSync(directory);
  const input = path.join(state.root,'bad.json'); fs.writeFileSync(input,'{invalid');
  await assert.rejects(execute(process.execPath,[api,directory,input],{timeout:5000}),error => /Invalid replay fixture JSON/.test(error.stderr));
  assert.equal(fs.existsSync(path.join(directory,'ready')),false);
});

test('a replay from another fixture implementation is rejected',{timeout:15000},async t => {
  const state = owned(t), first = await start(state,'first');
  const input = path.join(state.root,'changed.json');
  fs.writeFileSync(input,JSON.stringify({...first.fixture,sourceSha256:'0'.repeat(64)}));
  const directory = path.join(state.root,'changed'); fs.mkdirSync(directory);
  await assert.rejects(execute(process.execPath,[api,directory,input],{timeout:5000}),error => /Unsupported or inconsistent replay fixture/.test(error.stderr));
  assert.equal(fs.existsSync(path.join(directory,'ready')),false);
});
