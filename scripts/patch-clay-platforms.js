// Clay 1.0.4 predates Flint and Gabbro. Its phone-side configuration
// library works independently of watch hardware, but Pebble's package
// builder rejects targets missing from the dependency's platform manifest.
// Extend the installed manifest without modifying upstream source.
'use strict';
const fs = require('fs');
const path = require('path');
const manifest = path.join(__dirname, '..', 'node_modules', 'pebble-clay', 'package.json');

if (!fs.existsSync(manifest)) {
  console.error('pebble-clay is missing; run npm install first');
  process.exit(1);
}
const pkg = JSON.parse(fs.readFileSync(manifest, 'utf8'));
if (!pkg.pebble || !Array.isArray(pkg.pebble.targetPlatforms)) {
  throw new Error('Unexpected pebble-clay manifest structure');
}
for (const platform of ['flint', 'gabbro']) {
  if (pkg.pebble.targetPlatforms.indexOf(platform) === -1) {
    pkg.pebble.targetPlatforms.push(platform);
  }
}
fs.writeFileSync(manifest, JSON.stringify(pkg, null, 2) + '\n');
console.log('Clay platform metadata ready: ' + pkg.pebble.targetPlatforms.join(', '));
