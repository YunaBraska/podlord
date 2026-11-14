'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const os = require('node:os');
const { promisify } = require('node:util');
const { execFile } = require('node:child_process');
const { test } = require('node:test');
const execute = promisify(execFile);
const checker = path.join(__dirname, 'check-native-ios-privacy.cjs');
const root = path.join(__dirname, '../native/ios');
const category = name => `NSPrivacyAccessedAPICategory${name}`;
const xml = value => typeof value === 'boolean' ? `<${value}/>` : typeof value === 'string' ? `<string>${value}</string>`
    : Array.isArray(value) ? `<array>${value.map(xml).join('')}</array>`
        : `<dict>${Object.entries(value).map(([key, data]) => `<key>${key}</key>${xml(data)}`).join('')}</dict>`;
const document = value => `<?xml version="1.0"?><plist version="1.0">${xml(value)}</plist>`;
const manifest = entries => ({ NSPrivacyTracking: false, NSPrivacyTrackingDomains: [], NSPrivacyCollectedDataTypes: [],
    NSPrivacyAccessedAPITypes: Object.entries(entries).map(([type, reasons]) => ({ NSPrivacyAccessedAPIType: category(type), NSPrivacyAccessedAPITypeReasons: reasons })) });

for (const scenario of ['valid', 'repeat', 'missing_app', 'missing_vendor', 'missing_framework', 'malformed', 'symlink', 'tracking', 'collection', 'wrong_reason', 'duplicate_category', 'unknown_key', 'invalid_bundle']) {
    test(`packaged iOS privacy ${scenario}`, { skip: process.platform !== 'darwin' }, async () => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-ios-privacy-'));
        const app = path.join(directory, 'Podlord.app');
        try {
            await fs.mkdir(app);
            await fs.copyFile(path.join(root, 'PrivacyInfo.xcprivacy'), path.join(app, 'PrivacyInfo.xcprivacy'));
            for (const [vendor, entries] of [
                ['QtCore', { FileTimestamp: ['0A2A.1'], DiskSpace: ['85F4.1'] }],
                ['QtGui', { SystemBootTime: ['35F9.1'] }], ['OpenSSL', { FileTimestamp: ['C617.1'] }]
            ]) {
                const bundle = path.join(app, `${vendor}Privacy.bundle`);
                await fs.mkdir(bundle);
                await fs.writeFile(path.join(bundle, 'PrivacyInfo.xcprivacy'), document(manifest(entries)));
                await fs.writeFile(path.join(bundle, 'Info.plist'), document({ CFBundleIdentifier: `berlin.yuna.podlord.privacy.${vendor}`, CFBundlePackageType: 'BNDL' }));
            }
            for (const component of ['avcodec', 'avformat', 'avutil', 'swresample', 'swscale']) {
                const bundle = path.join(app, 'Frameworks', `lib${component}.framework`);
                await fs.mkdir(bundle, { recursive: true });
                await fs.copyFile(path.join(root, component === 'avformat' || component === 'avutil' ? 'FFmpegFilePrivacy.xcprivacy' : 'FFmpegPrivacy.xcprivacy'), path.join(bundle, 'PrivacyInfo.xcprivacy'));
            }
            const own = path.join(app, 'PrivacyInfo.xcprivacy');
            if (scenario === 'missing_app') await fs.unlink(own);
            if (scenario === 'missing_vendor') await fs.unlink(path.join(app, 'OpenSSLPrivacy.bundle/PrivacyInfo.xcprivacy'));
            if (scenario === 'missing_framework') await fs.unlink(path.join(app, 'Frameworks/libavformat.framework/PrivacyInfo.xcprivacy'));
            if (scenario === 'malformed') await fs.writeFile(own, '<plist>');
            if (scenario === 'symlink') { await fs.unlink(own); await fs.symlink(path.join(root, 'PrivacyInfo.xcprivacy'), own); }
            if (['tracking', 'collection', 'wrong_reason', 'duplicate_category', 'unknown_key'].includes(scenario)) {
                const data = manifest({ FileTimestamp: ['C617.1', '3B52.1'], UserDefaults: ['CA92.1'] });
                if (scenario === 'tracking') data.NSPrivacyTracking = true;
                if (scenario === 'collection') data.NSPrivacyCollectedDataTypes = ['unexpected'];
                if (scenario === 'wrong_reason') data.NSPrivacyAccessedAPITypes[0].NSPrivacyAccessedAPITypeReasons = ['0A2A.1'];
                if (scenario === 'duplicate_category') data.NSPrivacyAccessedAPITypes.push(data.NSPrivacyAccessedAPITypes[0]);
                if (scenario === 'unknown_key') data.UnknownPrivacyKey = true;
                await fs.writeFile(own, document(data));
            }
            if (scenario === 'invalid_bundle') await fs.writeFile(path.join(app, 'QtGuiPrivacy.bundle/Info.plist'), document({ CFBundlePackageType: 'APPL' }));
            if (scenario === 'valid' || scenario === 'repeat') {
                const before = await fs.readFile(own);
                for (let run = 0; run < (scenario === 'repeat' ? 2 : 1); ++run) assert.match((await execute(process.execPath, [checker, app])).stdout, /9 packaged/);
                assert.deepEqual(await fs.readFile(own), before);
            } else await assert.rejects(execute(process.execPath, [checker, app]), error => error.code === 1 && /iOS privacy check failed/.test(error.stderr));
        } finally {
            await fs.rm(directory, { recursive: true, force: true });
        }
    });
}

