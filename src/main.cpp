#include <QApplication>
#include <QFileInfo>
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

    // Проверяем переданные аргументы ("Открыть с помощью..." передает путь в argv)
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        QString path = args.at(i);
        QFileInfo fi(path);
        if (fi.exists() && fi.isFile()) {
            if (fi.suffix().toLower() == "pdp") {
                window.loadFile(fi.absoluteFilePath());
                break;
            }
        }
    }

    return application.exec();
}
