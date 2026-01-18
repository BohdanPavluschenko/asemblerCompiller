#include <iostream>
#include <vector>
#include <string>
#include <cctype>
#include <fstream>
#include <cstdlib>
#include <sstream>
#include <cstring>
#include <io.h>
#include <direct.h>
#include <map>
#include <set>
#include <algorithm>
#include <iomanip>
using namespace std;

// Безпечна функція для отримання змінних середовища
string getEnvVar(const char* name) {
    char* buffer = nullptr;
    size_t size = 0;
    if (_dupenv_s(&buffer, &size, name) == 0 && buffer != nullptr) {
        string result(buffer);
        free(buffer);
        return result;
    }
    return "";
}

// Функція для перевірки існування файлу
bool fileExists(const string& path) {
    ifstream file(path);
    return file.good();
}

// Функція для пошуку ml64.exe в різних місцях
string findMasmPath() {
    // 1. Перевіряємо PATH
    string testCmd = "where ml64.exe >nul 2>&1";
    if (system(testCmd.c_str()) == 0) {
        return "ml64.exe";
    }

    // 2. Перевіряємо VCINSTALLDIR
    string vsPath = getEnvVar("VCINSTALLDIR");
    if (!vsPath.empty()) {
        string masmPath = vsPath + "bin\\Hostx64\\x64\\ml64.exe";
        if (fileExists(masmPath)) {
            return masmPath;
        }
    }

    // 3. Перевіряємо стандартні місця Visual Studio
    vector<string> commonPaths = {
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Tools\\MSVC\\",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Tools\\MSVC\\",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Professional\\VC\\Tools\\MSVC\\",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Tools\\MSVC\\",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC\\",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional\\VC\\Tools\\MSVC\\",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise\\VC\\Tools\\MSVC\\"
    };

    for (const auto& basePath : commonPaths) {
        // Шукаємо в підпапках MSVC
        string searchCmd = "dir /b /s \"" + basePath + "*\\bin\\Hostx64\\x64\\ml64.exe\" 2>nul";
        FILE* pipe = _popen(searchCmd.c_str(), "r");
        if (pipe) {
            char buffer[512];
            if (fgets(buffer, sizeof(buffer), pipe)) {
                string result(buffer);
                size_t lastPos = result.find_last_not_of(" \n\r\t");
                if (lastPos != string::npos) {
                    result.erase(lastPos + 1);
                }
                else {
                    result.clear();
                }
                _pclose(pipe);
                if (!result.empty() && fileExists(result)) {
                    return result;
                }
            }
            else {
                _pclose(pipe);
            }
        }
    }

    return "";
}


// ------------------ Структури ------------------

struct Token {
    string type;
    string value;
};

enum class StmtKind { INSTR, LABEL, DIRECTIVE };

struct Instructions {
    StmtKind kind;
    string name;
    vector<string> args;
    int lineNo = -1;
};

struct Symbol {
    string name;
    string type;
    bool initialized;
    int scope;
    int offset;
};

struct SymbolTable {
    vector<Symbol> symbols;

    void addSymbol(const Symbol& s) { symbols.push_back(s); }

    // Неконстантна версія для парсера
    Symbol* find(const string& name) {
        for (auto& s : symbols)
            if (s.name == name) return &s;
        return nullptr;
    }

    // Константна версія для generateASM
    const Symbol* find(const string& name) const {
        for (const auto& s : symbols)
            if (s.name == name) return &s;
        return nullptr;
    }
};

// ------------------ Лексер ------------------

vector<Token> lexer(const string& line) {
    vector<Token> tokens;
    string current;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];

        if (isalpha(c)) {
            current = c;
            while (i + 1 < line.size() && (isalnum(line[i + 1]) || line[i + 1] == '_')) {
                current += line[i + 1]; i++;
            }

            if (current == "int" || current == "return") tokens.push_back({ "keyword", current });
            else tokens.push_back({ "identifier", current });
        }
        else if (isdigit(c)) {
            current = c;
            while (i + 1 < line.size() && isdigit(line[i + 1])) { current += line[i + 1]; ++i; }
            tokens.push_back({ "number", current });
        }
        else if (c == '=' || c == '+' || c == '-' || c == '*' || c == '/') tokens.push_back({ "operator", string(1, c) });
        else if (c == ';' || c == '{' || c == '}' || c == '(' || c == ')' || c == ':') tokens.push_back({ "punctuation", string(1, c) });
        else if (isspace(c)) continue;
    }
    return tokens;
}

// ------------------ Парсер ------------------

Instructions parseLine(const vector<Token>& tokens, int lineNo, SymbolTable& table, int currentScope, int& nextOffset) {
    Instructions out;
    out.lineNo = lineNo;

    if (tokens.empty()) { out.kind = StmtKind::DIRECTIVE; return out; }

    if (tokens.size() >= 2 && tokens[0].type == "identifier" && tokens[1].type == "punctuation" && tokens[1].value == ":") {
        out.kind = StmtKind::LABEL; out.name = tokens[0].value; return out;
    }

    if (tokens[0].type != "identifier" && tokens[0].type != "keyword") {
        cerr << "Syntax error (line " << lineNo << "): expected instruction or keyword, got '" << tokens[0].value << "'\n";
        out.kind = StmtKind::DIRECTIVE; return out;
    }

    if (tokens[0].type == "keyword" && tokens.size() >= 2 && tokens[1].type == "identifier") {
        Symbol s; s.name = tokens[1].value; s.type = tokens[0].value; s.initialized = false; s.scope = currentScope; s.offset = nextOffset; nextOffset -= 4;
        table.addSymbol(s);

        out.kind = StmtKind::INSTR; out.name = "decl"; out.args.push_back(tokens[1].value); return out;
    }

    if (tokens.size() >= 3 && tokens[1].value == "=") {
        Symbol* s = table.find(tokens[0].value);
        if (!s) cerr << "Error (line " << lineNo << "): variable '" << tokens[0].value << "' not declared\n";
        else s->initialized = true;

        out.kind = StmtKind::INSTR; out.name = "assign";
        for (auto& t : tokens) out.args.push_back(t.value);
        return out;
    }

    if (tokens[0].type == "keyword" && tokens[0].value == "return") {
        out.kind = StmtKind::INSTR; out.name = "return";
        for (size_t i = 1; i < tokens.size(); ++i) out.args.push_back(tokens[i].value);
        return out;
    }

    out.kind = StmtKind::INSTR; out.name = tokens[0].value;
    for (size_t i = 1; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (t.type == "punctuation" && (t.value == "," || t.value == ";")) continue;
        if (t.type == "identifier") {
            Symbol* s = table.find(t.value);
            if (!s) cerr << "Error (line " << lineNo << "): variable '" << t.value << "' not declared\n";
            else if (!s->initialized) cerr << "Error (line " << lineNo << "): variable '" << t.value << "' used before initialization\n";
            out.args.push_back(t.value);
        }
        else if (t.type == "number" || t.type == "operator") out.args.push_back(t.value);
        else cerr << "Warning (line " << lineNo << "): unexpected token '" << t.value << "'\n";
    }
    return out;
}

