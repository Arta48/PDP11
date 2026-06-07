#include "Pdp11.h"
#include <QStringList>

Pdp11::Pdp11() {
    resetProcessor();
}

void Pdp11::resetProcessor() {
    registers.fill(0);
    memory.fill(0);
    registers[7] = 001000; // PC (R7) устанавливаем на классический стартовый адрес
    processorStatusWord = 0;
    isProcessorHalted = false;

    // Сброс состояния внешних устройств
    keyboardStatusRegister = 0;
    keyboardDataRegister = 0;

    // Сброс графического дисплея
    displayX = 0;
    displayY = 0;
    displayColor = 0;
}

// ==========================================
// СИСТЕМНЫЕ МЕТОДЫ И РАБОТА С ПАМЯТЬЮ
// ==========================================

void Pdp11::pushToStack(uint16_t value) {
    uint16_t oldSP = registers[6];
    registers[6] -= 2; // Автодекремент SP (R6)
    writeValue(registers[6], value, false);

    // Если во время записи в стек произошла аппаратная ошибка (isProcessorHalted стал true),
    // откатываем SP назад в исходное состояние
    if (isProcessorHalted) {
        registers[6] = oldSP;
    }
}

uint16_t Pdp11::popFromStack() {
    uint16_t value = readValue(registers[6], false);

    // Если чтение из стека вызвало ошибку шины, НЕ инкрементируем SP и выходим
    if (isProcessorHalted) {
        return 0;
    }

    registers[6] += 2; // Автоинкремент SP (R6)
    return value;
}

void Pdp11::handleInterrupt(uint16_t vectorAddress) {
    // Тихо останавливаем ЦП при Double Fault, так как вызов окна уже произошел в writeValue/readValue
    if (registers[6] >= 0160000 || registers[6] < 2) {
        isProcessorHalted = true;
        return;
    }

    pushToStack(processorStatusWord); // Сохраняем текущий PSW (RS)
    if (isProcessorHalted) return;

    pushToStack(registers[7]); // Сохраняем текущий PC (R7)
    if (isProcessorHalted) return;

    registers[7] = readValue(vectorAddress, false); // Загружаем новый PC
    if (isProcessorHalted) return;

    // ВАЖНО: Защищаем PSW от загрузки не поддерживаемых битов приоритета и трассировки
    processorStatusWord = readValue(vectorAddress + 2, false) & 0x000F;
}

void Pdp11::provideKeyboardInput(uint8_t charCode) {
    keyboardDataRegister = charCode; // Записываем код клавиши в регистр данных
    keyboardStatusRegister |= 0200;     // Устанавливаем 7-й бит (Флаг готовности)

    // Если установлен 6-й бит (Разрешение прерывания), процессор должен бросить прерывание
    if (keyboardStatusRegister & 0100) {
        handleInterrupt(060); // Вектор прерывания клавиатуры (060)
    }
}

uint16_t Pdp11::readValue(uint16_t address, bool isByteOperation) {
    uint16_t targetAddress = address & 0177777; // Гарантируем адресацию в пределах 64 КБ

    // --- Внешние устройства (Memory-Mapped I/O) ---

    // 1. Экран дисплея (Регистр состояния TPS)
    if (targetAddress == 0177564) {
        return isByteOperation ? (0200 & 0xFF) : 0200; // Всегда возвращаем бит готовности
    }

    // 2. Устройство печати (Регистр состояния принтера)
    if (targetAddress == 0177514) {
        return isByteOperation ? (0200 & 0xFF) : 0200; // Всегда готов печатать
    }

    // 3. Клавиатура (Регистр состояния TKS)
    if (targetAddress == 0177560) {
        return isByteOperation ? (keyboardStatusRegister & 0xFF) : keyboardStatusRegister;
    }

    // 4. Клавиатура (Регистр данных TKB)
    if (targetAddress == 0177562) {
        // ВАЖНО: Читаем именно РЕГИСТР ДАННЫХ (keyboardDataRegister), а не статус!
        uint16_t data = keyboardDataRegister;
        keyboardStatusRegister &= ~0200; // Сбрасываем флаг готовности после успешного чтения
        return isByteOperation ? (data & 0xFF) : data;
    }

    // 5. Графический дисплей
    if (targetAddress == 0177570) {
        return isByteOperation ? (displayX & 0xFF) : displayX;
    }
    if (targetAddress == 0177572) {
        return isByteOperation ? (displayY & 0xFF) : displayY;
    }
    if (targetAddress == 0177574) {
        return isByteOperation ? (displayColor & 0xFF) : displayColor;
    }

    // Защита от обращения к несуществующему устройству на шине / невалидному MMIO (Bus Error)
    if (targetAddress >= 0160000) {
        if (errorCallback) {
            errorCallback(getLocalizedText("Ошибка в команде", "Error in command"));
        }
        handleInterrupt(000004); // Вектор прерывания 4 (Bus Error)
        isProcessorHalted = true;
        return 0;
    }

    // --- Оперативная память ---
    uint16_t word = memory[targetAddress / 2];

    if (isByteOperation) {
        // Little-Endian: младший байт по четному адресу, старший — по нечетному
        return (targetAddress % 2 == 0) ? (word & 0xFF) : (word >> 8);
    }

    return word;
}

