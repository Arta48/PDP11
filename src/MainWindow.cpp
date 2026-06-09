#include "MainWindow.h"
#include <QMenuBar>
#include <QToolBar>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QApplication>
#include <QGroupBox>
#include <QKeyEvent>
#include <QFileDialog>
#include <QFile>
#include <QDataStream>
#include <QStyle>
#include <QAction>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QCoreApplication>
#include <QTabWidget>
#include <QSettings>
#include <QPixmapCache>
#include <QStyleFactory>
#include <QFontDatabase>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
    // Проверяем, есть ли шрифт "Segoe MDL2 Assets" уже в системе
    bool hasSystemIcons = QFontDatabase::families().contains("Segoe MDL2 Assets");

    if (!hasSystemIcons) {
        // Если системного шрифта нет (например, под Wine / macOS), грузим из ресурсов
        int fontId = QFontDatabase::addApplicationFont(":/segmdl2.ttf");
    }

    // Устанавливаем стиль Fusion и оборачиваем его в наш прокси для иконок
    QStyle *fusionStyle = QStyleFactory::create("Fusion");
    qApp->setStyle(new Win10StyleProxy(fusionStyle));

    // Глобальный фикс шрифтов (UI и таблицы)
    #ifdef Q_OS_WIN
    qApp->setFont(QFont("Segoe UI", 9));
    QFont::insertSubstitution("Monospace", "Consolas");
    #elif defined(Q_OS_MAC)
    // Явно задаем системный шрифт Apple (San Francisco) нативного размера 13pt
    qApp->setFont(QFont(".AppleSystemUIFont", 13));
    // Настраиваем качественный нативный моноширинный шрифт Menlo для таблиц
    QFont::insertSubstitution("Monospace", "Menlo");
    #endif
#endif

    // 1. Применяем тему (установка переменных CSS и палитры) БЕЗ обновления виджетов
    applyTheme(false);

    // 2. НАСТРОЙКА ОБРАТНЫХ ВЫЗОВОВ (CALLBACKS)
    // Обработчик для ошибок ядра
    processor.errorCallback = [this](const QString& errorMessage) {
        // Если программа выполнялась автоматически, останавливаем таймер выполнения
        if (isProgramRunning) {
            handleProgramExecution();
        }

        // Выводим критическое системное окно поверх всех окон
        QMessageBox::critical(this, getLocalizedText("Ошибка", "Error"), errorMessage);
    };

    // Обработчик для экрана дисплея (порт 177566)
    processor.charOutputCallback = [this](uint8_t charCode) {
        QChar character(static_cast<char>(charCode));
        screenTextBuffer.append(character); // Накапливаем символы в буфер

        if (screenTextModeWidget) {
            screenTextModeWidget->insertPlainText(character);
            screenTextModeWidget->ensureCursorVisible();
        }

        if (displayScreenDialog && displayScreenDialog->isVisible()) {
            updateAsciiModeView();
        }
    };

    // Обработчик для устройства печати (порт 177516)
    processor.printerOutputCallback =[this](uint8_t charCode) {
        QChar character(static_cast<char>(charCode));
        printerTextBuffer.append(character); // Накапливаем символы печати

        if (printerModeWidget) {
            printerModeWidget->insertPlainText(character);
            printerModeWidget->ensureCursorVisible();
        }
    };

    // Инициализация графического буфера, формат RGB32
    graphicsScreenBuffer = QImage(Pdp11::SCREEN_SIZE, Pdp11::SCREEN_SIZE, QImage::Format_RGB32);
    graphicsScreenBuffer.fill(Qt::black);

    // Связываем аппаратное событие отрисовки пикселя с методом UI
    processor.pixelOutputCallback = [this](uint8_t x, uint8_t y, uint8_t colorIndex) {
        drawPixelOnScreen(x, y, colorIndex);
    };

    // ==========================================
    // ИНИЦИАЛИЗАЦИЯ ИНТЕРФЕЙСА
    // ==========================================
    setupUserInterface();
    processor.resetProcessor();
    updateUserInterface();

#ifdef ENABLE_STUDENT_SECURITY
    QSettings settings("PDP11", "PDP11");

    // Сценарий 1: Конфиг существует
    if (settings.contains("student_id") && settings.contains("security_token")) {
        if (validateLocalToken()) {
            currentStudentId = settings.value("student_id").toString();
        } else {
            // Сценарий 2: Конфиг скопирован (хэш не совпал) -> Выводим предупреждение
            QMessageBox::warning(
                this,
                getLocalizedText("Внимание", "Warning"),
                getLocalizedText("Обнаружен перенос конфигурации или запуск на другом компьютере.\n\nДля подтверждения личности и продолжения работы необходимо заново авторизоваться под своей учетной записью Moodle (edu.vsu.ru).","Configuration transfer or execution on another computer detected.\n\nTo verify your identity and continue, please log in again with your Moodle account (edu.vsu.ru).")
            );

            if (!showLoginDialog()) {
                QMetaObject::invokeMethod(this, "close", Qt::QueuedConnection);
            }
        }
    } else {
        // Сценарий 3: Первый запуск (конфига нет)
        if (!showLoginDialog()) {
            QMetaObject::invokeMethod(this, "close", Qt::QueuedConnection);
        }
    }
#endif
}

// ==========================================
// ЛОГИКА ДИНАМИЧЕСКИХ ТЕМ
// ==========================================

void MainWindow::applyTheme(bool forceUpdateWidgets) {
    bool newIsDarkMode = false;

#ifdef Q_OS_WIN
    // Для Windows узнаем тему из реестра
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", QSettings::NativeFormat);
    newIsDarkMode = (settings.value("AppsUseLightTheme", 1).toInt() == 0);
#else
    // Для Linux и macOS Qt сам берет тему из системы, проверяем ее цвет
    newIsDarkMode = qApp->palette().color(QPalette::Window).lightness() < 128;
#endif

    // Выходим, если тема не изменилась (чтобы не гонять лишний раз)
    if (themeInitialized && isDarkMode == newIsDarkMode && !forceUpdateWidgets) {
        return;
    }

    isDarkMode = newIsDarkMode;
    themeInitialized = true;

    // Применяем темную палитру Fusion-стиля для Windows
#ifdef Q_OS_WIN
    if (isDarkMode) {
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(43, 43, 43));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(34, 34, 34));
        darkPalette.setColor(QPalette::AlternateBase, QColor(43, 43, 43));
        darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
        darkPalette.setColor(QPalette::ToolTipText, Qt::black);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::HighlightedText, Qt::white);
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, Qt::darkGray);
        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, Qt::darkGray);
        qApp->setPalette(darkPalette);
    } else {
        qApp->setPalette(qApp->style()->standardPalette());
    }
#endif

    // Настраиваем CSS переменные
    if (isDarkMode) {
        themeCssTable = "QTableWidget { background-color: #222; gridline-color: #444; border: 1px solid #555; color: #fff; }"
        "QHeaderView::section { background-color: #333; color: #fff; border: 1px solid #555; }";
        themeCssEditor = "background-color: #222; border: 1px solid #555; color: #fff;";
        themeCssGlobal = "QToolTip { color: #ffffff; background-color: #2b2b2b; border: 1px solid #353535; }"
        "QGroupBox { margin-top: 18px; border: 1px solid #555; border-radius: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; padding: 0 5px; color: #ffffff; }";
        themeCssScreenTab = "background-color: #222; border: 2px solid #555; color: #fff;";
        themeMnemonicColor = Qt::darkGray;
    } else {
        themeCssTable = "QTableWidget { background-color: #ffffff; gridline-color: #e0e0e0; border: 1px solid #c0c0c0; color: #232629; }"
        "QHeaderView::section { background-color: #f0f0f0; color: #232629; border: 1px solid #c0c0c0; }";
        themeCssEditor = "background-color: #ffffff; border: 1px solid #c0c0c0; color: #232629;";
        themeCssGlobal = "QToolTip { color: #232629; background-color: #ffffff; border: 1px solid #c0c0c0; }"
        "QGroupBox { margin-top: 18px; border: 1px solid #c0c0c0; border-radius: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; padding: 0 5px; color: #232629; }";
        themeCssScreenTab = "background-color: #ffffff; border: 2px solid #c0c0c0; color: #232629;";
        themeMnemonicColor = Qt::gray;
    }

    qApp->setStyleSheet(themeCssGlobal);

    if (forceUpdateWidgets) {
        updateWidgetsTheme();
    }
}

