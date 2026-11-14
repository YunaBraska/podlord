const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const path = require('node:path');
const { test } = require('node:test');

const inputs = { extra: 'unexpected-build-directory', empty: '' };
const scenario = process.argv[2];
assert.ok(Object.hasOwn(inputs, scenario), 'Choose the extra or empty argument scenario');

test(`Native runner rejects the ${scenario} argument before accessing tools or profiles`, () => {
    const result = spawnSync('/bin/sh', [path.join(__dirname, 'test-native.sh'), inputs[scenario]], {
        encoding: 'utf8', env: { ...process.env, PATH: '/nonexistent' }
    });
    assert.equal(result.error, undefined);
    assert.equal(result.status, 2);
    assert.equal(result.stdout, '');
    assert.match(result.stderr, /^No arguments are supported\. Set PODLORD_NATIVE_BUILD_DIR to select the build directory\.\n$/);
});
