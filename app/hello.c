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
#include <sys/sysinfo.h>   // 用于获取开机时间
#include <ifaddrs.h>       // 用于获取IP
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <linux/if_link.h>
#include <regex.h>
#include <libnl3/netlink/netlink-compat.h>

#define MAX_LINE 256

// 结构体：保存发行版信息
typedef struct {
    char name[128];
    char version[128];
} DistributionInfo;

// 全局系统信息
struct utsname global_sysinfo;

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
    printf("        内核版本: %s %s\n", global_sysinfo.sysname, global_sysinfo.release);
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

// 获取系统开机时间（返回字符串，需free）
char* get_uptime_str() {
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        return strdup("未知");
    }
    int days = info.uptime / (60*60*24);
    int hours = (info.uptime % (60*60*24)) / 3600;
    int minutes = (info.uptime % 3600) / 60;
    int seconds = info.uptime % 60;
    char *buf = malloc(128);
    if (!buf) return strdup("内存不足");
    snprintf(buf, 128, "%d天 %02d:%02d:%02d", days, hours, minutes, seconds);
    return buf;
}

// 打印系统开机时间
void print_uptime() {
    char *uptime = get_uptime_str();
    printf("        开机时长: %s\n", uptime);
    free(uptime);
}

// 获取本机第一个非127.0.0.1的IPv4地址（返回字符串，需free）
char* get_local_ip() {
    struct ifaddrs *ifaddr, *ifa;
    char *ip = NULL;
    if (getifaddrs(&ifaddr) == -1) {
        return strdup("未知");
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            if (strcmp(ifa->ifa_name, "lo") != 0) {
                ip = strdup(inet_ntoa(sa->sin_addr));
                break;
            }
        }
    }
    freeifaddrs(ifaddr);
    if (!ip) return strdup("未获取到IP");
    return ip;
}

// 打印本机IP
void print_local_ip() {
    char *ip = get_local_ip();
    printf("        本机IP: %s\n", ip);
    free(ip);
}

// 显示系统信息主函数
void system_info() {
    if (uname(&global_sysinfo) == -1) {
        perror("uname");
        return;
    }

    print_current_time();
    print_system_environment();
    print_distribution_info();
    print_kernel_and_hostname();
    print_cpu_info();
    print_memory_info();
    
    printf("        \n");  
    printf("        主机名称: %s\n", global_sysinfo.nodename);
    print_local_ip();
    print_uptime();
}

