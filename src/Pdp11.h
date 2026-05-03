#ifndef PDP11_H
#define PDP11_H

#include <cstdint>
#include <QString>
#include <array>
#include <functional>

/**
 * @brief Типы математических операций для корректного обновления флагов (NZVC).
 */
enum OpType { OP_LOGIC, OP_ADD, OP_SUB, OP_CMP, OP_OTHER };

/**
 * @brief Класс, эмулирующий работу центрального процессора и шины PDP-11.
 */
class Pdp11 {
public:
    // ==========================================
    // АРХИТЕКТУРА И СОСТОЯНИЕ ПРОЦЕССОРА
    // ==========================================

    /**
     * @brief Регистры общего назначения (R0-R5), Указатель стека (R6/SP), Счетчик команд (R7/PC).
     */
    std::array<uint16_t, 8> registers;

    /**
     * @brief Регистр состояния процессора (PSW/RS). Биты: 3=N (Negative), 2=Z (Zero), 1=V (Overflow), 0=C (Carry).
     */
    uint16_t processorStatusWord;

    /**
     * @brief Оперативная память: 64 КБ (32768 слов по 16 бит).
     */
    std::array<uint16_t, 32768> memory;

    // ==========================================
    // СОСТОЯНИЕ ДЛЯ ИНТЕРФЕЙСА
    // ==========================================

    QString currentInstructionName;
    QString currentInstructionType;
    QString currentInstructionFormat;
    bool isProcessorHalted = false;

    // ==========================================
    // БАЗОВЫЕ МЕТОДЫ ПРОЦЕССОРА
    // ==========================================

    Pdp11();
    void resetProcessor();
    void executeSingleStep();

    // ==========================================
    // ИНСТРУМЕНТЫ ДИЗАССЕМБЛЕРА
    // ==========================================

    QString disassemble(uint16_t instruction, uint16_t address);
    void getInstructionDetails(uint16_t instruction, uint16_t address, QString &name, QString &type, QString &format);
    QString getAddressModeString(uint8_t mode, uint8_t registerIndex, uint16_t indexValue = 0);

    // ==========================================
    // РАБОТА СО СТЕКОМ И ПРЕРЫВАНИЯМИ
    // ==========================================

    void pushToStack(uint16_t value);
    uint16_t popFromStack();
    void handleInterrupt(uint16_t vectorAddress);

    // ==========================================
    // ВНЕШНИЕ УСТРОЙСТВА (ВВОД-ВЫВОД)
    // ==========================================

    std::function<void(uint8_t)> charOutputCallback; /// Callback для Экрана (вывод в порт 177566)
    std::function<void(uint8_t)> printerOutputCallback; /// Callback для Принтера (вывод в порт 177516)

    uint16_t keyboardStatusRegister = 0; /// 177560 (Регистр состояния клавиатуры)
    uint16_t keyboardDataRegister = 0; /// 177562 (Регистр данных клавиатуры)

    /**
     * @brief Передача нажатой клавиши из графического интерфейса в процессор.
     */
    void provideKeyboardInput(uint8_t charCode);

    uint16_t readValue(uint16_t address, bool isByteOperation);
    void writeValue(uint16_t address, uint16_t value, bool isByteOperation);

private:
    // ==========================================
    // ВНУТРЕННЯЯ ЛОГИКА ИСПОЛНЕНИЯ
    // ==========================================

    void decodeAndExecute(uint16_t instruction);
    void updateConditionCodes(uint32_t result, bool isByteOperation, OpType type, uint16_t sourceValue = 0, uint16_t destinationValue = 0);
    uint16_t getEffectiveAddress(uint8_t mode, uint8_t registerIndex, bool isByteOperation);
};

#endif // PDP11_H
