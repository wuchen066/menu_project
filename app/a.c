#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// 自定义四舍五入函数（保留到整数 GB）
double my_round(double x) {
    int integer_part = (int)x;
    double decimal_part = x - integer_part;

    if (decimal_part >= 0.5) {
        return integer_part + 1;
    } else {
        return integer_part;
    }
}

// 获取物理内存总量（通过 dmidecode，需要 root 权限）
double get_physical_memory_gb() {
    FILE *fp = popen("sudo dmidecode -t memory | grep 'Size' | grep -v 'No Module Installed'", "r");
    if (!fp) {
        fprintf(stderr, "无法执行 dmidecode\n");
        return -1;
    }

    char line[256];
    double total_gb = 0.0;

    while (fgets(line, sizeof(line), fp)) {
        int size;
        char unit[16];

        if (sscanf(line, " Size: %d %s", &size, unit) == 2) {
            if (strcmp(unit, "MB") == 0) {
                total_gb += size / 1024.0;
            } else if (strcmp(unit, "GB") == 0) {
                total_gb += size;
            }
        }
    }

    pclose(fp);
    return total_gb;
}

// 打印内存信息函数
void print_memory_info() {
    // 第一步：获取物理内存总量（来自 dmidecode）
    double phys_mem_gb = get_physical_memory_gb();
    if (phys_mem_gb <= 0) {
        fprintf(stderr, "无法获取物理内存大小\n");
        return;
    }

    // 第二步：读取 /proc/meminfo 获取 MemFree、Buffers、Cached
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        perror("无法打开 /proc/meminfo");
        return;
    }

    double mem_free = 0.0;
    double buffers = 0.0;
    double cached = 0.0;

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char key[64], val[64];

        if (sscanf(line, "%63[^:]: %63s", key, val) != 2)
            continue;

        if (strcmp(key, "MemFree") == 0) {
            mem_free = atof(val) / (1024.0 * 1024.0); // KB -> GiB
        } else if (strcmp(key, "Buffers") == 0) {
            buffers = atof(val) / (1024.0 * 1024.0);
        } else if (strcmp(key, "Cached") == 0) {
            cached = atof(val) / (1024.0 * 1024.0);
        }
    }

    fclose(fp);

    // 计算已使用内存
    double used = phys_mem_gb - (mem_free + buffers + cached);
    double percent = (used / phys_mem_gb) * 100.0;

    // 四舍五入显示
    double rounded_used = my_round(used);
    double rounded_total = my_round(phys_mem_gb);

    // 带颜色输出
    printf("\033[0;33m%.0f GiB\033[0m of \033[1;34m%.0f GiB\033[0m RAM used (\033[0;33m%.2f%%\033[0m)\n",
           rounded_used, rounded_total, percent);
}

int main() {
    print_memory_info();
    return 0;
}