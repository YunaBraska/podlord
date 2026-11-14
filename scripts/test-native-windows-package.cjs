'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const script = path.join(__dirname, 'check-native-windows-package.ps1');
const root = path.resolve(__dirname, '..');
const pwsh = spawnSync('pwsh', ['-NoProfile', '-NonInteractive', '-Command', '$PSVersionTable.PSVersion.Major'], {encoding: 'utf8', timeout: 5000});
const missingPowerShell = pwsh.error?.code === 'ENOENT';

for (const [scenario, bytes, valid] of [['over installed target', 100000001, true], ['empty payload', 0, false]]) {
    test(`Windows public package report: ${scenario}`, {skip: missingPowerShell ? 'PowerShell is not installed on this host' : false}, t => {
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'podlord-windows-size-'));
        t.after(() => fs.rmSync(directory, {recursive: true, force: true}));
        const source = fs.readFileSync(script, 'utf8');
        const declaration = source.slice(source.indexOf('$result = [ordered]@{'), source.indexOf('\n$archiveCreated ='));
        const phase = source.slice(source.indexOf('    $result.installedBytes ='), source.indexOf('    $extracted = Join-Path'));
        assert.ok(declaration && phase);
        const payload = path.join(directory, 'package'); fs.mkdirSync(payload);
        fs.writeFileSync(path.join(payload, 'payload.bin'), ''); fs.truncateSync(path.join(payload, 'payload.bin'), bytes);
        const result = spawnSync('pwsh', ['-NoProfile', '-NonInteractive', '-Command', `
Set-StrictMode -Version Latest; $ErrorActionPreference = 'Stop'
$PackageDirectory = $env:TEST_PACKAGE; $ArchivePath = $env:TEST_ARCHIVE; $EvidenceDirectory = $env:TEST_EVIDENCE
$crtRoot = ''; $sslRoot = ''; $d3dCompiler = ''
${declaration}
$manifest = @(Get-ChildItem -LiteralPath $PackageDirectory -File | ForEach-Object { [pscustomobject]@{ bytes = $_.Length } })
${phase}
if ($result.errors.Count) { throw 'Size advisories were recorded as package failures.' }
Write-Output ('RESULT_JSON=' + ($result | ConvertTo-Json -Depth 8 -Compress))
`], {encoding: 'utf8', timeout: 15000, env: {...process.env, TEST_PACKAGE: payload,
            TEST_ARCHIVE: path.join(directory, 'package.zip'), TEST_EVIDENCE: directory}});
        assert.ifError(result.error);
        assert.equal(result.status === 0, valid, result.stdout + result.stderr);
        if (valid) {
            const data = JSON.parse(result.stdout.match(/^RESULT_JSON=(.+)$/m)[1]);
            assert.equal(data.installedBytes, bytes);
            assert.equal(data.installedTargetBytes, 100000000);
            assert.equal(data.downloadTargetBytes, 50000000);
            assert.equal(data.sizePolicy, 'advisory');
            assert.equal(data.sizeStatus, 'advisory-exceeded');
            assert.equal(data.errors.length, 0);
            assert.match(data.warnings[0], /metric=installed measured_bytes=100000001 target_bytes=100000000/);
            assert.ok(fs.statSync(path.join(directory, 'package.zip')).size > 0);
        } else assert.match(result.stdout + result.stderr, /runtime package is empty/);
    });
}

// Parse the real script on any host with PowerShell. This is not Windows execution proof.
test('PowerShell parser accepts the actual Windows package checker', {skip: missingPowerShell ? 'PowerShell is not installed on this host' : false}, () => {
    assert.ifError(pwsh.error);
    assert.equal(pwsh.status, 0, pwsh.stdout + pwsh.stderr);
    const result = spawnSync('pwsh', ['-NoProfile', '-NonInteractive', '-Command',
        '$tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile($env:PODLORD_WINDOWS_PACKAGE_SCRIPT,[ref]$tokens,[ref]$errors) | Out-Null; if($errors.Count){$errors | ForEach-Object {Write-Error $_}; exit 1}'],
    {encoding: 'utf8', timeout: 10000, env: {...process.env, PODLORD_WINDOWS_PACKAGE_SCRIPT: script}});
    assert.ifError(result.error);
    assert.equal(result.status, 0, result.stdout + result.stderr);
});

