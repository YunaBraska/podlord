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
for (const version of ['6.11.2', '6.11.3']) {
    test(`toolchain cache accepts only the pinned Qt patch: ${version}`, async () => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-qt-patch-'));
        const target = path.join(directory, 'toolchain');
        try {
            const files = [
                'qt/plugins/platforms/libqcocoa.dylib', 'qt/plugins/tls/libqopensslbackend.dylib',
                'qt/qml/QtQuick/Controls/qmldir', 'yaml-cpp/lib/cmake/yaml-cpp/yaml-cpp-config.cmake',
                'yaml-cpp/lib/libyaml-cpp.dylib',
                ...['Multimedia', 'WebSockets', 'ShaderTools'].map(module => `qt/lib/cmake/Qt6${module}/Qt6${module}Config.cmake`)
            ];
            for (const file of files) {
                await fs.mkdir(path.dirname(path.join(target, file)), { recursive: true });
                await fs.writeFile(path.join(target, file), '');
            }
            for (const [file, output] of [['qt/bin/qmake', version], ['qt/bin/qsb', ''], ['openssl/bin/openssl', 'OpenSSL 3.5.9 LTS']]) {
                await fs.mkdir(path.dirname(path.join(target, file)), { recursive: true });
                await fs.writeFile(path.join(target, file), `#!/bin/sh\nprintf '%s\\n' '${output}'\n`, { mode: 0o755 });
            }
            await fs.symlink('qt', path.join(target, 'qt-sdk'));
            await fs.writeFile(path.join(target, 'build-inputs.txt'),
                `Qt: ${version} official\nyaml-cpp: 0.9.0\nOpenSSL: 3.5.9 LTS\nBuild architecture: ${process.arch === 'x64' ? 'x86_64' : process.arch}\nCandidate deployment floor: macOS 13.0\n`);
            if (version === '6.11.3') {
                const result = await execute('/bin/sh', [installer, '--check', target]);
                assert.match(result.stdout, /Pinned public macOS toolchain validated/);
            } else {
                await assert.rejects(execute('/bin/sh', [installer, '--check', target]), error => error.code === 1);
            }
            assert.equal(await fs.readlink(path.join(target, 'qt-sdk')), 'qt');
        } finally { await fs.rm(directory, { recursive: true, force: true }); }
    });
}
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
test('toolchain cache validation refuses relative destinations', async () => reject(['--check', 'relative'], /must be absolute/));
for (const scenario of ['missing', 'incomplete']) {
    test(`toolchain cache validation rejects ${scenario} inputs without modifying them`, async () => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-toolchain-cache-'));
        const target = path.join(directory, 'toolchain');
        try {
            if (scenario === 'incomplete') {
                await fs.mkdir(target);
                await fs.writeFile(path.join(target, 'keep.txt'), 'preserve');
            }
            await assert.rejects(execute('/bin/sh', [installer, '--check', target]), error => error.code === 1);
            if (scenario === 'missing') await assert.rejects(fs.lstat(target), { code: 'ENOENT' });
            else assert.equal(await fs.readFile(path.join(target, 'keep.txt'), 'utf8'), 'preserve');
        } finally { await fs.rm(directory, { recursive: true, force: true }); }
    });
}
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