void Pdp11::writeValue(uint16_t address, uint16_t value, bool isByteOperation) {
    uint16_t targetAddress = address & 0177777; // Гарантируем адресацию в пределах 64 КБ

    // --- Внешние устройства (Memory-Mapped I/O) ---

    // 1. Экран дисплея (Регистр данных TPB)
    if (targetAddress == 0177566) {
        if (charOutputCallback) {
            charOutputCallback(static_cast<uint8_t>(value & 0xFF));
        }
        return; // Запись в порт не попадает в физическую память
    }

    // 2. Устройство печати (Регистр данных принтера)
    if (targetAddress == 0177516) {
        if (printerOutputCallback) {
            printerOutputCallback(static_cast<uint8_t>(value & 0xFF));
        }
        return;
    }

    // 3. Клавиатура (Регистр состояния TKS)
    if (targetAddress == 0177560) {
        // Пользователю разрешено менять только 6-й бит (Разрешение прерываний)
        if (value & 0100) {
            keyboardStatusRegister |= 0100;
        } else {
            keyboardStatusRegister &= ~0100;
        }
        return;
    }

    // 4. Графический дисплей
    if (targetAddress == 0177570) {
        // Ограничиваем координату X текущим размером экрана
        displayX = value % SCREEN_SIZE;
        return;
    }
    if (targetAddress == 0177572) {
        // Ограничиваем координату Y текущим размером экрана
        displayY = value % SCREEN_SIZE;
        return;
    }
    if (targetAddress == 0177574) {
        displayColor = value & 0xFF; // Код цвета (0..15 для палитры)
        if (pixelOutputCallback) {
            pixelOutputCallback(static_cast<uint8_t>(displayX),
                                static_cast<uint8_t>(displayY),
                                static_cast<uint8_t>(displayColor));
        }
        return;
    }

    // Защита от нелегальной записи в область MMIO / невалидный адрес шины (Bus Error)
    if (targetAddress >= 0160000) {
        if (errorCallback) {
            errorCallback(getLocalizedText("Ошибка в команде", "Error in command"));
        }
        handleInterrupt(000004); // Вектор прерывания 4 (Bus Error)
        isProcessorHalted = true;
        return;
    }

    // --- Оперативная память ---
    uint16_t word = memory[targetAddress / 2];

    if (isByteOperation) {
        if (targetAddress % 2 == 0) {
            word = (word & 0xFF00) | (value & 0xFF); // Перезаписываем младший байт
        } else {
            word = (word & 0x00FF) | ((value & 0xFF) << 8); // Перезаписываем старший байт
        }
    } else {
        word = value; // Запись слова целиком
    }

    memory[targetAddress / 2] = word;
}

// ==========================================
// РЕЖИМЫ АДРЕСАЦИИ PDP-11 (8 режимов)
// ==========================================

uint16_t Pdp11::getEffectiveAddress(uint8_t mode, uint8_t registerIndex, bool isByteOperation) {
    uint16_t address = 0;
    // R6(SP) и R7(PC) всегда изменяются на 2 байта даже при байтовых операциях
    uint16_t step = (isByteOperation && registerIndex < 6) ? 1 : 2;

    switch (mode) {
        case 0: // Регистровый (Register)
            return 0;
        case 1: // Косвенно-регистровый (Register Deferred)
            return registers[registerIndex];
        case 2: // Автоинкрементный (Autoincrement)
            address = registers[registerIndex];
            registers[registerIndex] += step;
            return address;
        case 3: // Косвенно-автоинкрементный (Autoincrement Deferred)
            address = memory[(registers[registerIndex] & 0177777) / 2];
            registers[registerIndex] += 2;
            return address;
        case 4: // Автодекрементный (Autodecrement)
            registers[registerIndex] -= step;
            return registers[registerIndex];
        case 5: // Косвенно-автодекрементный (Autodecrement Deferred)
            registers[registerIndex] -= 2;
            return memory[(registers[registerIndex] & 0177777) / 2];
        case 6: { // Индексный (Index) / Относительный
            // ВАЖНО: PC должен быть увеличен ДО сложения с базовым регистром,
            // чтобы X(PC) вычислял смещение от уже обновленного счетчика команд!
            uint16_t indexWord = memory[(registers[7] & 0177777) / 2];
            registers[7] += 2;
            return registers[registerIndex] + indexWord;
        }
        case 7: { // Косвенно-индексный (Index Deferred)
            uint16_t indexWord = memory[(registers[7] & 0177777) / 2];
            registers[7] += 2;
            uint16_t indexAddress = registers[registerIndex] + indexWord;
            return memory[(indexAddress & 0177777) / 2];
        }
    }
    return 0;
}

// ==========================================
// ЦИКЛ ИСПОЛНЕНИЯ И ДЕКОДИРОВАНИЯ
// ==========================================

void Pdp11::executeSingleStep() {
    uint16_t programCounterAddress = registers[7];
    if (programCounterAddress / 2 >= memory.size()) return;

    // Fetch (Извлечение команды)
    uint16_t instruction = memory[programCounterAddress / 2];
    registers[7] += 2;

    decodeAndExecute(instruction);
}

