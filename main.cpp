#pragma execution_character_set("utf-8")
#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <windows.h>
#include <conio.h>
#include "sqlite3.h"
#include <mmsystem.h>
#include <intrin.h>

#pragma comment(lib, "winmm.lib")

/* Глобальные состояния */
std::atomic<bool> g_clicking{ false };
std::atomic<bool> g_running{ true };
std::atomic<int> g_cps{ 10 };
std::atomic<int> g_real_cps{ 0 };
std::string g_current_profile = "default";
std::atomic<bool> g_exit_requested{ false };
std::atomic<long long> g_total_clicks{ 0 };
std::atomic<long long> g_session_clicks{ 0 };

/* Цвета */
enum ConsoleColor : unsigned char {
    BLACK = 0, DARK_BLUE = 1, DARK_GREEN = 2, DARK_CYAN = 3,
    DARK_RED = 4, DARK_MAGENTA = 5, DARK_YELLOW = 6, GRAY = 7,
    DARK_GRAY = 8, BLUE = 9, GREEN = 10, CYAN = 11,
    RED = 12, MAGENTA = 13, YELLOW = 14, WHITE = 15
};

HANDLE g_hConsole = nullptr;
int g_clicks_line_y = 0;
int g_status_line_y = 0;
sqlite3* g_db = nullptr;

void SetColor(int color) {
    SetConsoleTextAttribute(g_hConsole, static_cast<WORD>(color));
}

void PlaySound(const std::string& type) {
    if (type == "start") Beep(800, 100);
    else if (type == "stop") Beep(600, 100);
    else if (type == "error") Beep(300, 200);
    else if (type == "done") { Beep(1000, 100); Beep(1200, 100); }
}

/* Сохранение Total clicks в БД */
void SaveTotalClicks() {
    if (g_db) {
        char sql[256];
        sprintf_s(sql, "INSERT OR REPLACE INTO profiles (name, cps) VALUES ('__total_clicks__', %lld);",
            g_total_clicks.load());
        sqlite3_exec(g_db, sql, nullptr, nullptr, nullptr);
    }
}

/* Ввод */
std::string ReadLine() {
    std::string result;
    while (!g_exit_requested && g_running) {
        if (_kbhit()) {
            char ch = static_cast<char>(_getch());
            if (ch == '\r' || ch == '\n') { std::cout << std::endl; break; }
            else if (ch == '\b') {
                if (!result.empty()) { result.pop_back(); std::cout << "\b \b"; }
            }
            else if (ch >= 32 && ch <= 126) { result += ch; std::cout << ch; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return result;
}

int ReadInt() {
    std::string input = ReadLine();
    if (input.empty() || g_exit_requested) return -1;
    try { return std::stoi(input); }
    catch (...) { return -1; }
}

/* Очистка экрана */
void ClearScreen() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD topLeft = { 0, 0 };
    DWORD written;
    GetConsoleScreenBufferInfo(g_hConsole, &csbi);
    FillConsoleOutputCharacter(g_hConsole, ' ', csbi.dwSize.X * csbi.dwSize.Y, topLeft, &written);
    FillConsoleOutputAttribute(g_hConsole, csbi.wAttributes, csbi.dwSize.X * csbi.dwSize.Y, topLeft, &written);
    SetConsoleCursorPosition(g_hConsole, topLeft);
}

/* Линия-разделитель */
void DrawLine(int length = 50) {
    for (int i = 0; i < length; i++) std::cout << "-";
}

/* Настройка окна */
void SetupConsole() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleA("AEGIS MACRO");

    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    HWND hWnd = GetConsoleWindow();
    LONG style = GetWindowLong(hWnd, GWL_STYLE);
    style = style & ~WS_MAXIMIZEBOX & ~WS_SIZEBOX;
    SetWindowLong(hWnd, GWL_STYLE, style);

    COORD size = { 80, 30 };
    SetConsoleScreenBufferSize(g_hConsole, size);
    SMALL_RECT rect = { 0, 0, 79, 29 };
    SetConsoleWindowInfo(g_hConsole, TRUE, &rect);
}

/* Обновление строки статуса */
void UpdateStatusLine() {
    if (g_status_line_y == 0) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hConsole, &csbi);
    COORD saved_pos = csbi.dwCursorPosition;

    COORD status_pos = { 0, (SHORT)g_status_line_y };
    SetConsoleCursorPosition(g_hConsole, status_pos);

    // Очищаем строку
    for (int i = 0; i < 80; i++) std::cout << " ";

    // Пишем заново
    SetConsoleCursorPosition(g_hConsole, status_pos);
    std::cout << "  ";
    SetColor(YELLOW); std::cout << "[STATUS]"; SetColor(WHITE); std::cout << " ";
    if (g_clicking) {
        SetColor(GREEN); std::cout << "RUNNING";
    }
    else {
        SetColor(RED); std::cout << "STOPPED";
    }

    std::cout << "  ";
    SetColor(YELLOW); std::cout << "[CPS]"; SetColor(WHITE); std::cout << " ";
    SetColor(CYAN); std::cout << g_cps.load();
    if (g_clicking) {
        std::cout << " (real: ";
        SetColor(GREEN); std::cout << g_real_cps.load();
        SetColor(CYAN); std::cout << ")";
    }

    std::cout << "  ";
    SetColor(YELLOW); std::cout << "[PROFILE]"; SetColor(WHITE); std::cout << " ";
    SetColor(MAGENTA); std::cout << g_current_profile;

    SetConsoleCursorPosition(g_hConsole, saved_pos);
}