void MainWindow::updateWidgetsTheme() {
    auto updateTableStyles = [this](QTableWidget* table) {
        if (!table) return;
        table->setStyleSheet(themeCssTable);
        for (int i = 0; i < 14; ++i) {
            QTableWidgetItem *item = table->item(i, 2);
            if (item) item->setForeground(QBrush(themeMnemonicColor));
        }
    };

    updateTableStyles(memoryTableLeft);
    updateTableStyles(memoryTableRight);

    if (!dialog.isNull()) {
        updateTableStyles(dialogTableLeft);
        updateTableStyles(dialogTableRight);
    }

    // Редакторы регистров
    for (int i = 0; i < 8; ++i) {
        if (registerEditors[i]) registerEditors[i]->setStyleSheet(themeCssEditor);
    }
    if (pswEditor) pswEditor->setStyleSheet(themeCssEditor);

    // Панель Reference
    if (instructionFormatLabel) instructionFormatLabel->setStyleSheet(themeCssEditor);
    if (instructionTypeLabel) instructionTypeLabel->setStyleSheet(themeCssEditor);
    if (instructionNameLabel) instructionNameLabel->setStyleSheet(themeCssEditor);

    // Окно Screen
    if (!screenTextModeWidget.isNull()) screenTextModeWidget->setStyleSheet(themeCssScreenTab);
    if (!screenAsciiModeWidget.isNull()) screenAsciiModeWidget->setStyleSheet(themeCssScreenTab);
    if (!printerModeWidget.isNull()) printerModeWidget->setStyleSheet(themeCssScreenTab);

    // Перезапрашиваем Иконки для кнопок меню (Win10StyleProxy автоматически нарисует правильный цвет)
    QStyle *style = qApp->style();

    if (actOpen) actOpen->setIcon(style->standardIcon(QStyle::SP_DialogOpenButton));
    if (actSave) actSave->setIcon(style->standardIcon(QStyle::SP_DialogSaveButton));
    if (actExit) actExit->setIcon(style->standardIcon(QStyle::SP_BrowserStop));
    if (actStep) actStep->setIcon(style->standardIcon(QStyle::SP_MediaSkipForward));
    if (actRun) actRun->setIcon(style->standardIcon(QStyle::SP_MediaPlay));
    if (actWindow) actWindow->setIcon(style->standardIcon(QStyle::SP_TitleBarNormalButton));
    if (actScreen) actScreen->setIcon(style->standardIcon(QStyle::SP_DesktopIcon));
    if (actionTimerToggle) actionTimerToggle->setIcon(style->standardIcon(QStyle::SP_BrowserReload));
    if (actRef) actRef->setIcon(style->standardIcon(QStyle::SP_MessageBoxInformation));
    if (actAbout) actAbout->setIcon(style->standardIcon(QStyle::SP_MessageBoxQuestion));

    if (ramSubMenu) ramSubMenu->setIcon(style->standardIcon(QStyle::SP_TitleBarNormalButton));
    if (screenSubMenu) screenSubMenu->setIcon(style->standardIcon(QStyle::SP_DesktopIcon));
    if (refIconButton) refIconButton->setIcon(style->standardIcon(QStyle::SP_MessageBoxInformation));
}

void MainWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        applyTheme(true);
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::setupUserInterface() {
    QStyle *style = QApplication::style();
    menuBar()->setNativeMenuBar(false); // для macOS, Меню теперь будет отображаться внутри окна

    // ==========================================
    // 1. СОЗДАНИЕ ДЕЙСТВИЙ (ACTIONS)
    // ==========================================

    // Меню File
    actOpen = new QAction(style->standardIcon(QStyle::SP_DialogOpenButton), getLocalizedText("&Открыть файл", "&Open File"), this);
    actOpen->setShortcut(QKeySequence("Ctrl+O"));
    connect(actOpen, &QAction::triggered, this, &MainWindow::handleOpenFile);

    actSave = new QAction(style->standardIcon(QStyle::SP_DialogSaveButton), getLocalizedText("&Сохранить файл", "&Save File"), this);
    actSave->setShortcut(QKeySequence("Ctrl+S"));
    connect(actSave, &QAction::triggered, this, &MainWindow::handleSaveFile);

    actExit = new QAction(style->standardIcon(QStyle::SP_BrowserStop), getLocalizedText("Вы&ход", "E&xit"), this);
    connect(actExit, &QAction::triggered, this, &MainWindow::handleExit);

    // Меню Run
    actStep = new QAction(style->standardIcon(QStyle::SP_MediaSkipForward), getLocalizedText("&Пошаговый режим", "&Step Mode"), this);
    connect(actStep, &QAction::triggered, this, &MainWindow::handleStepExecution);

    actRun = new QAction(style->standardIcon(QStyle::SP_MediaPlay), getLocalizedText("&Программа", "&Program Mode"), this);
    connect(actRun, &QAction::triggered, this, &MainWindow::handleProgramExecution);

    // Меню Devices -> RAM
    actRamClear = new QAction(getLocalizedText("Очистить", "Clear"), this);
    connect(actRamClear, &QAction::triggered, this, &MainWindow::handleRamClear);

    actRamAdd = new QAction(getLocalizedText("Добавить", "Add"), this);
    connect(actRamAdd, &QAction::triggered, this, &MainWindow::handleRamAdd);

    // Меню Devices -> Display Screen
    actScreenShow = new QAction(getLocalizedText("Показать", "Show"), this);
    connect(actScreenShow, &QAction::triggered, this, &MainWindow::handleScreenShow);

    actScreenClear = new QAction(getLocalizedText("Очистить", "Clear"), this);
    connect(actScreenClear, &QAction::triggered, this, &MainWindow::handleScreenClear);

    actScreenRemove = new QAction(getLocalizedText("Удалить", "Remove"), this);
    connect(actScreenRemove, &QAction::triggered, this, &MainWindow::handleScreenRemove);

    // Управление окнами
    actWindow = new QAction(style->standardIcon(QStyle::SP_TitleBarNormalButton), getLocalizedText("&Окно", "&Window"), this);
    connect(actWindow, &QAction::triggered, this, &MainWindow::handleRamAdd);

    actScreen = new QAction(style->standardIcon(QStyle::SP_DesktopIcon), getLocalizedText("&Экран", "&Screen"), this);
    connect(actScreen, &QAction::triggered, this, &MainWindow::handleScreenShow);

    // Устройства -> Таймер
    actionTimerToggle = new QAction(getLocalizedText("Таймер", "Timer"), this);
    actionTimerToggle->setCheckable(true);
    actionTimerToggle->setIcon(style->standardIcon(QStyle::SP_BrowserReload));
    actionTimerToggle->setToolTip(getLocalizedText("Таймер выключен", "Timer off"));
    connect(actionTimerToggle, &QAction::toggled, this, &MainWindow::handleTimerToggle);

    // Меню Help
    actRef = new QAction(style->standardIcon(QStyle::SP_MessageBoxInformation), getLocalizedText("&Справочник", "&Reference"), this);
    actRef->setShortcut(QKeySequence("Ctrl+H"));
    connect(actRef, &QAction::triggered, this, &MainWindow::handleReference);

    actAbout = new QAction(style->standardIcon(QStyle::SP_MessageBoxQuestion), getLocalizedText("&О программе", "&About"), this);
    connect(actAbout, &QAction::triggered, this, &MainWindow::handleAbout);


    // ==========================================
    // 2. ВЕРХНЕЕ МЕНЮ (MenuBar)
    // ==========================================
    QMenu *fileMenu = menuBar()->addMenu(getLocalizedText("&Файл", "&File"));
    fileMenu->addAction(actOpen);
    fileMenu->addAction(actSave);
    fileMenu->addSeparator();
    fileMenu->addAction(actExit);

    QMenu *runMenu = menuBar()->addMenu(getLocalizedText("&Выполнить", "&Run"));
    runMenu->addAction(actStep);
    runMenu->addAction(actRun);

    QMenu *devicesMenu = menuBar()->addMenu(getLocalizedText("&Устройства", "&Devices"));
    ramSubMenu = devicesMenu->addMenu(getLocalizedText("ОЗУ", "RAM"));
    ramSubMenu->setIcon(style->standardIcon(QStyle::SP_TitleBarNormalButton));
    ramSubMenu->addAction(actRamClear);
    ramSubMenu->addAction(actRamAdd);

    screenSubMenu = devicesMenu->addMenu(getLocalizedText("Экран дисплея", "Display Screen"));
    screenSubMenu->setIcon(style->standardIcon(QStyle::SP_DesktopIcon));
    screenSubMenu->addAction(actScreenShow);
    screenSubMenu->addAction(actScreenClear);
    screenSubMenu->addAction(actScreenRemove);

    devicesMenu->addAction(actionTimerToggle);

    menuBar()->addMenu(getLocalizedText("&Режимы", "&Modes")); // Пустое меню

    QMenu *helpMenu = menuBar()->addMenu(getLocalizedText("&Помощь", "&Help"));
    helpMenu->addAction(actRef);
    helpMenu->addAction(actAbout);


    // ==========================================
    // 3. ПАНЕЛЬ ИНСТРУМЕНТОВ (ToolBar)
    // ==========================================
    QToolBar *toolBar = addToolBar("Main");
    toolBar->setMovable(false);
    toolBar->setIconSize(QSize(24, 24));

    toolBar->addAction(actOpen);
    toolBar->addAction(actSave);
    toolBar->addSeparator();

    toolBar->addAction(actStep);
    toolBar->addAction(actRun);
    toolBar->addSeparator();

    toolBar->addAction(actWindow);
    toolBar->addAction(actScreen);
    toolBar->addAction(actionTimerToggle);
    toolBar->addSeparator();

    toolBar->addAction(actRef);
    toolBar->addAction(actAbout);
    toolBar->addSeparator();

    toolBar->addAction(actExit);


    // ==========================================
    // 4. ОСНОВНОЙ ИНТЕРФЕЙС (Таблицы и Регистры)
    // ==========================================
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // --- Таблицы оперативной памяти ---
    QHBoxLayout *memoryLayout = new QHBoxLayout();

    auto setupTable = [&](QTableWidget* table) {
        table->setColumnCount(3);
        table->setRowCount(14);
        table->setHorizontalHeaderLabels({getLocalizedText("Адрес", "Address"), getLocalizedText("Данные", "Data"), getLocalizedText("Мнемоника", "Mnemonic")});
        table->verticalHeader()->setVisible(false);

        // Отключаем прокрутку
        table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        table->setFont(QFont("Monospace", MONO_FONT_SIZE, QFont::Bold));
        table->setColumnWidth(0, 70);
        table->setColumnWidth(1, 70);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->verticalHeader()->setMinimumSectionSize(30);

        // Минимальные размеры (для корректного отображения 14 строк)
        // 30 * 14 + 40 = 460 пикселей
        table->setMinimumHeight(460);
        // 70 + 70 + 135 = 275 пикселей
        table->setMinimumWidth(275);
        table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        // Применение динамического стиля
        table->setStyleSheet(themeCssTable);

        table->installEventFilter(this); // Слежка за кнопками клавиатуры

        // Предварительное создание ячеек
        for (int i = 0; i < 14; ++i) {
            // Ячейка: Адрес (Только для чтения)
            QTableWidgetItem *addressItem = new QTableWidgetItem();
            addressItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            addressItem->setTextAlignment(Qt::AlignCenter);
            table->setItem(i, 0, addressItem);

            // Ячейка: Данные (Редактируемая)
            QTableWidgetItem *dataItem = new QTableWidgetItem();
            dataItem->setTextAlignment(Qt::AlignCenter);
            table->setItem(i, 1, dataItem);

            // Ячейка: Мнемоника (Только для чтения)
            QTableWidgetItem *mnemonicItem = new QTableWidgetItem();
            mnemonicItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            mnemonicItem->setForeground(QBrush(themeMnemonicColor)); // Применение цвета темы
            table->setItem(i, 2, mnemonicItem);
        }

        // Установка делегатов для ограничения ввода (только 8-ричные числа)
        table->setItemDelegateForColumn(1, new OctalDelegate());

        connect(table, &QTableWidget::itemChanged, this, &MainWindow::handleTableItemChanged);
        connect(table, &QTableWidget::itemSelectionChanged, this, &MainWindow::handleSelectionChanged);
    };

    memoryTableLeft = new QTableWidget(this);
    memoryTableRight = new QTableWidget(this);

    setupTable(memoryTableLeft);
    setupTable(memoryTableRight);

    memoryLayout->addWidget(memoryTableLeft);
    memoryLayout->addWidget(memoryTableRight);

    QGroupBox *ramGroupBox = new QGroupBox(getLocalizedText("Оперативная память", "RAM"));
    ramGroupBox->setLayout(memoryLayout);
    mainLayout->addWidget(ramGroupBox);


    // ==========================================
    // 5. НИЖНЯЯ ПАНЕЛЬ (Регистры и Справка)
    // ==========================================
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(10);

    // --- Блок регистров процессора ---
    QGroupBox *registersGroupBox = new QGroupBox(getLocalizedText("Регистры центрального процессора", "CPU Registers"));
    registersGroupBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    QGridLayout *registersGrid = new QGridLayout(registersGroupBox);
    QStringList registerNames = {"R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "RS"};

    QRegularExpression octalRegex("[0-7]{0,6}");
    for (int i = 0; i < 9; ++i) {
        int row = i % 3;
        int column = i / 3;
        registersGrid->addWidget(new QLabel(registerNames[i]), row, column * 2);

        QLineEdit *editor = new QLineEdit("000000");
        editor->setFixedWidth(70);
        editor->setAlignment(Qt::AlignCenter);
        editor->setFont(QFont("Monospace", MONO_FONT_SIZE, QFont::Bold));
        editor->setStyleSheet(themeCssEditor); // Применение стиля
        editor->setValidator(new QRegularExpressionValidator(octalRegex, this));
        editor->setMaxLength(6);

        connect(editor, &QLineEdit::editingFinished, this, &MainWindow::handleRegisterEdited);
        registersGrid->addWidget(editor, row, column * 2 + 1);

        if (registerNames[i] == "RS") {
            pswEditor = editor;
        } else {
            int registerIndex = registerNames[i].mid(1).toInt();
            registerEditors[registerIndex] = editor;
        }
    }
    bottomLayout->addWidget(registersGroupBox, 0);

    // --- Блок справки (Reference) ---
    QGroupBox *referenceGroupBox = new QGroupBox(getLocalizedText("Справка", "Reference"));
    referenceGroupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QGridLayout *referenceGrid = new QGridLayout(referenceGroupBox);
    referenceGrid->setSpacing(5);
    referenceGrid->setColumnStretch(0, 0);
    referenceGrid->setColumnStretch(1, 1);

    instructionNameLabel = new AutoTooltipLabel(getLocalizedText("Останов", "Halt"));
    instructionTypeLabel = new AutoTooltipLabel(getLocalizedText("Безадресная команда", "No-address command"));
    instructionFormatLabel = new AutoTooltipLabel("000000");

    auto addLabel = [&](QString title, QLabel* valueLabel, int row, int col, int colspan, QSizePolicy::Policy hPolicy) {
        valueLabel->setMinimumWidth(80);
        valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        valueLabel->setFont(QFont("Monospace", MONO_FONT_SIZE, QFont::Bold));
        valueLabel->setStyleSheet(themeCssEditor); // Применение стиля
        valueLabel->setSizePolicy(hPolicy, QSizePolicy::Preferred);

        if (hPolicy == QSizePolicy::Fixed) {
            valueLabel->setFixedWidth(80);
        }

        QLabel* titleLabel = new QLabel(title);
        referenceGrid->addWidget(titleLabel, row, col);
        referenceGrid->addWidget(valueLabel, row + 1, col, 1, colspan);
    };

    addLabel(getLocalizedText("Формат:", "Format:"), instructionFormatLabel, 0, 0, 1, QSizePolicy::Fixed);
    addLabel(getLocalizedText("Тип:", "Type:"), instructionTypeLabel, 0, 1, 1, QSizePolicy::Expanding);
    addLabel(getLocalizedText("Название:", "Name:"), instructionNameLabel, 2, 0, 2, QSizePolicy::Expanding);

    // Кнопка иконки справки
    refIconButton = new QPushButton(this);
    refIconButton->setIcon(style->standardIcon(QStyle::SP_MessageBoxInformation));
    refIconButton->setIconSize(QSize(48, 48));
    refIconButton->setFixedSize(75, 75);
    connect(refIconButton, &QPushButton::clicked, this, &MainWindow::handleReference);
    referenceGrid->addWidget(refIconButton, 0, 2, 4, 1, Qt::AlignCenter);

    bottomLayout->addWidget(referenceGroupBox, 1);
    mainLayout->addLayout(bottomLayout);

    setWindowTitle(getLocalizedText("Эмулятор системы команд PDP-11", "Command System Emulator PDP-11"));
    resize(655, 673);

    // Инициализация таймеров
    programExecutionTimer = new QTimer(this);
    connect(programExecutionTimer, &QTimer::timeout, this, &MainWindow::handleProgramTick);

    hardwareTimer = new QTimer(this);
    connect(hardwareTimer, &QTimer::timeout, this, &MainWindow::handleHardwareTimerTick);
}

void MainWindow::updateUserInterface() {
    isUpdatingInterface = true; // Блокировка сигналов при обновлении UI

    // 1. Обновление значений регистров процессора (R0-R7)
    for (int i = 0; i < 8; ++i) {
        registerEditors[i]->setText(toOctalString(processor.registers[i], 6));
    }

    // Обновление регистра состояния (PSW / RS)
    pswEditor->setText(toOctalString(processor.processorStatusWord, 6));

    // Лямбда-функция для заполнения таблицы
    auto fillTable = [&](QTableWidget* table, uint16_t baseAddress) {
        for (int i = 0; i < 14; ++i) {
            uint16_t currentAddress = baseAddress + (i * 2);
            uint16_t dataValue = processor.memory[currentAddress / 2];

            // Обновляем адрес (только если он изменился при скроллинге)
            QString currentAddressStr = toOctalString(currentAddress, 6);
            if (table->item(i, 0)->text() != currentAddressStr) {
                table->item(i, 0)->setText(currentAddressStr);
            }

            // Обновляем данные
            QTableWidgetItem *dataItem = table->item(i, 1);
            QString dataStr = toOctalString(dataValue, 6);
            if (dataItem->text() != dataStr) {
                dataItem->setText(dataStr);
            }
            // UserRole обновляем всегда (это просто число, памяти не просит)
            dataItem->setData(Qt::UserRole, currentAddress);

            // Обновляем мнемонику (самая "дорогая" операция из-за дисассемблера)
            QString mnemonic = processor.disassemble(dataValue, currentAddress);
            if (table->item(i, 2)->text() != mnemonic) {
                table->item(i, 2)->setText(mnemonic);
            }
        }
    };

    // Левая таблица: начало с плавающего адреса
    fillTable(memoryTableLeft, currentViewStartAddress);

    // Правая таблица: 14 слов (28 байт) после левой
    fillTable(memoryTableRight, currentViewStartAddress + 28);

    // Обновление окна Additional RAM (если оно открыто и видимо)
    if (!dialog.isNull() && dialog->isVisible()) {
        updateDialogUserInterface();
    }

    // Обновление окна Display Screen (вкладка ASCII Mode)
    if (displayScreenDialog && displayScreenDialog->isVisible()) {
        updateAsciiModeView();
    }

    isUpdatingInterface = false;
}

void MainWindow::handleSelectionChanged() {
    QTableWidget *table = qobject_cast<QTableWidget*>(sender());
    if (!table) return;

    int row = table->currentRow();
    if (row < 0) return;

    QTableWidgetItem *dataItem = table->item(row, 1);
    if (dataItem) {
        bool conversionSuccess;
        uint16_t value = dataItem->text().toUInt(&conversionSuccess, 8);
        if (conversionSuccess) {
            updateReferencePanel(value);
        }
    }
}

void MainWindow::updateReferencePanel(uint16_t value) {
    int row = -1;
    uint16_t addr = 0;

    // Ищем выделенную строку для расчета переходов (Branch)
    if (memoryTableLeft->selectedItems().count() > 0) {
        row = memoryTableLeft->currentRow();
        if (row != -1 && memoryTableLeft->item(row, 1)) {
            addr = memoryTableLeft->item(row, 1)->data(Qt::UserRole).toUInt();
        }
    } else if (memoryTableRight->selectedItems().count() > 0) {
        row = memoryTableRight->currentRow();
        if (row != -1 && memoryTableRight->item(row, 1)) {
            addr = memoryTableRight->item(row, 1)->data(Qt::UserRole).toUInt();
        }
    }

    QString name, type, format;
    processor.getInstructionDetails(value, addr, name, type, format);

    instructionNameLabel->setText(name);
    instructionTypeLabel->setText(type);
    instructionFormatLabel->setText(format);

    // Обновление ToolTips
    instructionNameLabel->setToolTip(name);
    instructionTypeLabel->setToolTip(type);
    instructionFormatLabel->setToolTip(format);
}

void MainWindow::handleRegisterEdited() {
    if (isUpdatingInterface) return;

    // Считываем значения из интерфейса обратно в эмулятор
    for (int i = 0; i < 8; ++i) {
        processor.registers[i] = registerEditors[i]->text().toUInt(nullptr, 8);
    }
    processor.processorStatusWord = pswEditor->text().toUInt(nullptr, 8);

    updateUserInterface();
}

void MainWindow::handleTableItemChanged(QTableWidgetItem *item) {
    if (isUpdatingInterface || item->column() != 1) return;

    bool conversionSuccess;
    uint16_t newValue = item->text().toUInt(&conversionSuccess, 8);
    uint16_t memoryAddress = item->data(Qt::UserRole).toUInt();

    if (conversionSuccess) {
        // Записываем в память процессора
        processor.memory[memoryAddress / 2] = static_cast<uint16_t>(newValue & 0xFFFF);

        // Обновляем панель Reference
        QTableWidget *table = item->tableWidget();
        if (table->currentRow() == item->row()) {
            updateReferencePanel(newValue);
        }
    }
    updateUserInterface(); // Обновление основной таблицы
}

void MainWindow::synchronizeTableWithProgramCounter() {
    uint16_t programCounterAddress = processor.registers[7];

    // Если R7 вышел за пределы видимых таблиц, выполняем "скроллинг"
    if (programCounterAddress < currentViewStartAddress ||
        programCounterAddress >= (currentViewStartAddress + 56)) {

        currentViewStartAddress = programCounterAddress & 0xFFFE;
    updateUserInterface();
    }

    // Фокус на нужной строке в левой таблице
    if (programCounterAddress >= currentViewStartAddress &&
        programCounterAddress < (currentViewStartAddress + 28)) {

        int rowIndex = (programCounterAddress - currentViewStartAddress) / 2;
        memoryTableLeft->setFocus();
        memoryTableLeft->setCurrentCell(rowIndex, 1);
    }
    // Фокус на нужной строке в правой таблице
    else if (programCounterAddress >= (currentViewStartAddress + 28) &&
        programCounterAddress < (currentViewStartAddress + 56)) {

        int rowIndex = (programCounterAddress - (currentViewStartAddress + 28)) / 2;
        memoryTableRight->setFocus();
        memoryTableRight->setCurrentCell(rowIndex, 1);
    }
}

void MainWindow::handleOpenFile() {
    QString fileFilter = getLocalizedText("Файлы кода PDP-11 (*.pdp);;Все файлы (*.*)", "PDP-11 code files (*.pdp);;All Files (*.*)");
    QString selectedFileName = QFileDialog::getOpenFileName(this, getLocalizedText("Открыть файл", "Open File"), QString(), fileFilter);

    if (selectedFileName.isEmpty()) return;

    QFile dataFile(selectedFileName);
    if (!dataFile.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, getLocalizedText("Ошибка", "Error"), getLocalizedText("Не удалось открыть файл", "Unable to open file"));
        return;
    }

    QDataStream in(&dataFile);
    in.setByteOrder(QDataStream::LittleEndian);

    // Перед загрузкой новых данных сбрасываем состояние процессора
    processor.resetProcessor();

#ifdef ENABLE_STUDENT_SECURITY
    uint32_t magic = 0;
    in >> magic;

    if (magic == 0x53445031) { // "SDP1"
        QString fileOwnerId;
        QByteArray fileSignature;
        in >> fileOwnerId;
        in >> fileSignature;

        // Изменено: Читаем полезную нагрузку через QDataStream из кэша потока
        QByteArray payload;
        in >> payload;

        // Проверяем криптографическую подпись структуры данных
        QByteArray expectedSignature = calculateFileSignature(payload, fileOwnerId);
        if (fileSignature != expectedSignature) {
            QMessageBox::critical(this, getLocalizedText("Ошибка", "Error"),
                                  getLocalizedText("Целостность файла нарушена или файл поврежден.", "File integrity check failed."));
            dataFile.close();
            return;
        }

        // Защита от списывания: Владелец файла должен совпадать с текущим студентом
        if (fileOwnerId != currentStudentId) {
            QMessageBox::critical(this, getLocalizedText("Доступ запрещен", "Access Denied"),
                                  getLocalizedText(QString("Этот файл принадлежит другому студенту (%1).\nВыполнять чужие работы запрещено.").arg(fileOwnerId),
                                                   QString("This file belongs to another student (%1).\nUsing someone else's work is prohibited.").arg(fileOwnerId)));
            dataFile.close();
            return;
        }

        // Настраиваем поток для десериализации проверенной полезной нагрузки
        QDataStream payloadStream(&payload, QIODevice::ReadOnly);
        payloadStream.setByteOrder(QDataStream::LittleEndian);

        // Читаем регистры
        for (int i = 0; i < 8; ++i) {
            if (payloadStream.atEnd()) break;
            uint16_t registerValue;
            payloadStream >> registerValue;
            processor.registers[i] = registerValue;
        }

        // Читаем PSW
        if (!payloadStream.atEnd()) {
            uint16_t processorStatusValue;
            payloadStream >> processorStatusValue;
            processor.processorStatusWord = processorStatusValue;
        }

        // Читаем память
        while (!payloadStream.atEnd()) {
            uint16_t wordIndex = 0;
            uint16_t dataValue = 0;
            payloadStream >> wordIndex;
            if (payloadStream.atEnd()) break;
            payloadStream >> dataValue;
            if (wordIndex < 32768) {
                processor.memory[wordIndex] = dataValue;
            }
        }
    } else {
        // Если макрос безопасности активен, загрузка незащищенных файлов запрещается
        QMessageBox::critical(this, getLocalizedText("Доступ запрещен", "Access Denied"),
                              getLocalizedText("В системе включена защита файлов. Загрузка незащищенных или поврежденных файлов .pdp запрещена.",
                                               "Security mode is enabled. Loading unprotected or corrupted .pdp files is prohibited."));
        dataFile.close();
        return;
    }
#else
    // 1. Читаем регистры процессора из файла
    for (int i = 0; i < 8; ++i) {
        if (in.atEnd()) {
            break;
        }
        uint16_t registerValue;
        in >> registerValue;
        processor.registers[i] = registerValue;
    }

    // 2. Читаем Регистр Состояния Процессора (PSW / RS)
    if (!in.atEnd()) {
        uint16_t processorStatusValue;
        in >> processorStatusValue;
        processor.processorStatusWord = processorStatusValue;
    }

    // 3. Читаем данные памяти: [Индекс слова] -> [Значение]
    while (!in.atEnd()) {
        uint16_t wordIndex = 0;
        uint16_t dataValue = 0;

        in >> wordIndex;
        if (in.atEnd()) break;
        in >> dataValue;

        // Проверка границ (память ограничена 32К слов)
        if (wordIndex < 32768) {
            // Запись в память
            processor.memory[wordIndex] = dataValue;
        }
    }
#endif

    dataFile.close();

    processor.isProcessorHalted = false;
    currentViewStartAddress = processor.registers[7] & 0xFFFE;

    // Подготовка интерфейса после загрузки
    processor.isProcessorHalted = false;

    // Устанавливаем адрес просмотра на значение счетчика команд (PC / R7)
    currentViewStartAddress = processor.registers[7] & 0177776;

    // Обновляем главное окно
    updateUserInterface();
    synchronizeTableWithProgramCounter();
}

