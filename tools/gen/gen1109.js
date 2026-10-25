// 1109 矩阵主对角线之和
const { wc, rnd } = require('./_common');

const ID = 1109;
let n = 0;
function t(mat) {
  n++;
  const N = mat.length;
  let sum = 0;
  for (let i = 0; i < N; i++) sum += mat[i][i];
  const input = `${N}\n` + mat.map(r => r.join(' ')).join('\n');
  wc(ID, n, input, String(sum));
}

t([[1]]);
t([[1, 2], [3, 4]]);                       // 2x2
t([[-5, 0], [0, 5]]);                       // 负数
for (let N of [3, 10, 50, 100, 100]) {
  const m = Array.from({ length: N }, () => Array.from({ length: N }, () => rnd(-100, 100)));
  t(m);
}

console.log(`1109: generated ${n} test cases`);
