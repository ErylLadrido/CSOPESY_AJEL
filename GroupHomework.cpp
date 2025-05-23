#ifdef _WIN32  // For UTF-8 display
#include <windows.h>  // For UTF-8 display
#endif  // For UTF-8 display
#include <iostream>
#include <string>
#include <unordered_map>
#include <ctime>
#include <iomanip>  // For put_time
#include <sstream>  // For stringstream
#include <cstdlib>  // For system("clear") or system("cls")

using namespace std;

void displayHeader();

class Screen {
public:
    string name;
    int currentLine;
    int totalLines;
    string timestamp;

    // Default constructor
    Screen() : name(""), totalLines(100), currentLine(1) {}

    Screen(const string& name, int totalLines = 100)
        : name(name), currentLine(1), totalLines(totalLines) {
        // Get current time
        time_t now = time(0);
        tm localtm;

        // Use localtime_s for safety (MSVC)
        localtime_s(&localtm, &now);

        stringstream ss;
        ss << put_time(&localtm, "%m/%d/%Y, %I:%M:%S %p");
        timestamp = ss.str();
    }

    void display() const {
        #ifdef _WIN32
            system("cls");
        #else
            system("clear");
        #endif

        // Display header
        displayHeader();
        
        // Display screen information
        cout << "┌──────────────────────────────────────────────────────────────┐\n";
        cout << "│ Process: " << left << setw(52) << name.substr(0, 52) << "│\n";
        cout << "├──────────────────────────────────────────────────────────────┤\n";
        cout << "│ Current Instruction Line: " << setw(6) << currentLine 
             << " of " << setw(6) << totalLines 
             << " (" << setw(3) << (currentLine * 100 / totalLines) << "%)       │\n";
        cout << "├──────────────────────────────────────────────────────────────┤\n";
        cout << "│ Timestamp: " << left << setw(53) << timestamp << "│\n";
        cout << "└──────────────────────────────────────────────────────────────┘\n";
        
        // Display progress bar
        cout << "\nProgress:\n[";
        int progressWidth = 50;
        int pos = progressWidth * currentLine / totalLines;
        for (int i = 0; i < progressWidth; ++i) {
            if (i < pos) cout << "=";
            else if (i == pos) cout << ">";
            else cout << " ";
        }
        cout << "] " << (currentLine * 100 / totalLines) << "%\n";
        
        // Add specific instructions for the screen context
        cout << "\nType \"exit\" to return to main menu" << endl;
    }
    
    void advance() {
        if (currentLine < totalLines)
            currentLine++;
    }
};

void enableUTF8Console() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif
}

void displayHeader() {
    cout << R"(
   _____         ____. ___________ .____       ________      _________
  /  _  \       |    | \_   _____/ |    |      \_____  \    /   _____/
 /  /_\  \      |    |  |    __)_  |    |       /   |   \   \_____  \ 
/    |    \ /\__|    |  |        \ |    |___   /    |    \  /        \
\____|____/ \________| /_________/ |________\  \_________/ /_________/
)" << endl;
}

void displayMainMenu() {
    #ifdef _WIN32
        system("cls");
    #else
        system("clear");
    #endif
    
    displayHeader();
    cout << "Hello, Welcome to AJEL OS command.net" << endl;
    cout << "Available commands:" << endl;
    cout << "  screen -s <name>  - Create a new screen" << endl;
    cout << "  screen -r <name>  - Resume a screen" << endl;
    cout << "  clear             - Clear the screen" << endl;
    cout << "  exit              - Exit the program" << endl;
    cout << "  scheduler-test    - Test the scheduler" << endl;
    cout << "  scheduler-stop    - Stop the scheduler" << endl;
    cout << "  report-util       - Report utilization" << endl;
}

int main() {
    string command;
    unordered_map<string, Screen> screens;
    bool inScreen = false;  // Track if we're in a screen
    string currentScreen;   // Track current screen name

    // enable UTF-8
    enableUTF8Console();
    displayMainMenu();

    // Main command loop
    while (true) {
        if (!inScreen) {
            cout << "\nAJEL OS> ";
        } else {
            cout << "\nAJEL OS [" << currentScreen << "]> ";
        }
        
        getline(cin, command);

        if (command == "exit") {
            if (inScreen) {
                // In screen context - return to main menu
                inScreen = false;
                displayMainMenu();
            } else {
                // In main menu - exit program
                cout << "Exiting application." << endl;
                break;
            }
        }
        else if (command == "clear") {
            if (inScreen) {
                screens[currentScreen].display();
            } else {
                displayMainMenu();
            }
        }
        else if (command == "initialize") {
            cout << "initialize command recognized. Doing something." << endl;
        }
        else if (command.rfind("screen -s ", 0) == 0) { 
            if (inScreen) {
                cout << "Cannot create new screen while inside a screen. Type 'exit' first." << endl;
                continue;
            }
            
            string name = command.substr(10);
            if (screens.find(name) == screens.end()) {
                screens[name] = Screen(name);
                cout << "Screen \"" << name << "\" created." << endl;
            } else {
                cout << "Screen \"" << name << "\" already exists." << endl;
            }
        }
        else if (command.rfind("screen -r ", 0) == 0) { 
            if (inScreen) {
                cout << "Already in a screen. Type 'exit' first." << endl;
                continue;
            }
            
            string name = command.substr(10);
            auto it = screens.find(name);
            if (it != screens.end()) {
                inScreen = true;
                currentScreen = name;
                it->second.display();
            } else {
                cout << "Screen \"" << name << "\" not found." << endl;
            }
        }
        else if (command == "scheduler-test") {
            cout << "scheduler-test command recognized. Doing something." << endl;
        }
        else if (command == "scheduler-stop") {
            cout << "scheduler-stop command recognized. Doing something." << endl;
        }
        else if (command == "report-util") {
            cout << "report-util command recognized. Doing something." << endl;
        }
        else {
            cout << "Command not recognized." << endl;
        }
        
        // If we're in a screen, advance the progress
        if (inScreen && command != "exit") {
            screens[currentScreen].advance();
        }
    }

    return 0;
}