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
