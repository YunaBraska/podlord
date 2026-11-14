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

// Execute the owning report/publish phase without SDK builds, signing credentials or devices.
// Native package validity remains covered by the existing physical-package checks.
for (const platform of ['macos', 'linux']) {
    for (const [scenario, installed, download, dependencies, valid] of [
        ['below targets', '42', '43', 'pass', true],
        ['at targets', '100000000', '50000000', 'pass', true],
        ['over targets', '100000001', '50000001', 'pass', true],
        ['empty installed payload', '0', '43', 'pass', false],
        ['empty archive', '42', '0', 'pass', false],
        ['malformed installed measurement', 'NaN', '43', 'pass', false],
        ['malformed archive measurement', '42', 'NaN', 'pass', false],
        ['unavailable archive measurement', '42', 'unavailable', 'pass', false],
        ...(platform === 'macos' ? [['dependency failure over targets', '100000001', '50000001', 'fail', false]] : []),
    ]) {
        test(`${platform} package report preserves advisory evidence: ${scenario}`, async t => {
            const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-size-report-'));
            t.after(() => fs.rm(directory, {recursive: true, force: true}));
            const source = await fs.readFile(path.join(__dirname, `check-native-${platform}-package.sh`), 'utf8');
            const marker = platform === 'macos' ? 'DOWNLOAD=$(/usr/bin/stat' : 'find "$OUTPUT/podlord" -type f -printf';
            assert.ok(source.includes(marker));
            const bin = path.join(directory, 'bin'), work = path.join(directory, 'work'), output = path.join(directory, 'out');
            const evidence = platform === 'macos' ? path.join(work, 'evidence') : output;
            const app = path.join(evidence, platform === 'macos' ? 'podlord-native.app' : 'podlord');
            await fs.mkdir(path.join(app, 'Contents/MacOS'), {recursive: true});
            await fs.mkdir(bin);
            await fs.mkdir(work, {recursive: true});
            await fs.writeFile(path.join(app, 'Contents/MacOS/podlord-native'), 'retained payload');
            for (const file of ['podlord-native.zip', 'libraries.txt', 'runtime-dependencies.txt']) await fs.writeFile(path.join(evidence, file), 'retained evidence');
            // Only the external filesystem tools are controlled. Reporting and publication run unchanged.
            for (const [name, body] of [
                ['stat', 'if [ "$TEST_DOWNLOAD" = unavailable ]; then exit 29; fi; printf "%s\\n" "$TEST_DOWNLOAD"'],
                ['find', 'printf "%s\\n" "$TEST_INSTALLED"'],
                ['ldd', 'printf "fixture C library\\n"'],
                ['sha256sum', 'exec /usr/bin/shasum -a 256 "$@"'],
            ]) await fs.writeFile(path.join(bin, name), `#!/bin/sh\n${body}\n`, {mode: 0o700});
            const phase = source.slice(source.indexOf(marker));
            // macOS deliberately pins stat by absolute path. Bind that external command locally, not globally.
            const result = spawnSync('/bin/sh', ['-eu', '-c', `stat() { "${bin}/stat" "$@"; }\n${phase.replaceAll('/usr/bin/stat', 'stat')}`], {
                encoding: 'utf8', timeout: 10000,
                env: {...process.env, PATH: `${bin}:${process.env.PATH}`, WORK: work, OUTPUT: output, APP: app,
                    ARCH: platform === 'macos' ? 'arm64' : 'x64', STORE: 'OFF', DEPENDENCIES: dependencies,
                    TEST_INSTALLED: installed, TEST_DOWNLOAD: download},
            });
            assert.ifError(result.error);
            assert.equal(result.signal, null, result.stderr);
            assert.equal(result.status === 0, valid, result.stdout + result.stderr);
            if (valid || dependencies === 'fail') {
                const report = await fs.readFile(path.join(output, 'package-evidence.txt'), 'utf8');
                assert.match(report, /Advisory targets:/);
                assert.ok(report.includes(installed) && report.includes(download));
                assert.equal(await fs.readFile(path.join(output, platform === 'macos' ? 'podlord-native.app/Contents/MacOS/podlord-native' : 'podlord/Contents/MacOS/podlord-native'), 'utf8'), 'retained payload');
                assert.ok((await fs.stat(path.join(output, platform === 'macos' ? 'podlord-native.zip' : 'podlord-native-linux-x64.tar.gz'))).size > 0);
                if (scenario.includes('over targets')) {
                    for (const metric of ['installed', 'download']) assert.match(report, new RegExp(`WARNING: package-size advisory:.*metric=${metric} measured_bytes=\\d+ target_bytes=\\d+`));
                    assert.match(result.stderr, /WARNING: package-size advisory:/);
                } else assert.doesNotMatch(report, /WARNING:/);
            }
            if (dependencies === 'fail') assert.match(result.stderr, /Package gate failed/);
        });
    }
}