// 修改YUM源和APT源的函数
void change_package_source() {
    DistributionInfo distro_info;
    if (get_distribution_info(&distro_info) != 0) {
        printf("无法识别系统类型，无法自动更换源。\n");
        return;
    }
    printf("检测到系统: %s %s\n", distro_info.name, distro_info.version);

    // Ubuntu/Debian 系列
    if (strstr(distro_info.name, "Ubuntu") || strstr(distro_info.name, "Debian")) {
        printf("正在备份并更换APT源...\n");
        system("cp /etc/apt/sources.list /etc/apt/sources.list.bak 2>/dev/null");
        // 选择阿里云源
        FILE *fp = fopen("/etc/apt/sources.list", "w");
        if (!fp) {
            printf("无法写入 /etc/apt/sources.list\n");
            return;
        }
        if (strstr(distro_info.name, "Ubuntu")) {
            // 适配不同版本
            const char *ver = distro_info.version;
            char *source_content;
            if (strstr(ver, "14")) {
                source_content =
                    "deb https://mirrors.aliyun.com/ubuntu/ trusty main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ trusty main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ trusty-security main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ trusty-security main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ trusty-updates main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ trusty-updates main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ trusty-backports main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ trusty-backports main restricted universe multiverse\n";
            } else if (strstr(ver, "16")) {
                source_content =
                    "deb https://mirrors.aliyun.com/ubuntu/ xenial main\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ xenial main\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ xenial-updates main\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ xenial-updates main\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ xenial universe\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ xenial universe\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ xenial-updates universe\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ xenial-updates universe\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ xenial-security main\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ xenial-security main\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ xenial-security universe\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ xenial-security universe\n";
            } else if (strstr(ver, "18")) {
                source_content =
                    "deb https://mirrors.aliyun.com/ubuntu/ bionic main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ bionic main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ bionic-security main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ bionic-security main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ bionic-updates main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ bionic-updates main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ bionic-backports main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ bionic-backports main restricted universe multiverse\n";
            } else if (strstr(ver, "20")) {
                source_content =
                    "deb https://mirrors.aliyun.com/ubuntu/ focal main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ focal main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ focal-security main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ focal-security main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ focal-updates main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ focal-updates main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ focal-backports main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ focal-backports main restricted universe multiverse\n";
            } else if (strstr(ver, "22")) {
                source_content =
                    "deb https://mirrors.aliyun.com/ubuntu/ jammy main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ jammy main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ jammy-security main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ jammy-security main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ jammy-updates main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ jammy-updates main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ jammy-backports main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ jammy-backports main restricted universe multiverse\n";
            } else if (strstr(ver, "23")) {
                source_content =
                    "deb https://mirrors.aliyun.com/ubuntu/ lunar main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ lunar main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ lunar-security main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ lunar-security main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ lunar-updates main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ lunar-updates main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ lunar-backports main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ lunar-backports main restricted universe multiverse\n";
            } else if (strstr(ver, "24")) {
                source_content =
                    "deb https://mirrors.aliyun.com/ubuntu/ noble main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ noble main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ noble-security main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ noble-security main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ noble-updates main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ noble-updates main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ noble-backports main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ noble-backports main restricted universe multiverse\n";
            } else {
                // 默认 fallback
                source_content =
                    "deb https://mirrors.aliyun.com/ubuntu/ focal main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ focal main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ focal-security main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ focal-security main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ focal-updates main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ focal-updates main restricted universe multiverse\n"
                    "deb https://mirrors.aliyun.com/ubuntu/ focal-backports main restricted universe multiverse\n"
                    "deb-src https://mirrors.aliyun.com/ubuntu/ focal-backports main restricted universe multiverse\n";
            }
            fprintf(fp, "%s", source_content);
        } else {
            // Debian
            const char *ver = distro_info.version;
            const char *debian_content = NULL;
            if (strstr(ver, "7")) {
                debian_content =
                    "deb https://mirrors.aliyun.com/debian-archive/debian/ wheezy main non-free contrib\n"
                    "#deb https://mirrors.aliyun.com/debian-archive/debian/ wheezy-proposed-updates main non-free contrib\n"
                    "deb-src https://mirrors.aliyun.com/debian-archive/debian/ wheezy main non-free contrib\n"
                    "#deb-src https://mirrors.aliyun.com/debian-archive/debian/ wheezy-proposed-updates main non-free contrib\n";
            } else if (strstr(ver, "8")) {
                debian_content =
                    "deb https://mirrors.aliyun.com/debian-archive/debian/ jessie main non-free contrib\n"
                    "deb-src https://mirrors.aliyun.com/debian-archive/debian/ jessie main non-free contrib\n";
            } else if (strstr(ver, "9")) {
                debian_content =
                    "deb https://mirrors.aliyun.com/debian-archive/debian stretch main contrib non-free\n"
                    "#deb https://mirrors.aliyun.com/debian-archive/debian stretch-proposed-updates main non-free contrib\n"
                    "#deb https://mirrors.aliyun.com/debian-archive/debian stretch-backports main non-free contrib\n"
                    "deb https://mirrors.aliyun.com/debian-archive/debian-security stretch/updates main contrib non-free\n"
                    "deb-src https://mirrors.aliyun.com/debian-archive/debian stretch main contrib non-free\n"
                    "#deb-src https://mirrors.aliyun.com/debian-archive/debian stretch-proposed-updates main contrib non-free\n"
                    "#deb-src https://mirrors.aliyun.com/debian-archive/debian stretch-backports main contrib non-free\n"
                    "deb-src https://mirrors.aliyun.com/debian-archive/debian-security stretch/updates main contrib non-free\n";
            } else if (strstr(ver, "10")) {
                debian_content =
                    "deb https://mirrors.aliyun.com/debian/ buster main non-free contrib\n"
                    "deb-src https://mirrors.aliyun.com/debian/ buster main non-free contrib\n"
                    "deb https://mirrors.aliyun.com/debian-security buster/updates main\n"
                    "deb-src https://mirrors.aliyun.com/debian-security buster/updates main\n"
                    "deb https://mirrors.aliyun.com/debian/ buster-updates main non-free contrib\n"
                    "deb-src https://mirrors.aliyun.com/debian/ buster-updates main non-free contrib\n";
            } else if (strstr(ver, "11")) {
                debian_content =
                    "deb https://mirrors.aliyun.com/debian/ bullseye main non-free contrib\n"
                    "deb-src https://mirrors.aliyun.com/debian/ bullseye main non-free contrib\n"
                    "deb https://mirrors.aliyun.com/debian-security/ bullseye-security main\n"
                    "deb-src https://mirrors.aliyun.com/debian-security/ bullseye-security main\n"
                    "deb https://mirrors.aliyun.com/debian/ bullseye-updates main non-free contrib\n"
                    "deb-src https://mirrors.aliyun.com/debian/ bullseye-updates main non-free contrib\n"
                    "deb https://mirrors.aliyun.com/debian/ bullseye-backports main non-free contrib\n"
                    "deb-src https://mirrors.aliyun.com/debian/ bullseye-backports main non-free contrib\n";
            } else if (strstr(ver, "12")) {
                debian_content =
                    "deb http://mirrors.aliyun.com/debian/ bookworm main contrib non-free\n"
                    "deb http://mirrors.aliyun.com/debian/ bookworm-updates main contrib non-free\n"
                    "deb http://mirrors.aliyun.com/debian-security bookworm-security main contrib non-free\n"
                    "deb http://mirrors.aliyun.com/debian/ bookworm-backports main contrib non-free\n";
            } else {
                debian_content =
                    "deb http://mirrors.aliyun.com/debian/ bullseye main contrib non-free\n"
                    "deb http://mirrors.aliyun.com/debian/ bullseye-updates main contrib non-free\n"
                    "deb http://mirrors.aliyun.com/debian-security bullseye-security main contrib non-free\n"
                    "deb http://mirrors.aliyun.com/debian/ bullseye-backports main contrib non-free\n";
            }
            // 先读取原内容
            FILE *oldfp = fopen("/etc/apt/sources.list", "r");
            char *old_content = NULL;
            size_t old_size = 0;
            if (oldfp) {
                fseek(oldfp, 0, SEEK_END);
                old_size = ftell(oldfp);
                fseek(oldfp, 0, SEEK_SET);
                old_content = malloc(old_size + 1);
                if (old_content) {
                    fread(old_content, 1, old_size, oldfp);
                    old_content[old_size] = '\0';
                }
                fclose(oldfp);
            }
            // 覆盖写入新内容+原内容
            fp = fopen("/etc/apt/sources.list", "w");
            if (fp) {
                fprintf(fp, "%s", debian_content);
                if (old_content) fprintf(fp, "%s", old_content);
                fclose(fp);
            }
            if (old_content) free(old_content);
        }
        printf("APT源已切换为阿里云，正在更新缓存...\n");
        int ret = system("apt update > /dev/null 2>&1");
        if (ret != 0) {
            printf("APT源更新失败，请检查网络连接或手动更新。\n");
        }
        printf("APT源已切换并更新完成。\n");
        return;
    }

    // CentOS/RHEL 系列
    if (strstr(distro_info.name, "CentOS") || strstr(distro_info.name, "Red Hat") || strstr(distro_info.name, "RHEL")) {
        printf("正在备份并更换YUM源...\n");
        system("mkdir -p /etc/yum.repos.d/backup && mv /etc/yum.repos.d/*.repo /etc/yum.repos.d/backup/ 2>/dev/null");
        char cmd[512] = {0};
        int has_wget = (system("command -v wget > /dev/null 2>&1") == 0);
        int has_curl = (system("command -v curl > /dev/null 2>&1") == 0);
        const char *repo_url = NULL;
        const char *ver = distro_info.version;
        if (strstr(ver, "6")) {
            repo_url = "https://mirrors.aliyun.com/repo/Centos-vault-6.10.repo";
        } else if (strstr(ver, "7")) {
            repo_url = "https://mirrors.aliyun.com/repo/Centos-7.repo";
        } else if (strstr(ver, "8")) {
            repo_url = "https://mirrors.aliyun.com/repo/Centos-vault-8.5.2111.repo";
        } else if (strstr(ver, "9")) {
            printf("该系统目前无阿里云源，请手动处理。\n");
            return;
        } else {
            repo_url = "https://mirrors.aliyun.com/repo/Centos-7.repo";
        }
        if (has_wget) {
            snprintf(cmd, sizeof(cmd), "wget -O /etc/yum.repos.d/CentOS-Base.repo %s", repo_url);
        } else if (has_curl) {
            snprintf(cmd, sizeof(cmd), "curl -o /etc/yum.repos.d/CentOS-Base.repo %s", repo_url);
        } else {
            printf("未检测到wget或curl命令，无法下载YUM源配置文件！\n");
            return;
        }
        system(cmd);
        printf("YUM源已切换为阿里云，正在清理并生成缓存...\n");
        int ret = system("yum clean all > /dev/null 2>&1 && yum makecache > /dev/null 2>&1");
        if (ret != 0) {
            printf("YUM源清理和缓存生成失败，请检查网络连接或手动处理。\n");
        }
        printf("YUM源已切换并缓存更新完成。\n");
        return;
    }

    printf("暂不支持该系统自动换源，请手动处理。\n");
}

