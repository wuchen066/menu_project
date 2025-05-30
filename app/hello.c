#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>   // uid_t
#include <sys/utsname.h> // uname
#include <time.h>        // time, localtime
#include <dirent.h>      // opendir
#include <string.h>
#include <ctype.h>
#include <math.h>

#define MAX_LINE 256

// 结构体：保存发行版信息
typedef struct {
    char name[128];
    char version[128];
} DistributionInfo;

// 全局系统信息
struct utsname sysinfo;

// 工具函数：检查目录是否存在
int directory_exists(const char *path) {
    DIR *dir = opendir(path);
    if (dir) {
        closedir(dir);
        return 1;
    }
    return 0;
}

// 工具函数：读取文件内容到缓冲区（去除换行符）
char* read_file_content(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return NULL;

    char *line = malloc(MAX_LINE);
    if (!line) {
        fclose(fp);
        return NULL;
    }

    if (fgets(line, MAX_LINE, fp)) {
        line[strcspn(line, "\n")] = '\0'; // 移除换行符
    } else {
        free(line);
        line = NULL;
    }

    fclose(fp);
    return line;
}

// 工具函数：去除字符串两端的引号和空格
void trim_quotes_and_whitespace(char *str) {
    char *src = str;
    char *dst = str;

    while (isspace((unsigned char)*src)) src++;
    if (*src == '"' || *src == '\'') src++;

    while (*src && !((*src == '"' || *src == '\'') && src[1] == '\0' && src != dst))
        *dst++ = *src++;

    while (dst > str && isspace((unsigned char)*(dst - 1)))
        dst--;

    *dst = '\0';
}

// 获取 Android 品牌和型号（模拟 getprop）
char* get_android_model() {
    char brand[128] = {0};
    char model[128] = {0};

    FILE *fp_brand = popen("getprop ro.product.brand", "r");
    FILE *fp_model = popen("getprop ro.product.model", "r");

    if (!fp_brand || !fp_model) {
        if (fp_brand) pclose(fp_brand);
        if (fp_model) pclose(fp_model);
        return NULL;
    }

    if (fgets(brand, sizeof(brand), fp_brand))
        brand[strcspn(brand, "\n")] = '\0';

    if (fgets(model, sizeof(model), fp_model))
        model[strcspn(model, "\n")] = '\0';

    pclose(fp_brand);
    pclose(fp_model);

    if (!strlen(brand) || !strlen(model))
        return NULL;

    char *result = malloc(strlen(brand) + strlen(model) + 2);
    if (!result) return NULL;

    sprintf(result, "%s %s", brand, model);
    return result;
}

// 主函数：获取硬件模型信息
char* get_hardware_model() {
    char *model = NULL;

    // 判断是否为 Android 系统
    if (directory_exists("/system/app") && directory_exists("/system/priv-app")) {
        model = get_android_model();
        if (model) return model;
    }

    // DMI 设备信息（x86/PC/虚拟机）
    const char *file1 = "/sys/devices/virtual/dmi/id/product_name";
    const char *file2 = "/sys/devices/virtual/dmi/id/product_version";

    char *name = read_file_content(file1);
    char *version = read_file_content(file2);

    if (name && version) {
        model = malloc(strlen(name) + strlen(version) + 2);
        if (model) {
            sprintf(model, "%s %s", name, version);
            free(name);
            free(version);
            return model;
        }
        free(name);
        free(version);
    }

    // ARM 设备树信息
    const char *file3 = "/sys/firmware/devicetree/base/model";
    model = read_file_content(file3);
    if (model) return model;

    // tmp 文件缓存
    const char *file4 = "/tmp/sysinfo/model";
    model = read_file_content(file4);
    if (model) return model;

    // 默认兜底
    return strdup("Unknown Hardware");
}