for (const [scenario, measurement, valid] of [
    ['below target', '42 /data/app/base.apk\n', true],
    ['at target', '100000000 /data/app/base.apk\n', true],
    ['over target', '100000001 /data/app/base.apk\n', true],
    ['empty measurement', '', false],
    ['empty installed payload', '0 /data/app/base.apk\n', false],
    ['negative measurement', '-1 /data/app/base.apk\n', false],
    ['malformed measurement', '1e8 /data/app/base.apk\n', false],
    ['unsafe total', '9007199254740991 /data/app/base.apk\n1 /data/app/extra\n', false],
    ['unavailable measurement', null, false],
]) {
    test(`Android public size report: ${scenario}`, async t => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-android-size-'));
        t.after(() => fs.rm(directory, {recursive: true, force: true}));
        const source = await fs.readFile(path.join(__dirname, 'test-native-android-device.sh'), 'utf8');
        const marker = 'node - "$out" <<\'NODE\'\n';
        assert.ok(source.includes(marker));
        const report = source.split(marker).at(-1).split('\nNODE')[0];
        if (measurement !== null) await fs.writeFile(path.join(directory, 'installed-regular-file-bytes.txt'), measurement);
        await fs.writeFile(path.join(directory, 'system-image.txt'), 'system-images;android-36;google_apis;arm64-v8a\n');
        await fs.writeFile(path.join(directory, 'page-size.txt'), '4096\n');
        const result = spawnSync(process.execPath, ['-', directory], {input: report, encoding: 'utf8', timeout: 10000});
        assert.ifError(result.error);
        assert.equal(result.status === 0, valid, result.stdout + result.stderr);
        if (valid) {
            const data = JSON.parse(await fs.readFile(path.join(directory, 'result.json'), 'utf8'));
            assert.equal(data.status, 'passed');
            assert.equal(data.sizePolicy, 'advisory');
            assert.equal(data.installedRegularFileBytes, Number(measurement.split(' ')[0]));
            assert.equal(data.installedLimitBytes, 100000000);
            assert.equal(data.sizeStatus, scenario === 'over target' ? 'advisory-exceeded' : 'within-target');
            if (scenario === 'over target') assert.match(result.stderr, /WARNING: package-size advisory:.*measured_bytes=100000001 target_bytes=100000000/);
            else assert.equal(data.warnings.length, 0);
        } else await assert.rejects(fs.access(path.join(directory, 'result.json')), {code: 'ENOENT'});
    });
}

for (const [scenario, bytes, valid, commandStatus = 0] of [
    ['over target', '50000001', true],
    ['empty', '0', false],
    ['missing', '', false],
    ['negative', '-1', false],
    ['fractional', '1.5', false],
    ['malformed', 'NaN', false],
    ['failed with partial output', '50000001', false, 29],
    ['failed without output', '', false, 31],
]) {
    test(`release collector keeps size advisory artifacts: ${scenario}`, async t => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-release-size-'));
        t.after(() => fs.rm(directory, {recursive: true, force: true}));
        const workflow = await fs.readFile(path.join(root, '.github/workflows/native-qualify.yml'), 'utf8');
        const step = workflow.split('      - name: Collect verified native desktop candidates\n')[1].split('\n      - uses:')[0];
        const script = step.split('        run: |\n')[1].split('\n').map(line => line.replace(/^ {10}/, '')).join('\n');
        for (const relative of [
            'native-package-direct-macos-15-source/native-package/podlord-native.zip',
            'native-package-direct-macos-15-intel-source/native-package/podlord-native.zip',
            'native-windows-x64-source/podlord-windows-x64.zip',
            'native-linux-trial-x64-source/package/podlord-native-linux-x64.tar.gz',
            'native-linux-trial-arm64-source/package/podlord-native-linux-arm64.tar.gz',
        ]) {
            const file = path.join(directory, 'native-input', relative);
            await fs.mkdir(path.dirname(file), {recursive: true});
            await fs.writeFile(file, 'retained candidate');
        }
        const bin = path.join(directory, 'bin'); await fs.mkdir(bin);
        await fs.writeFile(path.join(bin, 'wc'), '#!/bin/sh\nprintf "%s\\n" "$TEST_BYTES"\nexit "$TEST_WC_STATUS"\n', {mode: 0o700});
        await fs.writeFile(path.join(bin, 'sha256sum'), '#!/bin/sh\nexec /usr/bin/shasum -a 256 "$@"\n', {mode: 0o700});
        const result = spawnSync('/bin/sh', ['-eu', '-c', script], {cwd: directory, encoding: 'utf8', timeout: 10000,
            env: {...process.env, PATH: `${bin}:${process.env.PATH}`, SOURCE_COMMIT: 'source', TEST_BYTES: bytes, TEST_WC_STATUS: String(commandStatus)}});
        assert.ifError(result.error);
        assert.equal(result.status, valid ? 0 : commandStatus || 1, result.stdout + result.stderr);
        if (valid) {
            assert.equal(await fs.readFile(path.join(directory, 'native-candidate/podlord-native-win-x64.zip'), 'utf8'), 'retained candidate');
            assert.match(await fs.readFile(path.join(directory, 'native-candidate/size-evidence.txt'), 'utf8'), /measured_bytes=50000001 target_bytes=50000000/);
            assert.equal((await fs.readFile(path.join(directory, 'native-candidate/SHA256SUMS'), 'utf8')).trim().split('\n').length, 5);
        } else assert.match(result.stderr, commandStatus ? /Candidate download measurement failed/ : /Invalid candidate download measurement/);
    });
}

