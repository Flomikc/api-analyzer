// api-analyzer.cpp
// Система автоматизированного анализа и документирования API
// Сборка: g++ -std=c++17 -O2 -pthread api-analyzer.cpp -o api-analyzer

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <future>
#include <mutex>
#include <thread>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

// ---------- ANSI ----------
#define RST  "\033[0m"
#define RED  "\033[31m"
#define YEL  "\033[33m"
#define GRN  "\033[32m"
#define CYN  "\033[36m"
#define BLD  "\033[1m"

// ---------- Логгер ----------
struct Logger {
    std::ofstream out;
    std::mutex mtx;
    Logger(const std::string& p) : out(p, std::ios::app) {}
    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lk(mtx);
        auto t = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        out << "[" << buf << "] " << msg << "\n";
        out.flush();
    }
};

// ---------- Данные ----------
struct Param { std::string type, name; };

struct Method {
    std::string httpMethod, path;
    std::string returnType, bodyType;
    std::vector<Param> params;
    std::string functionName, file, body;
    int line = 0;
    bool isAsync = false, isMiddleware = false, isFilter = false;
    std::vector<std::string> issues;
    std::vector<std::string> recommendations;
};

struct Controller {
    std::string name, route, file;
    std::vector<Method> methods;
};

// ---------- Утилиты ----------
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && std::equal(p.begin(), p.end(), s.begin());
}
static std::vector<std::string> splitWS(const std::string& s) {
    std::vector<std::string> r; std::istringstream iss(s); std::string t;
    while (iss >> t) r.push_back(t);
    return r;
}
static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