for (const scenario of ['valid', 'repeat', 'version', 'openssl_missing', 'framework_missing', 'signed_framework']) {
    test(`source SDK privacy installation ${scenario}`, async () => {
        const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'podlord-ios-sdk-privacy-'));
        try {
            await fs.writeFile(path.join(directory, 'build-inputs.txt'), `Qt: ${scenario === 'version' ? '0.0.0' : '6.11.3'}\nOpenSSL: 3.5.9\nFFmpeg: 7.1.5\n`);
            const upstream = path.join(directory, 'openssl/os-dep/Apple/PrivacyInfo.xcprivacy');
            await fs.mkdir(path.dirname(upstream), { recursive: true });
            if (scenario !== 'openssl_missing') await fs.copyFile(path.join(root, 'FFmpegFilePrivacy.xcprivacy'), upstream);
            for (const component of ['avcodec', 'avformat', 'avutil', 'swresample', 'swscale']) {
                if (scenario === 'framework_missing' && component === 'swscale') continue;
                const framework = path.join(directory, `qt/lib/ffmpeg/lib${component}.xcframework/ios-arm64/lib${component}.framework`);
                await fs.mkdir(framework, { recursive: true });
                await fs.writeFile(path.join(framework, `lib${component}`), 'external-sdk-binary-fixture');
                await fs.writeFile(path.join(framework, 'Info.plist'), document({ CFBundlePackageType: 'FMWK' }));
                if (scenario === 'signed_framework' && component === 'avformat') await fs.mkdir(path.join(framework, '_CodeSignature'));
            }
            const args = [`-DPODLORD_IOS_SDK_ROOT=${directory}`, '-P', path.join(__dirname, 'install-native-ios-privacy.cmake')];
            const installed = path.join(directory, 'ssl/share/podlord/PrivacyInfo.xcprivacy');
            if (scenario === 'valid' || scenario === 'repeat') {
                for (let run = 0; run < (scenario === 'repeat' ? 2 : 1); ++run) await execute('cmake', args);
                assert.deepEqual(await fs.readFile(installed), await fs.readFile(upstream));
                for (const component of ['avcodec', 'avformat', 'avutil', 'swresample', 'swscale']) {
                    const framework = path.join(directory, `qt/lib/ffmpeg/lib${component}.xcframework/ios-arm64/lib${component}.framework`);
                    assert.equal(await fs.readFile(path.join(framework, `lib${component}`), 'utf8'), 'external-sdk-binary-fixture');
                    assert.deepEqual(await fs.readFile(path.join(framework, 'PrivacyInfo.xcprivacy')),
                        await fs.readFile(path.join(root, component === 'avformat' || component === 'avutil' ? 'FFmpegFilePrivacy.xcprivacy' : 'FFmpegPrivacy.xcprivacy')));
                }
            } else {
                await assert.rejects(execute('cmake', args), error => error.code === 1);
                await assert.rejects(fs.stat(installed), { code: 'ENOENT' });
                const first = path.join(directory, 'qt/lib/ffmpeg/libavcodec.xcframework/ios-arm64/libavcodec.framework/PrivacyInfo.xcprivacy');
                await assert.rejects(fs.stat(first), { code: 'ENOENT' });
            }
        } finally {
            await fs.rm(directory, { recursive: true, force: true });
        }
    });
}
