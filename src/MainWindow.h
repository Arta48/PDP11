#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QLineEdit>
#include <QLabel>
#include <QGroupBox>
#include <QStyledItemDelegate>
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QTimer>
#include <QPointer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPalette>
#include <QProxyStyle>
#include <QPainter>
#include <QPixmap>
#include <array>
#include "Pdp11.h"

/**
 * @brief Делегат для ограничения ввода в ячейках таблицы.
 * Позволяет вводить только восьмеричные числа (0-7) длиной не более 6 символов.
 */
class OctalDelegate : public QStyledItemDelegate {
public:
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QLineEdit *editor = new QLineEdit(parent);
        QRegularExpression octalRegex("[0-7]{0,6}");
        editor->setValidator(new QRegularExpressionValidator(octalRegex, editor));
        editor->setMaxLength(6);
        return editor;
    }
};

/**
 * @brief Текстовая метка, которая автоматически показывает ToolTip,
 * если текст не помещается в отведенные границы.
 */
class AutoTooltipLabel : public QLabel {
    Q_OBJECT
public:
    using QLabel::QLabel;

protected:
    void enterEvent(QEnterEvent *event) override {
        QFontMetrics labelFontMetrics(font());
        int textWidth = labelFontMetrics.horizontalAdvance(text());
        int availableWidth = contentsRect().width();

        // Если текст шире, чем метка, показываем полную версию в подсказке
        if (textWidth > availableWidth) {
            setToolTip(text());
        } else {
            setToolTip("");
        }
        QLabel::enterEvent(event);
    }
};

#ifdef Q_OS_WIN
class Win10StyleProxy : public QProxyStyle {
public:
    Win10StyleProxy(QStyle *style = nullptr) : QProxyStyle(style) {}

    QIcon standardIcon(StandardPixmap standardIcon, const QStyleOption *option = nullptr, const QWidget *widget = nullptr) const override {
        QChar glyph;
        bool match = true;

        // Динамически определяем текущую тему по цвету фона главного окна
        bool isDark = true;
        if (qApp) {
            // lightness() возвращает значение от 0 до 255. Если меньше 128 - фон темный.
            isDark = qApp->palette().color(QPalette::Window).lightness() < 128;
        }

        // Цвет по умолчанию для монохромных иконок
        QColor defaultColor = isDark ? Qt::white : QColor(35, 38, 41); // Тёмно-серый для светлой темы
        QColor color = defaultColor;

        switch(standardIcon) {
            case QStyle::SP_DialogOpenButton:
                glyph = QChar(0xED25);
                color = isDark ? QColor(255, 210, 66) : QColor(218, 165, 32); // Желтый / Dark Goldenrod
                break;
            case QStyle::SP_DialogSaveButton:
                glyph = QChar(0xE74E);
                color = isDark ? QColor(66, 156, 255) : QColor(0, 120, 215); // Светло-синий / Синий Windows
                break;
            case QStyle::SP_BrowserStop:
                glyph = QChar(0xE711);
                color = isDark ? QColor(255, 82, 82) : QColor(232, 17, 35);  // Розоватый / Красный Windows
                break;
            case QStyle::SP_MediaSkipForward:
                glyph = QChar(0xE893); // Иконка Step
                color = defaultColor;
                break;
            case QStyle::SP_MediaPlay:
                glyph = QChar(0xE768);
                color = isDark ? QColor(76, 217, 100) : QColor(16, 137, 62); // Салатовый / Темно-зеленый
                break;
            case QStyle::SP_TitleBarNormalButton:
                glyph = QChar(0xE739); // Иконка Window
                color = defaultColor;
                break;
            case QStyle::SP_DesktopIcon:
                glyph = QChar(0xE7F4); // Иконка Screen
                color = defaultColor;
                break;
            case QStyle::SP_BrowserReload:
                glyph = QChar(0xE72C);
                color = isDark ? QColor(66, 210, 255) : QColor(0, 153, 204); // Голубой / Темно-голубой
                break;
            case QStyle::SP_MessageBoxInformation:
                glyph = QChar(0xE946); // Иконка Reference
                color = isDark ? QColor(66, 210, 255) : QColor(0, 153, 204);
                break;
            case QStyle::SP_MessageBoxQuestion:
                glyph = QChar(0xE897); // Иконка About
                color = defaultColor;
                break;
            default:
                match = false;
                break;
        }

        if (match) {
            // Увеличиваем разрешение для четкости
            QPixmap pixmap(64, 64);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::TextAntialiasing);

            // Используем имя шрифта, которое мы загрузили (Segoe MDL2 Assets)
            QFont font("Segoe MDL2 Assets");
            font.setPixelSize(42); // Делаем иконку крупнее внутри квадрата
            painter.setFont(font);
            painter.setPen(color);

            painter.drawText(pixmap.rect(), Qt::AlignCenter, QString(glyph));
            return QIcon(pixmap);
        }

        return QProxyStyle::standardIcon(standardIcon, option, widget);
    }
};
#endif

