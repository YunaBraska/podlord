import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { test } from 'node:test';
import { assessPerformance } from './performance_report.mjs';

function report(boundary = 'headless-software') {
    const records = ['filter', 'sort', 'cached-session-tab', 'cached-inspector'].map(name => ({
        type: 'interaction', name, samples: 30, p95Ms: 10, maxMs: 20, p95LimitMs: 50, maxLimitMs: 100,
        measurementValid: true, targetPolicy: 'advisory', passed: true,
    }));
    if (boundary !== 'headless-software') {
        records.push({ type: 'frameClockCalibration', passed: true });
        records.push(...['filter', 'sort', 'cached-session-tab', 'cached-inspector', 'scroll'].map(name => ({
            type: 'frameWork', name, samples: 90, distinctActions: 30, p95Ms: 5, maxMs: 6,
            actionMaximumP95Ms: 5, p95LimitMs: 8, measurementValid: true, targetPolicy: 'advisory', passed: true,
        })));
    }
    records.push({ type: 'memory', maxResidentBytes: 250000000, residentLimitBytes: 310000000,
        warmGrowthBytes: 1000000, warmGrowthLimitBytes: 5000000, measurementValid: true, targetPolicy: 'advisory', passed: true });
    records.push({ type: 'idle', milliseconds: 60000, foregroundLost: false, timedOut: false,
        cpuPercentOneCore: 1, cpuTargetPercentOneCore: 2, measurementValid: true, targetPolicy: 'advisory', passed: true });
    records.push({ type: 'result', passed: true, targetsMet: true, targetPolicy: 'advisory' });
    return records.map(r => ({ ...r, diagnosticOnly: boundary === 'background-cocoa-metal-diagnostic', renderingBoundary: boundary }));
}

for (const boundary of ['headless-software', 'foreground-cocoa', 'background-cocoa-metal-diagnostic']) {
    test(`complete ${boundary} measurement retains its boundary`, () => {
        const assessment = assessPerformance(report(boundary), boundary.startsWith('background'));
        assert.equal(assessment.passed, true);
        assert.equal(assessment.targetsMet, true);
        assert.equal(assessment.renderingBoundary, boundary);
    });
}
for (const [type, change] of [
    ['interaction', { p95Ms: 101, maxMs: 102 }],
    ['frameWork', { p95Ms: 21, maxMs: 24 }],
    ['memory', { maxResidentBytes: 310000001 }],
    ['idle', { cpuPercentOneCore: 2.04 }],
]) {
    test(`${type} target miss remains visible without invalidating the measurement`, () => {
        const records = report('foreground-cocoa');
        Object.assign(records.find(r => r.type === type), change, { passed: false });
        records.at(-1).targetsMet = false;
        const assessment = assessPerformance(records);
        assert.equal(assessment.passed, true);
        assert.equal(assessment.targetsMet, false);
        assert.equal(assessment.advisories.length, 1);
    });
}
const invalidCases = [
    ['incomplete sampling', r => { r[0].samples = 29; }],
    ['nonfinite timing', r => { r[0].p95Ms = NaN; }],
    ['missing metric', r => { r.splice(0, 1); }],
    ['duplicate metric', r => { r.push({ ...r[0] }); }],
    ['invalid memory', r => { r.find(v => v.type === 'memory').maxResidentBytes = 0; }],
    ['lost foreground', r => { r.find(v => v.type === 'idle').foregroundLost = true; }],
    ['incomplete idle window', r => { r.find(v => v.type === 'idle').milliseconds = 59999; }],
    ['idle watchdog expiry', r => { r.find(v => v.type === 'idle').timedOut = true; }],
    ['invalid measurement flag', r => { r[0].measurementValid = false; }],
    ['recorded functional failure', r => { r[0].type = 'failure'; }],
    ['failed protocol', r => { r.at(-1).passed = false; }],
    ['hidden target miss', r => { r[0].p95Ms = 101; r[0].maxMs = 102; }],
    ['wrong aggregate', r => { r.at(-1).targetsMet = false; }],
    ['mixed boundaries', r => { r[0].renderingBoundary = 'foreground-cocoa'; }],
];
for (const [name, change] of invalidCases) {
    test(`${name} still fails`, () => {
        const records = report();
        change(records);
        assert.equal(assessPerformance(records).passed, false);
    });
}
test('background diagnosis cannot masquerade as foreground evidence', () => {
    assert.equal(assessPerformance(report('background-cocoa-metal-diagnostic')).passed, false);
});
test('missing or failed frame calibration is not a budget warning', () => {
    const records = report('foreground-cocoa');
    records.find(r => r.type === 'frameClockCalibration').passed = false;
    assert.equal(assessPerformance(records).passed, false);
});
test('the public report command reports advisory success and rejects malformed input', t => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'podlord-report-'));
    t.after(() => fs.rmSync(dir, { recursive: true, force: true }));
    const input = path.join(dir, 'measurements.jsonl');
    const records = report();
    Object.assign(records.find(r => r.type === 'idle'), { cpuPercentOneCore: 2.04, passed: false });
    records.at(-1).targetsMet = false;
    fs.writeFileSync(input, records.map(r => JSON.stringify(r)).join('\n'));
    const command = fileURLToPath(new URL('./performance_report.mjs', import.meta.url));
    const result = spawnSync(process.execPath, [command, input], { encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr);
    assert.equal(JSON.parse(result.stdout).targetsMet, false);
    assert.match(result.stderr, /WARNING: performance target advisory: idle/);
    fs.writeFileSync(input, '{invalid');
    const invalid = spawnSync(process.execPath, [command, input], { encoding: 'utf8' });
    assert.equal(invalid.status, 1);
    assert.equal(JSON.parse(invalid.stdout).passed, false);
});
