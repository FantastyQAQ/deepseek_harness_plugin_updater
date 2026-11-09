// npm 包最新版本与发布时间批量查询脚本（供 dsh_plugin_updater 调用）
// ---------------------------------------------------------------------------
// 设计目标：
//   一次 node 进程并发查询一批 npm 包的最新版本（dist-tags.latest）及其发布时间，
//   避免为每个包单独起一个 npm.cmd 进程（单次 npm 启动 0.4s + registry 往返
//   1~3s，27 个包串行要 40s+；多个 npm 进程并发又会因缓存锁争抢更慢）。
//
//   为什么拉完整 packument 而不是轻量的 dist-tags 端点：dist-tags 端点不带发布
//   时间，而 DSH 安装插件的供应链策略（minimumReleaseAge = 24h）会把「发布不足
//   24 小时」的版本当作不存在——需要发布时间才能在界面上解释「更新成功但版本
//   没变」的现象。完整 packument gzip 后单个约 60~90KB，27 个包约 3~5s，可接受。
//
// 用法：
//   node npm_dist_tags.js            （包名从标准输入逐行读取）
//   node npm_dist_tags.js pkg1 pkg2  （也兼容命令行直接给包名）
//
// 输出（逐行流式打印，供宿主实时解析）：
//   <包名>\t<最新版本>\t<发布时间ISO8601>   查询成功（发布时间字段可空）
//   <包名>\t!<错误信息>                     查询失败（叹号前缀表示失败）
//
// 退出码：0（个别包失败不影响退出码，失败信息随行输出）
// ---------------------------------------------------------------------------
'use strict';

const fs = require('fs');
const os = require('os');
const path = require('path');
const https = require('https');
const zlib = require('zlib');

const CONCURRENCY = 16;   // 同时在飞的请求数上限，避免包很多时打爆 registry
const TIMEOUT_MS = 20000; // 单个请求超时

/**
 * 读取 ~/.npmrc，解析出全局 registry、按 scope 的 registry 以及鉴权 token。
 * 只做够用即可的解析，不追求覆盖 npmrc 全部语法。
 * @returns {{registry: string, scoped: Object, tokens: Array}}
 */
function readNpmConfig() {
  const result = {
    registry: 'https://registry.npmjs.org/',
    scoped: {},   // "@scope" -> registry
    tokens: []    // [{ match: "//host/path/", token: "xxx" }]
  };

  let text = '';
  for (const f of [path.join(os.homedir(), '.npmrc')]) {
    try { text += fs.readFileSync(f, 'utf8') + '\n'; } catch (e) { /* 无此文件则跳过 */ }
  }

  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith('#') || line.startsWith(';')) continue;
    const eq = line.indexOf('=');
    if (eq <= 0) continue;
    const key = line.slice(0, eq).trim();
    const val = line.slice(eq + 1).trim();

    if (key === 'registry') {
      result.registry = val;
    } else if (key.startsWith('@') && key.endsWith(':registry')) {
      // 形如 @myscope:registry=https://...
      result.scoped[key.slice(0, -':registry'.length)] = val;
    } else if (key.endsWith(':_authToken')) {
      // 形如 //registry.npmjs.org/:_authToken=xxx
      result.tokens.push({ match: key.slice(0, -':_authToken'.length), token: val });
    }
  }
  return result;
}

/**
 * 按包名选出应使用的 registry 地址（scoped 包优先用其专属 registry）。
 * @param {Object} cfg readNpmConfig 的返回值
 * @param {string} pkg 包名
 * @returns {string} 结尾不带斜杠的 registry 根地址
 */
function registryFor(cfg, pkg) {
  if (pkg.startsWith('@')) {
    const scope = pkg.slice(0, pkg.indexOf('/'));
    if (cfg.scoped[scope]) return cfg.scoped[scope].replace(/\/+$/, '');
  }
  return cfg.registry.replace(/\/+$/, '');
}