/* Обновление строки CLICKS */
void UpdateClicksLine() {
    if (g_clicks_line_y == 0) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hConsole, &csbi);
    COORD saved_pos = csbi.dwCursorPosition;

    COORD clicks_pos = { 0, (SHORT)g_clicks_line_y };
    SetConsoleCursorPosition(g_hConsole, clicks_pos);

    for (int i = 0; i < 80; i++) std::cout << " ";

    SetConsoleCursorPosition(g_hConsole, clicks_pos);
    std::cout << "  ";
    SetColor(YELLOW); std::cout << "[CLICKS]"; SetColor(WHITE); std::cout << " ";
    SetColor(GREEN);
    std::cout << "Total: " << g_total_clicks.load();

    SetConsoleCursorPosition(g_hConsole, saved_pos);
}

/* Проверка существования профиля */
bool ProfileExists(const std::string& name) {
    if (!g_db) return false;

    sqlite3_stmt* stmt;
    char sql[256];
    sprintf_s(sql, "SELECT COUNT(*) FROM profiles WHERE name='%s' AND name != '__total_clicks__';", name.c_str());

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            return count > 0;
        }
    }
    sqlite3_finalize(stmt);
    return false;
}

/* БД */
sqlite3* InitDatabase() {
    sqlite3* db;
    sqlite3_open("macros.db", &db);
    g_db = db;

    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS profiles (name TEXT PRIMARY KEY, cps INTEGER DEFAULT 10);",
        nullptr, nullptr, nullptr);
    sqlite3_exec(db, "INSERT OR IGNORE INTO profiles (name, cps) VALUES ('default', 10);",
        nullptr, nullptr, nullptr);

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT cps FROM profiles WHERE name='__total_clicks__';", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            g_total_clicks.store(sqlite3_column_int64(stmt, 0));
        }
    }
    sqlite3_finalize(stmt);

    return db;
}

void ShowProfiles(sqlite3* db) {
    std::cout << std::endl;
    SetColor(CYAN);
    std::cout << "  Profiles:" << std::endl;
    SetColor(WHITE);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT name, cps FROM profiles WHERE name != '__total_clicks__' ORDER BY name;", -1, &stmt, nullptr);

    bool empty = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        empty = false;
        std::string name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int cps = sqlite3_column_int(stmt, 1);

        std::cout << "  " << (name == g_current_profile ? "* " : "  ");
        if (name == g_current_profile) SetColor(GREEN);
        std::cout << name << " (CPS: " << cps << ")" << std::endl;
        SetColor(WHITE);
    }
    sqlite3_finalize(stmt);

    if (empty) {
        SetColor(RED);
        std::cout << "  No profiles found" << std::endl;
        SetColor(WHITE);
    }
    std::cout << std::endl;
}

