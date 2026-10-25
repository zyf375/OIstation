// ============================================================
// 题目 1001 (A+B problem) 测试数据生成器
// 输入：一行两个整数 a, b  输出：a + b
// 用法：node gen1001.js [数据目录]
// 默认输出到 ../problems/1001/
// ============================================================
const fs = require('fs');
const path = require('path');

const OUT_DIR = process.argv[2] || path.join(__dirname, '..', 'problems', '1001');

// 随机整数（含正负）
function randInt(min, max) {
    return Math.floor(Math.random() * (max - min + 1)) + min;
}

// 生成单个测试点
function makeCase(a, b) {
    return { input: `${a} ${b}\n`, output: `${a + b}\n` };
}

// 边界值 -1e9 ~ 1e9 的 A+B（用 BigInt 避免精度问题）
function bigInputOutput(a, b) {
    const A = BigInt(a), B = BigInt(b);
    return { input: `${a} ${b}\n`, output: `${(A + B).toString()}\n` };
}

function main() {
    fs.mkdirSync(OUT_DIR, { recursive: true });

    const cases = [];

    // 1. 基础样例
    cases.push({ input: '20 30\n', output: '50\n' });
    // 2. 0 + 0
    cases.push({ input: '0 0\n', output: '0\n' });
    // 3. 负数
    cases.push({ input: '-5 8\n', output: '3\n' });
    cases.push({ input: '-100 -200\n', output: '-300\n' });
    // 4. 大整数边界（1e9 级别）
    cases.push(bigInputOutput(1000000000, 1000000000));
    cases.push(bigInputOutput(-1000000000, -1000000000));
    cases.push(bigInputOutput(-1000000000, 1000000000));
    cases.push(bigInputOutput(999999999, 1));
    // 5. 一正一负，和为负
    cases.push(bigInputOutput(100, -1000));
    // 6. 随机数据（int 范围内）
    for (let i = 0; i < 12; i++) {
        const a = randInt(-1000000, 1000000);
        const b = randInt(-1000000, 1000000);
        cases.push(makeCase(a, b));
    }
    // 7. 再补几组更大随机数
    for (let i = 0; i < 4; i++) {
        const a = randInt(-1000000000, 1000000000);
        const b = randInt(-1000000000, 1000000000);
        cases.push(bigInputOutput(a, b));
    }

    // 写入 .in / .out 文件（从 1 开始编号）
    cases.forEach((c, idx) => {
        const n = idx + 1;
        fs.writeFileSync(path.join(OUT_DIR, `${n}.in`), c.input);
        fs.writeFileSync(path.join(OUT_DIR, `${n}.out`), c.output);
    });

    console.log(`✔ 已生成 ${cases.length} 组测试数据到 ${OUT_DIR}`);
    cases.forEach((c, idx) => {
        console.log(`  ${idx + 1}.in  ->  ${c.input.trim().replace(/\n/g, ' \\n ')}`);
    });
}

main();