// ---------- Парсер ----------
static std::vector<Controller> parseFile(const std::string& path, Logger& log) {
    std::vector<Controller> result;
    std::ifstream in(path);
    if (!in) { log.log("Не удалось открыть: " + path); return result; }

    std::string line;
    int lineno = 0;

    // текущее состояние разметки
    std::string pendingCtrlName, pendingCtrlRoute;
    bool hasCtrl = false;
    Controller* current = nullptr;

    bool hasPending = false;
    std::string pMethod, pPath, pReturn, pBody;
    std::vector<Param> pParams;
    bool pAsync = false, pMid = false, pFilter = false;
    int pendingLine = 0;

    auto ensureController = [&]() -> Controller* {
        if (current) return current;
        Controller c;
        if (hasCtrl) { c.name = pendingCtrlName; c.route = pendingCtrlRoute; }
        else          { c.name = "(global)"; }
        c.file = path;
        result.push_back(std::move(c));
        current = &result.back();
        return current;
    };

    auto flushMethod = [&](const std::string& fname, const std::string& body) {
        if (!hasPending) return;
        Controller* c = ensureController();
        Method m;
        m.httpMethod = pMethod;
        m.path       = pPath.empty() && !c->route.empty() ? c->route : pPath;
        m.returnType = pReturn;
        m.bodyType   = pBody;
        m.params     = pParams;
        m.functionName = fname;
        m.file       = path;
        m.line       = pendingLine;
        m.isAsync    = pAsync;
        m.isMiddleware = pMid;
        m.isFilter   = pFilter;
        m.body       = body;
        c->methods.push_back(std::move(m));

        hasPending = false;
        pMethod.clear(); pPath.clear(); pReturn.clear(); pBody.clear();
        pParams.clear(); pAsync = pMid = pFilter = false;
    };

    static const std::set<std::string> ctrlKw = {
        "if","for","while","switch","catch","else","do","return","case"
    };

    while (std::getline(in, line)) {
        ++lineno;
        std::string t = trim(line);
        if (t.empty()) continue;

        // --- аннотации ---
        if (startsWith(t, "//")) {
            std::string cm = trim(t.substr(2));

            if (startsWith(cm, "@Controller")) {
                pendingCtrlName = trim(cm.substr(11));
                hasCtrl = true;
                current = nullptr;   // начнём новый контроллер
            } else if (startsWith(cm, "@Route")) {
                std::string r = trim(cm.substr(6));
                if (hasPending) pPath = r;
                else {
                    pendingCtrlRoute = r;
                    if (hasCtrl && !current) {
                        Controller c{ pendingCtrlName, r, path, {} };
                        result.push_back(std::move(c));
                        current = &result.back();
                    }
                }
            } else if (startsWith(cm, "@GET")) {
                hasPending = true; pMethod = "GET";
                pPath = trim(cm.substr(4)); pendingLine = lineno;
            } else if (startsWith(cm, "@POST")) {
                hasPending = true; pMethod = "POST";
                pPath = trim(cm.substr(5)); pendingLine = lineno;
            } else if (startsWith(cm, "@PUT")) {
                hasPending = true; pMethod = "PUT";
                pPath = trim(cm.substr(4)); pendingLine = lineno;
            } else if (startsWith(cm, "@DELETE")) {
                hasPending = true; pMethod = "DELETE";
                pPath = trim(cm.substr(7)); pendingLine = lineno;
            } else if (startsWith(cm, "@Param")) {
                auto parts = splitWS(cm.substr(6));
                Param p;
                if (parts.size() >= 2)      { p.name = parts[0]; p.type = parts[1]; }
                else if (parts.size() == 1) { p.type = parts[0]; }
                if (!p.type.empty() || !p.name.empty()) pParams.push_back(p);
            } else if (startsWith(cm, "@Body")) {
                pBody = trim(cm.substr(5));
            } else if (startsWith(cm, "@Return")) {
                pReturn = trim(cm.substr(7));
            } else if (startsWith(cm, "@Async")) {
                pAsync = true;
            } else if (startsWith(cm, "@Middleware")) {
                pMid = true;
            } else if (startsWith(cm, "@Filter")) {
                pFilter = true;
            }
            continue;
        }

        // --- сигнатура функции ---
        bool looksLikeSig = t.find('(') != std::string::npos
                         && t.find(')') != std::string::npos
                         && t.back() != ';';
        if (!looksLikeSig) continue;

        // первое слово не должно быть управляющей конструкцией
        std::string firstWord;
        {
            size_t i = 0;
            while (i < t.size() &&
                   (std::isalnum((unsigned char)t[i]) || t[i]=='_' || t[i]==':')) ++i;
            firstWord = t.substr(0, i);
        }
        if (ctrlKw.count(firstWord)) continue;

        // имя функции — идентификатор перед '('
        size_t paren = t.find('(');
        size_t j = paren; while (j > 0 && std::isspace((unsigned char)t[j-1])) --j;
        size_t k = j;
        while (k > 0 && (std::isalnum((unsigned char)t[k-1]) || t[k-1]=='_' || t[k-1]=='~')) --k;
        std::string fname = t.substr(k, j - k);
        if (fname.empty()) continue;

        // Собираем тело функции (до сбалансированных скобок)
        std::string body = line + "\n";
        int depth = 0; bool inFn = false;
        for (char ch : line) {
            if (ch == '{') { ++depth; inFn = true; }
            else if (ch == '}') --depth;
        }
        if (!inFn) {
            while (std::getline(in, line)) {
                ++lineno; body += line + "\n";
                for (char ch : line) {
                    if (ch == '{') { ++depth; inFn = true; }
                    else if (ch == '}') --depth;
                }
                if (inFn) break;
            }
        }
        while (depth > 0 && std::getline(in, line)) {
            ++lineno; body += line + "\n";
            for (char ch : line) {
                if (ch == '{') ++depth;
                else if (ch == '}') --depth;
            }
        }

        flushMethod(fname, body);
    }
    flushMethod("", "");
    return result;
}

// ---------- Анализ архитектуры ----------
static void analyzeMethod(Method& m) {
    bool hasInput = !m.params.empty() || !m.bodyType.empty();
    bool anyIf = m.body.find("if (")  != std::string::npos
              || m.body.find("if(")   != std::string::npos;
    bool anyThrow = m.body.find("throw") != std::string::npos;

    // 1) валидация
    if (hasInput && !anyIf && !anyThrow) {
        m.issues.push_back("Отсутствует валидация входных данных");
        m.recommendations.push_back("Добавьте проверку входных параметров");
    }

    // 2) обработка исключений
    bool hasTry = m.body.find("try") != std::string::npos
               && m.body.find("catch") != std::string::npos;
    if (!hasTry && !m.body.empty()) {
        m.issues.push_back("Отсутствует обработка исключений");
        m.recommendations.push_back("Используйте try/catch или глобальный обработчик");
    }

    // 3) неправильное использование HTTP
    if (m.httpMethod == "GET") {
        std::string fn = toLower(m.functionName);
        static const char* muts[] = {"create","update","delete","remove",
                                     "insert","save","set","add","push"};
        for (auto k : muts) {
            if (startsWith(fn, k)) {
                m.issues.push_back("GET-метод изменяет данные");
                m.recommendations.push_back("Используйте POST/PUT/DELETE для мутаций");
                break;
            }
        }
    }

    // 4) кэширование
    if (m.httpMethod == "GET" && m.returnType.find("List") != std::string::npos) {
        m.recommendations.push_back("Рассмотрите кэширование результата");
    }
}

static void detectDuplication(std::vector<Controller>& ctrls) {
    std::map<std::string, std::vector<std::pair<int,int>>> routeMap;
    std::map<std::string, std::vector<std::pair<int,int>>> bodyMap;
    for (int i = 0; i < (int)ctrls.size(); ++i)
        for (int j = 0; j < (int)ctrls[i].methods.size(); ++j) {
            auto& m = ctrls[i].methods[j];
            std::string routeKey = m.httpMethod + " " + m.path;
            routeMap[routeKey].push_back({i, j});

            // грубый "хэш" тела — нормализованное содержимое
            std::string norm;
            for (char c : m.body) if (!std::isspace((unsigned char)c)) norm += c;
            if (norm.size() > 40) bodyMap[norm].push_back({i, j});
        }

    auto mark = [&](const std::vector<std::pair<int,int>>& v, const std::string& what) {
        if (v.size() < 2) return;
        for (auto& pr : v) {
            ctrls[pr.first].methods[pr.second].issues.push_back(
                "Дублирование (" + what + ")");
            ctrls[pr.first].methods[pr.second].recommendations.push_back(
                "Вынесите общую логику в отдельный сервис/функцию");
        }
    };
    for (auto& [k, v] : routeMap) mark(v, "маршрут " + k);
    for (auto& [k, v] : bodyMap)  mark(v, "идентичное тело метода");
}

// ---------- Сериализаторы ----------
static std::string escJSON(const std::string& s) {
    std::string r;
    for (char c : s) switch (c) {
        case '"':  r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n";  break;
        case '\t': r += "\\t";  break;
        case '\r': r += "\\r";  break;
        default: r += c;
    }
    return r;
}
static std::string escHTML(const std::string& s) {
    std::string r;
    for (char c : s) switch (c) {
        case '<': r += "&lt;";   break;
        case '>': r += "&gt;";   break;
        case '&': r += "&amp;";  break;
        case '"': r += "&quot;"; break;
        default: r += c;
    }
    return r;
}

static std::string genMarkdown(const std::vector<Controller>& c) {
    std::ostringstream o;
    o << "# API Documentation\n\n";
    o << "_Автоматически сгенерировано api-analyzer_\n\n";
    for (auto& cc : c) {
        o << "## Контроллер: " << cc.name << "\n\n";
        if (!cc.route.empty())
            o << "**Базовый маршрут:** `" << cc.route << "`\n\n";
        for (auto& m : cc.methods) {
            o << "### `" << m.httpMethod << " " << m.path << "`\n\n";
            o << "- **Функция:** `" << m.returnType << " " << m.functionName << "(...)`\n";
            if (!m.params.empty()) {
                o << "- **Параметры:**\n";
                for (auto& p : m.params)
                    o << "  - `" << (p.name.empty() ? "arg" : p.name)
                      << "` (" << p.type << ")\n";
            }
            if (!m.bodyType.empty())
                o << "- **Тело запроса:** `" << m.bodyType << "`\n";
            if (!m.returnType.empty())
                o << "- **Возвращает:** `" << m.returnType << "`\n";
            if (m.isAsync)      o << "- _async_\n";
            if (m.isMiddleware) o << "- _middleware_\n";
            if (m.isFilter)     o << "- _filter_\n";
            if (!m.issues.empty()) {
                o << "- **Проблемы:**\n";
                for (auto& i : m.issues) o << "  - " << i << "\n";
            }
            o << "\n**Пример запроса:**\n```http\n"
              << m.httpMethod << " " << m.path << " HTTP/1.1\n```\n\n";
            o << "**Пример ответа:**\n```json\n"
              << (m.returnType.empty() ? std::string("{}") : m.returnType)
              << "\n```\n\n";
        }
    }
    return o.str();
}

static std::string genHTML(const std::vector<Controller>& c) {
    std::ostringstream o;
    o << "<!DOCTYPE html>\n<html lang=\"ru\"><head><meta charset=\"utf-8\">"
         "<title>API Documentation</title>\n"
         "<style>body{font-family:sans-serif;max-width:960px;margin:auto;padding:2em}"
         "code{background:#f4f4f4;padding:2px 5px;border-radius:3px}"
         ".method{border:1px solid #ddd;padding:1em;margin:1em 0;border-radius:6px}"
         ".GET{color:#2196f3}.POST{color:#4caf50}.PUT{color:#ff9800}.DELETE{color:#f44336}"
         ".issue{color:#d32f2f;font-weight:bold}</style></head><body>\n";
    o << "<h1>API Documentation</h1>\n";
    for (auto& cc : c) {
        o << "<h2>Контроллер: " << escHTML(cc.name) << "</h2>\n";
        if (!cc.route.empty())
            o << "<p><b>Базовый маршрут:</b> <code>" << escHTML(cc.route) << "</code></p>\n";
        for (auto& m : cc.methods) {
            o << "<div class=\"method\">\n";
            o << "<h3 class=\"" << escHTML(m.httpMethod) << "\">"
              << escHTML(m.httpMethod) << " " << escHTML(m.path) << "</h3>\n";
            o << "<p><b>Функция:</b> <code>" << escHTML(m.returnType) << " "
              << escHTML(m.functionName) << "(...)</code></p>\n";
            if (!m.params.empty()) {
                o << "<p><b>Параметры:</b></p><ul>";
                for (auto& p : m.params)
                    o << "<li><code>" << escHTML(p.name) << "</code> ("
                      << escHTML(p.type) << ")</li>";
                o << "</ul>\n";
            }
            if (!m.bodyType.empty())
                o << "<p><b>Тело запроса:</b> <code>" << escHTML(m.bodyType) << "</code></p>\n";
            if (!m.returnType.empty())
                o << "<p><b>Возвращает:</b> <code>" << escHTML(m.returnType) << "</code></p>\n";
            if (!m.issues.empty()) {
                o << "<p class=\"issue\">Проблемы:</p><ul>";
                for (auto& i : m.issues) o << "<li class=\"issue\">" << escHTML(i) << "</li>";
                o << "</ul>\n";
            }
            o << "</div>\n";
        }
    }
    o << "</body></html>\n";
    return o.str();
}

