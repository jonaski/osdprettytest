#include <QApplication>
#include <QtDebug>
#include <QLoggingCategory>

#include "mainwindow.h"

int main(int argc, char *argv[]) {

  QCoreApplication::setApplicationName("osdprettytest");
  QCoreApplication::setOrganizationName("osdprettytest");
  QCoreApplication::setOrganizationDomain("jkvinge.net");

  QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, true);

  Q_INIT_RESOURCE(data);

  QApplication a(argc, argv);
  MainWindow w;
  return a.exec();

}