// 从 os-release 加载发行版信息
int load_os_release(const char *filename, DistributionInfo *info) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return -1;

    char line[MAX_LINE];
    int found_name = 0, found_version = 0;

    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '#' || strlen(line) == 0)
            continue;

        char *equal_sign = strchr(line, '=');
        if (!equal_sign)
            continue;

        size_t key_len = equal_sign - line;
        char key[64], value[256];

        strncpy(key, line, key_len);
        key[key_len] = '\0';

        strcpy(value, equal_sign + 1);
        trim_quotes_and_whitespace(value);

        if (strcmp(key, "NAME") == 0) {
            snprintf(info->name, sizeof(info->name), "%s", value);
            found_name = 1;
        } else if (strcmp(key, "VERSION_ID") == 0) {
            snprintf(info->version, sizeof(info->version), "%s", value);
            found_version = 1;
        }

        if (found_name && found_version) break;
    }

    fclose(fp);
    return (found_name && found_version) ? 0 : -1;
}

// 尝试从多个来源获取发行版信息
int get_distribution_info(DistributionInfo *info) {
    if (load_os_release("/etc/os-release", info) == 0)
        return 0;

    const char *paths[] = {
        "/etc/lsb-release",
        "/etc/fedora-release",
        "/etc/centos-release"
    };

    size_t i;
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        FILE *fp = fopen(paths[i], "r");
        if (fp) {
            char buffer[MAX_LINE];
            if (fgets(buffer, sizeof(buffer), fp)) {
                strtok(buffer, " ");
                snprintf(info->name, sizeof(info->name), "%s", buffer);
                info->version[0] = '\0';
            }
            fclose(fp);
            return 0;
        }
    }

#ifdef __FreeBSD__
    FILE *fp = popen("uname -sr", "r");
    if (fp) {
        char buffer[MAX_LINE];
        if (fgets(buffer, sizeof(buffer), fp)) {
            sscanf(buffer, "%s %s", info->name, info->version);
        }
        pclose(fp);
        return 0;
    }
#endif

    snprintf(info->name, sizeof(info->name), "Unknown");
    snprintf(info->version, sizeof(info->version), "Unknown");
    return -1;
}

// 显示系统时间
void print_current_time() {
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char buffer[100];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    printf("    当前时间: %s\n", buffer);
}

// 显示系统环境信息
void print_system_environment() {
    char *hardware_model = get_hardware_model();
    printf("        系统环境: %s\n", hardware_model);
    free(hardware_model);
}

// 显示发行版信息
void print_distribution_info() {
    DistributionInfo distro_info;
    if (get_distribution_info(&distro_info) == 0) {
        printf("        系统版本: %s %s\n",
               distro_info.name,
               distro_info.version[0] ? distro_info.version : "");
    } else {
        printf("无法识别发行版信息。\n");
    }
}

// 显示内核和主机名信息
void print_kernel_and_hostname() {
    printf("        内核版本: %s\n", sysinfo.release);
    printf("        主机名称: %s\n", sysinfo.nodename);
}

// 显示CPU 型号和逻辑核心数
void print_cpu_info() {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) {
        perror("无法打开 /proc/cpuinfo");
        return;
    }

    char line[512];
    char cpu_model[256] = {0};
    int logical_cores = 0;

    while (fgets(line, sizeof(line), fp)) {
        // Trim newline
        line[strcspn(line, "\n")] = '\0';

        // 获取 CPU 型号
        if (strncmp(line, "model name", 10) == 0 && cpu_model[0] == '\0') {
            char *colon = strchr(line, ':');
            if (colon) {
                const char *model = colon + 1;
                while (*model == ' ') model++;  // Skip leading spaces
                strncpy(cpu_model, model, sizeof(cpu_model) - 1);
                cpu_model[sizeof(cpu_model) - 1] = '\0';
            }
        }

        // 统计逻辑处理器数量
        if (strncmp(line, "processor", 9) == 0 && line[9] == '\t') {
            logical_cores++;
        }
    }

    fclose(fp);

    // Fallback 到 sysconf 如果 processor 行不存在
    if (logical_cores == 0) {
        logical_cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
    }

    // 打印结果
    printf("        CPU 型号: %s , 总逻辑处理器数量：%d \n", cpu_model[0] ? cpu_model : "Unknown" , logical_cores);
    // printf("总逻辑处理器数量：%d\n", logical_cores);
}