/**
 * 在 token 表里找与目标 URL 主机/路径前缀匹配的鉴权 token。
 * npm 的约定是 //host/path/:_authToken，这里按 URL 去掉协议后的串做前缀匹配。
 * @param {Object} cfg readNpmConfig 的返回值
 * @param {string} url 完整请求 URL
 * @returns {string} 命中的 token，没有则空串
 */
function tokenForUrl(cfg, url) {
  const stripped = url.replace(/^https?:/, ''); // //host/path/...
  for (const t of cfg.tokens) {
    if (stripped.startsWith(t.match)) return t.token;
  }
  return '';
}

/**
 * 查询单个包的最新版本与发布时间（拉完整 packument）。
 * @param {Object} cfg npm 配置
 * @param {string} pkg 包名
 * @returns {Promise<string>} 成功返回 "版本\t发布时间ISO"；失败返回以 "!" 开头的错误信息
 */
function fetchLatest(cfg, pkg) {
  const base = registryFor(cfg, pkg);
  // scoped 包的 / 需要转义成 %2f（encodeURIComponent 会做），@ 保留可读形式
  const url = base + '/' + encodeURIComponent(pkg).replace('%40', '@');
  const headers = {
    accept: 'application/vnd.npm+json', // 完整 packument（含 dist-tags 与 time）
    'accept-encoding': 'gzip',          // packument 较大，要求压缩传输
  };
  const token = tokenForUrl(cfg, url);
  if (token) headers.authorization = 'Bearer ' + token;

  return new Promise((resolve) => {
    const req = https.get(url, { headers, timeout: TIMEOUT_MS }, (res) => {
      // 响应可能 gzip 压缩也可能未压缩，按响应头决定是否过解压流
      let stream = res;
      if (res.headers['content-encoding'] === 'gzip') {
        stream = res.pipe(zlib.createGunzip());
      }
      let body = '';
      stream.setEncoding('utf8');
      stream.on('data', (c) => { body += c; });
      stream.on('error', () => resolve('!gunzip error'));
      stream.on('end', () => {
        if (res.statusCode !== 200) return resolve('!HTTP ' + res.statusCode);
        try {
          const j = JSON.parse(body);
          const tags = (j && j['dist-tags']) || {};
          if (!tags.latest) return resolve('!no latest tag');
          // time 表里按版本号取发布时间；个别 registry 可能缺 time，容错为空串
          const time = (j.time && j.time[tags.latest]) || '';
          resolve(tags.latest + '\t' + time);
        } catch (e) {
          resolve('!parse error');
        }
      });
    });
    req.on('timeout', () => { req.destroy(); resolve('!timeout'); });
    req.on('error', (e) => resolve('!' + (e.message || 'request error')));
  });
}

/**
 * 入口：读取包名（优先标准输入，其次命令行参数），按并发上限逐个查询并即时打印。
 */
(async function main() {
  let pkgs = process.argv.slice(2).filter(Boolean);

  // 命令行未给包名时，从标准输入逐行读取（宿主一次性写入后关闭 stdin）
  if (pkgs.length === 0) {
    const chunks = [];
    await new Promise((resolve) => {
      process.stdin.setEncoding('utf8');
      process.stdin.on('data', (c) => chunks.push(c));
      process.stdin.on('end', resolve);
      process.stdin.on('error', resolve);
    });
    pkgs = chunks.join('').split(/\r?\n/).map((s) => s.trim()).filter(Boolean);
  }
  if (pkgs.length === 0) return;

  const cfg = readNpmConfig();
  let next = 0; // 下一个待取的任务下标（多个 worker 共享）

  // 工人模型：固定 CONCURRENCY 个 worker 并发抓取，谁先完成谁先打印
  async function worker() {
    while (next < pkgs.length) {
      const pkg = pkgs[next++];
      const value = await fetchLatest(cfg, pkg);
      process.stdout.write(pkg + '\t' + value + '\n');
    }
  }

  const workers = [];
  const n = Math.min(CONCURRENCY, pkgs.length);
  for (let i = 0; i < n; i++) workers.push(worker());
  await Promise.all(workers);
})();
