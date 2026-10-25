# 与 merge_sort.cpp 逻辑完全一致的 Python 移植，用于验证算法正确性
def insertion_sort(a, lo, hi):
    for i in range(lo + 1, hi + 1):
        key = a[i]
        j = i - 1
        while j >= lo and a[j] > key:
            a[j + 1] = a[j]
            j -= 1
        a[j + 1] = key

def merge(a, tmp, lo, mid, hi):
    for i in range(lo, hi + 1):
        tmp[i] = a[i]
    i, j, k = lo, mid + 1, lo
    while i <= mid and j <= hi:
        if tmp[i] <= tmp[j]:
            a[k] = tmp[i]; i += 1
        else:
            a[k] = tmp[j]; j += 1
        k += 1
    while i <= mid:
        a[k] = tmp[i]; i += 1; k += 1
    while j <= hi:
        a[k] = tmp[j]; j += 1; k += 1

def merge_sort(a, tmp, lo, hi):
    if lo >= hi:
        return
    if hi - lo < 16:
        insertion_sort(a, lo, hi)
        return
    mid = lo + (hi - lo) // 2
    merge_sort(a, tmp, lo, mid)
    merge_sort(a, tmp, mid + 1, hi)
    merge(a, tmp, lo, mid, hi)

def sort(a):
    tmp = [0] * len(a)
    merge_sort(a, tmp, 0, len(a) - 1)
    return a

# 1) 样例
data = [38, 27, 43, 3, 9, 82, 10, 1, 55, 26, 91, 7, 19, 64, 33, 42]
print("排序前:", data)
print("排序后:", sort(list(data)))

# 2) 随机对拍 vs Python 内置 sorted
import random
random.seed(0)
ok = True
for t in range(2000):
    n = random.randint(0, 200)
    arr = [random.randint(-50, 50) for _ in range(n)]
    if sort(list(arr)) != sorted(arr):
        print("FAIL", arr); ok = False; break
print("随机对拍 2000 组:", "全部通过" if ok else "失败")

# 3) 稳定性检查：key = (值, 原下标)，排序后原下标应非递减
pairs = [(random.randint(0, 5), i) for i in range(300)]
res = sort(list(pairs))
stable = all(res[i][1] < res[i+1][1] for i in range(len(res)-1) if res[i][0] == res[i+1][0])
print("稳定性:", "稳定" if stable else "不稳定")
