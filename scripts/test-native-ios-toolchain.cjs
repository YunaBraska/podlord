'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const { test } = require('node:test');
const execute = promisify(execFile);
const installer = path.join(__dirname, 'build-native-ios-toolchain.sh');

for (const [name, args, pattern] of [
    ['missing arguments', [], /Usage:/],
    ['extra arguments', ['iphoneos', '/host', '/output', 'extra'], /Usage:/],
    ['unknown SDK', ['watchos', '/host', '/output'], /Unsupported iOS SDK/],
    ['relative host', ['iphoneos', 'host', '/output'], /Host Qt path must be absolute/],
    ['relative destination', ['iphoneos', '/host', 'output'], /Output directory must be absolute/]
]) {
    test(`iOS toolchain rejects ${name} before writing`, async () => {
        await assert.rejects(execute('/bin/sh', [installer, ...args]), error => error.code === 2 && pattern.test(error.stderr));
    });
}

for (const state of ['directory', 'file', 'symlink', 'broken-link']) {
    test(`iOS toolchain preserves an existing ${state}`, async () => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-ios-toolchain-'));
        const target = path.join(directory, 'target');
        const sentinel = path.join(directory, 'keep.txt');
        try {
            await fs.writeFile(sentinel, 'preserve');
            if (state === 'directory') await fs.mkdir(target);
            if (state === 'file') await fs.writeFile(target, 'existing');
            if (state === 'symlink' || state === 'broken-link') await fs.symlink(state === 'symlink' ? sentinel : path.join(directory, 'absent'), target);
            await assert.rejects(execute('/bin/sh', [installer, 'iphonesimulator', '/host', target]), error => error.code === 2 && /new toolchain directory/.test(error.stderr));
            assert.equal(await fs.readFile(sentinel, 'utf8'), 'preserve');
            if (state === 'directory') assert.deepEqual(await fs.readdir(target), []);
            if (state === 'file') assert.equal(await fs.readFile(target, 'utf8'), 'existing');
            if (state === 'symlink' || state === 'broken-link') assert.ok((await fs.lstat(target)).isSymbolicLink());
        } finally {
            await fs.rm(directory, { recursive: true, force: true });
        }
    });
}

for (const level of ['0', '-1', 'invalid', '1 2', '00']) {
    test(`iOS toolchain rejects parallel level ${level} without creating output`, async () => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-ios-jobs-'));
        const target = path.join(directory, 'target');
        try {
            await assert.rejects(execute('/bin/sh', [installer, 'iphoneos', '/host', target], {
                env: { ...process.env, CMAKE_BUILD_PARALLEL_LEVEL: level }
            }), error => error.code === 2 && /parallel level/.test(error.stderr));
            await assert.rejects(fs.lstat(target), { code: 'ENOENT' });
        } finally {
            await fs.rm(directory, { recursive: true, force: true });
        }
    });
}