// 功能示例：功能一
void feature_1() {
    printf("👉 功能 1 已执行。\n");
}

// 功能三：修改源
void feature_3() {
    printf("👉 功能 3：自动更换YUM/APT源\n");
    change_package_source();
}

// 校验IP地址格式
int is_valid_ip(const char *ip) {
    struct in_addr addr;
    return inet_pton(AF_INET, ip, &addr) == 1;
}

// 校验掩码格式（255.255.255.0 或 /24）
int is_valid_mask(const char *mask) {
    // 支持 /24
    if (mask[0] == '/' && atoi(mask+1) >= 0 && atoi(mask+1) <= 32) return 1;
    // 支持 255.255.255.0
    struct in_addr addr;
    return inet_pton(AF_INET, mask, &addr) == 1;
}

// 获取所有物理网卡名，返回数量，names为输出数组
int get_all_ifnames(char names[][IFNAMSIZ], int max) {
    struct ifaddrs *ifaddr, *ifa;
    int count = 0;
    if (getifaddrs(&ifaddr) == -1) return 0;
    for (ifa = ifaddr; ifa != NULL && count < max; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_PACKET) {
            // 不重复
            int found = 0;
            int i;
            for (i = 0; i < count; ++i) if (strcmp(names[i], ifa->ifa_name) == 0) { found = 1; break; }
            if (!found) strncpy(names[count++], ifa->ifa_name, IFNAMSIZ);
        }
    }
    freeifaddrs(ifaddr);
    return count;
}