void MainWindow::handleSaveFile() {
    QString fileFilter = getLocalizedText("Файлы кода PDP-11 (*.pdp);;Все файлы (*.*)", "PDP-11 code files (*.pdp);;All Files (*.*)");
    QString selectedFileName = QFileDialog::getSaveFileName(this, getLocalizedText("Сохранить файл", "Save File"), QString(), fileFilter);

    if (selectedFileName.isEmpty()) return;

    if (!selectedFileName.endsWith(".pdp", Qt::CaseInsensitive)) {
        selectedFileName += ".pdp";
    }

    QFile dataFile(selectedFileName);
    if (!dataFile.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, getLocalizedText("Ошибка", "Error"), getLocalizedText("Не удалось открыть файл для записи", "Unable to open file for writing"));
        return;
    }

    QDataStream out(&dataFile);
    out.setByteOrder(QDataStream::LittleEndian);

#ifdef ENABLE_STUDENT_SECURITY
    // 1. Записываем сигнатуру защищенного файла и ID текущего студента
    out << (uint32_t)0x53445031; // "SDP1"
    out << currentStudentId;

    // 2. Сериализуем полезную нагрузку во временный буфер для расчета подписи
    QByteArray payload;
    QDataStream payloadStream(&payload, QIODevice::WriteOnly);
    payloadStream.setByteOrder(QDataStream::LittleEndian);

    for (int i = 0; i < 8; ++i) {
        payloadStream << (uint16_t)processor.registers[i];
    }
    payloadStream << (uint16_t)processor.processorStatusWord;
    for (uint16_t index = 0; index < 32768; ++index) {
        if (processor.memory[index] != 0) {
            payloadStream << index;
            payloadStream << (uint16_t)processor.memory[index];
        }
    }

    // 3. Вычисляем подпись и записываем сигнатуру и payload строго через QDataStream
    QByteArray fileSignature = calculateFileSignature(payload, currentStudentId);
    out << fileSignature;
    out << payload; // Изменено: записываем массив через поток, чтобы Qt управлял длинами сам
