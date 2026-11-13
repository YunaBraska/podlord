const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const root = path.resolve(__dirname, '..');
const exporter = fs.readFileSync(path.join(__dirname, 'export-native-locales.sh'));
const reference = fs.readFileSync(path.join(root, 'src/Podlord.App/PodlordLocalizer.cs'));
const sdk = fs.readFileSync(path.join(root, 'global.json'));
const catalog = JSON.parse(fs.readFileSync(path.join(root, 'native/ui/locales.json')));
const sharedKey = [...reference.toString().matchAll(/"([a-z]+(?:\.[a-zA-Z][a-zA-Z0-9]*)+)"/g)]
    .map(match => match[1]).find(key => Object.hasOwn(catalog.english, key));
assert.ok(sharedKey, 'The actual C# reference must contain a shared native key.');

function fixture(t, alter) {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'podlord-reference-catalog-'));
    t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
    for (const name of ['scripts', 'src/Podlord.App', 'native/ui'])
        fs.mkdirSync(path.join(directory, name), { recursive: true });
    const script = path.join(directory, 'scripts/export-native-locales.sh');
    const input = path.join(directory, 'native/ui/locales.json');
    fs.writeFileSync(script, exporter);
    fs.writeFileSync(path.join(directory, 'src/Podlord.App/PodlordLocalizer.cs'), reference);
    fs.writeFileSync(path.join(directory, 'global.json'), sdk);
    fs.writeFileSync(input, JSON.stringify(catalog));
    const subject = {
        input,
        output: path.join(directory, 'exported.json'),
        run: output => spawnSync('/bin/sh', [script, output], {
            cwd: directory, env: process.env, encoding: 'utf8', timeout: 90000,
        }),
    };
    // Exercise comparison semantics against an actual reference export, not
    // current product wording that may intentionally differ from the reference.
    const exported = subject.run(subject.output);
    assert.equal(exported.status, 0, exported.stdout + exported.stderr);
    const baseline = JSON.parse(fs.readFileSync(subject.output));
    fs.writeFileSync(input, typeof alter === 'string' ? alter : JSON.stringify(alter(baseline)));
    return subject;
}

test('reference check ignores JSON whitespace and object property order without rewriting input', t => {
    const subject = fixture(t, baseline => ({ ...baseline,
        english: Object.fromEntries(Object.entries(baseline.english).reverse()) }));
    const before = fs.readFileSync(subject.input);
    const result = subject.run('--check');
    assert.equal(result.status, 0, result.stdout + result.stderr);
    assert.deepEqual(fs.readFileSync(subject.input), before);
});

test('reference check rejects a changed shared English value', t => {
    const result = fixture(t, baseline => {
        baseline.english[sharedKey] += ' changed';
        return baseline;
    }).run('--check');
    assert.notEqual(result.status, 0);
    assert.match(result.stdout + result.stderr, /native language catalog differs|locales\.json.*differ/i);
});

test('reference check preserves meaningful language option order', t => {
    const result = fixture(t, baseline => ({ ...baseline, languages: [...baseline.languages].reverse() })).run('--check');
    assert.notEqual(result.status, 0);
    assert.match(result.stdout + result.stderr, /native language catalog differs|locales\.json.*differ/i);
});

test('reference check rejects malformed catalog input', t => {
    const result = fixture(t, '{invalid json}').run('--check');
    assert.notEqual(result.status, 0);
    assert.match(result.stdout + result.stderr, /JsonException|invalid.*JSON|JSON.*invalid/i);
});

test('reference export retains native-only strings and does not rewrite its input', t => {
    const subject = fixture(t, baseline => {
        baseline.english['referenceTest.nativeOnly'] = 'Native-only value';
        return baseline;
    });
    const before = fs.readFileSync(subject.input);
    const result = subject.run(subject.output);
    assert.equal(result.status, 0, result.stdout + result.stderr);
    assert.equal(JSON.parse(fs.readFileSync(subject.output)).english['referenceTest.nativeOnly'], 'Native-only value');
    assert.deepEqual(fs.readFileSync(subject.input), before);
});
