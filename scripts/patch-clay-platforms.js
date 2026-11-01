// Patch both npm and Pebble's generated package manifest for Clay 1.0.4.
'use strict';
const fs = require('fs');
const path = require('path');
const root = path.join(__dirname, '..', 'node_modules', 'pebble-clay');
const manifests = [
  path.join(root, 'package.json'),
  path.join(root, 'dist', 'include', 'pebble-clay', 'package.json')
];
let patched = 0;
for (const manifest of manifests) {
  if (!fs.existsSync(manifest)) continue;
  const pkg = JSON.parse(fs.readFileSync(manifest, 'utf8'));
  if (!pkg.pebble || !Array.isArray(pkg.pebble.targetPlatforms)) {
    throw new Error('Unexpected Clay package manifest: ' + manifest);
  }
  for (const platform of ['flint', 'gabbro']) {
    if (pkg.pebble.targetPlatforms.indexOf(platform) === -1) pkg.pebble.targetPlatforms.push(platform);
  }
  fs.writeFileSync(manifest, JSON.stringify(pkg, null, 2) + '\n');
  console.log('Updated Clay platforms in ' + manifest);
  patched++;
}
if (!patched) throw new Error('Clay package not installed');