#else
    // 1. Записываем регистры
    for (int i = 0; i < 8; ++i) {
        out << (uint16_t)processor.registers[i];
    }

    // 2. Записываем PSW
    out << (uint16_t)processor.processorStatusWord;

    // 3. Записываем память (только ненулевые значения)
    for (uint16_t index = 0; index < 32768; ++index) {
        if (processor.memory[index] != 0) {
            out << index;
            out << (uint16_t)processor.memory[index];
        }
    }
#endif

    dataFile.close();
    QMessageBox::information(this, getLocalizedText("Сохранение файла", "File Save"), selectedFileName);
}

void MainWindow::handleStepExecution() {
    if (isProgramRunning) {
        handleProgramExecution(); // Остановить авто-выполнение
        return;
    }

    processor.isProcessorHalted = false;
    processor.executeSingleStep();
    updateUserInterface();
    synchronizeTableWithProgramCounter();
}

QString MainWindow::toOctalString(uint16_t value, int digits) {
    return QString::number(value, 8).rightJustified(digits, '0');
}

void MainWindow::handleExit() {
    QApplication::quit();
}

void MainWindow::handleProgramExecution() {
    if (isProgramRunning) {
        programExecutionTimer->stop();
        isProgramRunning = false;
    } else {
        processor.isProcessorHalted = false;
        isProgramRunning = true;
        programExecutionTimer->start(0); // Выполняем максимально быстро
    }
}

void MainWindow::handleProgramTick() {
    processor.executeSingleStep();
    updateUserInterface();
    synchronizeTableWithProgramCounter();

    if (processor.isProcessorHalted) {
        programExecutionTimer->stop();
        isProgramRunning = false;
    }
}

void MainWindow::handleTimerToggle(bool checked) {
    if (checked) {
        actionTimerToggle->setToolTip(getLocalizedText("Таймер включен", "Timer on"));
        hardwareTimer->start(100);
    } else {
        actionTimerToggle->setToolTip(getLocalizedText("Таймер выключен", "Timer off"));
        hardwareTimer->stop();
    }
}

void MainWindow::handleHardwareTimerTick() {
    if (processor.isProcessorHalted) return;

    // Вектор прерывания таймера - 100(8) = 64(10)
    processor.handleInterrupt(0100);

    updateUserInterface();
    synchronizeTableWithProgramCounter();
}

void MainWindow::handleRamClear() {
    processor.memory.fill(0);
    updateUserInterface();
}

