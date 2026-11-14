import fs from 'node:fs';
import { pathToFileURL } from 'node:url';

// Budget misses are advisory. Incomplete or inconsistent measurements are not.
export function assessPerformance(records, diagnostic = false) {
    const invalid = error => ({ passed: false, error });
    if (!Array.isArray(records) || records.length === 0
        || records.some(r => !r || typeof r !== 'object' || Array.isArray(r))) return invalid('Invalid measurement records.');
    const results = records.filter(r => r.type === 'result');
    if (results.length !== 1 || results[0].passed !== true || results[0].targetPolicy !== 'advisory')
        return invalid('The measurement protocol did not complete successfully.');
    const result = results[0];
    const boundaries = diagnostic ? ['background-cocoa-metal-diagnostic'] : ['foreground-cocoa', 'headless-software'];
    if (!boundaries.includes(result.renderingBoundary)
        || records.some(r => r.diagnosticOnly !== diagnostic || r.renderingBoundary !== result.renderingBoundary || r.type === 'failure'))
        return invalid('Wrong measurement boundary or a recorded functional failure.');
    const metrics = records.filter(r => ['interaction', 'frameWork', 'memory', 'idle'].includes(r.type));
    const expected = ['filter', 'sort', 'cached-session-tab', 'cached-inspector'].map(name => `interaction:${name}`);
    expected.push('memory:', 'idle:');
    if (result.renderingBoundary !== 'headless-software') {
        expected.push(...['filter', 'sort', 'cached-session-tab', 'cached-inspector', 'scroll'].map(name => `frameWork:${name}`));
        const calibration = records.filter(r => r.type === 'frameClockCalibration');
        if (calibration.length !== 1 || calibration[0].passed !== true) return invalid('Frame-clock calibration is missing or invalid.');
    }
    const keys = metrics.map(r => `${r.type}:${r.name ?? ''}`).sort();
    if (JSON.stringify(keys) !== JSON.stringify(expected.sort())) return invalid('Missing, duplicate or unknown metric.');
    const advisories = [];
    for (const r of metrics) {
        if (r.measurementValid !== true || r.targetPolicy !== 'advisory' || typeof r.passed !== 'boolean')
            return invalid(`Invalid measurement: ${r.type}.`);
        const number = key => typeof r[key] === 'number' && Number.isFinite(r[key]);
        let met;
        if (r.type === 'interaction' || r.type === 'frameWork') {
            if (!number('p95Ms') || r.p95Ms < 0 || !number('p95LimitMs') || r.p95LimitMs <= 0
                || !number('maxMs') || r.maxMs < r.p95Ms) return invalid('Invalid timing values.');
            if (r.type === 'interaction') {
                if (r.samples !== 30 || !number('maxLimitMs') || r.maxLimitMs <= 0) return invalid('Incomplete interaction sampling.');
                met = r.p95Ms <= r.p95LimitMs && r.maxMs <= r.maxLimitMs;
            } else {
                if (r.distinctActions !== 30 || !Number.isInteger(r.samples) || r.samples < 90
                    || !number('actionMaximumP95Ms') || r.actionMaximumP95Ms < 0) return invalid('Incomplete frame sampling.');
                met = r.p95Ms <= r.p95LimitMs && r.actionMaximumP95Ms <= r.p95LimitMs;
            }
        } else if (r.type === 'memory') {
            if (!number('maxResidentBytes') || r.maxResidentBytes <= 0 || !number('residentLimitBytes') || r.residentLimitBytes <= 0
                || !number('warmGrowthBytes') || !number('warmGrowthLimitBytes') || r.warmGrowthLimitBytes < 0)
                return invalid('Invalid resident-memory measurement.');
            met = r.maxResidentBytes <= r.residentLimitBytes && r.warmGrowthBytes <= r.warmGrowthLimitBytes;
        } else {
            if (!number('milliseconds') || r.milliseconds < 60000 || r.foregroundLost !== false || r.timedOut !== false
                || !number('cpuPercentOneCore') || r.cpuPercentOneCore < 0
                || !number('cpuTargetPercentOneCore') || r.cpuTargetPercentOneCore <= 0) return invalid('Invalid nonsync idle measurement.');
            met = r.cpuPercentOneCore <= r.cpuTargetPercentOneCore;
        }
        if (r.passed !== met) return invalid('Target comparison does not match the measured values.');
        if (!met) advisories.push({ metric: `${r.type}:${r.name ?? ''}` });
    }
    const targetsMet = advisories.length === 0;
    if (result.targetsMet !== targetsMet) return invalid('Inconsistent aggregate target assessment.');
    return { passed: true, targetsMet, diagnosticOnly: diagnostic, renderingBoundary: result.renderingBoundary, advisories };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    let assessment;
    try {
        if (process.argv.length > 4 || !process.argv[2]
            || (process.argv[3] && process.argv[3] !== '--background-metal-diagnostic')) throw new Error('Invalid arguments.');
        const records = fs.readFileSync(process.argv[2], 'utf8').trim().split('\n').map(JSON.parse);
        assessment = assessPerformance(records, process.argv[3] === '--background-metal-diagnostic');
    } catch {
        assessment = { passed: false, error: 'Cannot read a valid performance report.' };
    }
    process.stdout.write(`${JSON.stringify(assessment)}\n`);
    for (const advisory of assessment.advisories ?? []) process.stderr.write(`WARNING: performance target advisory: ${advisory.metric}\n`);
    if (!assessment.passed) process.stderr.write(`${assessment.error}\n`);
    process.exitCode = assessment.passed ? 0 : 1;
}
