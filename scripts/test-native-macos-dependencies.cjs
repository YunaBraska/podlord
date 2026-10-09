'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { execFile, spawn } = require('node:child_process');
const { promisify } = require('node:util');
const { test } = require('node:test');
const execute = promisify(execFile);
const bundle = process.env.PODLORD_NATIVE_PACKAGE;
if (!bundle || !path.isAbsolute(bundle)) throw new Error('PODLORD_NATIVE_PACKAGE must identify a real absolute packaged application.');
const checker = path.join(__dirname, 'check-native-macos-dependencies.sh');
async function check(args) {
    try { const output = await execute('/bin/sh', [checker, ...args]); return { code: 0, ...output }; }
    catch (error) { return { code: error.code, stdout: error.stdout, stderr: error.stderr }; }
}
test('packaged application has a complete bundled dependency closure', async () => {
    const result = await check([bundle]);
    assert.equal(result.code, 0, result.stderr);
    assert.match(result.stdout, /QtWebSockets/);
});
test('packaged application excludes unused timeline frameworks, plugins and QML', async () => {
    for (const relative of [
        'Contents/Frameworks/QtQuickTimeline.framework',
        'Contents/Frameworks/QtQuickTimelineBlendTrees.framework',
        'Contents/PlugIns/quick/libqtquicktimelineplugin.dylib',
        'Contents/PlugIns/quick/libqtquicktimelineblendtreesplugin.dylib',
        'Contents/Resources/qml/QtQuick/Timeline',
        'Contents/PlugIns/sqldrivers/libqsqlodbc.dylib',
        'Contents/PlugIns/sqldrivers/libqsqlpsql.dylib',
        'Contents/PlugIns/sqldrivers/libqsqlmimer.dylib'
    ]) await assert.rejects(fs.lstat(path.join(bundle, relative)), { code: 'ENOENT' });
});
for (const scenario of ['spaces', 'missing-websockets', 'corrupt-executable', 'missing-executable', 'missing-ssl', 'missing-crypto', 'missing-openssl-plugin']) {
    test(`actual packaged application: ${scenario}`, { timeout: 30000 }, async () => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-package-check-'));
        const copy = path.join(directory, 'Podlord package.app');
        try {
            await fs.cp(bundle, copy, { recursive: true, dereference: false, verbatimSymlinks: true });
            if (scenario === 'missing-websockets') await fs.rm(path.join(copy, 'Contents/Frameworks/QtWebSockets.framework'), { recursive: true });
            if (scenario === 'corrupt-executable') await fs.truncate(path.join(copy, 'Contents/MacOS/podlord-native'), 0);
            if (scenario === 'missing-executable') await fs.rm(path.join(copy, 'Contents/MacOS/podlord-native'));
            if (scenario === 'missing-ssl') await fs.rm(path.join(copy, 'Contents/Frameworks/libssl.3.dylib'));
            if (scenario === 'missing-crypto') await fs.rm(path.join(copy, 'Contents/Frameworks/libcrypto.3.dylib'));
            if (scenario === 'missing-openssl-plugin') await fs.rm(path.join(copy, 'Contents/PlugIns/tls/libqopensslbackend.dylib'));
            const result = await check([copy]);
            if (scenario === 'spaces') assert.equal(result.code, 0, result.stderr);
            else {
                assert.notEqual(result.code, 0, 'A broken actual artifact must not pass its dependency check.');
                if (scenario === 'missing-websockets') assert.match(result.stderr, /Missing bundled runtime dependency: .*QtWebSockets/);
                if (scenario === 'missing-ssl' || scenario === 'missing-crypto') assert.match(result.stderr, /Missing bundled runtime dependency: .*lib(ssl|crypto)/);
                if (scenario === 'missing-openssl-plugin') assert.match(result.stderr, /Missing bundled TLS runtime:/);
            }
        } finally { await fs.rm(directory, { recursive: true, force: true }); }
    });
}
for (const [name, args] of [['missing argument', []], ['relative path', ['application.app']], ['missing bundle', ['/nonexistent/podlord-package.app']]]) {
    test(`dependency checker rejects ${name}`, async () => {
        assert.equal((await check(args)).code, 2);
    });
}