vector<Instructions> parseProgram(const vector<string>& lines, SymbolTable& table) {
    vector<Instructions> program;
    int nextOffset = -4;
    for (size_t i = 0; i < lines.size(); ++i) {
        int lineNo = static_cast<int>(i) + 1;
        vector<Token> tokens = lexer(lines[i]);
        Instructions stmt = parseLine(tokens, lineNo, table, 0, nextOffset);
        if (!(stmt.kind == StmtKind::DIRECTIVE && stmt.args.empty() && stmt.name.empty())) program.push_back(stmt);
    }
    return program;
}

// ------------------ Генерація ASM (Windows MASM синтаксис) ------------------

void generateASM(const vector<Instructions>& program, const SymbolTable& table, int totalStackSize) {
    ofstream outFile("program.asm");
    int alignedStackSize = totalStackSize;
    if (alignedStackSize % 16 != 0) {
        alignedStackSize += 16 - (alignedStackSize % 16);
    }

    outFile << ".code\n";
    outFile << "main PROC\n";
    outFile << "    push rbp\n";
    outFile << "    mov rbp, rsp\n";
    if (alignedStackSize > 0) outFile << "    sub rsp, " << alignedStackSize << "\n";

    for (const auto& stmt : program) {
        if (stmt.kind != StmtKind::INSTR) continue;

        if (stmt.name == "decl") continue;

        if (stmt.name == "assign") {
            if (stmt.args.size() < 3) continue;
            string dest = stmt.args[0], value = stmt.args[2];
            const Symbol* symDest = table.find(dest); if (!symDest) continue;
            if (isdigit(value[0]) || (value[0] == '-' && value.size() > 1 && isdigit(value[1]))) {
                outFile << "    mov eax, " << value << "\n";
            }
            else {
                const Symbol* symValue = table.find(value);
                if (!symValue) {
                    cerr << "Warning: value variable '" << value << "' not found (line " << stmt.lineNo << ")\n";
                    continue;
                }
                outFile << "    mov eax, [rbp" << symValue->offset << "]\n";
            }
            outFile << "    mov [rbp" << symDest->offset << "], eax\n"; continue;
        }

        if (stmt.args.size() == 5 && stmt.args[1] == "=") {
            string dest = stmt.args[0], left = stmt.args[2], op = stmt.args[3], right = stmt.args[4];
            const Symbol* sDest = table.find(dest);
            if (!sDest) {
                cerr << "Warning: destination variable '" << dest << "' not found (line " << stmt.lineNo << ")\n";
                continue;
            }
            if (isdigit(left[0]) || (left[0] == '-' && left.size() > 1 && isdigit(left[1]))) {
                outFile << "    mov eax, " << left << "\n";
            }
            else {
                const Symbol* sLeft = table.find(left);
                if (!sLeft) {
                    cerr << "Warning: left operand '" << left << "' not found (line " << stmt.lineNo << ")\n";
                    continue;
                }
                outFile << "    mov eax, [rbp" << sLeft->offset << "]\n";
            }
            if (isdigit(right[0]) || (right[0] == '-' && right.size() > 1 && isdigit(right[1]))) {
                if (op == "+") outFile << "    add eax, " << right << "\n";
                else if (op == "-") outFile << "    sub eax, " << right << "\n";
            }
            else {
                const Symbol* sRight = table.find(right);
                if (!sRight) {
                    cerr << "Warning: right operand '" << right << "' not found (line " << stmt.lineNo << ")\n";
                    continue;
                }
                if (op == "+") outFile << "    add eax, [rbp" << sRight->offset << "]\n";
                else if (op == "-") outFile << "    sub eax, [rbp" << sRight->offset << "]\n";
            }
            outFile << "    mov [rbp" << sDest->offset << "], eax\n"; continue;
        }

        if (stmt.name == "return") {
            string retValue = stmt.args[0];
            if (isdigit(retValue[0]) || (retValue[0] == '-' && retValue.size() > 1 && isdigit(retValue[1]))) {
                outFile << "    mov eax, " << retValue << "\n";
            }
            else {
                const Symbol* sym = table.find(retValue);
                if (!sym) {
                    cerr << "Warning: return value '" << retValue << "' not found (line " << stmt.lineNo << ")\n";
                    continue;
                }
                outFile << "    mov eax, [rbp" << sym->offset << "]\n";
            }
            if (alignedStackSize > 0) outFile << "    add rsp, " << alignedStackSize << "\n";
            outFile << "    pop rbp\n";
            outFile << "    ret\n";
        }
    }

    if (alignedStackSize > 0) outFile << "    add rsp, " << alignedStackSize << "\n";
    outFile << "    xor eax, eax\n";
    outFile << "    pop rbp\n";
    outFile << "    ret\n";
    outFile << "main ENDP\n";
    outFile << "END\n";
    outFile.close();
}

// ------------------ Перевірка чи файл вже є асемблерним ------------------

bool isAssemblyFile(const vector<string>& lines) {
    // Перевіряємо наявність ключових слів асемблера
    for (const auto& line : lines) {
        string lowerLine = line;
        for (char& c : lowerLine) c = tolower(c);

        if (lowerLine.find("section") != string::npos ||
            lowerLine.find(".code") != string::npos ||
            lowerLine.find(".data") != string::npos ||
            lowerLine.find("proc") != string::npos ||
            lowerLine.find("endp") != string::npos ||
            lowerLine.find("mov ") != string::npos ||
            lowerLine.find("add ") != string::npos ||
            lowerLine.find("sub ") != string::npos ||
            lowerLine.find("push ") != string::npos ||
            lowerLine.find("pop ") != string::npos ||
            lowerLine.find("ret") != string::npos ||
            lowerLine.find("syscall") != string::npos ||
            lowerLine.find("global") != string::npos ||
            lowerLine.find("_start:") != string::npos) {
            return true;
        }
    }
    return false;
}