for (const [scenario, output, status] of [
    ['partial output', '42\n', 29],
    ['no output', '', 31],
]) {
    test(`Store installed measurement preserves command failure: ${scenario}`, async t => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-store-measurement-'));
        t.after(() => fs.rm(directory, {recursive: true, force: true}));
        const source = await fs.readFile(packageScript, 'utf8');
        const measurement = source.indexOf('find "$APP" -type f -exec /usr/bin/stat');
        assert.ok(measurement > 0);
        const script = source.slice(source.lastIndexOf('\n', measurement) + 1);
        const result = spawnSync('/bin/sh', ['-eu', '-c',
            `find() { printf '%s' "$TEST_OUTPUT"; return "$TEST_STATUS"; }\n${script}`], {
            encoding: 'utf8', timeout: 10000,
            env: {...process.env, APP: directory, DIST: directory, TEST_OUTPUT: output,
                TEST_STATUS: String(status), version: '1.2.3', build_number: '4.5.6'},
        });
        assert.ifError(result.error);
        assert.equal(result.status, status, result.stdout + result.stderr);
        await assert.rejects(fs.access(path.join(directory, 'native-build.txt')), {code: 'ENOENT'});
    });
}

for (const [scenario, bytes, valid] of [
    ['below target', '42', true],
    ['at target', '200000000', true],
    ['over target', '200000001', true],
    ['missing', '', false],
    ['empty payload', '0', false],
    ['negative', '-1', false],
    ['fractional', '1.5', false],
    ['malformed', 'NaN', false],
    ['out-of-range integer', '99999999999999999999999999', false],
]) {
    test(`Universal2 Store installed size advisory: ${scenario}`, async t => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-store-installed-size-'));
        t.after(() => fs.rm(directory, {recursive: true, force: true}));
        const source = await fs.readFile(packageScript, 'utf8');
        const marker = 'case "$INSTALLED" in';
        assert.ok(source.includes(marker));
        const script = source.slice(source.indexOf(marker));
        const app = path.join(directory, 'Podlord.app'); await fs.mkdir(app);
        await fs.writeFile(path.join(app, 'retained-payload'), 'signed input bytes');
        const result = spawnSync('/bin/sh', ['-eu', '-c', script], {encoding: 'utf8', timeout: 10000,
            env: {...process.env, DIST: directory, INSTALLED: bytes, version: '1.2.3', build_number: '4.5.6'}});
        assert.ifError(result.error);
        assert.equal(result.status, valid ? 0 : 1, result.stdout + result.stderr);
        assert.equal(await fs.readFile(path.join(app, 'retained-payload'), 'utf8'), 'signed input bytes');
        if (valid) {
            const evidence = await fs.readFile(path.join(directory, 'size-evidence.txt'), 'utf8');
            assert.ok(evidence.includes(`Universal2 Store installed bytes: ${bytes}\n`));
            assert.match(evidence, /Advisory installed target: 200000000/);
            assert.match(await fs.readFile(path.join(directory, 'native-build.txt'), 'utf8'), /architectures=arm64 x86_64/);
            if (scenario === 'over target') {
                assert.match(evidence, /WARNING: package-size advisory:.*metric=installed measured_bytes=200000001 target_bytes=200000000/);
                assert.match(result.stderr, /WARNING: package-size advisory:/);
            } else assert.doesNotMatch(evidence, /WARNING:/);
        } else {
            assert.match(result.stderr, /Invalid Universal2 Store installed measurement/);
            await assert.rejects(fs.access(path.join(directory, 'native-build.txt')), {code: 'ENOENT'});
        }
    });
}

