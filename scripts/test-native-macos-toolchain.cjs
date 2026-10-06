'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const { test } = require('node:test');
const http = require('node:http');
const execute = promisify(execFile);
const installer = path.join(__dirname, 'install-native-macos-toolchain.sh');
async function reject(args, pattern, env = process.env) {
    await assert.rejects(execute('/bin/sh', [installer, ...args], { env }), error => error.code === 2 && pattern.test(error.stderr));
}
test('failed external registry installation cleans only its new toolchain', { timeout: 60000 }, async () => {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-toolchain-failure-'));
    const target = path.join(directory, 'target');
    const sentinel = path.join(directory, 'keep.txt');
    let requests = 0;
    // Only the external Python package registry is unavailable; the installer is real.
    const registry = http.createServer((_request, response) => { requests++; response.writeHead(503); response.end('Unavailable'); });
    try {
        await fs.writeFile(sentinel, 'preserve');
        await new Promise(resolve => registry.listen(0, '127.0.0.1', resolve));
        await assert.rejects(execute('/bin/sh', [installer, target], { timeout: 45000, env: {
            ...process.env, PIP_CONFIG_FILE: '/dev/null', PIP_INDEX_URL: `http://127.0.0.1:${registry.address().port}/simple`,
            PIP_EXTRA_INDEX_URL: '', PIP_NO_INDEX: '0', PIP_RETRIES: '0', PIP_TIMEOUT: '1',
            HTTP_PROXY: '', HTTPS_PROXY: '', ALL_PROXY: '', NO_PROXY: '127.0.0.1'
        } }), error => error.code === 1);
        assert.ok(requests > 0, 'The real package installer did not reach its external boundary.');
        await assert.rejects(fs.lstat(target), { code: 'ENOENT' });
        assert.equal(await fs.readFile(sentinel, 'utf8'), 'preserve');
    } finally {
        await new Promise(resolve => registry.close(resolve));
        await fs.rm(directory, { recursive: true, force: true });
    }
});
test('toolchain installer requires its destination', async () => reject([], /Usage:/));
test('toolchain installer refuses extra arguments', async () => reject(['/tmp/new', 'extra'], /Usage:/));
test('toolchain installer refuses relative destinations', async () => reject(['relative'], /must be absolute/));
for (const scenario of ['directory', 'file', 'symlink', 'broken-link', 'missing-tool']) {
    test(`toolchain installation preserves existing state: ${scenario}`, async () => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-toolchain-test-'));
        const target = path.join(directory, 'target');
        const sentinel = path.join(directory, 'keep.txt');
        try {
            await fs.writeFile(sentinel, 'preserve');
            if (scenario === 'directory') await fs.mkdir(target);
            if (scenario === 'file') await fs.writeFile(target, 'existing');
            if (scenario === 'symlink' || scenario === 'broken-link') await fs.symlink(scenario === 'symlink' ? sentinel : path.join(directory, 'absent'), target);
            await reject([target], scenario === 'missing-tool' ? /Required tool is missing: cmake/ : /new toolchain directory/,
                scenario === 'missing-tool' ? { ...process.env, PATH: '/usr/bin:/bin' } : process.env);
            assert.equal(await fs.readFile(sentinel, 'utf8'), 'preserve');
            if (scenario === 'file') assert.equal(await fs.readFile(target, 'utf8'), 'existing');
            if (scenario === 'symlink' || scenario === 'broken-link') assert.ok((await fs.lstat(target)).isSymbolicLink());
            if (scenario === 'directory') assert.deepEqual(await fs.readdir(target), []);
            if (scenario === 'missing-tool') await assert.rejects(fs.lstat(target), { code: 'ENOENT' });
        } finally { await fs.rm(directory, { recursive: true, force: true }); }
    });
}
