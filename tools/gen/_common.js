// 通用数据生成工具：写入测试点 / 随机数
const fs = require('fs');
const path = require('path');

const ROOT = 'E:/LL/OIstation';

// 写入一个测试点（id 题目编号，n 测试点序号，input 输入，output 输出）
function wc(id, n, input, output) {
  const dir = path.join(ROOT, 'problems', String(id));
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, n + '.in'), String(input).replace(/\n$/, '') + '\n');
  fs.writeFileSync(path.join(dir, n + '.out'), String(output).replace(/\n$/, '') + '\n');
}

// [a,b] 区间随机整数
function rnd(a, b) { return a + Math.floor(Math.random() * (b - a + 1)); }
// [0,max) 随机整数
function ri(max) { return Math.floor(Math.random() * max); }

function shuffle(a) {
  for (let i = a.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [a[i], a[j]] = [a[j], a[i]];
  }
  return a;
}

// 生成随机的可读字符串（不含空格）
function rstr(len, charset) {
  const c = charset || 'abcdefghijklmnopqrstuvwxyz0123456789';
  let s = '';
  for (let i = 0; i < len; i++) s += c[ri(c.length)];
  return s;
}

module.exports = { ROOT, wc, rnd, ri, shuffle, rstr };