for (const [scenario, bytes, valid] of [['over target', 80000001, true], ['empty', 0, false], ['unavailable', null, false]]) {
    test(`signed Store PKG size advisory: ${scenario}`, {skip: !mac}, async t => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-store-size-'));
        t.after(() => fs.rm(directory, {recursive: true, force: true}));
        const workflow = await fs.readFile(path.join(root, '.github/workflows/app-store.yml'), 'utf8');
        const start = "          bytes=$(/usr/bin/stat -f '%z' dist/app-store/Podlord.pkg)\n";
        assert.ok(workflow.includes(start));
        const script = workflow.slice(workflow.indexOf(start)).split('\n      - uses:')[0].split('\n').map(line => line.replace(/^ {10}/, '')).join('\n');
        const dist = path.join(directory, 'dist/app-store'); await fs.mkdir(dist, {recursive: true});
        if (bytes !== null) { await fs.writeFile(path.join(dist, 'Podlord.pkg'), ''); await fs.truncate(path.join(dist, 'Podlord.pkg'), bytes); }
        const result = spawnSync('/bin/sh', ['-eu', '-c', script], {cwd: directory, encoding: 'utf8', timeout: 10000});
        assert.ifError(result.error);
        assert.equal(result.status === 0, valid, result.stdout + result.stderr);
        if (valid) {
            assert.equal((await fs.stat(path.join(dist, 'Podlord.pkg'))).size, bytes);
            assert.match(await fs.readFile(path.join(dist, 'size-evidence.txt'), 'utf8'), /measured_bytes=80000001 target_bytes=80000000/);
            assert.match(result.stderr, /WARNING: package-size advisory:/);
        }
    });
}

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

function ciJob(workflow, name) {
    const heading = `  ${name}:\n`;
    assert.ok(workflow.includes(heading), `Required independent job is missing: ${name}`);
    return workflow.split(heading)[1].split(/\n  [a-z][a-z0-9-]*:\n/)[0];
}

test('pull requests and snapshots reuse the same native qualification', async () => {
    const workflow = await fs.readFile(path.join(root, '.github/workflows/ci.yml'), 'utf8');
    const request = await fs.readFile(path.join(root, '.github/workflows/build-pr.yml'), 'utf8');
    const snapshot = await fs.readFile(path.join(root, '.github/workflows/build-merge.yml'), 'utf8');
    const pullRequest = request.split('  pull_request:\n')[1].split('\n  workflow_dispatch:')[0];
    assert.doesNotMatch(pullRequest, /paths|branches|if:/);
    for (const event of ['opened', 'synchronize', 'reopened', 'ready_for_review'])
        assert.ok(pullRequest.includes('      - ' + event));
    assert.doesNotMatch(workflow, /  pull_request:|  push:/);
    assert.match(workflow, /  workflow_dispatch:/);
    assert.match(workflow, /  workflow_call:/);
    assert.match(ciJob(request, 'build'), /uses: \.\/\.github\/workflows\/ci\.yml/);
    assert.match(ciJob(snapshot, 'build'), /uses: \.\/\.github\/workflows\/ci\.yml/);
    assert.match(ciJob(workflow, 'native-preflight'), /uses: \.\/\.github\/workflows\/native-qualify\.yml/);
    assert.match(ciJob(workflow, 'build'), /if: \$\{\{ always\(\) \}\}/);
    assert.match(ciJob(workflow, 'build'), /needs: native-preflight/);
});

for (const result of ['success', 'failure', 'cancelled', 'skipped']) {
    test('required native build result: ' + result, async () => {
        const workflow = await fs.readFile(path.join(root, '.github/workflows/ci.yml'), 'utf8');
        const script = ciJob(workflow, 'build').split('        run: |\n')[1]
            .trimEnd().split('\n').map(line => line.replace(/^ {10}/, '')).join('\n');
        const actual = spawnSync('/bin/sh', ['-eu', '-c', script], {encoding: 'utf8', timeout: 10000,
            env: {...process.env, NATIVE_RESULT: result}});
        assert.ifError(actual.error);
        assert.equal(actual.status, result === 'success' ? 0 : 1, actual.stdout + actual.stderr);
    });
}

test('snapshots never create a GitHub release or release assets', async () => {
    const workflow = await fs.readFile(path.join(root, '.github/workflows/build-merge.yml'), 'utf8');
    assert.match(workflow, /push:\n    branches:\n      - main/);
    assert.match(ciJob(workflow, 'build'), /uses: \.\/\.github\/workflows\/ci\.yml/);
    assert.doesNotMatch(workflow, /contents: write|wc_dotnet_create_github_release|release-assets|SNAPSHOT|semver-info-action/);
});