for (const scenario of ['declared-floor', 'missing-floor', 'empty-floor', 'invalid-floor', 'too-low-floor', 'higher-plugin-floor', 'wrong-plugin-platform', 'missing-plugin-architecture']) {
    test(`actual packaged runtime compatibility: ${scenario}`, { timeout: 30000 }, async () => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-package-platform-'));
        const copy = path.join(directory, 'Podlord package.app');
        try {
            await fs.cp(bundle, copy, { recursive: true, dereference: false, verbatimSymlinks: true });
            const plist = path.join(copy, 'Contents/Info.plist');
            await execute('/usr/libexec/PlistBuddy', ['-c', 'Set :LSMinimumSystemVersion 27.0', plist]).catch(async () => {
                await execute('/usr/libexec/PlistBuddy', ['-c', 'Add :LSMinimumSystemVersion string 27.0', plist]);
            });
            if (scenario === 'missing-floor') await execute('/usr/libexec/PlistBuddy', ['-c', 'Delete :LSMinimumSystemVersion', plist]);
            if (scenario === 'empty-floor' || scenario === 'invalid-floor' || scenario === 'too-low-floor') {
                const value = scenario === 'empty-floor' ? '' : scenario === 'invalid-floor' ? 'later' : '12.0';
                await execute('/usr/libexec/PlistBuddy', ['-c', `Set :LSMinimumSystemVersion ${value}`, plist]);
            }
            if (scenario === 'higher-plugin-floor' || scenario === 'wrong-plugin-platform') {
                const plugin = path.join(copy, 'Contents/PlugIns/platforms/libqcocoa.dylib');
                await execute('/usr/bin/xcrun', ['vtool', '-set-build-version', scenario === 'wrong-plugin-platform' ? 'ios' : 'macos', '28.0', '28.0', '-replace', '-output', plugin, plugin]);
            }
            if (scenario === 'missing-plugin-architecture') {
                const plugin = path.join(copy, 'Contents/PlugIns/platforms/libqcocoa.dylib');
                const content = await fs.readFile(plugin);
                assert.equal(content.readUInt32LE(0), 0xfeedfacf, 'Expected the actual single-architecture 64-bit plugin.');
                const architecture = content.readUInt32LE(4);
                assert.ok(architecture === 0x0100000c || architecture === 0x01000007);
                content.writeUInt32LE(architecture === 0x0100000c ? 0x01000007 : 0x0100000c, 4);
                content.writeUInt32LE(architecture === 0x0100000c ? 3 : 0, 8);
                await fs.writeFile(plugin, content);
            }
            const result = await check([copy]);
            if (scenario === 'declared-floor') assert.equal(result.code, 0, result.stderr);
            else {
                assert.equal(result.code, 1, 'An incompatible real runtime artifact passed its package gate.');
                assert.match(result.stderr, scenario === 'higher-plugin-floor' || scenario === 'too-low-floor'
                    ? /Runtime minimum macOS version exceeds declared floor:/
                    : scenario === 'wrong-plugin-platform' ? /Non-macOS runtime platform:/
                    : scenario === 'missing-plugin-architecture' ? /Missing runtime architecture:/ : /Invalid or missing LSMinimumSystemVersion:/);
            }
        } finally { await fs.rm(directory, { recursive: true, force: true }); }
    });
}

for (const scenario of ['escape-executable', 'escape-loader', 'escape-rpath', 'escape-file-symlink', 'escape-directory-symlink', 'escape-runtime-plugin-symlink', 'internal-file-symlink']) {
    test(`actual packaged dependency remains inside the bundle: ${scenario}`, { timeout: 30000 }, async () => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-package-boundary-'));
        const copy = path.join(directory, 'Podlord package.app');
        try {
            await fs.cp(bundle, copy, { recursive: true, dereference: false, verbatimSymlinks: true });
            const name = 'libyaml-cpp.0.9.dylib';
            const library = path.join(copy, 'Contents/Frameworks', name);
            const executable = path.join(copy, 'Contents/MacOS/podlord-native');
            const outside = path.join(directory, 'external');
            await fs.mkdir(outside);
            await fs.copyFile(library, path.join(outside, name));
            const libraries = await execute('/usr/bin/otool', ['-L', executable]);
            const entry = libraries.stdout.match(/^\s+(\S*\/libyaml-cpp\.0\.9\.dylib)\s+\(/m);
            assert.ok(entry, 'Cannot locate the actual bundled YAML dependency.');
            const original = entry[1];
            if (scenario === 'escape-runtime-plugin-symlink') {
                const plugin = path.join(copy, 'Contents/PlugIns/platforms/libqcocoa.dylib');
                const target = path.join(outside, 'libqcocoa.dylib');
                await fs.rename(plugin, target);
                await fs.symlink(target, plugin);
            } else if (scenario === 'escape-file-symlink') {
                await fs.rm(library);
                await fs.symlink(path.join(outside, name), library);
            } else if (scenario === 'internal-file-symlink') {
                await fs.rename(library, library + '.actual.dylib');
                await fs.symlink(name + '.actual.dylib', library);
            } else {
                let replacement;
                if (scenario === 'escape-directory-symlink') {
                    await fs.symlink(outside, path.join(copy, 'Contents/Frameworks/external'));
                    replacement = `@executable_path/../Frameworks/external/${name}`;
                } else replacement = `${scenario === 'escape-executable' ? '@executable_path' : scenario === 'escape-loader' ? '@loader_path' : '@rpath'}/../../../external/${name}`;
                await execute('/usr/bin/install_name_tool', ['-change', original, replacement, executable]);
            }
            const result = await check([copy]);
            if (scenario === 'internal-file-symlink') assert.equal(result.code, 0, result.stderr);
            else {
                assert.equal(result.code, 1, 'An external dependency escaped the application boundary.');
                assert.match(result.stderr, /Dependency resolves outside the application bundle:/);
            }
        } finally { await fs.rm(directory, { recursive: true, force: true }); }
    });
}

