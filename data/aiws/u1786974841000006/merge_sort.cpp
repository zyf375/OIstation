// merge_sort.cpp —— 归并排序（自顶向下的递归 + 优化：小数组用插入排序）
// 编译: g++ -std=c++17 -O2 merge_sort.cpp -o merge_sort
#include <algorithm>
#include <iostream>
#include <vector>

// 插入排序：用于小规模区间，减少递归开销
void insertion_sort(std::vector<int>& a, int lo, int hi) {
    for (int i = lo + 1; i <= hi; ++i) {
        int key = a[i];
        int j = i - 1;
        while (j >= lo && a[j] > key) {
            a[j + 1] = a[j];
            --j;
        }
        a[j + 1] = key;
    }
}

// 合并两个有序区间 [lo, mid] 和 [mid+1, hi]，结果写回 a[lo..hi]
void merge(std::vector<int>& a, std::vector<int>& tmp, int lo, int mid, int hi) {
    for (int i = lo; i <= hi; ++i) tmp[i] = a[i];   // 复制到辅助数组

    int i = lo, j = mid + 1, k = lo;
    while (i <= mid && j <= hi) {
        if (tmp[i] <= tmp[j]) a[k++] = tmp[i++];    // <= 保证稳定
        else                 a[k++] = tmp[j++];
    }
    while (i <= mid) a[k++] = tmp[i++];
    while (j <= hi)  a[k++] = tmp[j++];
}

// 归并排序主函数
void merge_sort(std::vector<int>& a, std::vector<int>& tmp, int lo, int hi) {
    if (lo >= hi) return;                          // 递归出口

    // 优化：小数组直接插入排序
    if (hi - lo < 16) {
        insertion_sort(a, lo, hi);
        return;
    }

    int mid = lo + (hi - lo) / 2;                  // 防溢出
    merge_sort(a, tmp, lo, mid);
    merge_sort(a, tmp, mid + 1, hi);
    merge(a, tmp, lo, mid, hi);
}

void merge_sort(std::vector<int>& a) {
    std::vector<int> tmp(a.size());
    merge_sort(a, tmp, 0, static_cast<int>(a.size()) - 1);
}

int main() {
    std::vector<int> data = {38, 27, 43, 3, 9, 82, 10, 1, 55, 26, 91, 7, 19, 64, 33, 42};
    std::cout << "排序前: ";
    for (int x : data) std::cout << x << ' ';
    std::cout << '\n';

    merge_sort(data);

    std::cout << "排序后: ";
    for (int x : data) std::cout << x << ' ';
    std::cout << '\n';
    return 0;
}