void SelectProfile(sqlite3* db) {
    ShowProfiles(db);
    std::cout << "  Name: ";
    std::string name = ReadLine();
    if (name.empty()) return;

    if (!ProfileExists(name)) {
        PlaySound("error");
        SetColor(RED);
        std::cout << "  Profile not found" << std::endl;
        SetColor(WHITE);
        return;
    }

    sqlite3_stmt* stmt;
    char sql[256];
    sprintf_s(sql, "SELECT cps FROM profiles WHERE name='%s';", name.c_str());

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            g_cps.store(sqlite3_column_int(stmt, 0));
            g_current_profile = name;
            PlaySound("done");
            SetColor(GREEN);
            std::cout << "  Loaded: " << name << " (CPS: " << g_cps.load() << ")" << std::endl;
        }
    }
    sqlite3_finalize(stmt);
}

void CreateProfile(sqlite3* db) {
    std::cout << "  Name: ";
    std::string name = ReadLine();
    if (name.empty()) return;

    if (name == "__total_clicks__") {
        PlaySound("error");
        SetColor(RED);
        std::cout << "  Reserved name" << std::endl;
        SetColor(WHITE);
        return;
    }

    std::cout << "  CPS: ";
    int cps = ReadInt();
    if (cps < 1) cps = 10;
    if (cps > 100) cps = 100;

    char sql[256];
    sprintf_s(sql, "INSERT OR REPLACE INTO profiles VALUES ('%s', %d);", name.c_str(), cps);

    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK) {
        PlaySound("done");
        SetColor(GREEN);
        std::cout << "  Created: " << name << " (" << cps << " CPS)" << std::endl;
    }
}

void UpdateProfile(sqlite3* db) {
    ShowProfiles(db);
    std::cout << "  Name: ";
    std::string name = ReadLine();
    if (name.empty()) return;

    if (!ProfileExists(name)) {
        PlaySound("error");
        SetColor(RED);
        std::cout << "  Profile not found" << std::endl;
        SetColor(WHITE);
        return;
    }

    std::cout << "  New CPS: ";
    int cps = ReadInt();
    if (cps < 1) cps = 10;
    if (cps > 100) cps = 100;

    char sql[256];
    sprintf_s(sql, "UPDATE profiles SET cps=%d WHERE name='%s';", cps, name.c_str());

    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK) {
        if (name == g_current_profile) g_cps.store(cps);
        PlaySound("done");
        SetColor(GREEN);
        std::cout << "  Updated: " << name << " -> " << cps << " CPS" << std::endl;
    }
}

void DeleteProfile(sqlite3* db) {
    ShowProfiles(db);

    std::cout << "  Delete: ";
    std::string name = ReadLine();
    if (name.empty()) return;

    if (name == "default") {
        SetColor(YELLOW);
        std::cout << "  Cannot delete default profile" << std::endl;
        SetColor(WHITE);
        return;
    }

    if (!ProfileExists(name)) {
        PlaySound("error");
        SetColor(RED);
        std::cout << "  Profile not found" << std::endl;
        SetColor(WHITE);
        return;
    }

    char sql[256];
    sprintf_s(sql, "DELETE FROM profiles WHERE name='%s';", name.c_str());

    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK) {
        if (name == g_current_profile) {
            g_current_profile = "default";
            g_cps.store(10);
        }
        PlaySound("done");
        SetColor(GREEN);
        std::cout << "  Deleted: " << name << std::endl;
    }
}

/* Высокоточный таймер */
class PrecisionTimer {
private:
    LARGE_INTEGER m_frequency;
    LARGE_INTEGER m_start_time;
    double m_interval_ticks;

public:
    PrecisionTimer() {
        QueryPerformanceFrequency(&m_frequency);
        QueryPerformanceCounter(&m_start_time);
        m_interval_ticks = 0;
    }

    void SetInterval(int cps) {
        if (cps > 0) {
            m_interval_ticks = static_cast<double>(m_frequency.QuadPart) / cps;
        }
    }

