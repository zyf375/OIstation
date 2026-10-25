// 1102 偶数个数
const { wc, rnd } = require('./_common');

const ID = 1102;
let n = 0;
function t(arr) {
  n++;
  const out = arr.filter(x => x % 2 === 0).length;
  wc(ID, n, `${arr.length}\n${arr.join(' ')}`, String(out));
}

t([1]);                       // 单个奇数
t([2]);                       // 单个偶数
t([0]);                       // 0 是偶数
t([-3, -2, -1, 0, 1, 2, 3]);  // 含负数
// 随机
for (let sz of [10, 100, 1000, 10000, 100000]) {
  const a = Array.from({ length: sz }, () => rnd(-1000000000, 1000000000));
  t(a);
}
console.log(`1102: generated ${n} test cases`);