// 掩码长度转点分十进制
void masklen_to_str(int masklen, char *out) {
    unsigned int mask = masklen == 0 ? 0 : 0xFFFFFFFF << (32 - masklen);
    sprintf(out, "%u.%u.%u.%u",
        (mask >> 24) & 0xFF,
        (mask >> 16) & 0xFF,
        (mask >> 8) & 0xFF,
        mask & 0xFF);
}

// 检查文件是否存在
int file_exists(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp) { fclose(fp); return 1; }
    return 0;
}

// 查找下一个可用的 IPADDRX 编号
int find_next_ip_index(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int max_idx = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        int idx;
        if (sscanf(line, "IPADDR%d=", &idx) == 1) {
            if (idx > max_idx) max_idx = idx;
        } else if (strncmp(line, "IPADDR=", 7) == 0 && max_idx == 0) {
            max_idx = 0; // IPADDR= 视为0号
        }
    }
    fclose(f);
    return max_idx + 1;
}

// 实际添加IP到配置文件
void do_add_ip(const char *ifname, const char *ip, const char *mask) {
    // 检查 NetworkManager 是否 running
    int is_nm_running = (system("systemctl is-active --quiet NetworkManager") == 0);
    if (is_nm_running) {
        // 优先用 nmcli 配置
        char nmcli_cmd[512];
        // 先查找对应网卡的 connection 名称
        char con_name[128] = "";
        char nmcli_query_cmd[256];
        snprintf(nmcli_query_cmd, sizeof(nmcli_query_cmd), "nmcli -g GENERAL.CONNECTION device show %s 2>/dev/null", ifname);
        FILE *con_fp = popen(nmcli_query_cmd, "r");
        if (con_fp) {
            if (fgets(con_name, sizeof(con_name), con_fp)) {
                con_name[strcspn(con_name, "\n")] = '\0'; // 去除换行
            }
            pclose(con_fp);
        }
        if (strlen(con_name) == 0 || strcmp(con_name, "--") == 0) {
            printf("未找到网卡 %s 的 NetworkManager 连接名，自动切换为配置文件方式。\n", ifname);
            // 不 return，继续走后面配置文件逻辑
        } else {
            // 需要拼接 CIDR 掩码
            int masklen = 0;
            if (strchr(mask, '.')) {
                // 点分十进制转掩码长度
                unsigned int m1, m2, m3, m4;
                if (sscanf(mask, "%u.%u.%u.%u", &m1, &m2, &m3, &m4) == 4) {
                    int i;
                    unsigned int mask32 = (m1 << 24) | (m2 << 16) | (m3 << 8) | m4;
                    for (i = 31; i >= 0; --i) {
                        if ((mask32 >> i) & 1) masklen++;
                    }
                }
            } else if (mask[0] == '/' && atoi(mask+1) > 0) {
                masklen = atoi(mask+1);
            }
            if (masklen <= 0 || masklen > 32) masklen = 24; // 默认
            snprintf(nmcli_cmd, sizeof(nmcli_cmd),
                "nmcli connection modify '%s' +ipv4.addresses %s/%d",
                con_name, ip, masklen);
            printf("检测到 NetworkManager 正在运行，推荐使用 nmcli 配置：\n%s\n", nmcli_cmd);
            int ret = system(nmcli_cmd);
            if (ret == 0) {
                // 配置成功后激活连接
                char up_cmd[256];
                snprintf(up_cmd, sizeof(up_cmd), "nmcli connection up '%s' || ifup %s", con_name, ifname);
                printf("正在激活连接: %s\n", up_cmd);
                system(up_cmd);
                printf("IP 已通过 nmcli 添加并激活。\n");
                return;
            } else {
                printf("nmcli 配置失败，建议用 'nmcli connection show' 查看所有连接名，并手动配置。\n");
                // 失败时也继续走配置文件逻辑
            }
        }
    }
    // 检测系统类型
    FILE *fp = fopen("/etc/os-release", "r");
    char osid[64] = "", line[256];
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "ID=", 3) == 0) {
                sscanf(line, "ID=%63s", osid);
                // 去除引号
                size_t len = strlen(osid);
                if (osid[0] == '"' || osid[0] == '\'') memmove(osid, osid+1, len-1);
                if (osid[strlen(osid)-1] == '"' || osid[strlen(osid)-1] == '\'') osid[strlen(osid)-1] = 0;
                break;
            }
        }
        fclose(fp);
    }
    // Ubuntu/Debian
    if (strstr(osid, "ubuntu") || strstr(osid, "debian")) {
        char path[256];
        snprintf(path, sizeof(path), "/etc/network/interfaces.d/%s.cfg", ifname);
        if (!file_exists(path)) {
            // 若主 interfaces 存在但 interfaces.d 不存在，则写主文件
            if (!file_exists("/etc/network/interfaces.d") && file_exists("/etc/network/interfaces")) {
                strcpy(path, "/etc/network/interfaces");
            }
        }
        // 检查IP是否已存在
        int ip_exists = 0;
        FILE *checkf = fopen(path, "r");
        if (checkf) {
            char linebuf[256];
            while (fgets(linebuf, sizeof(linebuf), checkf)) {
                if (strstr(linebuf, ip)) { ip_exists = 1; break; }
            }
            fclose(checkf);
        }
        if (ip_exists) {
            printf("该IP %s 已存在于 %s ，不重复添加。\n", ip, path);
            return;
        }
        FILE *f = fopen(path, "a");
        if (!f) { printf("无法写入 %s\n", path); return; }
        fprintf(f, "auto %s\niface %s inet static\n    address %s\n    netmask %s\n", ifname, ifname, ip, mask);
        fclose(f);
        printf("已写入 %s\n", path);
        // 配置文件方式直接重启network服务
        printf("正在重启网络服务: systemctl restart network\n");
        system("systemctl restart network");
        return;
    }
    // CentOS/RHEL/Fedora
    if (strstr(osid, "centos") || strstr(osid, "rhel") || strstr(osid, "fedora")) {
        char path[256];
        snprintf(path, sizeof(path), "/etc/sysconfig/network-scripts/ifcfg-%s", ifname);
        int is_new_file = 0;
        if (!file_exists(path)) {
            snprintf(path, sizeof(path), "/etc/sysconfig/network-scripts/ifcfg-%s", ifname);
            if (!file_exists(path)) {
                is_new_file = 1;
            }
        }
        // 检查IP是否已存在
        int ip_exists = 0;
        FILE *checkf = fopen(path, "r");
        if (checkf) {
            char linebuf[256];
            while (fgets(linebuf, sizeof(linebuf), checkf)) {
                if (strstr(linebuf, ip)) { ip_exists = 1; break; }
            }
            fclose(checkf);
        }
        if (ip_exists) {
            printf("该IP %s 已存在于 %s ，不重复添加。\n", ip, path);
            return;
        }
        int idx = file_exists(path) ? find_next_ip_index(path) : 0;
        FILE *f = fopen(path, is_new_file ? "w" : "a");
        if (!f) { printf("无法写入 %s\n", path); return; }
        if (is_new_file) {
            // 新建文件，写入完整配置
            char gw[64] = "";
            printf("请输入网关地址（可直接回车跳过）：");
            fgets(gw, sizeof(gw), stdin); // 先清空输入缓冲
            if (gw[0] == '\0' || gw[0] == '\n') {
                // 可能上次scanf未清空，补一次
                fgets(gw, sizeof(gw), stdin);
            }
            gw[strcspn(gw, "\n")] = '\0';
            // 校验网关格式
            int gw_valid = 0;
            if (gw[0]) {
                struct in_addr gw_addr;
                if (inet_pton(AF_INET, gw, &gw_addr) == 1) {
                    gw_valid = 1;
                } else {
                    printf("网关格式无效，未写入GATEWAY字段！\n");
                }
            }
            fprintf(f, "DEVICE=%s\nBOOTPROTO=static\nONBOOT=yes\nIPADDR=%s\nNETMASK=%s\n", ifname, ip, mask);
            if (gw_valid) {
                fprintf(f, "GATEWAY=%s\n", gw);
            }
        } else {
            fprintf(f, "IPADDR%d=%s\nNETMASK%d=%s\n", idx, ip, idx, mask);
        }
        fclose(f);
        printf("已写入 %s\n", path);
        // 判断是否为 CentOS 6
        int is_centos6 = 0;
        FILE *rel = fopen("/etc/centos-release", "r");
        if (rel) {
            char relstr[128] = "";
            if (fgets(relstr, sizeof(relstr), rel)) {
                if (strstr(relstr, "release 6")) is_centos6 = 1;
            }
            fclose(rel);
        }
        if (is_centos6) {
            printf("正在重启网络服务: service network restart\n");
            system("service network restart");
        } else {
            printf("正在重启网络服务: systemctl restart network\n");
            system("systemctl restart network");
        }
        return;
    }
    printf("暂不支持该系统自动写入配置，请手动配置。\n");
}

