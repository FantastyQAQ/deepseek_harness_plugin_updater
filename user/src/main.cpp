/**
 * @file main.cpp
 * @brief 程序入口：Fusion 风格 + 残留清理 + 主窗口启动
 */

#include "main_window.h"
#include "update_applier.h"

#include <QApplication>

/**
 * @brief       程序入口：清理上次更新残留，创建主窗口并进入事件循环
 *
 * 清理必须放在创建窗口之前：老版本在线更新会把旧 exe 改名为 .old、把下载中的
 * 包写成 .new，这些文件如果还留着，要么占空间，要么会让下次更新误判。
 * @param[in]   {argc} int 参数个数
 * @param[in]   {argv} char*[] 参数数组
 * @return      进程退出码
 * @doxygen_sig_fp f4b9f73e14950a17
 */
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle(QStringLiteral("Fusion"));

    // 先清理上次在线更新留下的 .old / .new，再进入正常流程
    UpdateApplier::cleanup_leftovers();

    MainWindow w;
    w.show();

    return a.exec();
}
