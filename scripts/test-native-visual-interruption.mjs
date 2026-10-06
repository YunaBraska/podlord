// Real local Kubernetes interruption complements the external Docker failure tests.
import assert from 'node:assert/strict';
import { execFile, spawn } from 'node:child_process';
import { createWriteStream } from 'node:fs';
import fs from 'node:fs/promises';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';

const execute = promisify(execFile);
const root = fileURLToPath(new URL('../', import.meta.url));
const build = process.env.PODLORD_NATIVE_BUILD_DIR;
const evidence = process.env.PODLORD_VISUAL_EVIDENCE_DIR;
const signal = process.argv[2] || 'SIGTERM';
const script = process.argv[3] || path.join(root, 'scripts/test-native-visual-kubernetes.sh');
assert(build && path.isAbsolute(build), 'Set an absolute PODLORD_NATIVE_BUILD_DIR.');
assert(evidence && path.isAbsolute(evidence), 'Set an absolute PODLORD_VISUAL_EVIDENCE_DIR.');
assert(['SIGHUP', 'SIGINT', 'SIGTERM'].includes(signal), 'Choose SIGHUP, SIGINT or SIGTERM.');
assert(path.isAbsolute(script), 'The runner must be an absolute path.');
assert(process.argv.length <= 4, 'Expected a signal and optional frozen runner path.');
const context = process.env.DOCKER_CONTEXT || (!process.env.DOCKER_HOST
    && (await execute('docker', ['context', 'show'])).stdout.trim());
const endpoint = context ? (await execute('docker', ['context', 'inspect', context, '--format', '{{.Endpoints.docker.Host}}'])).stdout.trim()
    : process.env.DOCKER_HOST;
assert(endpoint?.startsWith('unix://') || endpoint?.startsWith('npipe://'), 'Only a local Docker daemon is permitted.');
const environment = { ...process.env, DOCKER_HOST: endpoint };
delete environment.DOCKER_CONTEXT;
const output = async (command, args) => (await execute(command, args, { timeout: 20000, env: environment })).stdout.trim();
const entries = async directory => fs.readdir(directory).catch(error => {
    if (error.code === 'ENOENT') return [];
    throw error;
});

test(`${signal} during real Kubernetes UI work removes the owned child, container, volumes and temporary profile`, async () => {
    await fs.mkdir(evidence, { recursive: true });
    const directory = await fs.mkdtemp(path.join(evidence, `cleanup-${signal.toLowerCase()}-`));
    const ambient = path.join(directory, 'ambient-tmp');
    await fs.mkdir(ambient, { mode: 0o700 });
    const log = createWriteStream(path.join(directory, 'runner.log'), { mode: 0o600 });
    const runner = spawn('/bin/sh', [script, 'native-fields-e2e'], {
        cwd: root, env: { ...environment, TMPDIR: ambient, PODLORD_VISUAL_EVIDENCE_DIR: directory },
        stdio: ['ignore', 'pipe', 'pipe']
    });
    runner.stdout.on('data', bytes => log.write(bytes));
    runner.stderr.on('data', bytes => log.write(bytes));
    let finished = false;
    const exited = new Promise((resolve, reject) => {
        runner.once('error', reject);
        runner.once('close', (code, termination) => { finished = true; resolve({ code, termination }); });
    });
    try {
        const deadline = Date.now() + 600000;
        let child, run;
        while (!child && !finished && Date.now() < deadline) {
            const children = await output('/usr/bin/pgrep', ['-P', String(runner.pid)]).catch(error => {
                if (error.code === 1) return '';
                throw error;
            });
            for (const pid of children.split(/\s+/).filter(Boolean)) {
                const command = await output('/bin/ps', ['-p', pid, '-o', 'command=']).catch(error => {
                    if (error.code === 1) return '';
                    throw error;
                });
                const suffix = command.indexOf('/metric-filter-ui-test ');
                if (suffix < 0) continue;
                const candidate = command.slice(0, suffix);
                if (path.dirname(candidate) !== path.join(build, 'e2e') || !path.basename(candidate).startsWith('visual-run.')) continue;
                child = pid; run = candidate; break;
            }
            if (!child) await new Promise(resolve => setTimeout(resolve, 250));
        }
        assert(child, 'The runner did not reach its owned real UI child within ten minutes.');
        const owner = `podlord-${path.basename(run).toLowerCase().replaceAll('.', '-')}`;
        const state = JSON.parse(await output('docker', ['inspect', '--type', 'container', owner, '--format', '{{json .}}']));
        assert.equal(state.Config.Labels['podlord.native.e2e'], owner);
        const volumes = state.Mounts.filter(mount => mount.Type === 'volume').map(mount => mount.Name);
        await new Promise(resolve => setTimeout(resolve, 500));
        const temporary = await entries(path.join(run, 'tmp'));
        assert(temporary.length > 0, 'The UI temporary profile must be inside the owned run, not ambient TMPDIR.');
        assert(runner.kill(signal), 'Could not signal the owned runner.');
        const result = await exited;
        assert.equal(result.code, { SIGHUP: 129, SIGINT: 130, SIGTERM: 143 }[signal]);
        const alive = await output('/bin/ps', ['-p', child, '-o', 'pid=']).catch(error => {
            if (error.code === 1) return '';
            throw error;
        });
        assert.equal(alive, '', 'Owned UI child survived cleanup.');
        assert.equal(await output('docker', ['container', 'ls', '-a', '--filter', `label=podlord.native.e2e=${owner}`, '--format', '{{.ID}}']), '');
        for (const volume of volumes) assert.equal(await output('docker', ['volume', 'ls', '--filter', `name=^${volume}$`, '--format', '{{.Name}}']), '');
        await assert.rejects(fs.access(run), { code: 'ENOENT' });
        assert.deepEqual(await entries(ambient), []);
        await fs.writeFile(path.join(directory, 'cleanup-proof.json'), JSON.stringify({
            signal, owner, runnerExitCode: result.code, ownedTemporaryDirectoriesBefore: temporary.length,
            childStopped: true, containerRemoved: true, volumesRemoved: true, runRemoved: true
        }, null, 2) + '\n', { mode: 0o600 });
    } finally {
        if (!finished) { runner.kill('SIGTERM'); await exited; }
        log.end();
        await fs.rm(ambient, { recursive: true, force: true });
    }
});