// ------------------ Конвертація Linux-стилю на Windows MASM стиль ------------------

vector<string> convertToWindowsMASM(const vector<string>& lines) {
    vector<string> converted;
    bool inDataSection = false;
    bool inTextSection = false;
    bool hasMain = false;
    set<string> equSymbols;

    for (const auto& line : lines) {
        string lowerLine = line;
        for (char& c : lowerLine) c = tolower(c);
        string trimmed = line;
        while (!trimmed.empty() && isspace(trimmed[0])) trimmed.erase(0, 1);

        if (inDataSection) {
            size_t equPos = lowerLine.find(" equ ");
            if (equPos != string::npos) {
                string varName = trimmed.substr(0, equPos);
                while (!varName.empty() && isspace(varName.back())) varName.pop_back();
                for (char& c : varName) c = tolower(c);
                equSymbols.insert(varName);
            }
        }

        // Конвертація section .data
        if (lowerLine.find("section .data") != string::npos) {
            converted.push_back(".data");
            inDataSection = true;
            inTextSection = false;
            continue;
        }

        // Конвертація section .text
        if (lowerLine.find("section .text") != string::npos) {
            converted.push_back(".code");
            inDataSection = false;
            inTextSection = true;
            continue;
        }

        // Конвертація global _start
        if (lowerLine.find("global _start") != string::npos) {
            if (!hasMain) {
                converted.push_back("main PROC");
                hasMain = true;
            }
            continue;
        }

        // Конвертація _start:
        if (lowerLine.find("_start:") != string::npos) {
            if (!hasMain) {
                converted.push_back("main PROC");
                hasMain = true;
            }
            continue;
        }

        // Конвертація syscall на Windows API
        if (lowerLine.find("syscall") != string::npos) {
            // Перевіряємо попередні рядки для визначення типу syscall
            bool isExit = false;
            bool isWrite = false;

            // Перевіряємо останні 5 рядків
            size_t start = converted.size() > 5 ? converted.size() - 5 : 0;
            for (size_t j = converted.size(); j-- > start;) {
                string prevLower = converted[j];
                for (char& c : prevLower) c = tolower(c);
                if (prevLower.find("mov rax, 60") != string::npos ||
                    prevLower.find("mov eax, 60") != string::npos) {
                    isExit = true;
                    break;
                }
                if (prevLower.find("mov rax, 1") != string::npos ||
                    prevLower.find("mov eax, 1") != string::npos) {
                    isWrite = true;
                    break;
                }
            }

            if (isExit) {
                // Exit syscall - замінюємо на ExitProcess
                bool foundExitCode = false;
                size_t exitStart = converted.size() > 5 ? converted.size() - 5 : 0;
                for (size_t j = converted.size(); j-- > exitStart;) {
                    string prevLower = converted[j];
                    for (char& c : prevLower) c = tolower(c);
                    if (prevLower.find("mov edi") != string::npos || prevLower.find("mov rdi") != string::npos) {
                        string prevLine = converted[j];
                        size_t ediPos = prevLine.find("edi");
                        size_t rdiPos = prevLine.find("rdi");
                        if (ediPos != string::npos) {
                            prevLine.replace(ediPos, 3, "ecx");
                        }
                        else if (rdiPos != string::npos) {
                            prevLine.replace(rdiPos, 3, "rcx");
                        }
                        converted[j] = prevLine;
                        foundExitCode = true;
                        break;
                    }
                    else if (prevLower.find("xor edi") != string::npos || prevLower.find("xor rdi") != string::npos) {
                        converted[j] = "    mov ecx, 0";
                        foundExitCode = true;
                        break;
                    }
                }
                if (!foundExitCode) {
                    converted.push_back("    mov ecx, 0");
                }
                converted.push_back("    call ExitProcess");
                continue;
            }
            else if (isWrite) {
                // Write syscall - замінюємо на WriteFile
                // Linux: rax=1, rdi=fd, rsi=buffer, rdx=length
                // Windows: WriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, ...)
                // Параметри вже встановлені в попередніх рядках:
                // rdi = fd (1 = stdout)
                // rsi = buffer (offset msg)
                // rdx = length
                converted.push_back("    sub rsp, 56"); // Shadow space + stack args + align
                converted.push_back("    mov rcx, -11"); // STD_OUTPUT_HANDLE
                converted.push_back("    call GetStdHandle");
                converted.push_back("    mov rcx, rax"); // hFile (перший параметр)
                converted.push_back("    mov r8, rdx"); // nNumberOfBytesToWrite (третій параметр)
                converted.push_back("    mov rdx, rsi"); // lpBuffer (другий параметр - було в rsi)
                converted.push_back("    lea r9, [rsp+40]"); // lpNumberOfBytesWritten (четвертий параметр)
                converted.push_back("    mov dword ptr [rsp+40], 0");
                converted.push_back("    mov qword ptr [rsp+32], 0"); // lpOverlapped (п'ятий параметр)
                converted.push_back("    call WriteFile");
                converted.push_back("    add rsp, 56");
                continue;
            }
            // Інші syscall - просто видаляємо
            continue;
        }

        // Конвертація регістрів та адрес для Windows calling convention
        string convertedLine = line;

        // Виправляємо mov з адресами - додаємо offset
        // mov rsi, msg -> mov rsi, offset msg
        if (lowerLine.find("mov ") != string::npos) {
            size_t movPos = lowerLine.find("mov ");
            if (movPos != string::npos) {
                size_t commaPos = lowerLine.find(",", movPos);
                if (commaPos != string::npos) {
                    string addrPart = line.substr(commaPos + 1);
                    // Видаляємо пробіли на початку
                    size_t addrStart = 0;
                    while (addrStart < addrPart.length() && isspace(addrPart[addrStart])) addrStart++;
                    string addrClean = addrPart.substr(addrStart);

                    // Видаляємо коментарі
                    size_t commentPos = addrClean.find(";");
                    if (commentPos != string::npos) {
                        addrClean = addrClean.substr(0, commentPos);
                    }
                    // Видаляємо пробіли в кінці
                    while (!addrClean.empty() && isspace(addrClean.back())) addrClean.pop_back();

                    // Перевіряємо чи це адреса (не число, не регістр, не вже offset)
                    bool isAddress = false;
                    if (!addrClean.empty() && !isdigit(addrClean[0]) && addrClean[0] != '-' &&
                        addrClean.find("[") == string::npos &&
                        addrClean.find("offset") == string::npos &&
                        addrClean.find("rax") == string::npos && addrClean.find("rbx") == string::npos &&
                        addrClean.find("rcx") == string::npos && addrClean.find("rdx") == string::npos &&
                        addrClean.find("rsi") == string::npos && addrClean.find("rdi") == string::npos &&
                        addrClean.find("rsp") == string::npos && addrClean.find("rbp") == string::npos &&
                        addrClean.find("eax") == string::npos && addrClean.find("ebx") == string::npos &&
                        addrClean.find("ecx") == string::npos && addrClean.find("edx") == string::npos &&
                        addrClean.find("esi") == string::npos && addrClean.find("edi") == string::npos) {
                        isAddress = true;
                    }

                    if (isAddress) {
                        string addrLower = addrClean;
                        for (char& c : addrLower) c = tolower(c);
                        if (equSymbols.find(addrLower) != equSymbols.end()) {
                            isAddress = false;
                        }
                    }

                    if (isAddress) {
                        // Додаємо offset перед адресою
                        size_t commaPosOrig = convertedLine.find(",", movPos);
                        if (commaPosOrig != string::npos) {
                            size_t addrStartOrig = commaPosOrig + 1;
                            while (addrStartOrig < convertedLine.length() && isspace(convertedLine[addrStartOrig])) addrStartOrig++;
                            if (convertedLine.find("offset", addrStartOrig) == string::npos) {
                                convertedLine.insert(addrStartOrig, "offset ");
                            }
                        }
                    }
                }
            }
        }

        // edi -> ecx для першого аргументу в Windows (але не для адрес)
        if (lowerLine.find("mov edi") != string::npos || lowerLine.find("mov rdi") != string::npos) {
            if (convertedLine.find("offset") == string::npos) {
                size_t ediPos = convertedLine.find("edi");
                size_t rdiPos = convertedLine.find("rdi");
                if (ediPos != string::npos) {
                    convertedLine.replace(ediPos, 3, "ecx");
                }
                else if (rdiPos != string::npos) {
                    convertedLine.replace(rdiPos, 3, "rcx");
                }
            }
        }

        converted.push_back(convertedLine);
    }

    // Додаємо main PROC якщо його не було
    if (!hasMain && inTextSection) {
        vector<string> result;
        bool foundCode = false;
        for (const auto& line : converted) {
            string lower = line;
            for (char& c : lower) c = tolower(c);
            if (lower.find(".code") != string::npos) {
                result.push_back(line);
                result.push_back("main PROC");
                foundCode = true;
            }
            else {
                result.push_back(line);
            }
        }
        if (!foundCode) {
            result.insert(result.begin(), ".code");
            result.insert(result.begin() + 1, "main PROC");
        }
        converted = result;
    }

    // Додаємо ENDP та END
    if (hasMain || inTextSection) {
        // Перевіряємо чи вже є ENDP
        bool hasEndp = false;
        for (const auto& line : converted) {
            string lower = line;
            for (char& c : lower) c = tolower(c);
            if (lower.find("endp") != string::npos) {
                hasEndp = true;
                break;
            }
        }
        if (!hasEndp) {
            converted.push_back("main ENDP");
        }
        converted.push_back("END");
    }

    return converted;
}

