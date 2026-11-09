/**
 * @file main.cpp
 * @brief 程序入口：Fusion 风格 + 主窗口启动
 */

#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setStyle(QStringLiteral("Fusion"));

    MainWindow w;
    w.show();

    return a.exec();
}
