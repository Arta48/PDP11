#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);

    // Устанавливаем иконку приложения из скомпилированных ресурсов (префикс ':/')
    application.setWindowIcon(QIcon(":/icon.png"));

    // ВАЖНО ДЛЯ WAYLAND: установка App ID (имя вашего .desktop файла без расширения)
    // Если этого не сделать, Wayland не поймет, какую иконку рисовать в панели задач
    // application.setDesktopFileName("pdp11");

    // Инициализация и запуск главного окна
    MainWindow window;
    window.show();

    return application.exec();
}