    void WaitForNextTick() {
        LARGE_INTEGER current_time;
        QueryPerformanceCounter(&current_time);

        double remaining = m_interval_ticks - (current_time.QuadPart - m_start_time.QuadPart);

        if (remaining > 0) {
            if (remaining > m_frequency.QuadPart / 500.0) {
                double sleep_ms = (remaining * 1000.0 / m_frequency.QuadPart) - 1.0;
                if (sleep_ms > 0) Sleep(static_cast<DWORD>(sleep_ms));

                do {
                    QueryPerformanceCounter(&current_time);
                    remaining = m_interval_ticks - (current_time.QuadPart - m_start_time.QuadPart);
                    if (remaining <= 0) break;
                    _mm_pause();
                } while (true);
            }
            else {
                do {
                    QueryPerformanceCounter(&current_time);
                    if ((current_time.QuadPart - m_start_time.QuadPart) >= m_interval_ticks) break;
                    _mm_pause();
                } while (true);
            }
        }

        m_start_time.QuadPart += static_cast<LONGLONG>(m_interval_ticks);
        QueryPerformanceCounter(&current_time);
        if (current_time.QuadPart > m_start_time.QuadPart + static_cast<LONGLONG>(m_interval_ticks)) {
            m_start_time.QuadPart = current_time.QuadPart;
        }
    }
};

/* Точный поток кликера */
void ClickerThread() {
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    PrecisionTimer timer;

    using namespace std::chrono;
    auto last_cps_update = high_resolution_clock::now();
    auto last_display_update = high_resolution_clock::now();
    auto last_save_update = high_resolution_clock::now();
    int click_counter = 0;
    int last_cps = 0;
    bool was_clicking = false;

    while (g_running && !g_exit_requested) {
        if (g_clicking) {
            // Обновляем статус при первом входе в режим кликера
            if (!was_clicking) {
                UpdateStatusLine();
                was_clicking = true;
            }

            int current_cps = g_cps.load();

            if (current_cps != last_cps) {
                timer.SetInterval(current_cps);
                last_cps = current_cps;
            }

            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);

            g_total_clicks++;
            g_session_clicks++;
            click_counter++;

            auto now = high_resolution_clock::now();

            auto elapsed = duration_cast<milliseconds>(now - last_cps_update).count();
            if (elapsed >= 500) {
                g_real_cps.store(click_counter * 2);
                click_counter = 0;
                last_cps_update = now;
            }

            auto display_elapsed = duration_cast<milliseconds>(now - last_display_update).count();
            if (display_elapsed >= 100) {
                UpdateStatusLine();
                UpdateClicksLine();
                last_display_update = now;
            }

            auto save_elapsed = duration_cast<seconds>(now - last_save_update).count();
            if (save_elapsed >= 5) {
                SaveTotalClicks();
                last_save_update = now;
            }

            timer.WaitForNextTick();
        }
        else {
            if (was_clicking) {
                UpdateStatusLine();
                was_clicking = false;
            }
            std::this_thread::sleep_for(milliseconds(50));
            click_counter = 0;
            last_cps = 0;
            timer = PrecisionTimer();
        }
    }

    timeEndPeriod(1);
}