/**
 * @brief Главное окно графического интерфейса эмулятора PDP-11.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);

protected:
    /**
     * @brief Перехват нажатий клавиш для навигации по таблицам памяти и ввода с клавиатуры.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

    // Перехват смены палитры
    void changeEvent(QEvent *event) override;

private slots:
    // ==========================================
    // УПРАВЛЕНИЕ ВЫПОЛНЕНИЕМ
    // ==========================================
    void handleStepExecution();
    void handleProgramExecution();
    void handleProgramTick();

    // ==========================================
    // ФАЙЛОВЫЕ ОПЕРАЦИИ И СПРАВКА
    // ==========================================
    void handleOpenFile();
    void handleSaveFile();
    void handleAbout();
    void handleReference();
    void handleExit();

    // ==========================================
    // ОБРАБОТКА ИЗМЕНЕНИЙ ИНТЕРФЕЙСА
    // ==========================================
    void handleTableItemChanged(QTableWidgetItem *item);
    void handleRegisterEdited();
    void handleSelectionChanged();

    // ==========================================
    // РАБОТА С УСТРОЙСТВАМИ
    // ==========================================
    void handleTimerToggle(bool checked);
    void handleHardwareTimerTick();

    void handleRamClear();
    void handleRamAdd();

    void handleScreenShow();
    void handleScreenClear();
    void handleScreenRemove();

private:
    // ==========================================
    // ВНУТРЕННЕЕ СОСТОЯНИЕ
    // ==========================================
    Pdp11 processor;
    bool isUpdatingInterface = false; /// Флаг блокировки рекурсивного обновления UI
    uint16_t currentViewStartAddress = 001000; /// Базовый адрес левой таблицы RAM

    const QString referenceFileName = "PDP11.pdf"; /// Имя файла документации

    // ==========================================
    // ТЕМАТИЗАЦИЯ И СТИЛИ
    // ==========================================
    bool themeInitialized = false;
    bool isDarkMode = false;
    QString themeCssTable;
    QString themeCssEditor;
    QString themeCssGlobal;
    QString themeCssScreenTab;
    QColor themeMnemonicColor;

    void applyTheme(bool forceUpdateWidgets = false);
    void updateWidgetsTheme(); // Метод для перерисовки запущенного приложения

    // ==========================================
    // УКАЗАТЕЛИ НА UI (для динамической перерисовки)
    // ==========================================
    QAction *actOpen = nullptr, *actSave = nullptr, *actExit = nullptr;
    QAction *actStep = nullptr, *actRun = nullptr;
    QAction *actRamClear = nullptr, *actRamAdd = nullptr;
    QAction *actScreenShow = nullptr, *actScreenClear = nullptr, *actScreenRemove = nullptr;
    QAction *actWindow = nullptr, *actScreen = nullptr;
    QAction *actionTimerToggle = nullptr;
    QAction *actRef = nullptr, *actAbout = nullptr;

    QMenu *ramSubMenu = nullptr;
    QMenu *screenSubMenu = nullptr;
    QPushButton *refIconButton = nullptr;

    // ==========================================
    // ЭЛЕМЕНТЫ ГЛАВНОГО ОКНА
    // ==========================================
    QTableWidget *memoryTableLeft;
    QTableWidget *memoryTableRight;
    std::array<QLineEdit*, 8> registerEditors;
    QLineEdit *pswEditor;

    AutoTooltipLabel *instructionNameLabel;
    AutoTooltipLabel *instructionTypeLabel;
    AutoTooltipLabel *instructionFormatLabel;

    // ==========================================
    // ТАЙМЕРЫ
    // ==========================================
    QTimer *programExecutionTimer;
    bool isProgramRunning = false;
    QTimer *hardwareTimer;

    // ==========================================
    // КОМПОНЕНТЫ ОКНА ADDITIONAL RAM
    // ==========================================
    QPointer<QDialog> dialog;
    QPointer<QTableWidget> dialogTableLeft;
    QPointer<QTableWidget> dialogTableRight;
    uint16_t dialogStartAddress = 001000;

    // ==========================================
    // КОМПОНЕНТЫ ЭКРАНА ДИСПЛЕЯ И ПРИНТЕРА
    // ==========================================
    QPointer<QDialog> displayScreenDialog;
    QPointer<QPlainTextEdit> screenTextModeWidget;
    QPointer<QPlainTextEdit> screenAsciiModeWidget;
    QPointer<QPlainTextEdit> printerModeWidget;

    QString screenTextBuffer;  ///< Буфер истории консоли терминала
    QString printerTextBuffer; ///< Буфер истории устройства печати

    // ==========================================
    // ВНУТРЕННИЕ ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ
    // ==========================================
    void setupUserInterface();
    void updateUserInterface();
    void updateReferencePanel(uint16_t value);
    void synchronizeTableWithProgramCounter();
    QString toOctalString(uint16_t value, int digits);

    void updateDialogUserInterface();
    void updateAsciiModeView();
    bool handleTableScroll(QTableWidget *table, QKeyEvent *keyEvent, QTableWidget *leftTable, QTableWidget *rightTable, uint16_t &startAddress, std::function<void()> updateFunc);
};

#endif // MAINWINDOW_H