// 新增IP交互
void add_ip() {
    char ifnames[32][IFNAMSIZ];
    int n = get_all_ifnames(ifnames, 32);
    if (n == 0) { printf("未检测到物理网卡！\n"); return; }
    printf("请选择要添加IP的网卡：\n");
    int i;
    for (i = 0; i < n; ++i) printf("%d) %s\n", i+1, ifnames[i]);
    int sel = 0;
    printf("输入序号: ");
    scanf("%d", &sel);
    if (sel < 1 || sel > n) { printf("无效选择！\n"); return; }
    printf("你选择的网卡是: %s\n", ifnames[sel-1]);
    // 输入IP
    char ip[64] = {0}, mask[64] = {0};
    printf("请输入新IP地址 (支持 1.1.1.1/24 或 1.1.1.1): ");
    scanf("%63s", ip);
    char *slash = strchr(ip, '/');
    if (slash) {
        // 1.1.1.1/24
        *slash = '\0';
        int masklen = atoi(slash+1);
        if (!is_valid_ip(ip) || masklen < 0 || masklen > 32) {
            printf("IP或掩码格式错误！\n"); return;
        }
        masklen_to_str(masklen, mask);
        printf("输入的IP: %s, 掩码: %s\n", ip, mask);
    } else {
        if (!is_valid_ip(ip)) { printf("IP格式错误！\n"); return; }
        printf("请输入子网掩码 (如 255.255.255.0): ");
        scanf("%63s", mask);
        if (!is_valid_mask(mask)) { printf("掩码格式错误！\n"); return; }
        printf("输入的IP: %s, 掩码: %s\n", ip, mask);
    }
    do_add_ip(ifnames[sel-1], ip, mask);
}

