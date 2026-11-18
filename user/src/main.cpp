/**
 * @file main.cpp
 * @brief 程序入口：Fusion 风格 + 主窗口启动
 */

#include "main_window.h"

#include <QApplication>

/**
 * @brief       程序入口：创建主窗口并进入事件循环
 *
 * @param[in]   {argc} int 参数个数
 * @param[in]   {argv} char*[] 参数数组
 * @return      进程退出码
 * @doxygen_sig_fp f4b9f73e14950a17
 */
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle(QStringLiteral("Fusion"));

    MainWindow w;
    w.show();

    return a.exec();
}
