// 1210 最长回文子串 (CSP-S, Manacher)
const { wc, rnd, rstr } = require('./_common');

const ID = 1210;
let n = 0;
function manacher(s) {
  const T = '#' + s.split('').join('#') + '#';
  const n2 = T.length, P = new Array(n2).fill(0);
  let C = 0, R = 0, maxLen = 0;
  for (let i = 0; i < n2; i++) {
    const mir = 2 * C - i;
    P[i] = R > i ? Math.min(R - i, P[mir]) : 0;
    while (i + P[i] + 1 < n2 && i - P[i] - 1 >= 0 && T[i + P[i] + 1] === T[i - P[i] - 1]) P[i]++;
    if (i + P[i] > R) { C = i; R = i + P[i]; }
    maxLen = Math.max(maxLen, P[i]);
  }
  return String(maxLen);
}
function t(s) {
  n++;
  wc(ID, n, s, manacher(s));
}

t('a');
t('ab');
t('aa');
t('abcba');
t('abacab');
t('aaaaaa');                 // 全相同
t('abcdefghijklmnopqrstuvwxyz');
for (let L of [50, 1000, 20000, 200000]) t(rstr(L, 'abcde'));
// 含长回文
t('a'.repeat(10000) + 'x' + 'a'.repeat(9999));

console.log(`1210: generated ${n} test cases`);
