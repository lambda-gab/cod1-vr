// Minimal MCP stdio client for REA: keeps one Ghidra session alive across queries.
// usage: node reacli.mjs <binary> <queries.json> <out.json>
//   queries.json: [{"tool":"name","args":{...}}, ...]   ("$BIN" in args is replaced by the binary path)
//   special: {"tool":"__list"} prints the tool catalog
import { spawn } from 'node:child_process';
import fs from 'node:fs';

const [bin, qfile, ofile] = process.argv.slice(2);
const entry = process.env.APPDATA + '\\npm\\node_modules\\rea-agents\\scripts\\rea.mjs';
const child = spawn(process.execPath, [entry, '--mcp'], {
  env: { ...process.env,
    // GHIDRA_INSTALL_DIR and JAVA_HOME come from your environment (see README)
    REA_GHIDRA_STARTUP_TIMEOUT_MS: '3600000' },
  stdio: ['pipe', 'pipe', 'inherit'] });

let buf = '', id = 0; const pending = new Map();
child.stdout.on('data', d => {
  buf += d; let i;
  while ((i = buf.indexOf('\n')) >= 0) {
    const line = buf.slice(0, i).trim(); buf = buf.slice(i + 1);
    if (!line) continue;
    let m; try { m = JSON.parse(line); } catch { continue; }
    if (m.id != null && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  }
});
const rpc = (method, params) => new Promise(r => { const i = ++id; pending.set(i, r);
  child.stdin.write(JSON.stringify({ jsonrpc: '2.0', id: i, method, params }) + '\n'); });
const sub = v => JSON.parse(JSON.stringify(v).replaceAll('$BIN', bin.replaceAll('\\', '\\\\')));

await rpc('initialize', { protocolVersion: '2025-06-18', capabilities: {}, clientInfo: { name: 'reacli', version: '1' } });
child.stdin.write(JSON.stringify({ jsonrpc: '2.0', method: 'notifications/initialized' }) + '\n');

const queries = JSON.parse(fs.readFileSync(qfile, 'utf8'));
const results = [];
for (const q of queries) {
  const t0 = Date.now();
  let r;
  if (q.tool === '__xdec') {
    // xrefs(address) -> containing procedures -> pseudocode saved to out/dec_<proc>.c
    const call = async (name, args) => (await rpc('tools/call', { name, arguments: args })).result?.structuredContent?.normalized_result;
    const refs = (await call('xrefs', { address: q.args.address })) || [];
    const procs = {};
    for (const a of refs.slice(0, q.args.max || 12)) {
      const p = (await call('resolve_containing_procedure', { address: a }))?.procedure?.address;
      if (p) (procs[p] ||= []).push(a);
    }
    for (const p of Object.keys(procs)) {
      if (fs.existsSync(`out/dec_${p}.c`)) continue;
      const code = await call('procedure_pseudo_code', { procedure: p });
      if (typeof code === 'string') fs.writeFileSync(`out/dec_${p}.c`, code.replace(/\r/g, ''));
    }
    r = { refs, procs };
  } else if (q.tool === '__dec') {
    for (const p of q.args.procs) {
      const code = (await rpc('tools/call', { name: 'procedure_pseudo_code', arguments: { procedure: p } })).result?.structuredContent?.normalized_result;
      if (typeof code === 'string') fs.writeFileSync(`out/dec_${p}.c`, code.replace(/\r/g, ''));
    }
    r = 'ok';
  } else if (q.tool === '__list') {
    r = await rpc('tools/list', {});
    r = (r.result?.tools || []).map(t => ({ name: t.name, desc: (t.description || '').slice(0, 160), args: Object.keys(t.inputSchema?.properties || {}) }));
  } else {
    r = await rpc('tools/call', { name: q.tool, arguments: sub(q.args || {}) });
    r = r.error ? { error: r.error } : (r.result?.structuredContent ?? r.result?.content?.map(c => c.text).join('\n'));
  }
  results.push({ q, secs: (Date.now() - t0) / 1000, r });
  fs.writeFileSync(ofile, JSON.stringify(results, null, 1));
  console.error(`[reacli] ${q.tool} done in ${(Date.now() - t0) / 1000}s`);
}
child.kill(); process.exit(0);