test('stable releases qualify once, sign direct macOS assets and prepare the Store package', async () => {
    const release = await fs.readFile(path.join(root, '.github/workflows/release.yml'), 'utf8');
    const upload = await fs.readFile(path.join(root, '.github/workflows/app-store-upload.yml'), 'utf8');
    const direct = await fs.readFile(path.join(root, 'scripts/package-direct-macos.sh'), 'utf8');
    assert.match(release, /# yuna-release: true/);
    assert.match(release, /github\.event\.repository\.default_branch/);
    assert.doesNotMatch(release, /GITHUB_DEFAULT_BRANCH/);
    assert.match(ciJob(release, 'qualify'), /uses: \.\/\.github\/workflows\/native-qualify\.yml/);
    assert.match(ciJob(release, 'direct_macos'), /SOURCE_COMMIT/);
    assert.match(ciJob(release, 'direct_macos'), /Sign and notarize direct macOS packages/);
    assert.match(ciJob(release, 'app_store_package'), /uses: \.\/\.github\/workflows\/app-store\.yml/);
    assert.match(ciJob(release, 'release'), /wc_dotnet_create_github_release\.yml@[a-f0-9]{40}/);
    assert.doesNotMatch(release, /app-store-upload\.yml/);
    assert.match(upload, /workflow_dispatch:/);
    assert.match(upload, /\.path == "\.github\/workflows\/app-store\.yml" or \.path == "\.github\/workflows\/release\.yml"/);
    for (const command of ['notarytool submit', 'stapler staple', 'spctl --assess', 'podlord-native-macos-$ARCH.dmg'])
        assert.ok(direct.includes(command), `Direct macOS packaging lost ${command}`);
});

test('macOS failure diagnostics retain only new owned crash reports', async t => {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-macos-crash-evidence-'));
    t.after(() => fs.rm(directory, {recursive: true, force: true}));
    const workflow = await fs.readFile(path.join(root, '.github/workflows/native.yml'), 'utf8');
    const step = ciJob(workflow, 'macos').split('      - name: Native tests, benchmarks and coverage report\n')[1]
        .split('\n      - name:')[0];
    const marker = 'reports="$HOME/Library/Logs/DiagnosticReports"';
    assert.ok(step.includes(marker), 'The failing run must retain its original owned crash reports');
    const script = step.slice(step.indexOf(marker)).split('\n          fi')[0]
        .split('\n').map(line => line.replace(/^ {12}/, '')).join('\n');
    const reports = path.join(directory, 'home/Library/Logs/DiagnosticReports');
    const output = path.join(directory, 'run');
    await fs.mkdir(reports, {recursive: true});
    await fs.mkdir(output);
    const stamp = path.join(output, 'native-tests.started');
    await fs.writeFile(stamp, ''); await fs.utimes(stamp, 2, 2);
    const expected = ['workspace_ui_test-new.ips', 'workspace_ui_test-new.crash', 'alert_ui_test-new.ips'];
    for (const name of [...expected, 'workspace_ui_test-old.ips', 'Family-new.ips', 'workspace_ui_test-new.txt']) {
        const file = path.join(reports, name);
        await fs.writeFile(file, name);
        await fs.utimes(file, name.includes('-old.') ? 1 : 3, name.includes('-old.') ? 1 : 3);
    }
    await fs.symlink(path.join(reports, 'Family-new.ips'), path.join(reports, 'workspace_ui_test-link.ips'));
    const result = spawnSync('/bin/sh', ['-eu', '-c', script], {encoding: 'utf8', timeout: 10000,
        env: {...process.env, HOME: path.join(directory, 'home'), RUNNER_TEMP: output}});
    assert.ifError(result.error);
    assert.equal(result.status, 0, result.stdout + result.stderr);
    const copied = path.join(output, 'native-crash-reports');
    assert.deepEqual((await fs.readdir(copied)).sort(), expected.sort());
    for (const name of expected) assert.equal(await fs.readFile(path.join(copied, name), 'utf8'), name);
    assert.equal((await fs.readdir(reports)).length, 7, 'Original reports must remain untouched');
});

test('macOS package matrix is independent of the complete native test job', async () => {
    const workflow = await fs.readFile(path.join(root, '.github/workflows/native.yml'), 'utf8');
    const native = ciJob(workflow, 'macos');
    const packages = ciJob(workflow, 'macos-package');
    for (const job of [native, packages]) {
        assert.match(job, /runner: \[macos-15, macos-15-intel\]/);
        assert.match(job, /timeout-minutes: 90/);
        assert.match(job, /fail-fast: false/);
    }
    assert.doesNotMatch(packages, /^    needs:/m);
    assert.match(packages, /channel: \[direct, store\]/);
    assert.match(native, /PODLORD_TEST_JOBS: '6'/);
    assert.match(native, /\/bin\/sh scripts\/test-native\.sh/);
    assert.match(native, /name: native-preflight-\$\{\{ matrix\.runner \}\}-\$\{\{ github\.sha \}\}/);
    for (const report of ['report.txt', 'native.profdata', 'benchmark.json', 'source-benchmark.json']) {
        assert.ok(native.includes('/podlord-native-build/coverage/' + report));
    }
    assert.doesNotMatch(native, /check-native-macos-package\.sh|test-native-macos-dependencies\.cjs|test-native-tls\.cjs|ctest --test-dir "\$store"/);
    assert.match(packages, /name: native-package-\$\{\{ matrix\.channel \}\}-\$\{\{ matrix\.runner \}\}-\$\{\{ github\.sha \}\}/);
});

test('macOS package jobs share pinned setup and retain every package scenario', async () => {
    const workflow = await fs.readFile(path.join(root, '.github/workflows/native.yml'), 'utf8');
    const action = await fs.readFile(path.join(root, '.github/actions/native-macos-toolchain/action.yml'), 'utf8');
    for (const job of [ciJob(workflow, 'macos'), ciJob(workflow, 'macos-package')]) {
        assert.equal(job.split('uses: ./.github/actions/native-macos-toolchain').length - 1, 1);
        assert.doesNotMatch(job, /brew install|uses: actions\/cache|\/bin\/sh scripts\/install-native-macos-toolchain\.sh/);
    }
    assert.match(action, /using: composite/);
    for (const command of ['brew install cmake ninja python',
        'node --test scripts/test-native-macos-toolchain.cjs scripts/test-native-store-packaging.cjs',
        'xcodebuild -version', 'xcrun --sdk macosx --show-sdk-path', 'xcrun --sdk macosx --show-sdk-version',
        'xcrun --sdk macosx clang --version', 'shasum -a 256',
        '/bin/sh scripts/install-native-macos-toolchain.sh --check "$RUNNER_TEMP/native-toolchain"']) {
        assert.ok(action.includes(command), `Shared setup lost: ${command}`);
    }
    assert.ok(action.includes("native-macos-toolchain-v1-${{ runner.os }}-${{ runner.arch }}-${{ steps.macos-toolchain-key.outputs.abi }}-${{ hashFiles('scripts/install-native-macos-toolchain.sh', 'scripts/qt-quick-shutdown.cmake', 'scripts/patches/qtdeclarative-6.11.3-shutdown.diff', 'native/tests/qt_shutdown/**') }}"));
    const publicCache = ['qt', 'qt-sdk', 'yaml-cpp', 'openssl', 'build-inputs.txt']
        .map(part => '${{ runner.temp }}/native-toolchain/' + part);
    for (const name of ['Restore exact public macOS toolchain', 'Retain validated public macOS toolchain']) {
        const step = action.split(`    - name: ${name}\n`)[1]?.split('\n    - name:')[0];
        assert.ok(step, `Shared cache step is missing: ${name}`);
        const cached = step.split('        path: |\n')[1]?.trim().split('\n').map(line => line.trim());
        assert.deepEqual(cached, publicCache, 'Cache must contain only the existing public SDK inputs');
    }
    const packages = ciJob(workflow, 'macos-package');
    const drivers = packages.split('      - name: Build direct package test drivers\n')[1]?.split('\n      - name:')[0];
    assert.ok(drivers, 'The direct package job must build its own real test drivers');
    for (const argument of ['-DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"', '-DOPENSSL_ROOT_DIR="$OPENSSL_ROOT_DIR"',
        '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON', '-DPODLORD_COVERAGE=OFF',
        '--target application_startup_test tls-connection-test']) assert.ok(drivers.includes(argument), argument);
    for (const [name, channel] of [
        ['Build direct package test drivers', 'direct'],
        ['Verify native Mac App Store boundaries', 'store'],
        ['Build and inspect the native Store slice', 'store'],
        ['Build and inspect the native macOS package', 'direct'],
        ['Verify packaged dependency boundaries', 'direct'],
        ['Verify packaged Cocoa startup and command-line metadata', 'direct'],
        ['Verify packaged mutual TLS and certificate rejection', 'direct'],
    ]) {
        const step = packages.split(`      - name: ${name}\n`)[1]?.split('\n      - name:')[0];
        assert.ok(step?.includes(`!cancelled() && matrix.channel == '${channel}'`), `Missing channel/failure boundary: ${name}`);
        if (name.startsWith('Verify packaged')) assert.ok(step.includes("steps.package.outcome == 'success'"));
    }
    for (const scenarios of [
        'default ambient_kubeconfig ambient_exec profile_file profile_symlink explicit_override explicit_empty',
        'help version help_invalid_profile version_invalid_profile unknown_option',
        'trusted untrusted_ca untrusted_client',
    ]) assert.ok(packages.includes(`for scenario in ${scenarios}; do`), `Lost package scenarios: ${scenarios}`);
    assert.ok(packages.includes("--tests-regex '^native\\.(store\\.|authentication\\.(eks|gke|oci)_|source_import\\.(gke|oci)_|ui\\.gke_prompt_)' --no-tests=error --output-on-failure --parallel 2"));
    assert.ok(packages.includes('--target store-boundary-test podlord-native native-eks-test native-gke-test native-oci-test gke-import-ui-test oci-import-ui-test workspace_ui_test --parallel 3'));
    for (const evidence of ['native-toolchain.log', 'native-toolchain/build-inputs.txt', 'native-macos-cache-inputs.log',
        'native-store.log', 'native-store-frames', 'native-store-package.log', 'native-store-package/podlord-native.zip',
        'native-store-package/package-evidence.txt', 'native-package-drivers.log', 'native-package.log',
        'native-package-startup.log', 'native-package-boundaries.log', 'native-package-tls.log',
        'native-package/package-evidence.txt', 'native-package/runtime-dependencies.txt',
        'native-package/libraries.txt', 'native-package/podlord-native.zip']) {
        assert.ok(packages.includes('${{ runner.temp }}/' + evidence), `Lost package evidence: ${evidence}`);
    }
});

test('native package consumers await the complete workflow and select their own channel', async () => {
    const store = await fs.readFile(path.join(root, '.github/workflows/app-store.yml'), 'utf8');
    const qualification = await fs.readFile(path.join(root, '.github/workflows/native-qualify.yml'), 'utf8');
    for (const workflow of [store, qualification]) {
        assert.ok(ciJob(workflow, 'native-preflight').includes('uses: ./.github/workflows/native.yml'));
        assert.doesNotMatch(workflow, /native-preflight-macos-15/);
    }
    assert.match(store, /needs: native-preflight/);
    const qualificationNeeds = ciJob(qualification, 'candidate').match(/^    needs: \[([^\]\r\n]+)\]$/m);
    assert.ok(qualificationNeeds, 'The native collector must declare its complete prerequisite list');
    const dependencies = new Set(qualificationNeeds[1].split(',').map(value => value.trim()));
    for (const required of ['native-preflight', 'linux-package-preflight']) {
        assert.ok(dependencies.has(required), `The release collector must await ${required}`);
    }
    assert.ok(ciJob(qualification, 'linux-package-preflight').includes('uses: ./.github/workflows/native-linux-package.yml'));
    for (const runner of ['macos-15', 'macos-15-intel']) {
        assert.ok(store.includes(`name: native-package-store-${runner}-` + '${{ github.sha }}'));
        assert.ok(qualification.includes(`native-input/native-package-direct-${runner}-$SOURCE_COMMIT/native-package/podlord-native.zip`));
    }
    assert.ok(store.includes('store-arm64/native-store-package/podlord-native.zip'));
    assert.ok(store.includes('store-x64/native-store-package/podlord-native.zip'));
    const downloaded = [...ciJob(qualification, 'candidate').matchAll(/^\s+name: (native-[^\r\n]+)$/gm)]
        .map(match => match[1]);
    assert.deepEqual(downloaded, [
        'native-package-direct-macos-15-${{ github.sha }}',
        'native-package-direct-macos-15-intel-${{ github.sha }}',
        'native-windows-x64-${{ github.sha }}',
        'native-linux-trial-x64-${{ github.sha }}',
        'native-linux-trial-arm64-${{ github.sha }}',
    ], 'Collect only the exact direct-download packages for this commit');
    assert.doesNotMatch(ciJob(qualification, 'candidate'), /\bpattern:/);
});