void Pdp11::decodeAndExecute(uint16_t instruction) {
    bool isByteOperation = (instruction & 0100000) != 0; // 15-й бит (байтовый флаг)

    // Команда SUB (16xxxx) - это исключение! У нее 15-й бит равен 1,
    // но это операция со словом (команды SUBB не существует).
    uint16_t fullOpcode = (instruction >> 12) & 017;
    if (fullOpcode == 016) {
        isByteOperation = false;
    }

    uint16_t statusWord = processorStatusWord;

    // Извлечение текущих флагов для ветвлений
    bool isNegative = (statusWord & 8); // N
    bool isZero = (statusWord & 4); // Z
    bool isOverflow = (statusWord & 2); // V
    bool isCarry = (statusWord & 1); // C

    // 1. Останов (HALT)
    if (instruction == 0) {
        isProcessorHalted = true; return;
    }

    // 2. Команды изменения признаков PSW (000240 - 000277)
    if ((instruction & 0177740) == 000240) {
        if (instruction & 020) {
            processorStatusWord |= (instruction & 017);  // Set
        } else {
            processorStatusWord &= ~(instruction & 017); // Clear
        }
        return;
    }

    // 3. Команды условного ветвления (Branch)
    uint16_t branchOpcode = instruction & 0177400;
    if (branchOpcode == 000400 || branchOpcode == 001000 || branchOpcode == 001400 ||
        branchOpcode == 002000 || branchOpcode == 002400 || branchOpcode == 003000 ||
        branchOpcode == 003400 || branchOpcode == 0100000 || branchOpcode == 0100400 ||
        branchOpcode == 0101000 || branchOpcode == 0101400 || branchOpcode == 0102000 ||
        branchOpcode == 0102400 || branchOpcode == 0103000 || branchOpcode == 0103400) {

        int8_t offset = (int8_t)(instruction & 0377); // Знаковое смещение
        bool takeBranch = false;

        if (branchOpcode == 000400) takeBranch = true; // BR
        else if (branchOpcode == 001000) takeBranch = !isZero; // BNE
        else if (branchOpcode == 001400) takeBranch = isZero; // BEQ
        else if (branchOpcode == 0100000) takeBranch = !isNegative; // BPL
        else if (branchOpcode == 0100400) takeBranch = isNegative; // BMI
        else if (branchOpcode == 0102000) takeBranch = !isOverflow; // BVC
        else if (branchOpcode == 0102400) takeBranch = isOverflow; // BVS
        else if (branchOpcode == 0103000) takeBranch = !isCarry; // BCC / BHIS
        else if (branchOpcode == 0103400) takeBranch = isCarry; // BCS / BLO
        else if (branchOpcode == 002000) takeBranch = (isNegative == isOverflow); // BGE
        else if (branchOpcode == 002400) takeBranch = (isNegative != isOverflow); // BLT
        else if (branchOpcode == 003000) takeBranch = (!isZero && (isNegative == isOverflow)); // BGT
        else if (branchOpcode == 003400) takeBranch = (isZero || (isNegative != isOverflow)); // BLE
        else if (branchOpcode == 0101000) takeBranch = (!isCarry && !isZero); // BHI
        else if (branchOpcode == 0101400) takeBranch = (isCarry || isZero); // BLOS

        if (takeBranch) {
            registers[7] += (offset * 2);
        }
        return;
    }

    // 4. Переходы и подпрограммы (JMP, JSR, RTS)
    if ((instruction & 0177700) == 000100) { // JMP
        uint8_t destinationMode = (instruction >> 3) & 07;
        if (destinationMode == 0) {
            handleInterrupt(000010); // JMP с регистровой адресацией — запрещенная команда
            return;
        }
        uint16_t destinationAddress = getEffectiveAddress(destinationMode, instruction & 07, false);
        if (isProcessorHalted) return; // <-- Аборт при ошибке адресации
        registers[7] = destinationAddress;
        return;
    }
    if ((instruction & 0177000) == 0004000) { // JSR
        uint8_t destinationMode = (instruction >> 3) & 07;
        if (destinationMode == 0) {
            handleInterrupt(000010); // JSR с регистровой адресацией — запрещенная команда
            return;
        }
        uint8_t registerIndex = (instruction >> 6) & 07;
        uint16_t destinationAddress = getEffectiveAddress(destinationMode, instruction & 07, false);
        if (isProcessorHalted) return; // <-- Аборт при ошибке адресации
        pushToStack(registers[registerIndex]);
        if (isProcessorHalted) return; // <-- КРИТИЧЕСКИЙ АБОРТ: если стек сломался, выходим без изменения PC и регистров!
        registers[registerIndex] = registers[7];
        registers[7] = destinationAddress;
        return;
    }
    if ((instruction & 0177770) == 0000200) { // RTS
        uint8_t registerIndex = instruction & 07;
        uint16_t poppedValue = popFromStack();
        if (isProcessorHalted) return; // <-- Аборт при чтении из невалидного стека
        registers[7] = registers[registerIndex];
        registers[registerIndex] = poppedValue;
        return;
    }

    // 5. SOB (Subtract One and Branch)
    if ((instruction & 0177000) == 0077000) {
        uint8_t registerIndex = (instruction >> 6) & 07;
        uint16_t oldValue = registers[registerIndex];
        registers[registerIndex]--;

        if (registers[registerIndex] != 0) {
            registers[7] -= (instruction & 077) * 2;
        }
        return;
    }

    // 6. Прерывания (RTI, EMT, TRAP и системные команды)
    if (instruction == 000001) return; // WAIT: В базовом эмуляторе работает как NOP

    if (instruction == 000005) {       // RESET: Сброс флагов готовности внешних устройств
        keyboardStatusRegister &= ~0200;
        return;
    }

    if (instruction == 000002 || instruction == 000006) { // RTI / RTT
        uint16_t newPC = popFromStack();
        if (isProcessorHalted) return; // <-- Аборт
        uint16_t newPSW = popFromStack();
        if (isProcessorHalted) return; // <-- Аборт

        registers[7] = newPC;
        // ВАЖНО: Аппаратно отсекаем мусор, оставляем только флаги NZVC (биты 0-3)
        processorStatusWord = newPSW & 0x000F;
        return;
    }

    if (instruction == 000003) { handleInterrupt(014); return; } // BPT
    if (instruction == 000004) { handleInterrupt(020); return; } // IOT
    if ((instruction & 0177400) == 0104000) { handleInterrupt(030); return; } // EMT
    if ((instruction & 0177400) == 0104400) { handleInterrupt(034); return; } // TRAP

    // 7. Двухадресные команды (MOV, CMP, ADD, SUB, BIT, BIC, BIS)
    uint16_t doubleOperandOpcode = fullOpcode & 07;

    // Проверяем диапазон 1-6 или команду SUB (16)
    if ((doubleOperandOpcode >= 1 && doubleOperandOpcode <= 6) || fullOpcode == 016) {
        uint8_t sourceMode = (instruction >> 9) & 07;
        uint8_t sourceRegisterIndex = (instruction >> 6) & 07;
        uint8_t destinationMode = (instruction >> 3) & 07;
        uint8_t destinationRegisterIndex = instruction & 07;

        uint16_t sourceValue = (sourceMode == 0) ? registers[sourceRegisterIndex] : readValue(getEffectiveAddress(sourceMode, sourceRegisterIndex, isByteOperation), isByteOperation);
        if (isProcessorHalted) return; // <-- Аборт при ошибке чтения источника

        // ВАЖНО: Если источник — регистр и операция байтовая, берем только младший байт (отсекаем мусор)
        if (isByteOperation && sourceMode == 0) {
            sourceValue &= 0xFF;
        }

        uint16_t destinationAddress = (destinationMode == 0) ? 0 : getEffectiveAddress(destinationMode, destinationRegisterIndex, isByteOperation);
        if (isProcessorHalted) return; // <-- Аборт при ошибке вычисления адреса приемника

        uint16_t destinationValue = 0;
        // ВАЖНО: Команды MOV и MOVB не должны читать значение приемника (чтобы избежать побочных эффектов I/O)
        if (doubleOperandOpcode != 01) {
            destinationValue = (destinationMode == 0) ? registers[destinationRegisterIndex] : readValue(destinationAddress, isByteOperation);

            // Если приемник — регистр и операция байтовая, отсекаем мусор старшего байта
            if (isByteOperation && destinationMode == 0) {
                destinationValue &= 0xFF;
            }
        }

        uint32_t result = 0;
        bool writeBack = true;
        OpType operationType = OP_LOGIC;

        // Сначала строго проверяем SUB, так как 16 & 7 = 6, и иначе она попадет в блок ADD!
        if (fullOpcode == 016) { // SUB
            result = (uint32_t) destinationValue - sourceValue;
            operationType = OP_SUB;
        } else if (doubleOperandOpcode == 01) { // MOV / MOVB
            result = sourceValue;
            // Специальное правило PDP-11: MOVB в регистр расширяет знак байта на всё слово
            if (isByteOperation && destinationMode == 0) {
                if (result & 0x80) result |= 0xFF00;
                else result &= 0x00FF;
            }
        } else if (doubleOperandOpcode == 02) { // CMP
            result = (uint32_t) sourceValue - destinationValue;
            writeBack = false;
            operationType = OP_CMP;
        } else if (doubleOperandOpcode == 03) { // BIT
            result = sourceValue & destinationValue;
            writeBack = false;
        } else if (doubleOperandOpcode == 04) { // BIC
            result = destinationValue & ~sourceValue;
        } else if (doubleOperandOpcode == 05) { // BIS
            result = destinationValue | sourceValue;
        } else if (doubleOperandOpcode == 06) { // ADD
            result = (uint32_t) sourceValue + destinationValue;
            operationType = OP_ADD;
        }

        if (writeBack) {
            if (destinationMode == 0) {
                // Если это MOVB в регистр (opcode 01 + байт), пишем результат целиком (со знаком)
                // Для остальных байтовых операций в регистр (BICB, BISB и т.д.) меняется только младший байт
                if (isByteOperation && doubleOperandOpcode != 01) {
                    registers[destinationRegisterIndex] = (registers[destinationRegisterIndex] & 0xFF00) | (result & 0xFF);
                } else {
                    registers[destinationRegisterIndex] = (uint16_t) result;
                }
            } else {
                writeValue(destinationAddress, (uint16_t) result, isByteOperation);
            }
        }
        updateConditionCodes(result, isByteOperation, operationType, sourceValue, destinationValue);
        return;
    }

    // 8. XOR (Exclusive OR)
    if ((instruction & 0177000) == 0074000) {
        uint8_t sourceRegisterIndex = (instruction >> 6) & 07;
        uint8_t destinationMode = (instruction >> 3) & 07;
        uint8_t destinationRegisterIndex = instruction & 07;

        uint16_t sourceValue = registers[sourceRegisterIndex];
        uint16_t destinationAddress = 0, destinationValue = 0;

        if (destinationMode == 0) {
            destinationValue = registers[destinationRegisterIndex];
        } else {
            destinationAddress = getEffectiveAddress(destinationMode, destinationRegisterIndex, false);
            destinationValue = readValue(destinationAddress, false);
        }

        uint16_t result = sourceValue ^ destinationValue;

        if (destinationMode == 0) {
            registers[destinationRegisterIndex] = result;
        } else {
            writeValue(destinationAddress, result, false);
        }

        updateConditionCodes(result, false, OP_LOGIC);
        return;
    }

    // 9. Одноадресные команды (CLR, COM, INC, DEC, NEG, ADC, SBC, TST, сдвиги)
    uint16_t singleOpcodeMask = instruction & 0077700;
    uint16_t baseOpcode = (instruction >> 6) & 0077;

    if (singleOpcodeMask == (baseOpcode << 6) && baseOpcode >= 0050 && baseOpcode <= 0063) {
        uint8_t destinationMode = (instruction >> 3) & 07;
        uint8_t destinationRegisterIndex = instruction & 07;

        uint16_t destinationAddress = (destinationMode == 0) ? 0 : getEffectiveAddress(destinationMode, destinationRegisterIndex, isByteOperation);
        if (isProcessorHalted) return; // <-- Аборт при ошибке вычисления адреса приемника
        uint16_t currentValue = (destinationMode == 0) ? registers[destinationRegisterIndex] : readValue(destinationAddress, isByteOperation);
        if (isByteOperation && destinationMode == 0) currentValue &= 0xFF;

        uint32_t result = 0;
        bool writeBack = true;
        OpType operationType = OP_OTHER;

        uint16_t oldCarry = (processorStatusWord & 1); // Сохраняем текущий флаг C
        uint16_t newCarryForShift = oldCarry;          // Временный C для сдвигов

        switch (baseOpcode) {
            case 0050: result = 0; operationType = OP_CLR; break; // CLR
            case 0051: result = ~currentValue; operationType = OP_COM; break; // COM
            case 0052: result = (uint32_t) currentValue + 1; operationType = OP_INC; break; // INC
            case 0053: result = (uint32_t) currentValue - 1; operationType = OP_DEC; break; // DEC
            case 0054: result = (uint32_t) 0 - currentValue; operationType = OP_NEG; break; // NEG
            case 0055: result = (uint32_t) currentValue + oldCarry; operationType = OP_ADC; break; // ADC
            case 0056: result = (uint32_t) currentValue - oldCarry; operationType = OP_SBC; break; // SBC
            case 0057: result = currentValue; operationType = OP_CLR; writeBack = false; break; // TST
            case 0060: // ROR (Rotate Right)
                result = (currentValue >> 1) | (oldCarry ? (isByteOperation ? 0x80 : 0x8000) : 0);
                newCarryForShift = (currentValue & 1) ? 1 : 0;
                operationType = OP_SHIFT;
                break;
            case 0061: // ROL (Rotate Left)
                result = (currentValue << 1) | (oldCarry ? 1 : 0);
                newCarryForShift = (currentValue & (isByteOperation ? 0x80 : 0x8000)) ? 1 : 0;
                operationType = OP_SHIFT;
                break;
            case 0062: // ASR (Arithmetic Shift Right)
                result = (currentValue >> 1) | (currentValue & (isByteOperation ? 0x80 : 0x8000));
                newCarryForShift = (currentValue & 1) ? 1 : 0;
                operationType = OP_SHIFT;
                break;
            case 0063: // ASL (Arithmetic Shift Left)
                result = (currentValue << 1);
                newCarryForShift = (currentValue & (isByteOperation ? 0x80 : 0x8000)) ? 1 : 0;
                operationType = OP_SHIFT;
                break;
        }

        if (writeBack) {
            if (destinationMode == 0) {
                if (isByteOperation) {
                    registers[destinationRegisterIndex] = (registers[destinationRegisterIndex] & 0xFF00) | (result & 0xFF);
                } else {
                    registers[destinationRegisterIndex] = (uint16_t) result;
                }
            } else {
                writeValue(destinationAddress, (uint16_t) result, isByteOperation);
            }
        }

        // Если это сдвиг, безопасно применяем вычисленный флаг C до вызова функции флагов
        if (operationType == OP_SHIFT) {
            processorStatusWord = (processorStatusWord & ~1) | newCarryForShift;
        }

        updateConditionCodes(result, isByteOperation, operationType, currentValue, 0);
        return;
    }

    // Если код сюда дошел — команда точно неизвестна
    handleInterrupt(000010);
}

