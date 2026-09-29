// Optional asset tooling. Normal C++ builds use the checked-in ICO and need no Node.js.
const fs = require('node:fs/promises');
const path = require('node:path');
const sharp = require('sharp');

async function main() {
    const assets = path.resolve(__dirname, '../assets');
    const source = await fs.readFile(path.join(assets, 'layoutdock.svg'));
    const sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256];
    const frames = await Promise.all(sizes.map(size => sharp(source, { density: 384 })
        .resize(size, size).png().toBuffer()));

    // ICO directory followed by PNG-compressed RGBA frames, supported by Windows 11.
    const directory = Buffer.alloc(6 + sizes.length * 16);
    directory.writeUInt16LE(1, 2);
    directory.writeUInt16LE(sizes.length, 4);
    let offset = directory.length;
    sizes.forEach((size, index) => {
        const entry = 6 + index * 16;
        directory[entry] = directory[entry + 1] = size === 256 ? 0 : size;
        directory.writeUInt16LE(1, entry + 4);
        directory.writeUInt16LE(32, entry + 6);
        directory.writeUInt32LE(frames[index].length, entry + 8);
        directory.writeUInt32LE(offset, entry + 12);
        offset += frames[index].length;
    });
    await fs.writeFile(path.join(assets, 'layoutdock.ico'), Buffer.concat([directory, ...frames]));
    await fs.writeFile(path.join(assets, 'layoutdock.png'), frames[frames.length - 1]);
    console.log(`Generated LayoutDock icon: ${sizes.join(', ')} px.`);
}

main().catch(error => { console.error(error); process.exitCode = 1; });
