// 1107 反转字符串
const { wc, rnd, rstr } = require('./_common');

const ID = 1107;
let n = 0;
function t(s) {
  n++;
  wc(ID, n, s, s.split('').reverse().join(''));
}

t('a');
t('ab');
t('abc');
t('A'); t('zZz'); t('hello_world_2026');
t('racecar');
t('0123456789');
for (let i = 0; i < 6; i++) t(rstr(rnd(1, 1000)));

console.log(`1107: generated ${n} test cases`);
