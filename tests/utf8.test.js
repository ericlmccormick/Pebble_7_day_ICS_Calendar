// Run with: node tests/utf8.test.js
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync('src/pkjs/index.js', 'utf8');
const start = source.indexOf('function encodeTitleUtf8(');
const end = source.indexOf('// --- Core Functions ---', start);
assert(start >= 0 && end > start);
const scope = {};
vm.runInNewContext(source.slice(start, end), scope);
function encode(s) {
  const buffer = new Uint8Array(56);
  scope.encodeTitleUtf8(s, buffer, 0);
  const end = buffer.indexOf(0);
  return Buffer.from(buffer.slice(0, end < 0 ? 56 : end));
}
for (const value of [
  'Réunion à l’école', 'Café Noël', 'École', 'مرحبا', '日本語',
  'Planning 📅', 'a'.repeat(55), 'é'.repeat(27),
  'a'.repeat(54) + 'é', '😀'.repeat(14), '\uD800', '\uDC00'
]) {
  const encoded = encode(value);
  assert(encoded.length <= 55, 'title exceeds budget');
  assert(!encoded.toString('utf8').includes('\uFFFD') || value.includes('\uD800') || value.includes('\uDC00'),
    'UTF-8 was truncated mid-character: ' + value);
}
assert.equal(encode('Café').toString('hex'), '436166c3a9');
assert.equal(encode('a'.repeat(54) + 'é').toString('utf8'), 'a'.repeat(54));
assert.equal(encode('📅').toString('hex'), 'f09f9385');
console.log('UTF-8 title tests passed');
