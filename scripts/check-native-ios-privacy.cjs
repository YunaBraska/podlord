'use strict';
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const assert = require('node:assert/strict');

try {
    assert.equal(process.argv.length, 3, 'Usage: node scripts/check-native-ios-privacy.cjs /absolute/Podlord.app');
    const app = process.argv[2];
    assert.ok(path.isAbsolute(app) && app.endsWith('.app') && fs.statSync(app).isDirectory(), 'Expected an absolute iOS app bundle');
    const category = name => `NSPrivacyAccessedAPICategory${name}`;
    const expected = new Map([
        ['PrivacyInfo.xcprivacy', { [category('FileTimestamp')]: ['3B52.1', 'C617.1'], [category('UserDefaults')]: ['CA92.1'] }],
        ['QtCorePrivacy.bundle/PrivacyInfo.xcprivacy', { [category('FileTimestamp')]: ['0A2A.1'], [category('DiskSpace')]: ['85F4.1'] }],
        ['QtGuiPrivacy.bundle/PrivacyInfo.xcprivacy', { [category('SystemBootTime')]: ['35F9.1'] }],
        ['OpenSSLPrivacy.bundle/PrivacyInfo.xcprivacy', { [category('FileTimestamp')]: ['C617.1'] }]
    ]);
    for (const component of ['avcodec', 'avformat', 'avutil', 'swresample', 'swscale']) {
        expected.set(`Frameworks/lib${component}.framework/PrivacyInfo.xcprivacy`,
            component === 'avformat' || component === 'avutil' ? { [category('FileTimestamp')]: ['C617.1'] } : {});
    }
    const plist = file => JSON.parse(execFileSync('/usr/bin/plutil', ['-convert', 'json', '-o', '-', file], { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }));
    const allowed = ['NSPrivacyTracking', 'NSPrivacyTrackingDomains', 'NSPrivacyCollectedDataTypes', 'NSPrivacyAccessedAPITypes'];
    for (const [relative, reasons] of expected) {
        const file = path.join(app, relative);
        assert.ok(fs.lstatSync(file).isFile(), `Missing regular privacy manifest: ${relative}`);
        const manifest = plist(file);
        assert.ok(manifest && !Array.isArray(manifest) && typeof manifest === 'object', `Invalid manifest: ${relative}`);
        assert.deepEqual(Object.keys(manifest).sort(), [...allowed].sort(), `Unexpected privacy keys: ${relative}`);
        assert.equal(manifest.NSPrivacyTracking, false, `Tracking declaration changed: ${relative}`);
        assert.deepEqual(manifest.NSPrivacyTrackingDomains, [], `Tracking domains changed: ${relative}`);
        assert.deepEqual(manifest.NSPrivacyCollectedDataTypes, [], `Collection declaration changed: ${relative}`);
        assert.ok(Array.isArray(manifest.NSPrivacyAccessedAPITypes), `Invalid API declaration: ${relative}`);
        const actual = {};
        for (const entry of manifest.NSPrivacyAccessedAPITypes) {
            assert.deepEqual(Object.keys(entry).sort(), ['NSPrivacyAccessedAPIType', 'NSPrivacyAccessedAPITypeReasons']);
            assert.equal(typeof entry.NSPrivacyAccessedAPIType, 'string');
            assert.ok(Array.isArray(entry.NSPrivacyAccessedAPITypeReasons));
            assert.ok(!Object.hasOwn(actual, entry.NSPrivacyAccessedAPIType), `Duplicate API category: ${relative}`);
            actual[entry.NSPrivacyAccessedAPIType] = entry.NSPrivacyAccessedAPITypeReasons.toSorted();
        }
        assert.deepEqual(actual, reasons, `Required API reasons changed: ${relative}`);
        if (relative.includes('.bundle/')) {
            const info = plist(path.join(path.dirname(file), 'Info.plist'));
            assert.equal(info.CFBundlePackageType, 'BNDL', `Invalid vendor resource bundle: ${relative}`);
            assert.ok(info.CFBundleIdentifier.startsWith('berlin.yuna.podlord.privacy.'));
        }
    }
    console.log(`iOS privacy declarations verified in ${expected.size} packaged app/vendor bundles.`);
} catch (error) {
    console.error(`iOS privacy check failed: ${error.message}`);
    process.exitCode = 1;
}