// Capture only this test's private fixture, never source or user directories.
function snapshot(directory, prefix = '') {
    const entries = [];
    for (const entry of fs.readdirSync(directory, {withFileTypes: true}).sort((a, b) => a.name.localeCompare(b.name))) {
        const name = path.join(prefix, entry.name);
        const full = path.join(directory, entry.name);
        if (entry.isDirectory()) entries.push([name, 'directory'], ...snapshot(full, name));
        else entries.push([name, fs.readFileSync(full).toString('base64')]);
    }
    return entries;
}

for (const [scenario, expected] of [
    ['existing package', /Output already exists/],
    ['existing archive', /Output already exists/],
    ['missing build', /Required build input missing/],
    ['relative path', /BuildDirectory must be absolute/],
    ['nested outputs', /Output paths must not overlap/],
    ['identical outputs', /Output paths must not overlap/],
    ['input-output collision', /Outputs must not overwrite source or SDK\/build inputs/],
]) {
    test(`Windows package CLI rejects ${scenario} without writes`, {
        skip: process.platform !== 'win32' ? 'Requires actual Windows PowerShell CLI execution'
            : missingPowerShell ? 'PowerShell is not installed on this host' : false,
    }, t => {
        assert.ifError(pwsh.error);
        assert.equal(pwsh.status, 0, pwsh.stdout + pwsh.stderr);
        const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'podlord-win-package-'));
        t.after(() => fs.rmSync(directory, {recursive: true, force: true}));
        const inputs = {
            BuildDirectory: path.join(directory, 'build'),
            QtDirectory: path.join(directory, 'qt'),
            PackageDirectory: path.join(directory, 'package'),
            ArchivePath: path.join(directory, 'package.zip'),
            EvidenceDirectory: path.join(directory, 'evidence'),
            SourceDirectory: root,
        };
        // Boundary inputs only. There is no fake application, Qt, CRT, or deploy tool.
        for (const input of [inputs.BuildDirectory, inputs.QtDirectory]) {
            fs.mkdirSync(input);
            fs.writeFileSync(path.join(input, 'preserve.txt'), 'existing input bytes');
        }
        if (scenario === 'existing package') {
            fs.mkdirSync(inputs.PackageDirectory);
            fs.writeFileSync(path.join(inputs.PackageDirectory, 'preserve.txt'), 'existing output bytes');
        } else if (scenario === 'existing archive') {
            fs.writeFileSync(inputs.ArchivePath, 'existing archive bytes');
        } else if (scenario === 'relative path') {
            inputs.BuildDirectory = 'relative-build';
        } else if (scenario === 'nested outputs') {
            inputs.EvidenceDirectory = path.join(inputs.PackageDirectory, 'evidence');
        } else if (scenario === 'identical outputs') {
            inputs.EvidenceDirectory = inputs.PackageDirectory;
        } else if (scenario === 'input-output collision') {
            inputs.PackageDirectory = path.join(inputs.BuildDirectory, 'package');
        }
        const before = snapshot(directory);
        const args = Object.entries(inputs).flatMap(([key, value]) => [`-${key}`, value]);
        const result = spawnSync('pwsh', ['-NoProfile', '-NonInteractive', '-File', script, ...args], {
            cwd: directory, encoding: 'utf8', timeout: 10000,
        });
        assert.ifError(result.error);
        assert.equal(result.signal, null, result.stdout + result.stderr);
        assert.notEqual(result.status, 0, result.stdout + result.stderr);
        assert.match(result.stdout + result.stderr, expected);
        assert.deepEqual(snapshot(directory), before, 'Rejected input must preserve all fixture bytes and create no outputs');
    });
}
