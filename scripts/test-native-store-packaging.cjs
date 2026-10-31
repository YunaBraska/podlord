'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const packageScript = path.join(__dirname, 'package-app-store.sh');
const mergeScript = path.join(__dirname, 'merge-native-macos-apps.sh');
const mac = process.platform === 'darwin';

for (const [name, args, expected] of [
    ['missing arguments', [], /Usage:/],
    ['invalid marketing version', ['1.0;exit', '1'], /Invalid marketing version/],
    ['leading-zero marketing version', ['01.0.0', '1'], /Invalid marketing version/],
    ['oversized build component', ['1.0.0', '10000.1.0'], /Build number/],
    ['invalid build characters', ['1.0.0', '1;exit'], /Build number/],
    ['valid single-component build reaches signing boundary', ['1.0.0', '1'], /APP_STORE_BUNDLE_ID/],
    ['valid three-component build reaches signing boundary', ['1.0.0', '12.1.0'], /APP_STORE_BUNDLE_ID/],
]) {
    test(name, {skip: !mac}, () => {
        const env = {...process.env};
        delete env.APP_STORE_BUNDLE_ID;
        const result = spawnSync('/bin/sh', [packageScript, ...args], {cwd: root, env, encoding: 'utf8'});
        assert.notEqual(result.status, 0);
        assert.match(result.stderr, expected);
    });
}

for (const [stage, code, calls] of [
    ['configure', 17, ['configure']],
    ['build', 23, ['configure', 'build']],
    ['test', 8, ['configure', 'build', 'test']],
    ['none', 0, ['configure', 'build', 'test']],
]) {
    test(`Store CI preserves SDK arguments, logs and exit status: ${stage}`, {skip: !mac}, async t => {
        const workflow = await fs.readFile(path.join(root, '.github/workflows/native.yml'), 'utf8');
        const heading = '      - name: Verify native Mac App Store boundaries\n';
        assert.ok(workflow.includes(heading));
        const step = workflow.split(heading)[1].split('\n      - name:')[0];
        const body = step.split('        run: |\n')[1];
        assert.ok(body, 'Store workflow must expose its actual shell entrypoint');
        const script = body.split('\n').map(line => line.replace(/^ {10}/, '')).join('\n');
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-store-ci guard-'));
        t.after(() => fs.rm(directory, {recursive: true, force: true}));
        const bin = path.join(directory, 'bin');
        await fs.mkdir(bin);
        // Only external build tools are controlled. The owning workflow runs unchanged.
        await fs.writeFile(path.join(bin, 'cmake'), `#!/bin/sh
set -eu
stage=configure
[ "$1" != --build ] || stage=build
printf '%s\\n' "$stage" >> "$RUNNER_TEMP/calls"
if [ "$stage" = configure ]; then
    prefix=0; ssl=0
    for arg in "$@"; do
        [ "$arg" != "-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH" ] || prefix=1
        [ "$arg" != "-DOPENSSL_ROOT_DIR=$OPENSSL_ROOT_DIR" ] || ssl=1
    done
    [ "$prefix" -eq 1 ] && [ "$ssl" -eq 1 ] || exit 31
fi
printf 'fixture-%s\\n' "$stage"
[ "$PODLORD_FAIL_STAGE" != "$stage" ] || exit "$PODLORD_FAIL_CODE"
`, {mode: 0o700});
        await fs.writeFile(path.join(bin, 'ctest'), `#!/bin/sh
printf 'test\\n' >> "$RUNNER_TEMP/calls"
printf 'fixture-test\\n'
[ "$PODLORD_FAIL_STAGE" != test ] || exit "$PODLORD_FAIL_CODE"
`, {mode: 0o700});
        const result = spawnSync('/bin/sh', ['-eu', '-c', script], {
            cwd: root, encoding: 'utf8', timeout: 10000,
            env: {...process.env, PATH: `${bin}:/usr/bin:/bin`, RUNNER_TEMP: directory,
                CMAKE_PREFIX_PATH: '/fixture/qt sdk;/fixture/yaml;/fixture/openssl', OPENSSL_ROOT_DIR: '/fixture/openssl',
                PODLORD_FAIL_STAGE: stage, PODLORD_FAIL_CODE: String(code)},
        });
        assert.ifError(result.error);
        assert.equal(result.status, code, result.stdout + result.stderr);
        assert.ok(result.stdout.includes(`fixture-${stage === 'none' ? 'test' : stage}`), 'Build/test output must remain visible on failure');
        assert.deepEqual((await fs.readFile(path.join(directory, 'calls'), 'utf8')).trim().split('\n'), calls);
    });
}

test('merger preserves an existing output', {skip: !mac}, async t => {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-store-guard-'));
    t.after(() => fs.rm(directory, {recursive: true, force: true}));
    const output = path.join(directory, 'existing.app');
    await fs.mkdir(output);
    await fs.writeFile(path.join(output, 'owned-data'), 'keep');
    const result = spawnSync('/bin/sh', [mergeScript, 'missing-arm.zip', 'missing-x64.zip', output], {encoding: 'utf8'});
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /Output already exists/);
    assert.equal(await fs.readFile(path.join(output, 'owned-data'), 'utf8'), 'keep');
});

test('archive cannot write through an escaping symlink', {skip: !mac}, async t => {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-store-archive-'));
    t.after(() => fs.rm(directory, {recursive: true, force: true}));
    const fixture = path.join(directory, 'fixture');
    const bundle = path.join(fixture, 'podlord-native.app');
    const outside = path.join(directory, 'outside');
    await fs.mkdir(bundle, {recursive: true});
    await fs.mkdir(outside);
    const link = path.join(bundle, 'escape');
    await fs.symlink(outside, link);
    const archive = path.join(directory, 'escape.zip');
    let zipped = spawnSync('/usr/bin/zip', ['-y', archive, 'podlord-native.app/escape'], {cwd: fixture});
    assert.equal(zipped.status, 0);
    await fs.unlink(link);
    await fs.mkdir(link);
    await fs.writeFile(path.join(link, 'written'), 'must not escape');
    zipped = spawnSync('/usr/bin/zip', [archive, 'podlord-native.app/escape/written'], {cwd: fixture});
    assert.equal(zipped.status, 0);
    const output = path.join(directory, 'merged.app');
    const result = spawnSync('/bin/sh', [mergeScript, archive, archive, output], {encoding: 'utf8'});
    assert.notEqual(result.status, 0);
    assert.deepEqual(await fs.readdir(outside), []);
    await assert.rejects(fs.access(output), {code: 'ENOENT'});
});