// ==========================================
// УПРАВЛЕНИЕ ФЛАГАМИ СОСТОЯНИЯ ПРОЦЕССОРА
// ==========================================

void Pdp11::updateConditionCodes(uint32_t result, bool isByteOperation, OpType type, uint16_t sourceValue, uint16_t destinationValue) {
    uint16_t mask = isByteOperation ? 0xFF : 0xFFFF;
    uint16_t signBit = isByteOperation ? 0x80 : 0x8000;
    uint16_t result16 = (uint16_t) (result & mask);

    uint16_t oldCarry = processorStatusWord & 1; // Сохраняем старый Carry

    // ВАЖНО: захватываем текущий Carry, т.к. для команд сдвига он уже был вычислен и установлен
    uint16_t currentCarry = processorStatusWord & 1;

    processorStatusWord &= 0xFFF0; // Очищаем старые N, Z, V, C

    if (result16 == 0) processorStatusWord |= 4; // Z
    if (result16 & signBit) processorStatusWord |= 8; // N

    if (type == OP_ADD) {
        if (result > mask) processorStatusWord |= 1; // C
        if (!((sourceValue ^ destinationValue) & signBit) && ((sourceValue ^ result16) & signBit)) processorStatusWord |= 2; // V
    } else if (type == OP_CMP) {
        if (sourceValue < destinationValue) processorStatusWord |= 1; // C
        if (((sourceValue ^ destinationValue) & signBit) && ((sourceValue ^ result16) & signBit)) processorStatusWord |= 2; // V
    } else if (type == OP_SUB) {
        if (destinationValue < sourceValue) processorStatusWord |= 1; // C
        if (((destinationValue ^ sourceValue) & signBit) && ((destinationValue ^ result16) & signBit)) processorStatusWord |= 2; // V
    } else if (type == OP_INC) {
        if ((isByteOperation && sourceValue == 0177) || (!isByteOperation && sourceValue == 077777)) processorStatusWord |= 2; // V
        processorStatusWord |= oldCarry; // C не меняется
    } else if (type == OP_DEC) {
        if ((isByteOperation && sourceValue == 0200) || (!isByteOperation && sourceValue == 0100000)) processorStatusWord |= 2; // V
        processorStatusWord |= oldCarry; // C не меняется
    } else if (type == OP_NEG) {
        if ((isByteOperation && sourceValue == 0200) || (!isByteOperation && sourceValue == 0100000)) processorStatusWord |= 2; // V
        if (result16 != 0) processorStatusWord |= 1; // C устанавливается для всех чисел кроме нуля
    } else if (type == OP_ADC) {
        // ADC: C устанавливается только если (DST) был 177777 и C был 1, иначе сбрасывается
        if (oldCarry && sourceValue == mask) processorStatusWord |= 1; // C
        // V устанавливается, если (DST) был 077777 и C был 1
        if (oldCarry && sourceValue == (isByteOperation ? 0177 : 077777)) processorStatusWord |= 2; // V
    } else if (type == OP_SBC) {
        // SBC: C СБРАСЫВАЕТСЯ, только если (DST) был 0 и C был 1, иначе УСТАНАВЛИВАЕТСЯ
        if (!(oldCarry && sourceValue == 0)) processorStatusWord |= 1; // C
        // V устанавливается, если (DST) был 100000 и C был 1
        if (oldCarry && sourceValue == signBit) processorStatusWord |= 2; // V
    } else if (type == OP_COM) {
        processorStatusWord |= 1; // COM всегда устанавливает C = 1, V = 0
    } else if (type == OP_CLR) {
        // OP_CLR (используется для CLR и TST)
        // Для CLR: N = 0, Z = 1 (т.к. result = 0), C = 0, V = 0.
        // Для TST: N и Z зависят от результата, C = 0, V = 0.
        // Маска 0xFFF0 в начале функции уже успешно сбросила C и V в 0.
    } else if (type == OP_SHIFT) {
        processorStatusWord |= oldCarry; // Восстанавливаем C, вычисленный самой командой сдвига
        bool n = (processorStatusWord & 8) != 0;
        bool c = (processorStatusWord & 1) != 0;
        if (n ^ c) processorStatusWord |= 2; // Для сдвигов V = N ^ C
    } else if (type == OP_LOGIC || type == OP_OTHER) {
        processorStatusWord |= oldCarry; // Логические операции C не меняют, V = 0
    }
}