void MainWindow::handleRamAdd() {
    // Проверка на существование окна
    if (dialog) {
        if (dialog->isHidden()) {
            dialog->show();
        }
        dialog->raise();
        dialog->activateWindow();
        return;
    }

    // Создание диалогового окна
    dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose); // Автоматическое удаление при закрытии
    dialog->setWindowTitle(getLocalizedText("Дополнительное ОЗУ", "Additional RAM"));
    dialog->resize(700, 480);

    QVBoxLayout *dialogLayout = new QVBoxLayout(dialog);
    QHBoxLayout *memoryLayout = new QHBoxLayout();

    // Вспомогательная лямбда для настройки таблиц памяти
    auto setupDialogTable = [&](QTableWidget* table) {
        table->setColumnCount(3);
        table->setRowCount(14);
        table->setHorizontalHeaderLabels({getLocalizedText("Адрес", "Address"), getLocalizedText("Данные", "Data"), getLocalizedText("Мнемоника", "Mnemonic")});
        table->verticalHeader()->setVisible(false);

        table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table->setFont(QFont("Monospace", MONO_FONT_SIZE, QFont::Bold));

        table->setColumnWidth(0, 70);
        table->setColumnWidth(1, 70);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->verticalHeader()->setMinimumSectionSize(30);

        table->setMinimumHeight(460);
        table->setMinimumWidth(275);
        table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        // Применение динамического стиля темы
        table->setStyleSheet(themeCssTable);

        for (int i = 0; i < 14; ++i) {
            QTableWidgetItem *addressItem = new QTableWidgetItem();
            addressItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            addressItem->setTextAlignment(Qt::AlignCenter);
            table->setItem(i, 0, addressItem);

            QTableWidgetItem *dataItem = new QTableWidgetItem();
            dataItem->setTextAlignment(Qt::AlignCenter);
            table->setItem(i, 1, dataItem);

            QTableWidgetItem *mnemonicItem = new QTableWidgetItem();
            mnemonicItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            mnemonicItem->setForeground(QBrush(Qt::darkGray));
            table->setItem(i, 2, mnemonicItem);
        }

        table->setItemDelegateForColumn(1, new OctalDelegate());
        table->installEventFilter(this); // Слежка за клавиатурой
    };

    // Создание и настройка таблиц
    dialogTableLeft = new QTableWidget(dialog);
    dialogTableRight = new QTableWidget(dialog);

    setupDialogTable(dialogTableLeft);
    setupDialogTable(dialogTableRight);

    memoryLayout->addWidget(dialogTableLeft);
    memoryLayout->addWidget(dialogTableRight);

    QGroupBox *ramGroupBox = new QGroupBox(getLocalizedText("Оперативная память", "RAM"));
    ramGroupBox->setLayout(memoryLayout);
    dialogLayout->addWidget(ramGroupBox);

    // Обработка ручного редактирования ячеек пользователем
    auto handleDialogItemChanged = [&](QTableWidgetItem *item) {
        if (isUpdatingInterface || item->column() != 1) return;

        bool conversionSuccess;
        uint16_t newValue = item->text().toUInt(&conversionSuccess, 8);
        uint16_t memoryAddress = item->data(Qt::UserRole).toUInt();

        if (conversionSuccess) {
            processor.memory[memoryAddress / 2] = newValue; // Записываем в память
            updateUserInterface(); // Синхронизируем интерфейс
        }
    };

    connect(dialogTableLeft, &QTableWidget::itemChanged, handleDialogItemChanged);
    connect(dialogTableRight, &QTableWidget::itemChanged, handleDialogItemChanged);

    // Заполнение данных и показ окна
    updateDialogUserInterface();
    dialog->show();
}

void MainWindow::updateDialogUserInterface() {
    // Если окно не создано или уже удалено пользователем, выходим
    if (dialog.isNull() || dialogTableLeft.isNull() || dialogTableRight.isNull()) {
        return;
    }

    isUpdatingInterface = true;

    auto fillTable = [&](QTableWidget* table, uint16_t baseAddress) {
        for (int i = 0; i < 14; ++i) {
            uint16_t currentAddress = baseAddress + (i * 2);
            uint16_t dataValue = processor.memory[currentAddress / 2];

            // Обновляем адрес (только если он изменился при скроллинге)
            QString currentAddressStr = toOctalString(currentAddress, 6);
            if (table->item(i, 0)->text() != currentAddressStr) {
                table->item(i, 0)->setText(currentAddressStr);
            }

            // Обновляем данные
            QTableWidgetItem *dataItem = table->item(i, 1);
            QString dataStr = toOctalString(dataValue, 6);
            if (dataItem->text() != dataStr) {
                dataItem->setText(dataStr);
            }
            // UserRole обновляем всегда (это просто число, памяти не просит)
            dataItem->setData(Qt::UserRole, currentAddress);

            // Обновляем мнемонику (самая "дорогая" операция из-за дисассемблера)
            QString mnemonic = processor.disassemble(dataValue, currentAddress);
            if (table->item(i, 2)->text() != mnemonic) {
                table->item(i, 2)->setText(mnemonic);
            }
        }
    };

    fillTable(dialogTableLeft, dialogStartAddress);
    fillTable(dialogTableRight, dialogStartAddress + 28);

    isUpdatingInterface = false;
}

