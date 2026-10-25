// 1110 排序去重
const { wc, rnd, shuffle } = require('./_common');

const ID = 1110;
let n = 0;
function t(arr) {
  n++;
  const uniq = [...new Set(arr)].sort((a, b) => a - b);
  wc(ID, n, `${arr.length}\n${arr.join(' ')}`, uniq.join(' '));
}

t([1]);                                    // 单元素
t([5, 5, 5, 5]);                           // 全重复
t([-3, -1, -2, -1, -3]);                   // 负数重复
t([1, 2, 3, 4, 5]);                        // 已升序
for (let sz of [10, 100, 1000, 50000]) {
  const a = Array.from({ length: sz }, () => rnd(-1000000000, 1000000000));
  t(a);
}
// 大量重复
{
  const a = shuffle(Array.from({ length: 10000 }, () => rnd(1, 20)));
  t(a);
}

console.log(`1110: generated ${n} test cases`);