test('Store entitlement grants only client networking and selected-file access', async () => {
    const entitlements = await fs.readFile(path.join(root, 'native/macos/AppStore.entitlements'), 'utf8');
    for (const key of [
        'com.apple.security.app-sandbox',
        'com.apple.security.files.user-selected.read-only',
        'com.apple.security.network.client',
    ]) assert.match(entitlements, new RegExp(`<key>${key}</key>\\s*<true/>`));
    assert.doesNotMatch(entitlements, /com\.apple\.security\.network\.server/);
    assert.doesNotMatch(entitlements, /com\.apple\.security\.files\.user-selected\.read-write/);
});

test('Android release packaging derives one valid code from the shared release version', async () => {
    const cmake = await fs.readFile(path.join(root, 'native/CMakeLists.txt'), 'utf8');
    const readme = await fs.readFile(path.join(root, 'native/README.md'), 'utf8');
    assert.match(cmake, /PROJECT_VERSION_MINOR GREATER 999 OR PROJECT_VERSION_PATCH GREATER 999/);
    assert.match(cmake, /math\(EXPR podlord_android_version_code "\$\{PROJECT_VERSION_MAJOR\} \* 1000000 \+ \$\{PROJECT_VERSION_MINOR\} \* 1000 \+ \$\{PROJECT_VERSION_PATCH\}"\)/);
    assert.match(cmake, /QT_ANDROID_VERSION_CODE "\$\{podlord_android_version_code\}"/);
    assert.match(readme, /cmake --build "\$ANDROID_BUILD_DIR" --target aab --parallel 4/);
});

