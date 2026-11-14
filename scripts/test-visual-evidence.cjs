const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const sharp = require('sharp');
const { PNG } = require('pngjs');

async function main() {
  assert.equal(process.argv.length, 4, 'Usage: node scripts/test-visual-evidence.cjs LEGACY_SCREENSHOT NATIVE_SCREENSHOT');
  const originals = process.argv.slice(2).map(file => path.resolve(file));
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'podlord-picture-check-'));
  try {
    for (const [index, original] of originals.entries()) {
      fs.copyFileSync(original, path.join(directory, `${index ? 'native' : 'legacy'}-pixel-check${path.extname(original)}`));
    }
    const result = spawnSync(process.execPath, [path.join(__dirname, 'compose-visual-evidence.cjs'), directory], { encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr || result.stdout);
    const entries = JSON.parse(fs.readFileSync(path.join(directory, 'comparisons.json'), 'utf8'));
    assert.equal(entries.length, 1);
    const composed = PNG.sync.read(fs.readFileSync(path.join(directory, entries[0].comparison)));
    const left = await sharp(originals[0]).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
    const right = await sharp(originals[1]).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
    assert.equal(composed.width, left.info.width + right.info.width);
    assert.equal(composed.height, Math.max(left.info.height, right.info.height));
    for (const [source, offset] of [[left, 0], [right, left.info.width]]) {
      const length = source.info.width * 4;
      for (let row = 0; row < source.info.height; row++) {
        const start = (row * composed.width + offset) * 4;
        assert.deepEqual(composed.data.subarray(start, start + length), source.data.subarray(row * length, (row + 1) * length));
      }
    }
    console.log('Passed: original decoded screenshot pixels preserved at both placements.');
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });
