/** === CSOPESY MCO2 Multitasking OS ===
 *  Group members: (alphabetical)
 *   - Tiu, Lance Wilem
 *   - 
 */









// ======================= Libraries ======================= //

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
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>

// ===================== Libraries - END ===================== //

using namespace std;

void displayHeader();

// ======================= Global Variables ======================= //

bool isSchedulerRunning = false; 
thread schedulerThread; // threads for the scheduler to run concurrently
mutex processMutex;
vector<struct Process> globalProcesses; // List of processes within the scheduler
bool screenActive = false;

// ===================== Global Variables - END ===================== //



// ===================== Classes ===================== //

// === Screen Class === //
/**
 * Class for the screen object. Creates a new screen with given name & a timestamp during its creation. 
 */
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

// =================== Classes - END =================== //


// ===================== Structures ===================== //

// === Process structure === //
/**
 * Defines the process object/structure
 */
struct Process {
    string name;
    time_t startTime;
    time_t endTime;
    int core;
    int tasksCompleted;
    int totalTasks;
    bool isFinished;
};
// =================== Structures _END =================== //


// ===================== Functions ===================== //


// === Display Scheduler UI Function === //
/**
 * Displays the Scheduler UI. Displays processes, their progress, and core assignment. 
 */
void displaySchedulerUI(const vector<Process>& processes) { 
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif

    cout << "------------------------FCFC SCHEDULER----------------------\n\n";

    cout << "Running processes:\n";
    for (const auto& p : processes) {
        if (!p.isFinished) {
            tm localtm; // local time 

// Time conversion (start)
#ifdef _WIN32
            localtime_s(&localtm, &p.startTime); 
#else
            localtm = *localtime(&proc.startTime);
#endif

            // Display process name, core, and progress
            cout << left << setw(12) << p.name << " (";
            cout << put_time(&localtm, "%m/%d/%Y %I:%M:%S%p") << ")";
            cout << right << setw(8) << "Core: " << p.core;
            cout << setw(8) << p.tasksCompleted << " / " << p.totalTasks << "\n";
        }
    }

    // Display finished processes
    cout << "\nFinished processes:\n";
    for (const auto& p : processes) {
        if (p.isFinished) {
            tm localtm;

// Time conversion (end)
#ifdef _WIN32
            localtime_s(&localtm, &p.endTime);
#else
            localtm = *localtime(&proc.endTime);
#endif

            // Display process name, core, and completion time
            cout << left << setw(12) << p.name << " (";
            cout << put_time(&localtm, "%m/%d/%Y %I:%M:%S%p") << ")";
            cout << right << setw(8) << "Core: " << p.core;
            cout << right << setw(12) << "Finished";
            cout << setw(8) << p.tasksCompleted << " / " << p.totalTasks << "\n";
        }
    }
    cout << "\n";
}

// === First-Come-First-Serve Scheduler Function === //
/**
 * Implements the FCFS logic for the scheduler function.
 */
void FCFSScheduler() {
    for (auto& p : globalProcesses) {

        if (!isSchedulerRunning) break; // stop if scheduler stops
        if (p.isFinished) continue; // skip finished processes

        // process start time
        p.startTime = time(nullptr);

        // Task progress
        for (int i = p.tasksCompleted; i < p.totalTasks && isSchedulerRunning; ++i) {
            this_thread::sleep_for(chrono::milliseconds(10));
            lock_guard<mutex> lock(processMutex); 
            p.tasksCompleted++;
        }

        // Mark process as finished when done
        if (isSchedulerRunning) {
            p.endTime = time(nullptr);
            p.isFinished = true;
            lock_guard<mutex> lock(processMutex);
            displaySchedulerUI(globalProcesses);
        }
    }
    
    isSchedulerRunning = false; // FCFS scheduler completed
}


// === Enable UTF8 Function === //
/**
 * Enables UTF8 to be used the OS UI.
 */
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

// === Display Header Function === //
/**
 * Displays the OS Logo.
 */
void displayHeader() {
    cout << R"(
   _____         ____. ___________ .____       ________      _________
  /  _  \       |    | \_   _____/ |    |      \_____  \    /   _____/
 /  /_\  \      |    |  |    __)_  |    |       /   |   \   \_____  \ 
