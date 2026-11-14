// Usage: NODE_PATH=<installed packages> node scripts/compose-visual-evidence.cjs <evidence directory>
// Original pixels are retained: C# on the left, C++ on the right, no crops or resizing.
const fs = require('node:fs/promises');
const path = require('node:path');
const { PNG } = require('pngjs');
const sharp = require('sharp');

async function main() {
    const directory = process.argv[2];
    if (process.argv.length !== 3 || !path.isAbsolute(directory)) throw Error('Expected one absolute evidence directory');
    const names = await fs.readdir(directory);
    const pairs = [];
    const decode = async file => {
        const {data,info} = await sharp(file).ensureAlpha().raw().toBuffer({resolveWithObject:true});
        return {data,width:info.width,height:info.height};
    };
    for (const legacy of names.filter(name => /^legacy-.+\.(png|jpg)$/.test(name)).sort()) {
        const suffix = legacy.slice('legacy-'.length);
        const native = `native-${suffix}`;
        if (!names.includes(native)) continue;
        const left = path.join(directory, legacy), right = path.join(directory, native);
        const a = await decode(left), b = await decode(right);
        if (!a.width || !a.height || !b.width || !b.height) throw Error(`Invalid screenshot pair: ${suffix}`);
        const output = `comparison-${suffix.replace(/\.(jpg|png)$/,'.png')}`;
        const combined = new PNG({width:a.width+b.width,height:Math.max(a.height,b.height)});
        combined.data.fill(255);
        for (const [source, offset] of [[a,0],[b,a.width]]) {
            for (let row=0;row<source.height;row++) {
                source.data.copy(combined.data,(row*combined.width+offset)*4,row*source.width*4,(row+1)*source.width*4);
            }
        }
        await fs.writeFile(path.join(directory,output),PNG.sync.write(combined));
        pairs.push({scenario:suffix.slice(0,-4),legacy,native,comparison:output,leftWidth:a.width,rightWidth:b.width,height:Math.max(a.height,b.height)});
    }
    if (!pairs.length) throw Error('No matching legacy/native screenshot pairs');
    await fs.writeFile(path.join(directory,'comparisons.json'),JSON.stringify(pairs,null,2)+'\n');
    const escape = text => text.replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('"','&quot;');
    await fs.writeFile(path.join(directory,'index.html'),`<!doctype html><html lang="en"><meta charset="utf-8"><title>Podlord visual comparison</title><style>body{font:16px Georgia,serif;margin:24px;background:#eee;color:#111}img{max-width:100%;height:auto}section{margin:32px 0}a{color:#164a6d}</style><h1>Podlord: C# left, C++ right</h1><p>Decoded screenshot pixels are preserved in lossless PNG comparisons. Original screenshots are retained; the capture service supplies JPEG or PNG. No crop, resize, overlay or generated UI. The browser may scale the preview. Missing views are recorded in the review, not simulated.</p>${pairs.map(pair=>`<section><h2>${escape(pair.scenario)}</h2><a href="${escape(pair.comparison)}"><img loading="lazy" alt="${escape(pair.scenario)}: C# left, C++ right" src="${escape(pair.comparison)}"></a></section>`).join('')}</html>`);
    console.log(`Composed ${pairs.length} lossless screenshot pairs`);
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
