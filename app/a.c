#include <stdio.h>
#include <stdlib.h>
#include <string.h>
<<<<<<< HEAD
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
=======
#include <termios.h>
#include <unistd.h>

// 保存原始终端设置
struct termios orig_termios;

// 恢复终端模式
void reset_terminal_mode() {
    tcsetattr(0, TCSAFLUSH, &orig_termios);
}

// 设置终端为非规范模式（无缓冲，实时读取）
void set_conio_terminal_mode() {
    struct termios new_attr = orig_termios;
    new_attr.c_lflag &= ~ICANON;
    new_attr.c_lflag &= ~ECHO;
    new_attr.c_cc[VMIN] = 1;
    new_attr.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &new_attr);
}

// 读取单个字符
int getch() {
    return getchar();
}

// 显示菜单并高亮选中的选项
void print_menu(int highlight) {
    const char *options[] = {"Option 1", "Option 2", "Option 3", "Exit"};
    int count = sizeof(options) / sizeof(options[0]);

    // 清除当前行
    printf("\r\033[2K"); // 清除当前行
    int i;
    for (i = 0; i < count; i++) 
    {
        if (i + 1 == highlight) {
            printf("\033[7m%s\033[m ", options[i]); // 反显
        } else {
            printf("%s ", options[i]);
        }
    }
    fflush(stdout);
}

int main() {
    // 保存原始终端设置
    if (tcgetattr(0, &orig_termios) < 0) {
        perror("tcgetattr");
        exit(1);
    }

    // 注册退出时恢复终端设置
    atexit(reset_terminal_mode);

    // 设置非阻塞输入
    set_conio_terminal_mode();

    const char *menu_options[] = {"Option 1", "Option 2", "Option 3", "Exit"};
    int option_count = sizeof(menu_options) / sizeof(menu_options[0]);
    int selected = 1;

    print_menu(selected);

    while (1) {
        int c = getch();

        if (c == '\033') { // 处理 ESC 序列（方向键）
            getch(); // 跳过 [
            switch (getch()) {
                case 'A': // 上箭头
                    selected = (selected > 1) ? selected - 1 : option_count;
                    print_menu(selected);
                    break;
                case 'B': // 下箭头
                    selected = (selected < option_count) ? selected + 1 : 1;
                    print_menu(selected);
                    break;
            }
        } else if (c == '\n') { // 回车键
            printf("\nYou selected: %d\n", selected);
            if (selected == 4) {
                break;
            }
            // 这里可以添加其他功能分支
        }
    }

>>>>>>> 5ed245f8f6aa789781ae6c3d1cc690b0b4109f17
    return 0;
}