// ==========================================
// УНИВЕРСАЛЬНАЯ ЛОГИКА ПРОКРУТКИ ТАБЛИЦ
// ==========================================
bool MainWindow::handleTableScroll(QTableWidget *table, QKeyEvent *keyEvent, QTableWidget *leftTable, QTableWidget *rightTable, uint16_t &startAddress, std::function<void()> updateFunc) {
    int currentRow = table->currentRow();

    if (keyEvent->key() == Qt::Key_Down) {
        // Переход из левой таблицы в правую
        if (table == leftTable && currentRow == 13) {
            leftTable->clearSelection(); // Очищаем старое выделение
            rightTable->setFocus();
            rightTable->setCurrentCell(0, 1);
            return true;
        }
        // Прокрутка правой таблицы вниз
        if (table == rightTable && currentRow == 13) {
            startAddress += 2;
            updateFunc(); // Вызываем переданную функцию обновления
            rightTable->setCurrentCell(13, 1);

            // Принудительно обновляем Reference
            updateReferencePanel(rightTable->item(13, 1)->text().toUInt(nullptr, 8));
            return true;
        }
    }

    if (keyEvent->key() == Qt::Key_Up) {
        // Переход из правой таблицы в левую
        if (table == rightTable && currentRow == 0) {
            rightTable->clearSelection(); // Очищаем старое выделение
            leftTable->setFocus();
            leftTable->setCurrentCell(13, 1);
            return true;
        }
        // Прокрутка левой таблицы вверх
        if (table == leftTable && currentRow == 0) {
            if (startAddress > 0) {
                startAddress -= 2;
                updateFunc(); // Вызываем переданную функцию обновления
                leftTable->setCurrentCell(0, 1);

                // Принудительно обновляем Reference
                updateReferencePanel(leftTable->item(0, 1)->text().toUInt(nullptr, 8));
            }
            return true;
        }
    }
    return false;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    // Если изменился размер окна дисплея, перерисовываем графический экран под новые габариты
    if (displayScreenDialog && watched == displayScreenDialog && event->type() == QEvent::Resize) {
        updateGraphicsScreenView();
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

        // --- 1. Обработка ввода с клавиатуры для Терминала ---
        if (screenTextModeWidget && watched == screenTextModeWidget) {
            QString text = keyEvent->text();
            if (!text.isEmpty()) {
                uint8_t charCode = static_cast<uint8_t>(text.at(0).toLatin1());
                processor.provideKeyboardInput(charCode);
            }
            return true; // Блокируем стандартный ввод Qt
        }

        // --- 2. Обработка ввода с клавиатуры для Экрана ---
        if (displayScreenDialog && watched == displayScreenDialog) {
            QString text = keyEvent->text();
            if (!text.isEmpty()) {
                uint8_t charCode = static_cast<uint8_t>(text.at(0).toLatin1());
                processor.provideKeyboardInput(charCode);
                return true; // Блокируем клавиши управления, чтобы они не переключали табы интерфейса
            }
        }


        // --- 3. Обработка скроллинга таблиц RAM ---
        QTableWidget *table = qobject_cast<QTableWidget *>(watched);
        if (table) {
            // Главное окно
            if (table == memoryTableLeft || table == memoryTableRight) {
                if (handleTableScroll(table, keyEvent, memoryTableLeft, memoryTableRight, currentViewStartAddress, [this](){ updateUserInterface(); })) {
                    return true;
                }
            }
            // Окно Additional RAM
            else if (dialogTableLeft && dialogTableRight && (table == dialogTableLeft || table == dialogTableRight)) {
                if (handleTableScroll(table, keyEvent, dialogTableLeft, dialogTableRight, dialogStartAddress,[this](){ updateDialogUserInterface(); })) {
                    return true;
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}


void MainWindow::handleScreenShow() {
    if (displayScreenDialog) {
        if (displayScreenDialog->isHidden()) {
            displayScreenDialog->show();
        }
        displayScreenDialog->raise();
        displayScreenDialog->activateWindow();
        return;
    }

    displayScreenDialog = new QDialog(this);
    displayScreenDialog->setAttribute(Qt::WA_DeleteOnClose);
    displayScreenDialog->setWindowTitle(getLocalizedText("Экран дисплея", "Display Screen"));
    displayScreenDialog->resize(600, 350);

    // Устанавливаем фильтр событий на окно, чтобы отслеживать изменение его размеров
    displayScreenDialog->installEventFilter(this);

    QVBoxLayout *mainLayout = new QVBoxLayout(displayScreenDialog);
    QTabWidget *tabWidget = new QTabWidget(displayScreenDialog);

    // 1. Вкладка "Text Mode" (Консоль терминала)
    screenTextModeWidget = new QPlainTextEdit(displayScreenDialog);
    screenTextModeWidget->setReadOnly(true);
    screenTextModeWidget->setFont(QFont("Monospace", MONO_FONT_SIZE));
    screenTextModeWidget->setStyleSheet(themeCssScreenTab);
    screenTextModeWidget->setPlainText(screenTextBuffer);
    tabWidget->addTab(screenTextModeWidget, getLocalizedText("Текстовый режим", "Text Mode"));

    // 2. Вкладка "ASCII Mode" (Дамп памяти)
    screenAsciiModeWidget = new QPlainTextEdit(displayScreenDialog);
    screenAsciiModeWidget->setReadOnly(true);
    screenAsciiModeWidget->setFont(QFont("Monospace", MONO_FONT_SIZE));
    screenAsciiModeWidget->setStyleSheet(themeCssScreenTab);
    tabWidget->addTab(screenAsciiModeWidget, getLocalizedText("ASCII режим", "ASCII Mode"));

    // 2.5 Вкладка "Graphics Mode" (Растровый дисплей)
    screenGraphicsWidget = new QLabel(displayScreenDialog);
    screenGraphicsWidget->setAlignment(Qt::AlignCenter);
    screenGraphicsWidget->setStyleSheet(themeCssScreenTab);

    // Теперь QTabWidget будет сам диктовать размер нашему экрану, избегая бесконечного цикла ресайза.
    screenGraphicsWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    tabWidget->addTab(screenGraphicsWidget, getLocalizedText("Графический режим", "Graphics Mode"));

    // 3. Вкладка "Printer" (Устройство печати)
    printerModeWidget = new QPlainTextEdit(displayScreenDialog);
    printerModeWidget->setReadOnly(true);
    printerModeWidget->setFont(QFont("Monospace", MONO_FONT_SIZE));
    printerModeWidget->setStyleSheet(themeCssScreenTab);
    printerModeWidget->setPlainText(printerTextBuffer);
    tabWidget->addTab(printerModeWidget, getLocalizedText("Принтер", "Printer"));

    // Подключение фильтра для чтения клавиатуры
    screenTextModeWidget->installEventFilter(this);

    mainLayout->addWidget(tabWidget);

    connect(tabWidget, &QTabWidget::currentChanged, [this](int index) {
        if (index == 1) {
            updateAsciiModeView();
        } else if (index == 2) {
            updateGraphicsScreenView();
        }
    });

    updateAsciiModeView();
    displayScreenDialog->show();
}

void MainWindow::drawPixelOnScreen(uint8_t x, uint8_t y, uint8_t colorIndex) {
    if (x >= Pdp11::SCREEN_SIZE || y >= Pdp11::SCREEN_SIZE) return;

    // Классическая 16-цветная палитра (CGA/EGA-стиль)
    static const std::array<QRgb, 16> palette16 = {
        qRgb(0, 0, 0),       // 0: Глубокий черный
        qRgb(0, 0, 192),     // 1: Темно-синий
        qRgb(0, 192, 0),     // 2: Темно-зеленый
        qRgb(0, 192, 192),   // 3: Темно-голубой
        qRgb(192, 0, 0),     // 4: Темно-красный
        qRgb(192, 0, 192),   // 5: Темно-фиолетовый
        qRgb(139, 69, 19),   // 6: Коричневый (охра)
        qRgb(200, 200, 200), // 7: Светло-серый
        qRgb(96, 96, 96),    // 8: Темно-серый
        qRgb(0, 100, 255),   // 9: Яркий неоновый синий
        qRgb(0, 255, 0),     // 10: Чистый ярко-зеленый (лайм)
        qRgb(0, 255, 255),   // 11: Чистый циан (ярко-голубой)
        qRgb(255, 0, 0),     // 12: Чистый алый (ярко-красный)
        qRgb(255, 0, 255),   // 13: Яркая фуксия (ярко-фиолетовый)
        qRgb(255, 255, 0),   // 14: Чистый лимонный (ярко-желтый)
        qRgb(255, 255, 255)  // 15: Чистый белый
    };

    // Рисуем точку в буфер
    QRgb color = palette16[colorIndex & 0x0F];
    graphicsScreenBuffer.setPixel(x, y, color);

    // Если графический экран открыт на чтение, обновляем изображение
    if (displayScreenDialog && displayScreenDialog->isVisible()) {
        updateGraphicsScreenView();
    }
}

void MainWindow::updateGraphicsScreenView() {
    if (screenGraphicsWidget.isNull()) return;

    // Получаем родительский виджет (страницу QTabWidget)
    QWidget *parent = screenGraphicsWidget->parentWidget();
    if (!parent) return;

    // Получаем системные отступы родительского контейнера
    QMargins margins = parent->contentsMargins();

    // Вычисляем точную чистую ширину и высоту, доступную для нашего экрана
    int availableWidth = parent->width() - margins.left() - margins.right();
    int availableHeight = parent->height() - margins.top() - margins.bottom();

    // Экран должен оставаться квадратным, поэтому берем минимальную из сторон
    int side = std::min(availableWidth, availableHeight);

    // Добавляем небольшой внутренний отступ (например, по 10 пикселей с каждой стороны),
    // чтобы экран не прилипал вплотную к границам вкладки
    side = std::max(Pdp11::SCREEN_SIZE, side - 20);

    // Масштабируем буфер до вычисленного пиксель-в-пиксель размера
    QPixmap scaledPixmap = QPixmap::fromImage(graphicsScreenBuffer).scaled(side, side, Qt::KeepAspectRatio, Qt::FastTransformation);

    screenGraphicsWidget->setPixmap(scaledPixmap);
}

void MainWindow::updateAsciiModeView() {
    // Выход, если виджеты удалены или скрыты
    if (screenAsciiModeWidget.isNull() || displayScreenDialog.isNull() || !displayScreenDialog->isVisible()) {
        return;
    }

    // Если данных еще нет, оставляем вкладку пустой
    if (screenTextBuffer.isEmpty()) {
        screenAsciiModeWidget->clear();
        return;
    }

    // Максимум берем 128 символов
    int charsToProcess = std::min(static_cast<int>(screenTextBuffer.length()), 128);

    // Создаем ОДНУ строку и заранее выделяем память.
    // 128 символов * (максимум 3 цифры + 2 точки "..") = 640 байт.
    QString finalContent;
    finalContent.reserve(640);

    for (int i = 0; i < charsToProcess; ++i) {
        uint8_t charCode = static_cast<uint8_t>(screenTextBuffer.at(i).toLatin1());
        finalContent.append(QString::number(charCode, 8)); // Добавляем код
        finalContent.append(".."); // Добавляем разделитель
    }

    // Qt внутри проверит, изменился ли текст, и обновит виджет только при необходимости
    if (screenAsciiModeWidget->toPlainText() != finalContent) {
        screenAsciiModeWidget->setPlainText(finalContent);
    }
}

void MainWindow::handleScreenClear() {
    // Очищаем буферы текста (накопленную историю)
    screenTextBuffer.clear();  // Для терминала (Text / ASCII)
    printerTextBuffer.clear(); // Для устройства печати (Printer)

    // Очищаем виджеты, если они созданы и окно открыто
    if (screenTextModeWidget) {
        screenTextModeWidget->clear();
    }

    if (screenAsciiModeWidget) {
        screenAsciiModeWidget->clear();
    }

    graphicsScreenBuffer.fill(Qt::black);
    if (screenGraphicsWidget) {
        updateGraphicsScreenView();
    }

    if (printerModeWidget) {
        printerModeWidget->clear();
    }
}

void MainWindow::handleScreenRemove() {
    if (displayScreenDialog) {
        displayScreenDialog->close();
    }
}


void MainWindow::handleReference() {
    // Получаем путь к директории, откуда запущен сам эмулятор (обычно это папка build)
    QString appDirPath = QCoreApplication::applicationDirPath();

    // Формируем полный путь к файлу справки с учетом специфики структуры macOS App Bundle
#ifdef Q_OS_MAC
    // 1. Ищем внутри ресурсов бандла приложения (.app)
    QString pdfFilePath = appDirPath + "/../Resources/" + getReferenceFileName();
    if (!QFile::exists(pdfFilePath)) {
        // 2. Фолбэк: если бандл запущен портативно, ищем файлы рядом с ним (например, в корне DMG)
        pdfFilePath = QDir(appDirPath + "/../../../").absoluteFilePath(getReferenceFileName());
    }
#else
    QString pdfFilePath = appDirPath + QDir::separator() + getReferenceFileName();
#endif

    // Проверяем, существует ли файл физически на диске
    if (!QFile::exists(pdfFilePath)) {
        // Если файла нет, можно тоже показать это стандартное сообщение
        QMessageBox::warning(this, getLocalizedText("Ошибка", "Error"), getLocalizedText("Не удалось запустить справку.", "Failed to launch help."));

        return;
    }

    // Пытаемся открыть файл средствами операционной системы
    bool success = QDesktopServices::openUrl(QUrl::fromLocalFile(pdfFilePath));

    // Если операционная система не смогла открыть файл (нет программы для PDF и т.д.)
    if (!success) {
        QMessageBox::warning(this, getLocalizedText("Ошибка", "Error"), getLocalizedText("Не удалось запустить справку.", "Failed to launch help."));
    }
}

void MainWindow::handleAbout() {
    QMessageBox::about(this, getLocalizedText("О программе", "About"), getLocalizedText("Версия 1.0\nФакультет компьютерных наук\nCopyright © 2026", "Version 1.0\nDepartment of Computer Science\nCopyright © 2026"));
}

#ifdef ENABLE_STUDENT_SECURITY

bool MainWindow::validateLocalToken() {
    QSettings settings("PDP11", "PDP11");
    QString savedId = settings.value("student_id").toString();
    QString savedToken = settings.value("security_token").toString();

    return (savedToken == computeLocalToken(savedId));
}

bool MainWindow::showLoginDialog() {
    bool ok;
    QString username = QInputDialog::getText(
        this,
        getLocalizedText("Авторизация ВГУ Moodle", "VSU Moodle Login"),
        getLocalizedText("Введите номер студенческого билета (логин edu.vsu.ru):", "Enter student card number (edu.vsu.ru login):"),
        QLineEdit::Normal,
        "",
        &ok
    );

    if (!ok || username.trimmed().isEmpty()) return false;

    QString password = QInputDialog::getText(this,
        getLocalizedText("Авторизация VSU Moodle", "VSU Moodle Login"),
        getLocalizedText("Введите пароль от личного кабинета:", "Enter portal password:"),
        QLineEdit::Password, "", &ok);

    if (!ok) return false;

    setEnabled(false);
    AuthStatus status = authenticateViaMoodle(username.trimmed(), password);
    setEnabled(true);

    if (status == AuthStatus::Success) {
        currentStudentId = username.trimmed();

        QSettings settings("PDP11", "PDP11");
        settings.setValue("student_id", currentStudentId);
        settings.setValue("security_token", computeLocalToken(currentStudentId));

        QMessageBox::information(
            this,
            getLocalizedText("Успешно", "Success"),
            getLocalizedText("Авторизация пройдена успешно.", "Authorization completed successfully.")
        );
        return true;
    }
    else if (status == AuthStatus::NetworkError) {
        QMessageBox::critical(
            this,
            getLocalizedText("Ошибка сети", "Network Error"),
            getLocalizedText("Отсутствует интернет-соединение или сервер edu.vsu.ru недоступен.\nПожалуйста, проверьте подключение к сети.", "No internet connection or edu.vsu.ru is down.\nPlease check your network connection.")
        );
        return false;
    }
    else if (status == AuthStatus::SslError) {
        QMessageBox::critical(
            this,
            getLocalizedText("Ошибка SSL / Безопасности", "SSL / Security Error"),
            getLocalizedText("Ошибка безопасного соединения (SSL Handshake).\nУбедитесь, что на компьютере установлены библиотеки OpenSSL (они необходимы Qt для работы с HTTPS).", "Secure connection error (SSL Handshake).\nMake sure OpenSSL libraries are installed on your computer (they are required by Qt for HTTPS).")
        );
        return false;
    }
    else if (status == AuthStatus::TokenError) {
        QMessageBox::critical(
            this,
            getLocalizedText("Ошибка структуры сайта", "Site Error"),
            getLocalizedText("Не удалось получить токен сессии с сайта Moodle.\nВозможно, на сервере ведутся технические работы.", "Could not extract session token from Moodle.\nTechnical maintenance might be in progress on the server.")
        );
        return false;
    }
    else if (status == AuthStatus::LockedAccount) {
        QMessageBox::critical(
            this,
            getLocalizedText("Учетная запись заблокирована", "Account Locked"),
            getLocalizedText("Ваша учетная запись на портале Moodle временно заблокирована из-за частых попыток входа.\n\nПожалуйста, проверьте свою университетскую почту (vsu.ru) — туда было отправлено письмо со ссылкой для разблокировки аккаунта.", "Your Moodle account has been temporarily locked due to multiple login attempts.\n\nPlease check your university student email (vsu.ru) for an unlock link.")
        );
        return false;
    }
    else { // AuthStatus::InvalidCredentials
        QMessageBox::critical(
            this,
            getLocalizedText("Ошибка авторизации", "Authentication Error"),
            getLocalizedText("Неверный номер студенческого билета или пароль.", "Invalid student card number or password.")
        );
        return false;
    }
}

AuthStatus MainWindow::authenticateViaMoodle(const QString& username, const QString& password) {
    QNetworkAccessManager manager;
    manager.setCookieJar(new QNetworkCookieJar(&manager));

    QString userAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

    // Шаг 1: GET-запрос страницы входа для получения токена и кук сессии
    QUrl loginUrl("https://edu.vsu.ru/login/index.php");
    QNetworkRequest getRequest(loginUrl);
    getRequest.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    getRequest.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8");
    getRequest.setRawHeader("Accept-Language", "ru-RU,ru;q=0.9,en-US;q=0.8,en;q=0.7");

    QNetworkReply* getReply = manager.get(getRequest);
    QEventLoop loop;
    connect(getReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QNetworkReply::NetworkError getError = getReply->error();
    if (getError != QNetworkReply::NoError) {
        getReply->deleteLater();
        if (getError == QNetworkReply::SslHandshakeFailedError) {
            return AuthStatus::SslError;
        }
        return AuthStatus::NetworkError;
    }

    QString html = QString::fromUtf8(getReply->readAll());
    getReply->deleteLater();

    // Автоматический сбор скрытых полей формы
    QUrlQuery postData;
    QRegularExpression hiddenInputRegex("<input type=\"hidden\" name=\"([^\"]+)\" value=\"([^\"]*)\"");
    QRegularExpressionMatchIterator it = hiddenInputRegex.globalMatch(html);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        postData.addQueryItem(match.captured(1), match.captured(2));
    }

    QString loginToken = postData.queryItemValue("logintoken");
    if (loginToken.isEmpty()) {
        return AuthStatus::TokenError;
    }

    // Шаг 2: Подготовка POST-запроса
    postData.addQueryItem("username", username);
    postData.addQueryItem("password", password);

    if (!postData.hasQueryItem("anchor")) {
        postData.addQueryItem("anchor", "");
    }

    QNetworkRequest postRequest(loginUrl);
    postRequest.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    postRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    postRequest.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8");
    postRequest.setRawHeader("Referer", "https://edu.vsu.ru/login/index.php");
    postRequest.setRawHeader("Origin", "https://edu.vsu.ru");

    // Кодируем все параметры по стандарту x-www-form-urlencoded вручную
    QByteArray postBody;
    auto queryItems = postData.queryItems();
    for (int i = 0; i < queryItems.size(); ++i) {
        if (i > 0) {
            postBody.append('&');
        }
        postBody.append(QUrl::toPercentEncoding(queryItems[i].first));
        postBody.append('=');
        postBody.append(QUrl::toPercentEncoding(queryItems[i].second));
    }

    QNetworkReply* postReply = manager.post(postRequest, postBody);
    connect(postReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QNetworkReply::NetworkError postError = postReply->error();
    if (postError != QNetworkReply::NoError) {
        postReply->deleteLater();
        if (postError == QNetworkReply::SslHandshakeFailedError) {
            return AuthStatus::SslError;
        }
        return AuthStatus::NetworkError;
    }

    QUrl redirectUrl = postReply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    QUrl finalUrl = postReply->url();
    QString bodyStr = QString::fromUtf8(postReply->readAll());
    postReply->deleteLater();

    bool isSuccess = false;

    // Определение успешности перенаправления на домашнюю страницу
    if (!redirectUrl.isEmpty()) {
        QString redirectStr = redirectUrl.toString();
        if (!redirectStr.contains("login/index.php") && !redirectStr.contains("error")) {
            isSuccess = true;
        }
    } else {
        QString finalStr = finalUrl.toString();
        if (!finalStr.contains("login/index.php")) {
            isSuccess = true;
        }
    }

    // Проверка блокировки аккаунта
    if (bodyStr.contains("заблокирована") || bodyStr.contains("разблокировки") ||
        bodyStr.contains("locked") || bodyStr.contains("unlock")) {
        return AuthStatus::LockedAccount;
    }

    // Проверка наличия ошибки неверных учетных данных
    if (bodyStr.contains("loginerror") || bodyStr.contains("Неверный логин") || bodyStr.contains("Invalid login")) {
        isSuccess = false;
    }

    return isSuccess ? AuthStatus::Success : AuthStatus::InvalidCredentials;
}

QByteArray MainWindow::calculateFileSignature(const QByteArray& fileData, const QString& studentId) const {
    QByteArray key = (studentId + "VsuKeySalt").toUtf8();
    return QMessageAuthenticationCode::hash(fileData, key, QCryptographicHash::Sha256);
}

#endif
