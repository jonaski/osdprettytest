#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "ui_mainwindow.h"

#include "osdpretty.h"

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

 private slots:
  void Show();

 private:
  Ui_MainWindow *ui_;
  OSDPretty *osdpretty_;

};

#endif // MAINWINDOW_H