// 四舍五入函数（保留整数）
double my_round(double x) {
    int integer = (int)x;
    return (x - integer >= 0.5) ? integer + 1 : integer;
}

// 获取物理内存总量（通过 dmidecode）
double get_phys_mem() {
    FILE *fp = popen("sudo dmidecode -t memory | grep 'Size' | grep -v 'No Module Installed'", "r");
    if (!fp) return -1;

    double total = 0;
    char line[256];
    int size;
    char unit[16];

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, " Size: %d %s", &size, unit) == 2) {
            if (!strcmp(unit, "MB")) total += size / 1024.0;
            else if (!strcmp(unit, "GB")) total += size;
        }
    }

    pclose(fp);
    return total;
}

// 打印内存信息
void print_memory_info() {
    double phys_mem = get_phys_mem();
    if (phys_mem <= 0) {
        printf("无法获取物理内存\n");
        return;
    }

    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        perror("无法打开 /proc/meminfo");
        return;
    }

    double mem_available = 0;
    char key[64], val[64], line[256];

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%63[^:]: %63s", key, val) != 2)
            continue;

        if (!strcmp(key, "MemAvailable")) {
            mem_available = atof(val) / (1024.0 * 1024.0); // KB -> GiB
            break;
        }
    }

    fclose(fp);

    double used = phys_mem - mem_available;
    double percent = (used / phys_mem) * 100.0;

    // 四舍五入显示
    double rounded_used = my_round(used);
    double rounded_total = my_round(phys_mem);

    // 输出格式
    printf("        物理内存: 内存总量: %2.0f GiB   已使用内存：%2.0f GiB(%.2f%%) \n",
           rounded_total, rounded_used, percent);
}



// 显示系统信息主函数
void system_info() {
    if (uname(&sysinfo) == -1) {
        perror("uname");
        return;
    }

    print_current_time();
    print_system_environment();
    print_distribution_info();
    print_kernel_and_hostname();
    print_cpu_info();
    print_memory_info();
    
}

// 功能示例：功能一
void feature_1() {
    printf("👉 功能 1 已执行。\n");
}

// 菜单界面与用户交互
static void menu() {
    char select;
    while (true) {
        printf("\n\e[1;35m警告：非新环境中请勿使用 'yum update'\e[0m\n");
        printf("   \e[1;35m0、显示系统信息\e[0m\n");
        printf("   \e[1;35m1、功能一\e[0m\n");
        printf("   \e[1;35m2、功能二\e[0m\n");
        printf("   \e[1;35m3、功能三\e[0m\n");
        printf("   \e[1;35m4、功能四\e[0m\n");
        printf("   \e[1;35m5、功能五\e[0m\n");
        printf("   \e[1;35m6、功能六\e[0m\n");
        printf("   \e[1;35m7、功能七\e[0m\n");
        printf("\e[1;35m选择选项(0-9)，q 退出: \e[0m ");

        scanf(" %c", &select); // 注意前面空格跳过空白字符

        switch (select) {
            case '0':
                system_info();
                break;
            case '1':
                feature_1();
                break;
            case 'q':
            case 'Q':
                printf("👋 正在退出程序。\n");
                exit(0);
            default:
                printf("❗ 未知选项，请重新选择。\n");
                break;
        }

        // 按回车继续
        printf("\n按 Enter 键继续...");
        while (getchar() != '\n'); // 清空输入缓冲
        getchar();                 // 等待按下回车
        printf("\033[H\033[J");     // 清屏（可选）
    }
}

// 权限检查
static void check_root() {
    if (getuid() != 0) {
        printf("❌ 请以 root 权限运行脚本。\n");
        exit(1);
    }
}

// 主入口函数
int main(void) {
    check_root();

    printf("||=============================||\n");
    printf("||          Auto Script        ||\n");
    printf("||          Version: 2.1       ||\n");
    printf("||          Use: C语言         ||\n");
    printf("||          By : 喝口雪碧      ||\n");
    printf("||=============================||\n");

    menu(); // 主菜单循环直到输入 q

    return 0;
}