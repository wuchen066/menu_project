#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    return 0;
}