test('Store signing seals nested code before the app executable', {skip: !mac}, async t => {
    const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-store-signing-'));
    t.after(() => fs.rm(directory, {recursive: true, force: true}));
    const app = path.join(directory, 'Podlord.app');
    const executable = path.join(app, 'Contents/MacOS/podlord-native');
    const plugin = path.join(app, 'Contents/PlugIns/quick/libfixture.dylib');
    for (const folder of ['MacOS', 'PlugIns/quick', 'Frameworks'])
        await fs.mkdir(path.join(app, 'Contents', folder), {recursive: true});
    await fs.writeFile(path.join(app, 'Contents/Info.plist'), '<?xml version="1.0"?><plist version="1.0"><dict><key>CFBundleIdentifier</key><string>berlin.yuna.podlord.packaging-test</string><key>CFBundleExecutable</key><string>podlord-native</string><key>CFBundlePackageType</key><string>APPL</string></dict></plist>');
    const program = path.join(directory, 'fixture.c');
    const library = path.join(directory, 'library.c');
    await fs.writeFile(program, 'extern int fixture(void); int main(void) { return fixture(); }\n');
    await fs.writeFile(library, 'int fixture(void) { return 0; }\n');
    for (const args of [['-dynamiclib', library, '-install_name', '@rpath/libfixture.dylib', '-o', plugin],
        [program, plugin, '-Wl,-rpath,@executable_path/../PlugIns/quick', '-o', executable]]) {
        const built = spawnSync('/usr/bin/xcrun', ['clang', ...args], {encoding: 'utf8', timeout: 30000});
        assert.ifError(built.error);
        assert.equal(built.status, 0, built.stdout + built.stderr);
    }
    // Main-first is a valid filesystem enumeration order. It must not affect signing.
    await fs.writeFile(path.join(directory, 'runtime-files.txt'), executable + '\n' + plugin + '\n' + path.join(app, 'Contents/Info.plist') + '\n');
    const entitlements = path.join(directory, 'entitlements.plist');
    await fs.writeFile(entitlements, '<?xml version="1.0"?><plist version="1.0"><dict/></plist>');
    const signer = path.join(directory, 'codesign.cjs');
    // Only Apple's distribution identity/timestamp boundary changes. Real codesign still
    // signs and verifies the actual Mach-O bundle with an ad-hoc local identity.
    await fs.writeFile(signer,
        "const {spawnSync}=require('node:child_process');const fs=require('node:fs');" +
        "const args=process.argv.slice(2);const stamp=args.indexOf('--timestamp');if(stamp>=0)args.splice(stamp,1);" +
        "const sign=args.indexOf('--sign');if(sign>=0){args[sign+1]='-';fs.appendFileSync(process.env.SIGN_CALLS,args.at(-1)+'\\n');}" +
        "const result=spawnSync('/usr/bin/codesign',args,{stdio:'inherit'});if(result.error)throw result.error;process.exit(result.status??1);"
    );
    const source = await fs.readFile(packageScript, 'utf8');
    const start = source.indexOf('while IFS= read -r binary; do');
    const end = source.indexOf('find "$APP" -type f -exec /usr/bin/stat', start);
    assert.ok(start >= 0 && end > start);
    const phase = source.slice(start, end).replaceAll('/usr/bin/codesign', 'codesign');
    const calls = path.join(directory, 'calls');
    const result = spawnSync('/bin/sh', ['-eu', '-c',
        'codesign() { "' + process.execPath + '" "' + signer + '" "$@"; }\n' + phase], {
        encoding: 'utf8', timeout: 30000,
        env: {...process.env, APP: app, DIST: directory, ENTITLEMENTS: entitlements,
            MACOS_APP_STORE_APPLICATION_IDENTITY: 'external-distribution-boundary', SIGN_CALLS: calls},
    });
    assert.ifError(result.error);
    assert.equal(result.status, 0, result.stdout + result.stderr);
    assert.deepEqual((await fs.readFile(calls, 'utf8')).trim().split('\n'), [plugin, app]);
});