static std::string genJSON(const std::vector<Controller>& c) {
    std::ostringstream o;
    o << "{\n  \"generator\": \"api-analyzer\",\n  \"controllers\": [\n";
    for (size_t i = 0; i < c.size(); ++i) {
        auto& cc = c[i];
        o << "    {\n      \"name\": \"" << escJSON(cc.name) << "\",\n"
          << "      \"route\": \"" << escJSON(cc.route) << "\",\n"
          << "      \"file\": \"" << escJSON(cc.file) << "\",\n"
          << "      \"methods\": [\n";
        for (size_t j = 0; j < cc.methods.size(); ++j) {
            auto& m = cc.methods[j];
            o << "        {\n"
              << "          \"http\": \"" << escJSON(m.httpMethod) << "\",\n"
              << "          \"path\": \"" << escJSON(m.path) << "\",\n"
              << "          \"function\": \"" << escJSON(m.functionName) << "\",\n"
              << "          \"return\": \"" << escJSON(m.returnType) << "\",\n"
              << "          \"body\": \"" << escJSON(m.bodyType) << "\",\n"
              << "          \"async\": " << (m.isAsync ? "true" : "false") << ",\n"
              << "          \"middleware\": " << (m.isMiddleware ? "true" : "false") << ",\n"
              << "          \"filter\": " << (m.isFilter ? "true" : "false") << ",\n";
            o << "          \"params\": [";
            for (size_t k = 0; k < m.params.size(); ++k) {
                if (k) o << ", ";
                o << "{\"name\":\"" << escJSON(m.params[k].name)
                  << "\",\"type\":\"" << escJSON(m.params[k].type) << "\"}";
            }
            o << "],\n";
            o << "          \"issues\": [";
            for (size_t k = 0; k < m.issues.size(); ++k) {
                if (k) o << ", ";
                o << "\"" << escJSON(m.issues[k]) << "\"";
            }
            o << "],\n";
            o << "          \"recommendations\": [";
            for (size_t k = 0; k < m.recommendations.size(); ++k) {
                if (k) o << ", ";
                o << "\"" << escJSON(m.recommendations[k]) << "\"";
            }
            o << "]\n";
            o << "        }" << (j + 1 < cc.methods.size() ? "," : "") << "\n";
        }
        o << "      ]\n    }" << (i + 1 < c.size() ? "," : "") << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}

static std::string genReport(const std::vector<Controller>& c) {
    std::ostringstream o;
    o << "=== Отчёт анализа архитектуры API ===\n\n";
    int total = 0;
    for (auto& cc : c) {
        bool header = false;
        for (auto& m : cc.methods) {
            if (m.issues.empty()) continue;
            if (!header) { o << "Контроллер: " << cc.name << "\n"; header = true; }
            o << "  " << m.httpMethod << " " << m.path << ":\n";
            for (auto& i : m.issues)  { o << "    - " << i << "\n"; ++total; }
            for (auto& r : m.recommendations) o << "      → " << r << "\n";
        }
    }
    o << "\nВсего найдено проблем: " << total << "\n";
    return o.str();
}

// ---------- Интерактивный режим ----------
static void runInteractive(const std::vector<Controller>& ctrls, Logger& log) {
    if (ctrls.empty()) { std::cout << "Нет данных для анализа.\n"; return; }
    std::cout << "\n" << BLD << "=== Интерактивный режим ===" << RST << "\n";
    std::cout << "Доступные контроллеры:\n";
    for (size_t i = 0; i < ctrls.size(); ++i)
        std::cout << "  " << (i + 1) << ". " << ctrls[i].name << "\n";
    std::cout << "Выберите контроллер (1-" << ctrls.size() << "): ";
    int ch = 0; std::cin >> ch;
    if (ch < 1 || ch > (int)ctrls.size()) { std::cout << "Неверный выбор.\n"; return; }
    auto& c = ctrls[ch - 1];

    std::cout << "\nКонтроллер: " << c.name << "\nМетоды:\n";
    for (size_t i = 0; i < c.methods.size(); ++i)
        std::cout << "  " << (i + 1) << ". "
                  << c.methods[i].httpMethod << " " << c.methods[i].path << "\n";
    if (c.methods.empty()) { std::cout << "(нет методов)\n"; return; }
    std::cout << "Выберите метод для подробного анализа (1-"
              << c.methods.size() << "): ";
    int mc = 0; std::cin >> mc;
    if (mc < 1 || mc > (int)c.methods.size()) { std::cout << "Неверный выбор.\n"; return; }
    auto& m = c.methods[mc - 1];

    std::cout << "\n=== " << m.httpMethod << " " << m.path << " ===\n";
    if (!m.params.empty()) {
        std::cout << "Параметры: ";
        for (size_t k = 0; k < m.params.size(); ++k) {
            if (k) std::cout << ", ";
            std::cout << m.params[k].name << " (" << m.params[k].type << ")";
        }
        std::cout << "\n";
    }
    if (!m.returnType.empty()) std::cout << "Возвращает: " << m.returnType << "\n";
    if (!m.issues.empty()) {
        for (auto& i : m.issues)
            std::cout << "  " << RED << "Проблема: " << i << RST << "\n";
        for (auto& r : m.recommendations)
            std::cout << "  " << YEL << "Рекомендация: " << r << RST << "\n";
    } else {
        std::cout << "  " << GRN << "Проблем не обнаружено" << RST << "\n";
    }

    std::cout << "\nСохранить документацию? (y/n): ";
    char ans = 'n'; std::cin >> ans; std::cin.ignore();
    if (ans == 'y' || ans == 'Y') {
        std::cout << "Имя файла: ";
        std::string fn; std::getline(std::cin, fn);
        if (fn.empty()) fn = "api_documentation.md";
        std::string ext = fs::path(fn).extension().string();
        std::string content;
        if (ext == ".html")           content = genHTML(ctrls);
        else if (ext == ".json")      content = genJSON(ctrls);
        else                          content = genMarkdown(ctrls);
        std::ofstream of(fn);
        if (of) { of << content; std::cout << GRN << "[сохранено]" << RST << "\n";
                  log.log("Interactive save: " + fn); }
        else      std::cout << RED << "[ошибка записи]" << RST << "\n";
    }
}

// ---------- main ----------
int main(int argc, char** argv) {
    std::string source, format, output, reportPath;
    bool interactive = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--source"      && i + 1 < argc) source     = argv[++i];
        else if (a == "--format"      && i + 1 < argc) format     = argv[++i];
        else if (a == "--output"      && i + 1 < argc) output     = argv[++i];
        else if (a == "--report"      && i + 1 < argc) reportPath = argv[++i];
        else if (a == "--interactive") interactive = true;
        else if (a == "-h" || a == "--help") {
            std::cout <<
                "Использование:\n"
                "  ./api-analyzer --source PATH [--format markdown|html|json]\n"
                "                 [--output FILE] [--report FILE] [--interactive]\n";
            return 0;
        }
    }

    Logger log("api_analysis_log.txt");
    log.log("=== Запуск api-analyzer ===");

    if (interactive && source.empty()) {
        std::cout << "Введите путь к проекту: ";
        std::getline(std::cin, source);
    }
    if (source.empty()) {
        std::cerr << "Не указан --source. См. --help\n";
        return 1;
    }

    fs::path srcPath(source);
    std::error_code ec;
    if (!fs::exists(srcPath, ec)) {
        std::cerr << RED << "Ошибка: путь не существует: " << source << RST << "\n";
        log.log("Не существует: " + source);
        return 1;
    }

    // Сбор файлов
    std::vector<fs::path> files;
    if (fs::is_directory(srcPath, ec)) {
        for (auto it = fs::recursive_directory_iterator(
                 srcPath, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            auto ext = it->path().extension().string();
            if (ext == ".cpp" || ext == ".h" || ext == ".hpp" ||
                ext == ".cc"  || ext == ".cxx" || ext == ".c")
                files.push_back(it->path());
        }
    } else if (fs::is_regular_file(srcPath, ec)) {
        files.push_back(srcPath);
    }

    if (files.empty()) {
        std::cout << YEL << "Не найдено файлов для анализа.\n" << RST;
        log.log("Файлы не найдены.");
        return 0;
    }

    std::cout << BLD << "=== Анализ API ===" << RST << "\n";
    std::cout << "Проанализировано файлов: " << files.size() << "\n\n";
    log.log("Файлов найдено: " + std::to_string(files.size()));

    // Параллельный парсинг
    std::vector<Controller> all;
    std::mutex allMtx;
    unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::future<std::vector<Controller>>> futures;

    auto drain = [&]() {
        for (auto& f : futures) {
            auto res = f.get();
            std::lock_guard<std::mutex> lk(allMtx);
            for (auto& c : res) all.push_back(std::move(c));
        }
        futures.clear();
    };

    for (auto& p : files) {
        std::string ps = p.string();
        futures.push_back(std::async(std::launch::async,
            [ps, &log]() { return parseFile(ps, log); }));
        if (futures.size() >= hw * 4) drain();
    }
    drain();

    // Анализ архитектуры
    for (auto& c : all)
        for (auto& m : c.methods)
            analyzeMethod(m);
    detectDuplication(all);

    // Вывод сводки
    int totalIssues = 0;
    for (auto& c : all) {
        std::cout << CYN << "Контроллер: " << c.name << RST << "\n";
        if (!c.route.empty()) std::cout << "  Маршрут: " << c.route << "\n";
        for (auto& m : c.methods) {
            std::cout << "  " << GRN << m.httpMethod << " " << m.path << RST << "\n";
            if (!m.params.empty()) {
                std::cout << "    Параметры: ";
                for (size_t k = 0; k < m.params.size(); ++k) {
                    if (k) std::cout << ", ";
                    std::cout << m.params[k].name << " (" << m.params[k].type << ")";
                }
                std::cout << "\n";
            }
            if (!m.returnType.empty())
                std::cout << "    Возвращает: " << m.returnType << "\n";
            for (auto& i : m.issues) {
                std::cout << "    " << RED << "Проблема: " << i << RST << "\n";
                ++totalIssues;
            }
        }
        std::cout << "\n";
    }

    if (totalIssues) {
        std::cout << BLD << "Рекомендации:" << RST << "\n";
        int n = 1;
        for (auto& c : all)
            for (auto& m : c.methods)
                for (auto& r : m.recommendations)
                    std::cout << "  " << (n++) << ". " << r
                              << " (" << m.httpMethod << " " << m.path << ")\n";
        std::cout << "\n";
    }

    // Интерактивный режим
    if (interactive) runInteractive(all, log);

    // Сохранение документации
    if (!output.empty()) {
        if (format.empty()) format = "markdown";
        std::string content;
        if      (format == "markdown" || format == "md") content = genMarkdown(all);
        else if (format == "html")                        content = genHTML(all);
        else if (format == "json")                        content = genJSON(all);
        else std::cerr << RED << "Неизвестный формат: " << format << RST << "\n";

        if (!content.empty()) {
            std::ofstream of(output);
            if (of) {
                of << content;
                std::cout << GRN << "Документация сохранена: " << output << RST << "\n";
                log.log("Документация сохранена: " + output);
            } else {
                std::cerr << RED << "Не удалось записать: " << output << RST << "\n";
            }
        }
    }

    // Сохранение отчёта
    if (!reportPath.empty()) {
        std::ofstream of(reportPath);
        if (of) {
            of << genReport(all);
            std::cout << GRN << "Отчёт сохранён: " << reportPath << RST << "\n";
            log.log("Отчёт сохранён: " + reportPath);
        } else {
            std::cerr << RED << "Не удалось записать отчёт: " << reportPath << RST << "\n";
        }
    }

    log.log("Завершено. Проблем найдено: " + std::to_string(totalIssues));
    return 0;
}