/* Главный экран */
void DrawScreen() {
    ClearScreen();

    SetColor(CYAN);
    std::cout << R"(
     ╔════════════════════════════════════════════╗
     ║     █████╗ ███████╗ ██████╗ ██╗███████╗    ║
     ║    ██╔══██╗██╔════╝██╔════╝ ██║██╔════╝    ║
     ║    ███████║█████╗  ██║  ███╗██║███████╗    ║
     ║    ██╔══██║██╔══╝  ██║   ██║██║╚════██║    ║
     ║    ██║  ██║███████╗╚██████╔╝██║███████║    ║
     ║    ╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚═╝╚══════╝    ║
     ╚════════════════════════════════════════════╝
)" << std::endl;

    SetColor(GRAY);
    std::cout << "  "; DrawLine(); std::cout << std::endl;
    SetColor(WHITE);

    // Сохраняем позицию строки статуса
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(g_hConsole, &csbi);
    g_status_line_y = csbi.dwCursorPosition.Y;

    std::cout << "  ";
    SetColor(YELLOW); std::cout << "[STATUS]"; SetColor(WHITE); std::cout << " ";
    if (g_clicking) { SetColor(GREEN); std::cout << "RUNNING"; }
    else { SetColor(RED); std::cout << "STOPPED"; }

    std::cout << "  ";
    SetColor(YELLOW); std::cout << "[CPS]"; SetColor(WHITE); std::cout << " ";
    SetColor(CYAN); std::cout << g_cps.load();
    if (g_clicking) {
        std::cout << " (real: ";
        SetColor(GREEN); std::cout << g_real_cps.load();
        SetColor(CYAN); std::cout << ")";
    }

    std::cout << "  ";
    SetColor(YELLOW); std::cout << "[PROFILE]"; SetColor(WHITE); std::cout << " ";
    SetColor(MAGENTA); std::cout << g_current_profile << std::endl;

    GetConsoleScreenBufferInfo(g_hConsole, &csbi);
    g_clicks_line_y = csbi.dwCursorPosition.Y;

    std::cout << "  ";
    SetColor(YELLOW); std::cout << "[CLICKS]"; SetColor(WHITE); std::cout << " ";
    SetColor(GREEN); std::cout << "Total: " << g_total_clicks << std::endl;

    SetColor(GRAY);
    std::cout << "  "; DrawLine(); std::cout << std::endl;
    SetColor(WHITE);

    std::cout << "  ";
    SetColor(YELLOW); std::cout << "[F6]"; SetColor(WHITE); std::cout << " Start/Stop  ";
    SetColor(YELLOW); std::cout << "[F7]"; SetColor(WHITE); std::cout << " Exit";
    std::cout << std::endl;

    SetColor(GRAY);
    std::cout << "  "; DrawLine(); std::cout << std::endl;
    SetColor(WHITE);

    std::cout << std::endl;
    std::cout << "  [1] Select profile" << std::endl;
    std::cout << "  [2] Create profile" << std::endl;
    std::cout << "  [3] Change CPS" << std::endl;
    std::cout << "  [4] Delete profile" << std::endl;
    std::cout << "  [0] Exit" << std::endl;
    std::cout << std::endl;

    SetColor(GRAY);
    std::cout << "  "; DrawLine(); std::cout << std::endl;
    SetColor(WHITE);
    std::cout << "  Choice: ";
}

/* Главная функция */
int main() {
    SetupConsole();
    sqlite3* db = InitDatabase();

    std::thread clicker(ClickerThread);
    clicker.detach();

    std::thread hotkeys([]() {
        bool prev_f6 = false, prev_f7 = false;

        while (g_running && !g_exit_requested) {
            bool cur_f6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
            if (cur_f6 && !prev_f6) {
                g_clicking = !g_clicking;
                PlaySound(g_clicking ? "start" : "stop");
                if (!g_clicking) {
                    g_session_clicks = 0;
                    g_real_cps.store(0);
                    SaveTotalClicks();
                }
            }
            prev_f6 = cur_f6;

            bool cur_f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
            if (cur_f7 && !prev_f7) {
                g_exit_requested = true;
                g_running = false;
            }
            prev_f7 = cur_f7;

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        });
    hotkeys.detach();

    int choice = -1;
    while (g_running && !g_exit_requested) {
        DrawScreen();
        choice = ReadInt();

        if (g_exit_requested || !g_running) break;

        ClearScreen();

        switch (choice) {
        case 1: SelectProfile(db); break;
        case 2: CreateProfile(db); break;
        case 3: UpdateProfile(db); break;
        case 4: DeleteProfile(db); break;
        case 0:
            SaveTotalClicks();
            g_exit_requested = true;
            g_running = false;
            break;
        default:
            SetColor(RED); std::cout << "  Invalid choice" << std::endl;
            SetColor(WHITE);
            break;
        }

        if (g_running && choice != 0 && !g_exit_requested) {
            SetColor(GRAY); std::cout << "\n  Press Enter...";
            SetColor(WHITE); std::cout.flush();
            while (!g_exit_requested) {
                if (_kbhit() && _getch() == '\r') break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    ClearScreen();
    SetColor(CYAN);
    std::cout << "\n  👋 [ AEGIS MACRO - GOOD BYE ]👋" << std::endl;
    SetColor(WHITE);

    SaveTotalClicks();
    sqlite3_close(db);

    std::cout << "\n  Press any key...";
    _getch();

    return 0;
}