for (const workflowFile of ['ci.yml', 'native-linux-package.yml']) {
test(workflowFile + ' concurrency isolates callers, events and individual main snapshots', async () => {
    const workflow = await fs.readFile(path.join(root, '.github/workflows', workflowFile), 'utf8');
    const configured = workflow.match(/^concurrency:\n  group: (.+)$/m)?.[1];
    assert.ok(configured, 'Native CI requires an explicit concurrency group');
    // Model only the external GitHub context expansion, using the actual configuration.
    const group = context => {
        const expanded = configured.replace(/\$\{\{\s*github\.ref == 'refs\/heads\/main' && github\.sha \|\| 'latest'\s*\}\}/g,
            () => context.ref === 'refs/heads/main' ? context.sha : 'latest')
            .replace(/\$\{\{\s*github\.head_ref \|\| github\.ref_name\s*\}\}/g, () => context.head_ref || context.ref_name)
            .replace(/\$\{\{\s*github\.([a-z_]+)\s*\}\}/g, (_, key) => context[key]);
        assert.doesNotMatch(expanded, /\$\{\{|undefined/);
        return expanded;
    };
    const push = {workflow:'CI',event_name:'push',ref:'refs/heads/feat/context',ref_name:'feat/context',head_ref:'',sha:'first'};
    const pull = {...push,workflow:'Pull request',event_name:'pull_request',ref:'refs/pull/17/merge',head_ref:'feat/context'};
    const snapshot = {...push,workflow:'Release snapshot',ref:'refs/heads/main',ref_name:'main'};
    assert.notEqual(group(push),group(pull), 'A push must not cancel the required PR check');
    assert.equal(group(push),group({...push,sha:'second'}), 'Older same-branch feature checks remain cancellable');
    assert.notEqual(group(snapshot),group({...snapshot,sha:'second'}), 'Every main snapshot retains its qualification');
    assert.notEqual(group(snapshot),group({...snapshot,event_name:'workflow_dispatch'}), 'Manual and push entrypoints are independent');
});
}