/    |    \ /\__|    |  |        \ |    |___   /    |    \  /        \
\____|____/ \________| /_________/ |________\  \_________/ /_________/
)" << endl;
}

// === Display Main Menu Function === //
/**
 * Displays the main menu functions & commands.
 */
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
    cout << "  scheduler-test    - Test the scheduler" << endl;
    cout << "  scheduler-stop    - Stop the scheduler" << endl;
    cout << "  report-util       - Report utilization" << endl;
    cout << "  clear             - Clear the screen" << endl;
    cout << "  exit              - Exit the program" << endl;
}

// =================== Functions - END =================== //


// ===================== Main ===================== //
int main() {
    string command;
    unordered_map<string, Screen> screens;
    bool inScreen = false;  // Track if we're in a screen
    string currentScreen;   // Track current screen name

    // enable UTF-8 (windows)
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

        // == Exit Command == -- exit to main menu or program
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

        // == Clear Command == -- clear screen
        else if (command == "clear") {
            if (inScreen) {
                screens[currentScreen].display();
            } else {
                displayMainMenu();
            }
        }

        // == Initialize Command == 
        else if (command == "initialize") {
            cout << "initialize command recognized. Doing something." << endl;
        }

        // == Screen -s Command == -- create new screen
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

        // == Screen -r Command == -- open existing screen
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


        // UI TEMPLATE
        //else if (command == "scheduler-test") {
        //    vector<Process> exampleProcesses = {
        //        {"process05", time(nullptr) - 3600, 0, 0, 1235, 5876, false},
        //        {"process06", time(nullptr) - 1800, 0, 1, 3, 5876, false},
        //       {"process07", time(nullptr) - 900, 0, 2, 9, 1000, false},
        //        {"process08", time(nullptr) - 300, 0, 3, 12, 80, false},
        //        {"process01", time(nullptr) - 7200, time(nullptr) - 3600, 0, 5876, 5876, true},
        //       {"process02", time(nullptr) - 7100, time(nullptr) - 3500, 1, 5876, 5876, true},
        //        {"process03", time(nullptr) - 7000, time(nullptr) - 3400, 2, 1000, 1000, true},
        //        {"process04", time(nullptr) - 6900, time(nullptr) - 3300, 3, 80, 80, true}
        //    };
        //    displaySchedulerUI(exampleProcesses);
        //    cout << "Press Enter to return to main menu...";
        //    cin.ignore();  // Clear any existing input
        //    cin.get();     // Wait for Enter key

            // Return to main menu
        //    displayMainMenu();
        //}

        // == Scheduler-test Command == -- start the scheduler & its processes
        else if (command == "scheduler-test") {

            if (isSchedulerRunning) {
                cout << "Scheduler is already running." << endl;
                continue;
            }

            // Process list
            globalProcesses = {
                {"process01", 0, 0, 0, 0, 100, false},
                {"process02", 0, 0, 1, 0, 80, false},
                {"process03", 0, 0, 2, 0, 60, false},
                {"process04", 0, 0, 3, 0, 40, false}
            };

            // Activate scheduler
            isSchedulerRunning = true;
            schedulerThread = thread(FCFSScheduler);
            cout << "Scheduler started. Type 'scheduler-stop' to stop." << endl;
        } 

        // == Scheduler-stop Command == -- stop the scheduler & its processes
        else if (command == "scheduler-stop") {

            if (!isSchedulerRunning) {
                cout << "Scheduler is not running." << endl;
                continue;
            }

            // Stop scheduler
            isSchedulerRunning = false;
            if (schedulerThread.joinable())
                schedulerThread.join();

            cout << "Scheduler stopped." << endl;

            // Display process list
            lock_guard<mutex> lock(processMutex);
            displaySchedulerUI(globalProcesses);
            cout << "Press Enter to return to main menu...";

            cin.ignore(); // Clear any existing input
            cin.get(); // Wait for Enter key
            displayMainMenu(); 
        }

        // == Report-util Command ==
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
