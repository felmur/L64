// *****************************************************
// L64 - Yet Another C64 Emultator for Linux
// (c) 2026 by Felice Murolo, Salerno, Italia
// email: linuxboy@fel.hotpo.org
// *****************************************************

#include "mainwindow.h"

#include <QApplication>

QApplication *app=nullptr;

int main(int argc, char *argv[])
{
    app = new QApplication(argc, argv);
    MainWindow w;
    w.show();
    return QApplication::exec();
}
