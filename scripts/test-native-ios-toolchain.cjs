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

test('iOS toolchain finalizes privacy manifests from a changed working directory', async t => {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-ios-finalization-'));
    t.after(() => fs.rm(directory, { recursive: true, force: true }));
    const sdk = path.join(directory, 'sdk');
    const work = path.join(directory, 'module-build');
    await fs.mkdir(work);
    await fs.mkdir(path.join(sdk, 'openssl/os-dep/Apple'), { recursive: true });
    await fs.writeFile(path.join(sdk, 'build-inputs.txt'), 'Qt: 6.11.3\nOpenSSL: 3.5.9\nFFmpeg: 7.1.5\n');
    const openssl = '<?xml version="1.0"?><plist version="1.0"><dict/></plist>\n';
    await fs.writeFile(path.join(sdk, 'openssl/os-dep/Apple/PrivacyInfo.xcprivacy'), openssl);
    for (const name of ['avcodec', 'avformat', 'avutil', 'swresample', 'swscale']) {
        const framework = path.join(sdk, `qt/lib/ffmpeg/lib${name}.xcframework/ios-arm64/lib${name}.framework`);
        await fs.mkdir(framework, { recursive: true });
        await fs.writeFile(path.join(framework, `lib${name}`), 'external SDK fixture, not executable');
        await fs.writeFile(path.join(framework, 'Info.plist'), '<plist><dict/></plist>');
    }
    // Exercise the actual finalization command with real CMake. The external SDK
    // files are fixtures. No Podlord installer or manifest implementation is replaced.
    const source = await fs.readFile(installer, 'utf8');
    const setup = source.split('\n').find(line => line.startsWith('SCRIPT_DIR='));
    const finalize = source.split('\n').find(line => line.startsWith('cmake "-DPODLORD_IOS_SDK_ROOT=$ROOT" -P '));
    assert.ok(setup && finalize, 'The source-built SDK must finalize its privacy declarations');
    await execute('/bin/sh', ['-eu', '-c', `${setup}\ncd "$FOREIGN"\n${finalize}`, 'scripts/build-native-ios-toolchain.sh'], {
        cwd: path.resolve(__dirname, '..'), env: { ...process.env, ROOT: sdk, FOREIGN: work }
    });
    assert.equal(await fs.readFile(path.join(sdk, 'ssl/share/podlord/PrivacyInfo.xcprivacy'), 'utf8'), openssl);
    for (const name of ['avcodec', 'avformat', 'avutil', 'swresample', 'swscale']) {
        const declaration = ['avformat', 'avutil'].includes(name) ? 'FFmpegFilePrivacy' : 'FFmpegPrivacy';
        const expected = await fs.readFile(path.join(__dirname, '../native/ios', declaration + '.xcprivacy'), 'utf8');
        assert.equal(await fs.readFile(path.join(sdk, `qt/lib/ffmpeg/lib${name}.xcframework/ios-arm64/lib${name}.framework/PrivacyInfo.xcprivacy`), 'utf8'), expected);
    }
});
