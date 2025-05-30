#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>  // 提供 uid_t 类型定义

//  判断执行权限
static void root(){
    if (getuid() != 0)
    {
       printf("❌ Please run the script with root privileges。\n");
       exit(1);
    }
}

// 脚本目录
static void menu(){


    printf("\e[1;35m Do not use 'yum update' in a non-new environment.\e[0m \n");
    char select;
    while (true)
    {
    
    printf("   \e[1;35m 0、Show system info \e[0m \n");
    printf("   \e[1;35m 1、 \e[0m \n");
    printf("   \e[1;35m 2、 \e[0m \n");
    printf("   \e[1;35m 3、 \e[0m \n");
    printf("   \e[1;35m 4、 \e[0m \n");
    printf("   \e[1;35m 5、 \e[0m \n");
    printf("   \e[1;35m 6、 \e[0m \n");
    printf("   \e[1;35m 7、 \e[0m \n");
    printf("\e[1;35m Select(0-9): \e[0m ");
    scanf(" %c", &select);
    printf("读取的字符是: %c \n", select);
    
    break;
    }
}

int main(void)
{
    root();

    printf("||=============================||\n");
    printf("||          Auto  Script       ||\n");
    printf("||          Version: 2.1       ||\n");
    printf("||          By: One boy        ||\n");
    printf("||=============================||\n");
    menu();

}