for (const lane of ['native', 'reference']) {
    test(`desktop comparison rejects an undeclared ${lane} executable before contacting Docker`, { timeout: 30000 }, async () => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-comparison-entry-'));
        const app = path.join(directory, 'Owned comparison.app');
        try {
            await fs.cp(bundle, app, { recursive: true, dereference: false, verbatimSymlinks: true });
            const declared = path.join(app, 'Contents/MacOS/podlord-native');
            const unrelated = path.join(app, 'Contents/MacOS/undeclared-executable');
            await fs.copyFile(declared, unrelated);
            const env = { ...process.env, DOCKER_CONTEXT: '', DOCKER_HOST: `unix://${directory}/absent-docker.sock`,
                PODLORD_NATIVE_APP: lane === 'native' ? unrelated : declared,
                PODLORD_LEGACY_APP: lane === 'reference' ? unrelated : declared };
            const script = path.join(__dirname, 'test-native-visual-kubernetes.sh');
            await assert.rejects(execute('/bin/sh', [script, 'desktop'], { env, timeout: 15000 }), error => {
                assert.equal(error.code, 1);
                assert.match(error.stderr, /does not match its declared bundle executable/);
                assert.doesNotMatch(error.stdout, /VISUAL_RUN=|NATIVE_PID=|LEGACY_PID=/);
                return true;
            });
        } finally { await fs.rm(directory, { recursive: true, force: true }); }
    });
}

test('an exited real comparison app fails the review and cleans up its owned stack', {
    skip: !process.env.PODLORD_LEGACY_APP, timeout: 210000
}, async () => {
    const child = spawn('/bin/sh', [path.join(__dirname, 'test-native-visual-kubernetes.sh'), 'desktop'], {
        env: { ...process.env, PODLORD_NATIVE_APP: path.join(bundle, 'Contents/MacOS/podlord-native') },
        signal: AbortSignal.timeout(180000)
    });
    let stdout = '', stderr = '', stopped = false, failure;
    const finished = new Promise(resolve => child.once('close', resolve));
    child.on('error', error => { failure = error; });
    child.stdout.on('data', chunk => {
        stdout += chunk;
        const native = stdout.match(/^NATIVE_PID=(\d+)$/m);
        if (native && !stopped) {
            stopped = true;
            try { process.kill(Number(native[1]), 'SIGTERM'); } catch (error) { failure = error; }
        }
    });
    child.stderr.on('data', chunk => { stderr += chunk; });
    try {
        const code = await finished;
        assert.ifError(failure);
        assert.equal(code, 1, stderr);
        assert.equal(stopped, true, stdout);
        assert.match(stderr, /A comparison application exited; refusing to relaunch outside its isolated profile/);
        const run = stdout.match(/^VISUAL_RUN=(.+)$/m)?.[1];
        const owner = stdout.match(/^CLUSTER=(podlord-visual-run-[a-z0-9]+)$/m)?.[1];
        assert.ok(run && owner, stdout);
        await assert.rejects(fs.stat(run), { code: 'ENOENT' });
        const remaining = await execute('docker', ['container', 'ls', '-a', '--filter', `label=podlord.native.e2e=${owner}`, '--format', '{{.ID}}']);
        assert.equal(remaining.stdout.trim(), '', 'The comparison left its owned container behind.');
    } finally {
        if (child.exitCode === null && child.signalCode === null) { child.kill('SIGTERM'); await finished; }
    }
});