// ==========================================
// ДИЗАССЕМБЛЕР (ФОРМИРОВАНИЕ СТРОК)
// ==========================================

QString Pdp11::disassemble(uint16_t instruction, uint16_t address) {
    bool isByteOperation = (instruction & 0100000) != 0;
    QString byteSuffix = isByteOperation ? "B" : "";

    // 1. Прерывания
    uint16_t interruptMask = instruction & 0177400;
    if ((interruptMask == 0104000) || (interruptMask == 0104400)) {
        switch (interruptMask) {
            case 0104000: return "EMT";
            case 0104400: return "TRAP";
        }
    } else if (instruction >= 0 && instruction <= 000006) {
        switch (instruction) {
            case 000000: return "HALT";
            case 000001: return "WAIT";
            case 000002: return "RTI";
            case 000003: return "BPT";
            case 000004: return "IOT";
            case 000005: return "RESET";
            case 000006: return "RTT";
        }
    }

    // 2. Флаги
    uint16_t conditionMask = instruction & 0177740;
    if (conditionMask == 000240) {
        switch (instruction) {
            case 000240: return "NOP";
            case 000241: return "CLC";
            case 000242: return "CLV";
            case 000244: return "CLZ";
            case 000250: return "CLN";
            case 000257: return "CCC";
            case 000261: return "SEC";
            case 000262: return "SEV";
            case 000264: return "SEZ";
            case 000270: return "SEN";
            case 000277: return "SCC";
        }
    }

    uint16_t currentWordOffset = 2;
    auto resolveOperandText = [&](uint8_t mode, uint8_t registerIndex, uint16_t &offset) {
        uint16_t indexValue = 0;
        bool needsAdditionalWord = (mode >= 6) || (registerIndex == 7 && (mode == 2 || mode == 3));
        if (needsAdditionalWord) {
            indexValue = memory[(address + currentWordOffset) / 2];
            offset += 2;
        }
        return getAddressModeString(mode, registerIndex, indexValue);
    };

    // 3. Ветвления
    uint16_t branchMask = instruction & 0177400;
    if (branchMask == 000400 || branchMask == 001000 || branchMask == 001400 ||
        branchMask == 002000 || branchMask == 002400 || branchMask == 003000 ||
        branchMask == 003400 || branchMask == 0100000 || branchMask == 0100400 ||
        branchMask == 0101000 || branchMask == 0101400 || branchMask == 0102000 ||
        branchMask == 0102400 || branchMask == 0103000 || branchMask == 0103400) {

        QString instructionName;
        switch (branchMask) {
            case 000400: instructionName = "BR"; break;
            case 001000: instructionName = "BNE"; break;
            case 001400: instructionName = "BEQ"; break;
            case 002000: instructionName = "BGE"; break;
            case 002400: instructionName = "BLT"; break;
            case 003000: instructionName = "BGT"; break;
            case 003400: instructionName = "BLE"; break;
            case 0100000: instructionName = "BPL"; break;
            case 0100400: instructionName = "BMI"; break;
            case 0101000: instructionName = "BHI"; break;
            case 0101400: instructionName = "BLOS"; break;
            case 0102000: instructionName = "BVC"; break;
            case 0102400: instructionName = "BVS"; break;
            case 0103000: instructionName = "BCC"; break;
            case 0103400: instructionName = "BCS"; break;
        }
        int8_t offset = static_cast<int8_t>(instruction & 0377);

        // Оставляем только отсечение до 16 бит для корректной работы отрицательных смещений
        uint16_t targetAddress = (address + 2 + (offset * 2)) & 0177777;

        // Возвращаем оригинальное форматирование без ведущих нулей
        return QString("%1 %2").arg(instructionName).arg(QString::number(targetAddress, 8));
    }

    // 4. Специальные
    if ((instruction & 0177700) == 000100) {
        QString destinationOperand = resolveOperandText((instruction >> 3) & 07, instruction & 07, currentWordOffset);
        return QString("JMP %1").arg(destinationOperand);
    }
    if ((instruction & 0177000) == 004000) {
        QString registerName = QString("R%1").arg((instruction >> 6) & 07);
        QString destinationOperand = resolveOperandText((instruction >> 3) & 07, instruction & 07, currentWordOffset);
        return QString("JSR %1, %2").arg(registerName, destinationOperand);
    }
    if ((instruction & 0177770) == 000200) {
        return QString("RTS R%1").arg(instruction & 07);
    }
    if ((instruction & 0177000) == 077000) {
        uint8_t registerIndex = (instruction >> 6) & 07;
        uint16_t offset = (instruction & 077) * 2;

        uint16_t targetAddress = (address + 2 - offset) & 0177777;

        // Возвращаем оригинальное форматирование без ведущих нулей
        return QString("SOB R%1, %2").arg(registerIndex).arg(QString::number(targetAddress, 8));
    }
    if ((instruction & 0177000) == 074000) {
        QString registerName = QString("R%1").arg((instruction >> 6) & 07);
        QString destinationOperand = resolveOperandText((instruction >> 3) & 07, instruction & 07, currentWordOffset);
        return QString("XOR %1, %2").arg(registerName, destinationOperand);
    }

    // 5. Двухадресные
    uint16_t fullOpcode = (instruction >> 12) & 017;
    uint16_t doubleOpcode = (instruction >> 12) & 07;
    if ((doubleOpcode >= 01 && doubleOpcode <= 06) || fullOpcode == 016) {
        QString instructionName;

        if (fullOpcode == 016) {
            instructionName = "SUB";
        } else {
            switch (doubleOpcode) {
                case 001: instructionName = "MOV"; break;
                case 002: instructionName = "CMP"; break;
                case 003: instructionName = "BIT"; break;
                case 004: instructionName = "BIC"; break;
                case 005: instructionName = "BIS"; break;
                case 006: instructionName = "ADD"; break;
            }
        }

        // ADD и SUB не имеют байтовых аналогов (ADDB / SUBB не существует)
        QString finalName = (fullOpcode == 016 || doubleOpcode == 006) ? instructionName : instructionName + byteSuffix;

        QString sourceOperand = resolveOperandText((instruction >> 9) & 07, (instruction >> 6) & 07, currentWordOffset);
        QString destinationOperand = resolveOperandText((instruction >> 3) & 07, instruction & 07, currentWordOffset);

        return QString("%1 %2, %3").arg(finalName, sourceOperand, destinationOperand);
    }

    // 6. Одноадресные
    uint16_t singleOpcode = (instruction >> 6) & 0777;
    uint16_t baseOpcode = singleOpcode & 0077;
    if (baseOpcode >= 0050 && baseOpcode <= 0063) {
        QString instructionName;
        switch (baseOpcode) {
            case 0050: instructionName = "CLR"; break;
            case 0051: instructionName = "COM"; break;
            case 0052: instructionName = "INC"; break;
            case 0053: instructionName = "DEC"; break;
            case 0054: instructionName = "NEG"; break;
            case 0055: instructionName = "ADC"; break;
            case 0056: instructionName = "SBC"; break;
            case 0057: instructionName = "TST"; break;
            case 0060: instructionName = "ROR"; break;
            case 0061: instructionName = "ROL"; break;
            case 0062: instructionName = "ASR"; break;
            case 0063: instructionName = "ASL"; break;
        }
        QString destinationOperand = resolveOperandText((instruction >> 3) & 07, instruction & 07, currentWordOffset);
        return QString("%1%2 %3").arg(instructionName, byteSuffix, destinationOperand);
    }

    return "UNKNOWN";
}

