/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.11.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralwidget;
    QPushButton *btnStart;
    QPushButton *btnStop;
    QPushButton *btnUpload;
    QPushButton *btnSplitter;
    QPushButton *btnPost;
    QProgressBar *progressBar;
    QLabel *label;
    QLabel *label_2;
    QLineEdit *leIP;
    QLineEdit *lePort;
    QLabel *label_3;
    QLineEdit *leWavPath;
    QLineEdit *lePortSIO;
    QLabel *label_4;
    QMenuBar *menubar;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(508, 309);
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName("centralwidget");
        btnStart = new QPushButton(centralwidget);
        btnStart->setObjectName("btnStart");
        btnStart->setGeometry(QRect(20, 130, 251, 22));
        btnStop = new QPushButton(centralwidget);
        btnStop->setObjectName("btnStop");
        btnStop->setGeometry(QRect(20, 150, 251, 22));
        btnUpload = new QPushButton(centralwidget);
        btnUpload->setObjectName("btnUpload");
        btnUpload->setGeometry(QRect(290, 150, 161, 22));
        btnSplitter = new QPushButton(centralwidget);
        btnSplitter->setObjectName("btnSplitter");
        btnSplitter->setGeometry(QRect(290, 130, 161, 22));
        btnPost = new QPushButton(centralwidget);
        btnPost->setObjectName("btnPost");
        btnPost->setGeometry(QRect(290, 180, 161, 22));
        progressBar = new QProgressBar(centralwidget);
        progressBar->setObjectName("progressBar");
        progressBar->setGeometry(QRect(20, 210, 461, 23));
        progressBar->setValue(0);
        label = new QLabel(centralwidget);
        label->setObjectName("label");
        label->setGeometry(QRect(30, 20, 49, 16));
        label_2 = new QLabel(centralwidget);
        label_2->setObjectName("label_2");
        label_2->setGeometry(QRect(20, 50, 49, 16));
        leIP = new QLineEdit(centralwidget);
        leIP->setObjectName("leIP");
        leIP->setGeometry(QRect(70, 11, 161, 31));
        lePort = new QLineEdit(centralwidget);
        lePort->setObjectName("lePort");
        lePort->setGeometry(QRect(70, 50, 161, 31));
        label_3 = new QLabel(centralwidget);
        label_3->setObjectName("label_3");
        label_3->setGeometry(QRect(10, 90, 49, 16));
        leWavPath = new QLineEdit(centralwidget);
        leWavPath->setObjectName("leWavPath");
        leWavPath->setGeometry(QRect(70, 90, 381, 31));
        lePortSIO = new QLineEdit(centralwidget);
        lePortSIO->setObjectName("lePortSIO");
        lePortSIO->setGeometry(QRect(290, 50, 161, 31));
        label_4 = new QLabel(centralwidget);
        label_4->setObjectName("label_4");
        label_4->setGeometry(QRect(230, 60, 49, 16));
        MainWindow->setCentralWidget(centralwidget);
        menubar = new QMenuBar(MainWindow);
        menubar->setObjectName("menubar");
        menubar->setGeometry(QRect(0, 0, 508, 22));
        MainWindow->setMenuBar(menubar);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName("statusbar");
        MainWindow->setStatusBar(statusbar);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "MainWindow", nullptr));
        btnStart->setText(QCoreApplication::translate("MainWindow", "start", nullptr));
        btnStop->setText(QCoreApplication::translate("MainWindow", "stop", nullptr));
        btnUpload->setText(QCoreApplication::translate("MainWindow", "upload", nullptr));
        btnSplitter->setText(QCoreApplication::translate("MainWindow", "Split", nullptr));
        btnPost->setText(QCoreApplication::translate("MainWindow", "Join", nullptr));
        label->setText(QCoreApplication::translate("MainWindow", "IP", nullptr));
        label_2->setText(QCoreApplication::translate("MainWindow", "Port  Upload", nullptr));
        leIP->setText(QCoreApplication::translate("MainWindow", "192.168.1.22", nullptr));
        lePort->setText(QCoreApplication::translate("MainWindow", "8000", nullptr));
        label_3->setText(QCoreApplication::translate("MainWindow", "wav path", nullptr));
        leWavPath->setText(QCoreApplication::translate("MainWindow", "D:/wav/rec", nullptr));
        lePortSIO->setText(QCoreApplication::translate("MainWindow", "3000", nullptr));
        label_4->setText(QCoreApplication::translate("MainWindow", "SIO", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