// 网卡IP信息列表功能
void list_ip_config() {
    printf("========== 网卡配置信息 ==========\n");
    // 获取默认网关
    char gw[64] = "";
    FILE *fp = popen("ip route | grep default | awk '{print $3}'", "r");
    if (fp && fgets(gw, sizeof(gw), fp)) {
        gw[strcspn(gw, "\n")] = '\0';
    }
    if (fp) pclose(fp);
    printf("当前默认网关是：%s，别删除网关的同段IP\n", gw[0] ? gw : "未知");

    // 收集所有网卡及IP，按网卡名分组
    struct ifaddrs *ifaddr, *ifa;
    struct ip_entry { char ifname[IFNAMSIZ]; char ip[64]; } iplist[128];
    int ipcount = 0;
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return;
    }
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
            strncpy(iplist[ipcount].ifname, ifa->ifa_name, IFNAMSIZ);
            strncpy(iplist[ipcount].ip, inet_ntoa(sa->sin_addr), 63);
            iplist[ipcount].ip[63] = '\0';
            ipcount++;
        }
    }
    // 获取所有物理网卡名
    char ifnames[32][IFNAMSIZ];
    int n = get_all_ifnames(ifnames, 32);
    // lo放第一个，其余排序
    int i, j;
    for (i = 0; i < n; ++i) {
        if (strcmp(ifnames[i], "lo") == 0 && i != 0) {
            char tmp[IFNAMSIZ];
            strcpy(tmp, ifnames[0]);
            strcpy(ifnames[0], ifnames[i]);
            strcpy(ifnames[i], tmp);
            break;
        }
    }
    for (i = 1; i < n-1; ++i) {
        for (j = i+1; j < n; ++j) {
            if (strcmp(ifnames[i], ifnames[j]) > 0) {
                char tmp[IFNAMSIZ];
                strcpy(tmp, ifnames[i]);
                strcpy(ifnames[i], ifnames[j]);
                strcpy(ifnames[j], tmp);
            }
        }
    }
    // 显示，序号连续，每个网卡聚合所有IP，没有IP的网卡也显示
    int idx = 1;
    for (i = 0; i < n; ++i) {
        printf("%2d. 网卡: %s\n", idx++, ifnames[i]);
        int has_ip = 0;
        for (j = 0; j < ipcount; ++j) {
            if (strcmp(ifnames[i], iplist[j].ifname) == 0) {
                printf("    IP地址: %s\n", iplist[j].ip);
                has_ip = 1;
            }
        }
        if (!has_ip) {
            printf("    (无IP)\n");
        }
        printf(" --------------------------\n");
    }
    freeifaddrs(ifaddr);
    printf("======== 请选择需要的操作 ========\n");
    printf("1) 添加\n2) 删除\n3) 替换\n4) 退出\n");
    char select;
    printf("请选择一个选项: ");
    scanf(" %c", &select);
    switch (select) {
        case '1':
            add_ip();
            break;
        case '2':
            // 删除IP的实现
            break;
        case '3':
            // 替换IP的实现
            break;
        case '4':
            // 退出
            break;
        default:
            printf("❗ 未知选项，请重新选择。\n");
            break;
    }
}

