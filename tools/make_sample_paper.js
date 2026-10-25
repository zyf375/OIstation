// 建一份示例试卷（C++ 入门小测）
const HOST='http://127.0.0.1:8083';
async function login(user, pw) {
    const g = await fetch(HOST + '/login'); const html = await g.text();
    const tok = (html.match(/name='captcha_token' value='([^']+)'/) || [])[1];
    const q = (html.match(/验证码：([^<]+)<\/label>/) || [])[1] || '';
    const m = q.trim().match(/^(\d+)\s*([+\-x/])\s*(\d+)\s*=\s*\?$/);
    let ans=''; if(m){const a=+m[1],b=+m[3];ans=String(m[2]==='+'?a+b:m[2]==='-'?a-b:m[2]==='x'?a*b:Math.floor(a/b));}
    await new Promise(r=>setTimeout(r,1400));
    const body=`username=${user}&password=${pw}&captcha_token=${tok}&captcha_answer=${ans}`;
    const r=await fetch(HOST+'/login',{method:'POST',body,headers:{'Content-Type':'application/x-www-form-urlencoded'}});
    return (r.headers.getSetCookie?r.headers.getSetCookie():[]).map(s=>s.split(';')[0]).filter(s=>s.startsWith('SESSION=')).join('; ');
}
(async()=>{
  const sess=await login('admin','20121202');
  const f=new URLSearchParams();
  f.set('title','C++ 入门小测（一）');
  f.set('description','考察 C++ 基础语法与简单算法复杂度，共 6 题 100 分。交卷后自动批改并显示解析。');
  f.set('timeLimit','20');
  f.set('hidden','0');
  const Q=[
    {t:'choice',stem:'下列哪个头文件提供了 `cin` / `cout`？',opts:['<cstdio>','<iostream>','<cstdlib>','<cmath>'],ans:'B',score:15,ana:'`cin`、`cout` 定义在 `<iostream>` 中；`<cstdio>` 提供的是 `scanf`/`printf`。'},
    {t:'choice',stem:'`int` 在常见的 32 位编译器下占用多少字节？',opts:['2','4','8','取决于编译器设置，不能确定'],ans:'B',score:15,ana:'32/64 位 Windows 与 Linux 上 `int` 通常都是 4 字节。'},
    {t:'choice',stem:'以下哪些操作的平均时间复杂度是 $O(1)$？',opts:['数组下标访问','有序数组二分查找','哈希表查找','遍历单链表'],multi:true,ans:'AC',score:20,ana:'数组下标与哈希表平均都是 $O(1)$；二分是 $O(\\log n)$，遍历链表是 $O(n)$。',},
    {t:'fill',stem:'`1 + 2 * 3` 的结果是 ______ 。',ans:'7',score:15,ana:'乘法优先级高于加法。'},
    {t:'fill',stem:'`sizeof(int)` 为 4 时，`sizeof(int[10])` 等于 ______ 。',ans:'40',score:15,ana:'数组总大小 = 元素大小 × 元素个数 = 4 × 10 = 40。'},
    {t:'choice',stem:'若循环 `for (int i = 1; i <= n; i *= 2)`，循环体执行次数约为？',opts:['$n$','$\\sqrt{n}$','$\\log_2 n$','$n^2$'],ans:'C',score:20,ana:'i 每次翻倍，执行次数为 $\\lfloor \\log_2 n \\rfloor + 1$，即 $O(\\log n)$。'}
  ];
  Q.forEach((q,i)=>{
    f.set(`q${i}_type`,q.t);
    f.set(`q${i}_stem`,q.stem);
    if(q.opts) f.set(`q${i}_options`,q.opts.join('\n'));
    if(q.multi) f.set(`q${i}_multi`,'1');
    f.set(`q${i}_answer`,q.ans);
    f.set(`q${i}_score`,String(q.score));
    f.set(`q${i}_analysis`,q.ana||'');
  });
  const r=await fetch(HOST+'/admin/paper/add',{method:'POST',body:f,headers:{cookie:sess,'Content-Type':'application/x-www-form-urlencoded'}});
  const t=await r.text();
  console.log('created:', t.includes('/admin/papers'), t.slice(0,80));
})();
