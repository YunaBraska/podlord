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

function ciJob(workflow, name) {
    const heading = `  ${name}:\n`;
    assert.ok(workflow.includes(heading), `Required independent job is missing: ${name}`);
    return workflow.split(heading)[1].split(/\n  [a-z][a-z0-9-]*:\n/)[0];
}

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
    assert.ok(action.includes("native-macos-toolchain-v1-${{ runner.os }}-${{ runner.arch }}-${{ steps.macos-toolchain-key.outputs.abi }}-${{ hashFiles('scripts/install-native-macos-toolchain.sh') }}"));
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
    const release = await fs.readFile(path.join(root, '.github/workflows/release.yml'), 'utf8');
    for (const workflow of [store, release]) {
        assert.ok(ciJob(workflow, 'native-preflight').includes('uses: ./.github/workflows/native.yml'));
        assert.match(workflow, /needs: native-preflight/);
        assert.doesNotMatch(workflow, /native-preflight-macos-15/);
    }
    for (const runner of ['macos-15', 'macos-15-intel']) {
        assert.ok(store.includes(`name: native-package-store-${runner}-` + '${{ github.sha }}'));
        assert.ok(release.includes(`native-input/native-package-direct-${runner}-$SOURCE_COMMIT/native-package/podlord-native.zip`));
    }
    assert.ok(store.includes('store-arm64/native-store-package/podlord-native.zip'));
    assert.ok(store.includes('store-x64/native-store-package/podlord-native.zip'));
    assert.ok(release.includes('pattern: native-*-${{ github.sha }}'));
});