// 菜单界面与用户交互
static void menu() {
    char select;
    while (true) {
        printf("\n\e[1;35m警告：非新环境中请勿使用 'yum update'\e[0m\n");
        printf("   \e[1;35m0、显示系统信息\e[0m\n");
        printf("   \e[1;35m1、IP增删改查\e[0m\n");
        printf("   \e[1;35m2、功能二\e[0m\n");
        printf("   \e[1;35m3、自动更换YUM/APT源\e[0m\n");
        printf("   \e[1;35m4、功能四\e[0m\n");
        printf("   \e[1;35m5、功能五\e[0m\n");
        printf("   \e[1;35m6、功能六\e[0m\n");
        printf("   \e[1;35m7、功能七\e[0m\n");
        printf("\e[1;35m选择选项(0-9)，q 退出: \e[0m ");

        scanf(" %c", &select); // 注意前面空格跳过空白字符

        switch (select) {
            case '0':
                system_info();
                return;
            case '1':
                list_ip_config();
                break;
            case '2':
                // 功能二的实现
                break;
            case '3':
                feature_3();
                break;
            case '4':
                // 功能四的实现
                break;
            case '5':
                // 功能五的实现
                break;
            case '6':
                // 功能六的实现
                break;
            case '7':
                // 功能七的实现
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
        // printf("\n按 Enter 键继续...");
        // while (getchar() != '\n'); // 清空输入缓冲
        // getchar();                 // 等待按下回车
        // printf("\033[H\033[J");     // 清屏（可选）
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