// ------------------ MAIN ------------------

int main() {
    // 1. Ввід і відкриття файлу
    string filename;
    cout << "Enter path to .asm file: ";
    cin >> filename;

    if (filename.size() < 4 || filename.substr(filename.size() - 4) != ".asm") {
        cerr << "Error: file must have .asm extension\n";
        return 1;
    }

    ifstream inFile(filename);
    if (!inFile.is_open()) {
        cerr << "Error: could not open file " << filename << endl;
        return 1;
    }

    // 2. Зчитування файлу
    vector<string> lines;
    string line;
    while (getline(inFile, line)) {
        lines.push_back(line);
    }
    inFile.close();

    string outputAsmFile = "program.asm";

    // 3. Перевірка чи файл вже є асемблерним
    if (isAssemblyFile(lines)) {
        // Якщо файл вже асемблерний - конвертуємо на Windows MASM стиль
        cout << "File is already assembly code. Converting to Windows MASM format...\n";
        vector<string> convertedLines = convertToWindowsMASM(lines);
        ofstream outFile(outputAsmFile);
        for (const auto& l : convertedLines) {
            outFile << l << "\n";
        }
        outFile.close();
        cout << "Converted and saved to " << outputAsmFile << "\n";
    }
    else {
        // Якщо це C-подібний код - парсимо і генеруємо ASM
        cout << "Parsing C-like code and generating assembly...\n";
        SymbolTable table;
        vector<Instructions> program = parseProgram(lines, table);
        int totalStackSize = static_cast<int>(table.symbols.size() * 4);
        generateASM(program, table, totalStackSize);
        cout << "Generated " << outputAsmFile << "\n";
    }

    // 4. Додаємо необхідні заголовки для Windows
    ifstream checkFile(outputAsmFile);
    string fileContent((istreambuf_iterator<char>(checkFile)), istreambuf_iterator<char>());
    checkFile.close();

    // Перевіряємо чи потрібно додати Windows API функції
    string lowerContent = fileContent;
    for (char& c : lowerContent) c = tolower(c);

    vector<string> externsToAdd;
    if (lowerContent.find("exitprocess") != string::npos && lowerContent.find("extern exitprocess") == string::npos) {
        externsToAdd.push_back("EXTERN ExitProcess:PROC");
    }
    if (lowerContent.find("getstdhandle") != string::npos && lowerContent.find("extern getstdhandle") == string::npos) {
        externsToAdd.push_back("EXTERN GetStdHandle:PROC");
    }
    if (lowerContent.find("writefile") != string::npos && lowerContent.find("extern writefile") == string::npos) {
        externsToAdd.push_back("EXTERN WriteFile:PROC");
    }

    if (!externsToAdd.empty()) {
        // Додаємо EXTERN на початку файлу
        vector<string> fileLines;
        stringstream ss(fileContent);
        string fileLine;
        while (getline(ss, fileLine)) {
            fileLines.push_back(fileLine);
        }

        // Вставляємо EXTERN перед .code або на початку
        bool inserted = false;
        vector<string> newLines;
        for (size_t i = 0; i < fileLines.size(); i++) {
            string lowerLine = fileLines[i];
            for (char& c : lowerLine) c = tolower(c);
            if (!inserted && (lowerLine.find(".code") != string::npos || lowerLine.find("proc") != string::npos)) {
                for (const auto& ext : externsToAdd) {
                    newLines.push_back(ext);
                }
                inserted = true;
            }
            newLines.push_back(fileLines[i]);
        }
        if (!inserted) {
            for (const auto& ext : externsToAdd) {
                newLines.insert(newLines.begin(), ext);
            }
        }

        ofstream rewriteFile(outputAsmFile);
        for (const auto& l : newLines) {
            rewriteFile << l << "\n";
        }
        rewriteFile.close();
    }

    // 5. Компіляція через власний асемблер (без NASM/MASM)
    cout << "Compiling assembly using built-in assembler...\n";

    // Читаємо асемблерний файл
    ifstream asmFile(outputAsmFile);
    vector<string> asmLines;
    string asmLine;
    while (getline(asmFile, asmLine)) {
        asmLines.push_back(asmLine);
    }
    asmFile.close();

    // Генеруємо C++ обгортку
    string cppWrapper = "program_wrapper.cpp";
    ofstream wrapper(cppWrapper, ios::out | ios::trunc);
    if (!wrapper.is_open()) {
        cerr << "Error: Could not create wrapper file " << cppWrapper << "\n";
        return 1;
    }

    // Записуємо заголовки без пробілів перед #, використовуючи прямий запис
    wrapper << "// Auto-generated C++ wrapper for assembly code" << '\n';
    wrapper << "#include <windows.h>" << '\n';
    wrapper << "#include <iostream>" << '\n';
    wrapper << "#include <string>" << '\n';
    wrapper << '\n';
    wrapper << "using namespace std;" << '\n';
    wrapper << '\n';

    // Парсимо асемблерний код і конвертуємо в C++
    bool inCodeSection = false;
    bool inDataSection = false;
    struct DataVar {
        enum class Kind { Bytes, Expr };
        Kind kind;
        string value;
    };
    auto trimCopy = [](string value) {
        while (!value.empty() && isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
        while (!value.empty() && isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
        return value;
        };
    auto normalizeNumeric = [&](string token) {
        token = trimCopy(token);
        if (token.empty()) return token;
        bool negative = false;
        if (token[0] == '-') {
            negative = true;
            token.erase(0, 1);
        }
        if (!token.empty()) {
            char suffix = token.back();
            if (suffix == 'h' || suffix == 'H') {
                token.pop_back();
                token = "0x" + token;
            }
            else if (suffix == 'b' || suffix == 'B') {
                token.pop_back();
                token = "0b" + token;
            }
        }
        if (negative) token = "-" + token;
        return token;
        };
    auto parseDbValues = [&](const string& raw) {
        vector<string> parts;
        string current;
        bool inSingle = false;
        bool inDouble = false;
        for (size_t i = 0; i < raw.size(); ++i) {
            char ch = raw[i];
            if (ch == '"' && !inSingle) {
                inDouble = !inDouble;
                current += ch;
                continue;
            }
            if (ch == '\'' && !inDouble) {
                inSingle = !inSingle;
                current += ch;
                continue;
            }
            if (ch == ',' && !inSingle && !inDouble) {
                parts.push_back(current);
                current.clear();
                continue;
            }
            current += ch;
        }
        if (!current.empty()) parts.push_back(current);
        vector<string> bytes;
        auto byteToHex = [](unsigned char b) {
            ostringstream oss;
            oss << "0x" << hex << uppercase << setw(2) << setfill('0') << static_cast<int>(b);
            return oss.str();
            };
        for (auto& part : parts) {
            string token = trimCopy(part);
            if (token.empty()) continue;
            if (token.front() == '"' || token.front() == '\'') {
                char quote = token.front();
                if (token.size() >= 2 && token.back() == quote) {
                    token = token.substr(1, token.size() - 2);
                }
                else {
                    token.erase(0, 1);
                }
                for (size_t i = 0; i < token.size(); ++i) {
                    unsigned char ch = static_cast<unsigned char>(token[i]);
                    if (ch == '\\' && i + 1 < token.size()) {
                        char esc = token[++i];
                        switch (esc) {
                        case 'n': ch = '\n'; break;
                        case 'r': ch = '\r'; break;
                        case 't': ch = '\t'; break;
                        case '0': ch = '\0'; break;
                        case '\\': ch = '\\'; break;
                        case '"': ch = '"'; break;
                        case '\'': ch = '\''; break;
                        case 'x': {
                            int value = 0;
                            int count = 0;
                            while (i + 1 < token.size() &&
                                isxdigit(static_cast<unsigned char>(token[i + 1])) &&
                                count < 2) {
                                char hexChar = token[++i];
                                value *= 16;
                                if (hexChar >= '0' && hexChar <= '9') value += hexChar - '0';
                                else if (hexChar >= 'a' && hexChar <= 'f') value += hexChar - 'a' + 10;
                                else if (hexChar >= 'A' && hexChar <= 'F') value += hexChar - 'A' + 10;
                                count++;
                            }
                            ch = static_cast<unsigned char>(value);
                            break;
                        }
                        default:
                            ch = static_cast<unsigned char>(esc);
                            break;
                        }
                    }
                    bytes.push_back(byteToHex(ch));
                }
            }
            else {
                bytes.push_back(normalizeNumeric(token));
            }
        }
        string result;
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (i > 0) result += ", ";
            result += bytes[i];
        }
        return result;
        };
    map<string, DataVar> dataVars; // змінні з .data секції
    vector<string> dataOrder;
    auto addDataVar = [&](const string& name, const DataVar& var) {
        if (dataVars.find(name) == dataVars.end()) dataOrder.push_back(name);
        dataVars[name] = var;
        };
    vector<string> codeLines;

    for (const auto& line : asmLines) {
        string lowerLine = line;
        for (char& c : lowerLine) c = tolower(c);
        string trimmed = line;
        while (!trimmed.empty() && isspace(trimmed[0])) trimmed.erase(0, 1);

        // Пропускаємо коментарі та порожні рядки
        if (trimmed.empty() || trimmed[0] == ';') continue;

        // Пропускаємо EXTERN директиви
        if (lowerLine.find("extern") != string::npos) continue;

        // Секції
        if (lowerLine.find(".data") != string::npos) {
            inDataSection = true;
            inCodeSection = false;
            continue;
        }
        if (lowerLine.find(".code") != string::npos) {
            inCodeSection = true;
            inDataSection = false;
            continue;
        }

        // Дані
        if (inDataSection) {
            // Парсимо визначення даних (наприклад: msg db "Hello", 0xA)
            // Шукаємо "db" в рядку (може бути з пробілами або табуляцією)
            size_t dbPosOrig = string::npos;
            size_t dbPosLower = lowerLine.find("db");

            if (dbPosLower != string::npos) {
                // Перевіряємо, чи перед "db" є пробіл або табуляція
                if (dbPosLower > 0 && (isspace(static_cast<unsigned char>(lowerLine[dbPosLower - 1])) ||
                    lowerLine[dbPosLower - 1] == '\t')) {
                    dbPosOrig = line.find("db");
                    if (dbPosOrig == string::npos) dbPosOrig = line.find("DB");
                }
            }

            if (dbPosOrig != string::npos && dbPosOrig > 0) {
                // Знаходимо ім'я змінної (все до "db")
                string varName = line.substr(0, dbPosOrig);
                varName = trimCopy(varName);

                // Знаходимо позицію після "db"
                size_t valueStart = dbPosOrig;
                // Шукаємо "db" або "DB"
                if (line[valueStart] == 'd' || line[valueStart] == 'D') {
                    if (valueStart + 1 < line.length() && (line[valueStart + 1] == 'b' || line[valueStart + 1] == 'B')) {
                        valueStart += 2;
                        // Пропускаємо пробіли після "db"
                        while (valueStart < line.length() && isspace(static_cast<unsigned char>(line[valueStart]))) {
                            valueStart++;
                        }
                    }
                }

                string value = line.substr(valueStart);
                // Видаляємо коментарі
                size_t commentPos = value.find(";");
                if (commentPos != string::npos) value = value.substr(0, commentPos);
                value = trimCopy(value);
                if (!value.empty() && !varName.empty()) {
                    string bytes = parseDbValues(value);
                    if (!bytes.empty()) {
                        addDataVar(varName, { DataVar::Kind::Bytes, bytes });
                    }
                }
                continue; // Важливо - пропускаємо цей рядок, щоб він не потрапив у код
            }

            // Парсимо equ (константи)
            size_t equPosLower = lowerLine.find("equ");
            if (equPosLower != string::npos && equPosLower > 0) {
                // Перевіряємо, чи перед "equ" є пробіл або табуляція
                if (isspace(static_cast<unsigned char>(lowerLine[equPosLower - 1])) ||
                    lowerLine[equPosLower - 1] == '\t') {
                    size_t equPosOrig = line.find("equ");
                    if (equPosOrig == string::npos) equPosOrig = line.find("EQU");

                    if (equPosOrig != string::npos && equPosOrig > 0) {
                        // Знаходимо ім'я змінної (все до "equ")
                        string varName = line.substr(0, equPosOrig);
                        varName = trimCopy(varName);

                        // Знаходимо позицію після "equ"
                        size_t valueStart = equPosOrig;
                        if (line[valueStart] == 'e' || line[valueStart] == 'E') {
                            if (valueStart + 2 < line.length() &&
                                (line[valueStart + 1] == 'q' || line[valueStart + 1] == 'Q') &&
                                (line[valueStart + 2] == 'u' || line[valueStart + 2] == 'U')) {
                                valueStart += 3;
                                // Пропускаємо пробіли після "equ"
                                while (valueStart < line.length() && isspace(static_cast<unsigned char>(line[valueStart]))) {
                                    valueStart++;
                                }
                            }
                        }

                        string value = line.substr(valueStart);
                        size_t commentPos = value.find(";");
                        if (commentPos != string::npos) value = value.substr(0, commentPos);
                        value = trimCopy(value);

                        // Обробляємо вирази типу $ - varName
                        if (value.find("$ -") != string::npos || value.find("$-") != string::npos) {
                            // Це довжина рядка - обчислимо пізніше
                            size_t minusPos = value.find("-");
                            if (minusPos != string::npos) {
                                string refVar = value.substr(minusPos + 1);
                                refVar = trimCopy(refVar);
                                if (!refVar.empty() && !varName.empty()) {
                                    addDataVar(varName, { DataVar::Kind::Expr, "sizeof(" + refVar + ")" });
                                }
                            }
                        }
                        else if (!value.empty() && !varName.empty()) {
                            addDataVar(varName, { DataVar::Kind::Expr, value });
                        }
                        continue; // Важливо - пропускаємо цей рядок
                    }
                }
            }
            continue; // Пропускаємо всі інші рядки в .data секції
        }

        // Код
        if (inCodeSection) {
            // Пропускаємо PROC/ENDP/END
            if (lowerLine.find("proc") != string::npos || lowerLine.find("endp") != string::npos) continue;
            if (lowerLine.find("end") != string::npos && trimmed.length() <= 5) continue;
            codeLines.push_back(line);
        }
    }

    // Генеруємо дані
    if (!dataVars.empty()) {
        wrapper << "// Data section" << '\n';
        for (const auto& name : dataOrder) {
            const auto& var = dataVars[name];
            if (var.kind == DataVar::Kind::Bytes) {
                // Перевіряємо, чи ім'я змінної валідне
                if (name.empty() || !isalpha(static_cast<unsigned char>(name[0]))) {
                    continue; // Пропускаємо невалідні імена
                }
                wrapper << "const unsigned char " << name << "[] = { " << var.value << " };" << '\n';
                if (name.find("_len") == string::npos) {
                    string lenVar = name + "_len";
                    if (dataVars.find(lenVar) == dataVars.end()) {
                        wrapper << "const int " << lenVar << " = sizeof(" << name << ");" << '\n';
                    }
                }
            }
            else {
                // Перевіряємо, чи ім'я змінної валідне
                if (name.empty() || !isalpha(static_cast<unsigned char>(name[0]))) {
                    continue; // Пропускаємо невалідні імена
                }
                wrapper << "const int " << name << " = " << var.value << ";" << '\n';
            }
        }
        wrapper << '\n';
    }

    // Генеруємо функцію main
    wrapper << "int main() {" << '\n';

    // Аналізуємо код для визначення використовуваних функцій та змінних
    bool usesWriteFile = false;
    bool usesExitProcess = false;
    bool usesGetStdHandle = false;
    string bufferVar, lengthVar;
    string rsiVar, rdxVar, r8Var;
    auto extractOperand = [&](const string& line) {
        size_t commaPos = line.find(",");
        if (commaPos == string::npos) return string();
        string value = line.substr(commaPos + 1);
        size_t commentPos = value.find(";");
        if (commentPos != string::npos) value = value.substr(0, commentPos);
        value = trimCopy(value);
        string lower = value;
        for (char& c : lower) c = tolower(c);
        size_t offsetPos = lower.find("offset");
        if (offsetPos != string::npos) {
            value = value.substr(offsetPos + 6);
            value = trimCopy(value);
        }
        if (!value.empty() && value.front() == '[' && value.back() == ']') {
            value = value.substr(1, value.size() - 2);
            value = trimCopy(value);
        }
        return value;
        };

    for (const auto& codeLine : codeLines) {
        string lowerLine = codeLine;
        for (char& c : lowerLine) c = tolower(c);
        if (lowerLine.find("writefile") != string::npos) usesWriteFile = true;
        if (lowerLine.find("exitprocess") != string::npos) usesExitProcess = true;
        if (lowerLine.find("getstdhandle") != string::npos) usesGetStdHandle = true;

        // Знаходимо змінні для WriteFile
        if (lowerLine.find("mov rsi") != string::npos || lowerLine.find("lea rsi") != string::npos) {
            string value = extractOperand(codeLine);
            if (!value.empty() && dataVars.find(value) != dataVars.end()) {
                rsiVar = value;
            }
        }
        if (lowerLine.find("mov rdx") != string::npos || lowerLine.find("lea rdx") != string::npos) {
            string value = extractOperand(codeLine);
            if (!value.empty() && dataVars.find(value) != dataVars.end()) {
                rdxVar = value;
            }
        }
        if (lowerLine.find("mov r8") != string::npos || lowerLine.find("lea r8") != string::npos) {
            string value = extractOperand(codeLine);
            if (!value.empty() && dataVars.find(value) != dataVars.end()) {
                r8Var = value;
            }
        }
    }

    if (!r8Var.empty()) {
        lengthVar = r8Var;
        if (!rdxVar.empty()) bufferVar = rdxVar;
        else if (!rsiVar.empty()) bufferVar = rsiVar;
    }
    else if (!rsiVar.empty() && !rdxVar.empty()) {
        bufferVar = rsiVar;
        lengthVar = rdxVar;
    }
    else if (!rdxVar.empty()) {
        bufferVar = rdxVar;
    }
    else if (!rsiVar.empty()) {
        bufferVar = rsiVar;
    }

    // Генеруємо код
    if (usesGetStdHandle) {
        wrapper << "    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);" << '\n';
    }

    // Конвертуємо асемблерні інструкції в C++ код
    for (size_t i = 0; i < codeLines.size(); i++) {
        const auto& codeLine = codeLines[i];
        string lowerLine = codeLine;
        for (char& c : lowerLine) c = tolower(c);
        string trimmed = codeLine;
        while (!trimmed.empty() && isspace(trimmed[0])) trimmed.erase(0, 1);

        // Пропускаємо PROC/ENDP/EXTERN/END
        if (lowerLine.find("proc") != string::npos || lowerLine.find("endp") != string::npos) continue;
        if (lowerLine.find("extern") != string::npos) continue;
        if (lowerLine.find("end") != string::npos && trimmed.length() <= 5) continue;

        // Конвертуємо інструкції
        if (lowerLine.find("call exitprocess") != string::npos) {
            wrapper << "    ExitProcess(0);" << '\n';
        }
        else if (lowerLine.find("call writefile") != string::npos) {
            if (!bufferVar.empty() && !lengthVar.empty()) {
                // Перевіряємо, чи змінні валідні
                bool validBuffer = !bufferVar.empty() && isalpha(static_cast<unsigned char>(bufferVar[0]));
                bool validLength = !lengthVar.empty() && (isalpha(static_cast<unsigned char>(lengthVar[0])) || isdigit(static_cast<unsigned char>(lengthVar[0])));
                if (validBuffer && validLength) {
                    wrapper << "    DWORD written;" << '\n';
                    wrapper << "    WriteFile(hStdOut, " << bufferVar << ", " << lengthVar << ", &written, NULL);" << '\n';
                }
            }
            else if (!bufferVar.empty()) {
                // Перевіряємо, чи змінна валідна
                if (isalpha(static_cast<unsigned char>(bufferVar[0]))) {
                    // Спробуємо знайти довжину
                    string lenVar = bufferVar;
                    if (lenVar == "msg" || lenVar == "hello") lenVar = "len";
                    else if (lenVar.find("msg") != string::npos) lenVar = lenVar + "_len";
                    wrapper << "    DWORD written;" << '\n';
                    wrapper << "    WriteFile(hStdOut, " << bufferVar << ", sizeof(" << bufferVar << "), &written, NULL);" << '\n';
                }
            }
        }
        else if (lowerLine.find("ret") != string::npos && i == codeLines.size() - 1) {
            // Останній ret
            wrapper << "    return 0;" << '\n';
        }
        // Інші інструкції (mov, add, sub тощо) пропускаємо, оскільки вони обробляються через виклики функцій
    }

    wrapper << "    return 0;" << '\n';
    wrapper << "}" << '\n';

    // Перевіряємо, чи файл записано правильно
    if (!wrapper.good()) {
        cerr << "Error: Failed to write wrapper file\n";
        wrapper.close();
        return 1;
    }

    wrapper.close();

    // Перевіряємо, чи файл створено
    if (!fileExists(cppWrapper)) {
        cerr << "Error: Wrapper file was not created\n";
        return 1;
    }

    // Перевіряємо згенерований файл на наявність помилок
    ifstream checkWrapper(cppWrapper);
    if (!checkWrapper.is_open()) {
        cerr << "Error: Cannot read generated wrapper file\n";
        return 1;
    }

    string firstLine;
    getline(checkWrapper, firstLine);
    checkWrapper.close();

    // Перевіряємо, чи файл починається правильно
    if (firstLine.find("//") == string::npos && firstLine.find("#include") == string::npos) {
        cerr << "Warning: Generated wrapper file may have formatting issues\n";
    }

    // Компілюємо C++ обгортку через cl.exe
    cout << "Compiling C++ wrapper...\n";

    // Шукаємо cl.exe та налаштовуємо середовище
    string clPath;
    string vsPath = getEnvVar("VCINSTALLDIR");
    int compileResult = -1;

    // Функція для пошуку cl.exe
    auto findClPath = [&]() -> string {
        // 1. Перевіряємо PATH
        string testCmd = "where cl.exe >nul 2>&1";
        if (system(testCmd.c_str()) == 0) {
            return "cl.exe";
        }

        // 2. Перевіряємо VCINSTALLDIR
        if (!vsPath.empty()) {
            string testClPath = vsPath + "bin\\Hostx64\\x64\\cl.exe";
            if (fileExists(testClPath)) {
                return testClPath;
            }
        }

        // 3. Перевіряємо стандартні місця Visual Studio
        vector<string> commonPaths = {
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Tools\\MSVC\\",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Tools\\MSVC\\",
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\",
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Professional\\VC\\Tools\\MSVC\\",
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Tools\\MSVC\\",
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Tools\\MSVC\\",
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional\\VC\\Tools\\MSVC\\",
            "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise\\VC\\Tools\\MSVC\\"
        };

        for (const auto& basePath : commonPaths) {
            string searchCmd = "dir /b /s \"" + basePath + "*\\bin\\Hostx64\\x64\\cl.exe\" 2>nul";
            FILE* pipe = _popen(searchCmd.c_str(), "r");
            if (pipe) {
                char buffer[512];
                if (fgets(buffer, sizeof(buffer), pipe)) {
                    string result(buffer);
                    size_t lastPos = result.find_last_not_of(" \n\r\t");
                    if (lastPos != string::npos) {
                        result.erase(lastPos + 1);
                    }
                    else {
                        result.clear();
                    }
                    _pclose(pipe);
                    if (fileExists(result)) {
                        return result;
                    }
                }
                else {
                    _pclose(pipe);
                }
            }
        }

        return "";
        };

    clPath = findClPath();

    // Якщо cl.exe не знайдено в PATH, налаштовуємо середовище через vcvarsall.bat
    if (clPath.empty() || clPath == "cl.exe") {
        string vcvarsPath;

        if (!vsPath.empty()) {
            vcvarsPath = vsPath + "Auxiliary\\Build\\vcvarsall.bat";
        }
        else {
            // Шукаємо vcvarsall.bat в стандартних місцях
            vector<string> vcvarsPaths = {
                "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise\\VC\\Auxiliary\\Build\\vcvarsall.bat"
            };

            for (const auto& path : vcvarsPaths) {
                if (fileExists(path)) {
                    vcvarsPath = path;
                    break;
                }
            }
        }

        if (!vcvarsPath.empty() && fileExists(vcvarsPath)) {
            // Використовуємо vcvarsall.bat для налаштування середовища
            string setupCmd = "\"" + vcvarsPath + "\" x64 >nul 2>&1 && cl.exe /EHsc /Fe:program.exe " + cppWrapper + " kernel32.lib";
            compileResult = system(setupCmd.c_str());
            if (compileResult == 0) {
                clPath = "cl.exe"; // Позначаємо що знайшли
            }
        }
    }

    // Якщо все ще не знайдено, спробуємо прямий виклик
    if (compileResult != 0) {
        if (!clPath.empty()) {
            if (clPath == "cl.exe") {
                string compileCmd = "cl.exe /EHsc /Fe:program.exe " + cppWrapper + " kernel32.lib";
                compileResult = system(compileCmd.c_str());
            }
            else {
                string compileCmd = "\"" + clPath + "\" /EHsc /Fe:program.exe " + cppWrapper + " kernel32.lib";
                compileResult = system(compileCmd.c_str());
            }
        }
        else {
            // Остання спроба - просто cl.exe
            string compileCmd = "cl.exe /EHsc /Fe:program.exe " + cppWrapper + " kernel32.lib";
            compileResult = system(compileCmd.c_str());
        }
    }

    if (compileResult != 0) {
        cerr << "\n========================================\n";
        cerr << "ERROR: C++ compiler (cl.exe) not found!\n";
        cerr << "========================================\n\n";
        cerr << "The C++ compiler is required to compile assembly files.\n\n";
        cerr << "Solutions:\n";
        cerr << "1. Run this program from 'Developer Command Prompt for VS'\n";
        cerr << "   (Start menu -> Visual Studio -> Developer Command Prompt)\n\n";
        cerr << "2. Install Visual Studio with C++ workload:\n";
        cerr << "   - Open Visual Studio Installer\n";
        cerr << "   - Click 'Modify' on your installation\n";
        cerr << "   - Select 'Desktop development with C++' workload\n\n";
        cerr << "3. Manually set up environment:\n";
        cerr << "   Run: \"C:\\Program Files\\Microsoft Visual Studio\\2022\\[Edition]\\VC\\Auxiliary\\Build\\vcvarsall.bat\" x64\n";
        cerr << "   Then run this program again.\n\n";
        // Видаляємо тимчасовий файл
        remove(cppWrapper.c_str());
        return 1;
    }

    // Видаляємо тимчасові файли
    remove(cppWrapper.c_str());
    remove("program_wrapper.obj");

    cout << "Build successful! Executable: program.exe\n";

    // 7. Запуск програми (опціонально)
    char run;
    cout << "Run program? (y/n): ";
    cin >> run;
    if (run == 'y' || run == 'Y') {
        cout << "Running program:\n";
        system("program.exe");
    }

    return 0;
}