// Fails the build if the gzipped dashboard outgrows its budget: it is compiled into
// the firmware and every byte comes out of the app partition.
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, relative } from 'node:path';
import { gzipSync } from 'node:zlib';

const BUDGET_BYTES = 200 * 1024;
const dist = new URL('../dist/', import.meta.url).pathname;

function walk(dir) {
  return readdirSync(dir).flatMap((name) => {
    const p = join(dir, name);
    return statSync(p).isDirectory() ? walk(p) : [p];
  });
}

const rows = walk(dist).map((file) => {
  const raw = readFileSync(file);
  return {
    file: relative(dist, file),
    raw: raw.length,
    gz: Math.min(raw.length, gzipSync(raw, { level: 9 }).length),
  };
});
rows.sort((a, b) => b.gz - a.gz);
const total = rows.reduce((s, r) => s + r.gz, 0);
const kb = (n) => `${(n / 1024).toFixed(1)} KB`;

for (const r of rows) console.log(`${kb(r.gz).padStart(10)}  ${r.file}  (${kb(r.raw)} raw)`);
console.log(`${kb(total).padStart(10)}  total gzipped, budget ${kb(BUDGET_BYTES)}`);
if (total > BUDGET_BYTES) {
  console.error('Dashboard is over its size budget.');
  process.exit(1);
}