void Pdp11::getInstructionDetails(uint16_t instruction, uint16_t address, QString &name, QString &type, QString &format) {
    format = QString::number(instruction, 8).rightJustified(6, '0');
    bool isByteOperation = (instruction & 0100000) != 0;

    uint16_t interruptMask = instruction & 0177400;
    if ((interruptMask == 0104000) || (interruptMask == 0104400)) {
        format = QString::number(interruptMask, 8);
        type = getLocalizedText("Безадресная команда", "No-address command");
        switch (interruptMask) {
            case 0104000: name = getLocalizedText("Командное прерывание для системных программ", "Command interrupt for system programs"); break;
            case 0104400: name = getLocalizedText("Командное прерывание", "Command interrupt"); break;
        }
        return;
    } else if (instruction >= 0 && instruction <= 000006) {
        type = getLocalizedText("Безадресная команда", "No-address command");
        switch (instruction) {
            case 000000: name = getLocalizedText("Останов", "Halt"); break;
            case 000001: name = getLocalizedText("Ожидание", "Wait"); break;
            case 000002: name = getLocalizedText("Возврат из прерывания", "Return from interrupt"); break;
            case 000003: name = getLocalizedText("Командное прерывание для отладки", "Command interrupt for debugging"); break;
            case 000004: name = getLocalizedText("Командное прерывание для ввода-вывода", "Command interrupt for I/O"); break;
            case 000005: name = getLocalizedText("Сброс внешних устройств", "Reset external devices"); break;
            case 000006: name = getLocalizedText("Возврат из прерывания", "Return from interrupt"); break;
        }
        return;
    }

    uint16_t conditionMask = instruction & 0177740;
    if (conditionMask == 000240) {
        type = getLocalizedText("Безадресная команда", "No-address command");
        switch (instruction) {
            case 000240: name = getLocalizedText("Нет операции", "No operation"); break;
            case 000241: name = getLocalizedText("Очистка C", "Clear C"); break;
            case 000242: name = getLocalizedText("Очистка V", "Clear V"); break;
            case 000244: name = getLocalizedText("Очистка Z", "Clear Z"); break;
            case 000250: name = getLocalizedText("Очистка N", "Clear N"); break;
            case 000257: name = getLocalizedText("Очистка всех разрядов", "Clear all CC"); break;
            case 000261: name = getLocalizedText("Установка C", "Set C"); break;
            case 000262: name = getLocalizedText("Установка V", "Set V"); break;
            case 000264: name = getLocalizedText("Установка Z", "Set Z"); break;
            case 000270: name = getLocalizedText("Установка N", "Set N"); break;
            case 000277: name = getLocalizedText("Установка всех разрядов", "Set all CC"); break;
        }
        return;
    }

    uint16_t branchMask = instruction & 0177400;
    if (branchMask == 000400 || branchMask == 001000 || branchMask == 001400 ||
        branchMask == 002000 || branchMask == 002400 || branchMask == 003000 ||
        branchMask == 003400 || branchMask == 0100000 || branchMask == 0100400 ||
        branchMask == 0101000 || branchMask == 0101400 || branchMask == 0102000 ||
        branchMask == 0102400 || branchMask == 0103000 || branchMask == 0103400) {

        type = getLocalizedText("Команда ветвления", "Branch command");
        switch (branchMask) {
            case 000400: name = getLocalizedText("Ветвление безусловное", "Unconditional branch"); break;
            case 001000: name = getLocalizedText("Ветвление, если не равно", "Branch if not equal"); break;
            case 001400: name = getLocalizedText("Ветвление, если равно", "Branch if equal"); break;
            case 002000: name = getLocalizedText("Ветвление, если больше или равно", "Branch if greater or equal"); break;
            case 002400: name = getLocalizedText("Ветвление, если меньше", "Branch if less"); break;
            case 003000: name = getLocalizedText("Ветвление, если больше", "Branch if greater"); break;
            case 003400: name = getLocalizedText("Ветвление, если меньше или равно", "Branch if less or equal"); break;
            case 0100000: name = getLocalizedText("Ветвление, если плюс", "Branch if plus"); break;
            case 0100400: name = getLocalizedText("Ветвление, если минус", "Branch if minus"); break;
            case 0101000: name = getLocalizedText("Ветвление, если больше", "Branch if higher"); break;
            case 0101400: name = getLocalizedText("Ветвление, если меньше или равно", "Branch if lower or same"); break;
            case 0102000: name = getLocalizedText("Ветвление, если нет арифметического переполнения", "Branch if overflow clear"); break;
            case 0102400: name = getLocalizedText("Ветвление, если есть арифметическое переполнение", "Branch if overflow set"); break;
            case 0103000: name = getLocalizedText("Ветвление, если нет переноса", "Branch if carry clear"); break;
            case 0103400: name = getLocalizedText("Ветвление, если перенос", "Branch if carry set"); break;
        }
        format = QString::number(branchMask, 8).rightJustified(6, '0') + "+XXX";
        return;
    }

    uint16_t jmpMask = instruction & 0177700;
    uint16_t jsrMask = instruction & 0177000;
    uint16_t rtsMask = instruction & 0177770;
    uint16_t sobMask = instruction & 0177000;

    if (jmpMask == 000100 || jsrMask == 004000 || rtsMask == 000200 || sobMask == 077000) {
        if (jmpMask == 000100) {
            type = getLocalizedText("Одноадресная команда", "One-address command");
            name = getLocalizedText("Безусловный переход", "Unconditional jump");
            format = "0001DD";
        } else if (jsrMask == 004000) {
            type = getLocalizedText("Одноадресная команда с дополнительным регистром", "One-address command with extra register");
            name = getLocalizedText("Обращение к подпрограмме", "Call subroutine");
            format = "004RDD";
        } else if (rtsMask == 000200) {
            type = getLocalizedText("Возврат из подпрограммы", "Return from subroutine");
            name = getLocalizedText("Возврат из подпрограммы", "Return from subroutine");
            format = "00020R";
        } else if (sobMask == 077000) {
            type = getLocalizedText("Вычитание единицы и ветвление", "Decrement and branch");
            name = getLocalizedText("Вычитание единицы и ветвление", "Decrement and branch");
            format = "077RNN";
        }
        return;
    }

    uint16_t fullDoubleMask = (instruction >> 12) & 017;
    uint16_t doubleMask = (instruction >> 12) & 07;
    uint16_t xorMask = instruction & 0177000;

    if ((doubleMask >= 1 && doubleMask <= 6) || fullDoubleMask == 016 || xorMask == 0074000) {
        if (xorMask == 0074000) {
            type = getLocalizedText("Одноадресная команда с дополнительным регистром", "One-address command with extra register");
            name = getLocalizedText("Исключающее ИЛИ", "Exclusive OR");
            format = "074RDD";
        } else if (fullDoubleMask == 016) {
            type = getLocalizedText("Двухадресная команда", "Two-address command");
            name = getLocalizedText("Вычитание", "Subtraction");
            format = "16SSDD";
        } else if (doubleMask == 06) {
            type = getLocalizedText("Двухадресная команда", "Two-address command");
            name = getLocalizedText("Сложение", "Addition");
            format = "06SSDD";
        } else {
            type = getLocalizedText("Двухадресная команда", "Two-address command");
            switch (doubleMask) {
                case 1: name = getLocalizedText("Пересылка", "Move"); break;
                case 2: name = getLocalizedText("Сравнение", "Compare"); break;
                case 3: name = getLocalizedText("Проверка разрядов", "Bit test"); break;
                case 4: name = getLocalizedText("Очистка разрядов", "Bit clear"); break;
                case 5: name = getLocalizedText("Логическое сложение", "Bit set"); break;
            }
            QString prefix = isByteOperation ? "1" : "0";
            format = prefix + QString::number(doubleMask, 8) + "SSDD";
        }
        return;
    }

    uint16_t singleMask = (instruction >> 6) & 0777;
    uint16_t baseMask = singleMask & 0077;
    if (baseMask >= 0050 && baseMask <= 0063) {
        type = getLocalizedText("Одноадресная команда", "One-address command");
        switch (baseMask) {
            case 0050: name = getLocalizedText("Очистка", "Clear"); break;
            case 0051: name = getLocalizedText("Инвертирование", "Invert"); break;
            case 0052: name = getLocalizedText("Прибавление единицы", "Increment"); break;
            case 0053: name = getLocalizedText("Вычитание единицы", "Decrement"); break;
            case 0054: name = getLocalizedText("Изменение знака", "Negate"); break;
            case 0055: name = getLocalizedText("Прибавление переноса", "Add carry"); break;
            case 0056: name = getLocalizedText("Вычитание переноса", "Subtract carry"); break;
            case 0057: name = getLocalizedText("Проверка", "Test"); break;
            case 0060: name = getLocalizedText("Циклический сдвиг вправо", "Rotate right"); break;
            case 0061: name = getLocalizedText("Циклический сдвиг влево", "Rotate left"); break;
            case 0062: name = getLocalizedText("Арифметический сдвиг вправо", "Arithmetic shift right"); break;
            case 0063: name = getLocalizedText("Арифметический сдвиг влево", "Arithmetic shift left"); break;
        }
        QString prefix = isByteOperation ? "1" : "0";
        format = prefix + QString::number(baseMask, 8).rightJustified(3, '0') + "DD";
        return;
    }

    name = getLocalizedText("Неизвестная команда", "Unknown command");
    type = getLocalizedText("Неизвестно", "Unknown");
    format = "XXXXXX";
}

QString Pdp11::getAddressModeString(uint8_t mode, uint8_t registerIndex, uint16_t indexValue) {
    QString octalIndex = QString::number(indexValue, 8);

    if (registerIndex == 7) {
        switch (mode) {
            case 2: return QString("#%1").arg(octalIndex);
            case 3: return QString("@#%1").arg(octalIndex);
            case 6: return QString("%1(PC)").arg(octalIndex);
            case 7: return QString("@%1(PC)").arg(octalIndex);
        }
    }

    QString registerName = QString("R%1").arg(registerIndex);

    switch (mode) {
        case 0: return registerName;
        case 1: return QString("@%1").arg(registerName);
        case 2: return QString("(%1)+").arg(registerName);
        case 3: return QString("@(%1)+").arg(registerName);
        case 4: return QString("-(%1)").arg(registerName);
        case 5: return QString("@-(%1)").arg(registerName);
        case 6: return QString("%1(%2)").arg(octalIndex, registerName);
        case 7: return QString("@%1(%2)").arg(octalIndex, registerName);
        default: return "Unknown";
    }
}
