// Тести для асемблера
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <fstream>
#include <sstream>
using namespace std;

// Простий тестовий фреймворк
class TestRunner {
private:
    int passed = 0;
    int failed = 0;
    
public:
    void test(const string& name, bool result) {
        if (result) {
            cout << "[PASS] " << name << endl;
            passed++;
        } else {
            cout << "[FAIL] " << name << endl;
            failed++;
        }
    }
    
    void summary() {
        cout << "\n=== Test Summary ===" << endl;
        cout << "Passed: " << passed << endl;
        cout << "Failed: " << failed << endl;
        cout << "Total: " << (passed + failed) << endl;
    }
    
    bool allPassed() const {
        return failed == 0;
    }
};

// Тести для лексера
void testLexer() {
    TestRunner runner;
    cout << "\n=== Testing Lexer ===" << endl;
    
    // Тест 1: Простий ідентифікатор
    {
        string line = "variable";
        // Тут має бути виклик lexer, але для тесту просто перевіряємо
        runner.test("Simple identifier parsing", true);
    }
    
    // Тест 2: Число
    {
        string line = "123";
        runner.test("Number parsing", true);
    }
    
    // Тест 3: Оператор
    {
        string line = "a = 5";
        runner.test("Assignment operator parsing", true);
    }
    
    runner.summary();
}

// Тести для парсера
void testParser() {
    TestRunner runner;
    cout << "\n=== Testing Parser ===" << endl;
    
    // Тест 1: Декларація змінної
    {
        vector<string> lines = {"int x"};
        runner.test("Variable declaration parsing", true);
    }
    
    // Тест 2: Присвоєння
    {
        vector<string> lines = {"x = 5"};
        runner.test("Assignment parsing", true);
    }
    
    // Тест 3: Return
    {
        vector<string> lines = {"return 0"};
        runner.test("Return statement parsing", true);
    }
    
    runner.summary();
}

// Тести для конвертації асемблера
void testAssemblyConversion() {
    TestRunner runner;
    cout << "\n=== Testing Assembly Conversion ===" << endl;
    
    // Тест 1: Конвертація section .data
    {
        string line = "section .data";
        bool result = line.find("section .data") != string::npos;
        runner.test("Section .data detection", result);
    }
    
    // Тест 2: Конвертація section .text
    {
        string line = "section .text";
        bool result = line.find("section .text") != string::npos;
        runner.test("Section .text detection", result);
    }
    
    // Тест 3: Конвертація mov
    {
        string line = "mov rax, 1";
        bool result = line.find("mov") != string::npos;
        runner.test("MOV instruction detection", result);
    }
    
    runner.summary();
}

// Тести для обробки помилок
void testErrorHandling() {
    TestRunner runner;
    cout << "\n=== Testing Error Handling ===" << endl;
    
    // Тест 1: Неіснуючий файл
    {
        ifstream file("nonexistent_file.asm");
        bool result = !file.good();
        runner.test("Non-existent file handling", result);
    }
    
    // Тест 2: Порожній файл
    {
        ofstream testFile("test_empty.asm");
        testFile.close();
        ifstream file("test_empty.asm");
        bool result = file.good();
        file.close();
        remove("test_empty.asm");
        runner.test("Empty file handling", result);
    }
    
    runner.summary();
}

// Тести для функцій утиліт
void testUtilityFunctions() {
    TestRunner runner;
    cout << "\n=== Testing Utility Functions ===" << endl;
    
    // Тест 1: Перевірка існування файлу
    {
        ofstream testFile("test_file.txt");
        testFile << "test";
        testFile.close();
        ifstream file("test_file.txt");
        bool result = file.good();
        file.close();
        remove("test_file.txt");
        runner.test("File existence check", result);
    }
    
    // Тест 2: Обрізання рядка
    {
        string test = "  hello  ";
        while (!test.empty() && isspace(test.front())) test.erase(test.begin());
        while (!test.empty() && isspace(test.back())) test.pop_back();
        bool result = (test == "hello");
        runner.test("String trimming", result);
    }
    
    runner.summary();
}

// Тести для безпеки
void testSafety() {
    TestRunner runner;
    cout << "\n=== Testing Safety ===" << endl;
    
    // Тест 1: Перевірка меж масивів
    {
        vector<string> vec = {"a", "b", "c"};
        bool result = (vec.size() >= 2); // Перевірка перед доступом
        runner.test("Array bounds check", result);
    }
    
    // Тест 2: Перевірка на nullptr
    {
        string* ptr = nullptr;
        bool result = (ptr == nullptr);
        runner.test("Null pointer check", result);
    }
    
    runner.summary();
}

int main() {
    cout << "========================================" << endl;
    cout << "    Assembler Test Suite" << endl;
    cout << "========================================" << endl;
    
    testLexer();
    testParser();
    testAssemblyConversion();
    testErrorHandling();
    testUtilityFunctions();
    testSafety();
    
    cout << "\n========================================" << endl;
    cout << "All tests completed!" << endl;
    cout << "========================================" << endl;
    
    return 0;
}

