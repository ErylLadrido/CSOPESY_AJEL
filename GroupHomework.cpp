/** === CSOPESY MCO2 Multitasking OS ===
 *  Group members: (alphabetical)
 *   - Abendan, Ashley
 *   - Ladrido, Eryl
 *   - Rodriguez, Joaquin Andres
 *   - Tiu, Lance Wilem
 */









// ======================= Libraries ======================= //

#ifdef _WIN32  // For UTF-8 display
#include <windows.h>  // For UTF-8 display
#endif  // For UTF-8 display
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>  // For put_time
#include <sstream>  // For stringstream
#include <cstdlib>  // For system("clear") or system("cls")
#include <fstream>  // For file I/O (ofstream)
#include <queue>    // For the ready queue of processes
#include <condition_variable> // For thread synchronization


// ===================== Libraries - END ===================== //

using namespace std;

void displayHeader();
void displayMainMenu();

// ======================= Global Variables ======================= //

bool isSchedulerRunning = false;
bool isSystemInitialized = false; // New flag to track system initialization
thread schedulerThread;
vector<thread> cpu_workers;

mutex processMutex; // Used for protecting shared resources like globalProcesses and cout
vector<struct Process> globalProcesses; // List of all processes
bool screenActive = false;

// --- Threading & Scheduling Variables ---
queue<struct Process*> ready_queue; // Queue of processes ready for CPU execution
mutex queue_mutex;                  // Mutex to protect the ready_queue
condition_variable scheduler_cv;    // Notifies worker threads about new processes

// --- Configuration Variables ---
struct SystemConfig {
    int num_cpu;
    string scheduler;
    int quantum_cycles;
    int batch_process_freq;
    int min_ins;
    int max_ins;
    int delay_per_exec;
    
   // Constructor - no default values, must be loaded from config
    SystemConfig() : 
        num_cpu(0), 
        scheduler(""), 
        quantum_cycles(0),
        batch_process_freq(0),
        min_ins(0),
        max_ins(0),
        delay_per_exec(0) {}

    // Method to validate configuration
    bool isValid() const {
        return num_cpu > 0 && 
               !scheduler.empty() && 
               quantum_cycles > 0 && 
               batch_process_freq > 0 && 
               min_ins > 0 && 
               max_ins > 0 && 
               max_ins >= min_ins &&
               delay_per_exec >= 0;
    }
} systemConfig;

// ===================== Global Variables - END ===================== //

// ===================== Structures ===================== //

/**
 * Defines the process structure.
 */
struct Process {
    string name;
    time_t startTime;
    time_t endTime;
    int core; // Core that is executing or executed the process
    int tasksCompleted;
    int totalTasks; // In this context, this is the number of "print" commands
    bool isFinished;
};

// =================== Structures - END =================== //


// ===================== Classes ===================== //

/**
 * Class for the screen object. Creates a new screen with a given name & a timestamp.
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
        time_t now = time(0);
        tm localtm;
#ifdef _WIN32
        localtime_s(&localtm, &now);
#else
        localtm = *localtime(&now);
#endif
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

        displayHeader();

        cout << "┌──────────────────────────────────────────────────────────────┐\n";
        cout << "│ Process: " << left << setw(52) << name.substr(0, 52) << "│\n";
        cout << "├──────────────────────────────────────────────────────────────┤\n";
        cout << "│ Current Instruction Line: " << setw(6) << currentLine
            << " of " << setw(6) << totalLines
            << " (" << setw(3) << (currentLine * 100 / totalLines) << "%)      │\n";
        cout << "├──────────────────────────────────────────────────────────────┤\n";
        cout << "│ Timestamp: " << left << setw(53) << timestamp << "│\n";
        cout << "└──────────────────────────────────────────────────────────────┘\n";

        cout << "\nProgress:\n[";
        int progressWidth = 50;
        int pos = progressWidth * currentLine / totalLines;
        for (int i = 0; i < progressWidth; ++i) {
            if (i < pos) cout << "=";
            else if (i == pos) cout << ">";
            else cout << " ";
        }
        cout << "] " << (currentLine * 100 / totalLines) << "%\n";

        cout << "\nType \"exit\" to return to main menu" << endl;
    }

    void advance() {
        if (currentLine < totalLines)
            currentLine++;
    }
};

// =================== Classes - END =================== //

// ===================== Functions ===================== //

/**
 * Load configuration from config.txt file
 * Returns true if config was loaded successfully, false otherwise
 */
bool loadConfig() {
    ifstream configFile("config.txt");
    if (!configFile.is_open()) {
        cout << "Error: config.txt file not found!" << endl;
        cout << "Please create a config.txt file with the following format:" << endl;
        cout << "num-cpu=4" << endl;
        cout << "scheduler=fcfs" << endl;
        cout << "quantum-cycles=5" << endl;
        cout << "batch-process-freq=1" << endl;
        cout << "min-ins=1000" << endl;
        cout << "max-ins=2000" << endl;
        cout << "delay-per-exec=100" << endl;
        return false;
    }
    
    string line;
    vector<string> missingKeys;
    vector<string> requiredKeys = {"num-cpu", "scheduler", "quantum-cycles", 
                                   "batch-process-freq", "min-ins", "max-ins", "delay-per-exec"};
    vector<bool> keyFound(requiredKeys.size(), false);
    
    cout << "Reading configuration from config.txt..." << endl;
    
    while (getline(configFile, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;
        
        size_t equalPos = line.find('=');
        if (equalPos == string::npos) {
            cout << "Warning: Invalid line format ignored: " << line << endl;
            continue;
        }
        
        string key = line.substr(0, equalPos);
        string value = line.substr(equalPos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t\r\n"));
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);
        
        try {
            // Parse configuration values
            if (key == "num-cpu") {
                systemConfig.num_cpu = stoi(value);
                keyFound[0] = true;
                cout << "  ✓ num-cpu: " << systemConfig.num_cpu << endl;
            } else if (key == "scheduler") {
                systemConfig.scheduler = value;
                keyFound[1] = true;
                cout << "  ✓ scheduler: " << systemConfig.scheduler << endl;
            } else if (key == "quantum-cycles") {
                systemConfig.quantum_cycles = stoi(value);
                keyFound[2] = true;
                cout << "  ✓ quantum-cycles: " << systemConfig.quantum_cycles << endl;
            } else if (key == "batch-process-freq") {
                systemConfig.batch_process_freq = stoi(value);
                keyFound[3] = true;
                cout << "  ✓ batch-process-freq: " << systemConfig.batch_process_freq << endl;
            } else if (key == "min-ins") {
                systemConfig.min_ins = stoi(value);
                keyFound[4] = true;
                cout << "  ✓ min-ins: " << systemConfig.min_ins << endl;
            } else if (key == "max-ins") {
                systemConfig.max_ins = stoi(value);
                keyFound[5] = true;
                cout << "  ✓ max-ins: " << systemConfig.max_ins << endl;
            } else if (key == "delay-per-exec") {
                systemConfig.delay_per_exec = stoi(value);
                keyFound[6] = true;
                cout << "  ✓ delay-per-exec: " << systemConfig.delay_per_exec << " ms" << endl;
            } else {
                cout << "Warning: Unknown configuration key ignored: " << key << endl;
            }
        } catch (const exception& e) {
            cout << "Error: Invalid value for " << key << ": " << value << endl;
            configFile.close();
            return false;
        }
    }
    
    configFile.close();
    
    // Check for missing required keys
    for (size_t i = 0; i < requiredKeys.size(); ++i) {
        if (!keyFound[i]) {
            missingKeys.push_back(requiredKeys[i]);
        }
    }
    
    if (!missingKeys.empty()) {
        cout << "Error: Missing required configuration keys:" << endl;
        for (const auto& key : missingKeys) {
            cout << "  - " << key << endl;
        }
        return false;
    }
    
    // Validate configuration values
    if (!systemConfig.isValid()) {
        cout << "Error: Invalid configuration values detected:" << endl;
        if (systemConfig.num_cpu <= 0) cout << "  - num-cpu must be greater than 0" << endl;
        if (systemConfig.scheduler.empty()) cout << "  - scheduler cannot be empty" << endl;
        if (systemConfig.quantum_cycles <= 0) cout << "  - quantum-cycles must be greater than 0" << endl;
        if (systemConfig.batch_process_freq <= 0) cout << "  - batch-process-freq must be greater than 0" << endl;
        if (systemConfig.min_ins <= 0) cout << "  - min-ins must be greater than 0" << endl;
        if (systemConfig.max_ins <= 0) cout << "  - max-ins must be greater than 0" << endl;
        if (systemConfig.max_ins < systemConfig.min_ins) cout << "  - max-ins must be >= min-ins" << endl;
        if (systemConfig.delay_per_exec < 0) cout << "  - delay-per-exec must be >= 0" << endl;
        return false;
    }
    
    return true;
}

/**
 * Initialize the system with configuration parameters
 */
void initializeSystem() {
    if (isSystemInitialized) {
        cout << "System is already initialized." << endl;
        cout << "If you want to reinitialize with new config values, please restart the program." << endl;
        return;
    }
    
    cout << "Initializing system..." << endl;
    
    // Load configuration from config.txt
    if (!loadConfig()) {
        cout << "\nSystem initialization failed!" << endl;
        cout << "Please fix the config.txt file and try again." << endl;
        return;
    }
    
    // Display successfully loaded configuration
    cout << "\n" << string(50, '=') << endl;
    cout << "SYSTEM CONFIGURATION LOADED SUCCESSFULLY" << endl;
    cout << string(50, '=') << endl;
    cout << "├── Number of CPUs: " << systemConfig.num_cpu << endl;
    cout << "├── Scheduler Algorithm: " << systemConfig.scheduler << endl;
    cout << "├── Quantum Cycles: " << systemConfig.quantum_cycles << endl;
    cout << "├── Batch Process Frequency: " << systemConfig.batch_process_freq << endl;
    cout << "├── Min Instructions: " << systemConfig.min_ins << endl;
    cout << "├── Max Instructions: " << systemConfig.max_ins << endl;
    cout << "└── Delay per Execution: " << systemConfig.delay_per_exec << " ms" << endl;
    cout << string(50, '=') << endl;
    
    // Initialize system components
    globalProcesses.clear();
    
    // Mark system as initialized
    isSystemInitialized = true;
    cout << "\nSystem initialized successfully!" << endl;
    cout << "You can now use scheduler-start to begin process scheduling." << endl;
}


/**
 * Displays the Scheduler UI, showing running and finished processes.
 */
void displaySchedulerUI(const vector<Process>& processes) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif

    displayHeader();

    // Calculate CPU utilization statistics
    int totalCores = systemConfig.num_cpu;
    int coresUsed = 0;
    int runningProcesses = 0;
    int finishedProcesses = 0;

    // Count cores in use and process statistics
    vector<bool> coreInUse(totalCores, false);
    for (const auto& process : processes) {
        if (!process.isFinished) {
            runningProcesses++;
            if (process.core != -1 && process.core < totalCores) {
                coreInUse[process.core] = true;
            }
        }
        else {
            finishedProcesses++;
        }
    }

    // Count cores actually in use
    for (bool inUse : coreInUse) {
        if (inUse) coresUsed++;
    }

    int coresAvailable = totalCores - coresUsed;
    double cpuUtilization = totalCores > 0 ? (static_cast<double>(coresUsed) / totalCores) * 100.0 : 0.0;

    cout << "CPU UTILIZATION REPORT" << endl;
    cout << "======================================" << endl;
    cout << "CPU Utilization: " << fixed << setprecision(2) << cpuUtilization << "%" << endl;
    cout << "Cores used: " << coresUsed << endl;
    cout << "Cores available: " << coresAvailable << endl;
    cout << "Total cores: " << totalCores << endl;
    cout << endl;

    cout << "Running processes:" << endl;
    if (runningProcesses == 0) {
        cout << "No running processes." << endl;
    }
    else {
        for (const auto& p : processes) {
            if (!p.isFinished) {
                tm localtm;
                string startTimeStr = "Waiting...            ";
                if (p.startTime != 0) {
#ifdef _WIN32
                    localtime_s(&localtm, &p.startTime);
#else
                    localtm = *localtime(&p.startTime);
#endif
                    stringstream ss;
                    ss << put_time(&localtm, "%m/%d/%Y %I:%M:%S%p");
                    startTimeStr = ss.str();
                }

                cout << left << setw(12) << p.name;
                cout << " (" << setw(25) << startTimeStr << ")";
                cout << right << setw(8) << "Core: " << (p.core == -1 ? "N/A" : to_string(p.core));
                cout << setw(8) << p.tasksCompleted << " / " << p.totalTasks << endl;
            }
        }
    }

    cout << endl << "Finished processes:" << endl;
    if (finishedProcesses == 0) {
        cout << "No finished processes." << endl;
    }
    else {
        for (const auto& p : processes) {
            if (p.isFinished) {
                tm localtm;
#ifdef _WIN32
                localtime_s(&localtm, &p.endTime);
#else
                localtm = *localtime(&p.endTime);
#endif
                stringstream ss;
                ss << put_time(&localtm, "%m/%d/%Y %I:%M:%S%p");

                cout << left << setw(12) << p.name << " (";
                cout << setw(25) << ss.str() << ")";
                cout << right << setw(8) << "Core: " << p.core;
                cout << right << setw(12) << "Finished";
                cout << setw(8) << p.tasksCompleted << " / " << p.totalTasks << endl;
            }
        }
    }

    cout << endl << "======================================" << endl;
    cout << "Total processes: " << processes.size() << endl;
    cout << "Running: " << runningProcesses << endl;
    cout << "Finished: " << finishedProcesses << endl;
    cout << "======================================" << endl;
}


/**
 * @brief This is the main function for each CPU worker thread.
 * Each worker thread simulates a CPU core. It waits for processes to be added
 * to the ready queue, picks one, and executes its tasks (print commands).
 * @param coreId The ID of the CPU core this thread represents (0 to NUM_CORES-1).
 */
void cpu_worker_main(int coreId) {
    while (isSchedulerRunning) {
        Process* currentProcess = nullptr;

        // --- Critical Section: Wait for and get a process from the queue ---
        {
            unique_lock<mutex> lock(queue_mutex);
            // Wait until the queue is not empty OR the scheduler is stopping
            scheduler_cv.wait(lock, [] { return !ready_queue.empty() || !isSchedulerRunning; });

            // If the scheduler has stopped and the queue is empty, the thread can exit.
            if (!isSchedulerRunning && ready_queue.empty()) {
                return;
            }

            // Check again if queue is not empty before popping
            if (!ready_queue.empty()) {
                currentProcess = ready_queue.front();
                ready_queue.pop();
            }
        } // The unique_lock is automatically released here.

        // If a process was successfully dequeued, execute it.
        if (currentProcess != nullptr) {
            // --- Set process start time and assigned core ---
            {
                lock_guard<mutex> lock(processMutex);
                currentProcess->core = coreId;
                currentProcess->startTime = time(nullptr);
            }

            // --- Execute all tasks (print commands) for the process ---
            string logFileName = currentProcess->name + ".txt";
            ofstream outfile(logFileName); // Create/overwrite the log file

            if (!outfile.is_open()) {
                lock_guard<mutex> lock(processMutex);
                cerr << "Error: Could not open file " << logFileName << " for writing." << endl;
            }
            else {
                for (int i = 0; i < currentProcess->totalTasks; ++i) {
                    if (!isSchedulerRunning) break; // Allow scheduler to stop mid-process

                    this_thread::sleep_for(chrono::milliseconds(20)); // Simulate work for one task

                    lock_guard<mutex> lock(processMutex); // Lock for updating shared process data
                    currentProcess->tasksCompleted++;

                    // --- Write the "print" command output to the process's log file ---
                    time_t now = time(0);
                    tm localtm;
#ifdef _WIN32
                    localtime_s(&localtm, &now);
#else
                    localtm = *localtime(&now);
#endif
                    stringstream ss;
                    ss << put_time(&localtm, "(%m/%d/%Y %I:%M:%S %p)");

                    outfile << ss.str() << " Core:" << coreId << " \"Hello world from " << currentProcess->name << "!\"" << endl;
                }
            }
            outfile.close();


            // --- Mark process as finished ---
            {
                lock_guard<mutex> lock(processMutex);
                // Only mark as fully finished if it completed all tasks
                if (currentProcess->tasksCompleted == currentProcess->totalTasks) {
                    currentProcess->endTime = time(nullptr);
                    currentProcess->isFinished = true;
                }
            }
        }
    }
}


/**
 * @brief The main scheduler thread function.
 * This function initializes the CPU worker threads and then feeds them
 * processes from the global list in a First-Come-First-Serve manner.
 */
void FCFSScheduler() {
    // --- Start all CPU worker threads ---
    cpu_workers.clear(); // Clear any previous worker threads
    for (int i = 0; i < systemConfig.num_cpu; ++i) {
        cpu_workers.emplace_back(cpu_worker_main, i);
    }

    // --- Add all processes to the ready queue for the workers to pick up ---
    for (auto& proc : globalProcesses) {
        if (!isSchedulerRunning) break;
        {
            lock_guard<mutex> lock(queue_mutex);
            ready_queue.push(&proc); // Push a pointer to the process
        }
        scheduler_cv.notify_one(); // Notify one waiting worker thread that a job is available
    }

    // --- Wait for all worker threads to complete their execution ---
    for (auto& worker : cpu_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    // Once all workers are done, the scheduler's job is finished.
    isSchedulerRunning = false;
}


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
    _____           ____. ___________ .____         ________      _________
   /  _  \         |    | \_   _____/ |    |        \_____  \    /   _____/
  /  /_\  \        |    |  |    __)_  |    |         /   |   \   \_____  \ 
 /    |    \   /\__|    |  |        \ |    |___     /    |    \  /        \
 \____|__  /   \________| /_________/ |________\    \_______  / /_________/
         \/                                                 \/             
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
    cout << "  initialize          - Initialize the system with config parameters" << endl;
    cout << "  screen -s <name>    - Create a new screen" << endl;
    cout << "  screen -r <name>    - Resume a screen" << endl;
    cout << "  screen -ls          - List running/finished processes" << endl;
    cout << "  scheduler-start     - Start the scheduler" << endl;
    cout << "  scheduler-stop      - Stop the scheduler" << endl;
    cout << "  report-util         - Generate CPU utilization report" << endl;
    cout << "  clear               - Clear the screen" << endl;
    cout << "  exit                - Exit the program" << endl;
}

/**
 * Generate CPU utilization report and save to csopesy-log.txt
 */
void generateUtilizationReport() {
    lock_guard<mutex> lock(processMutex);

    // Calculate CPU utilization statistics
    int totalCores = systemConfig.num_cpu;
    int coresUsed = 0;
    int runningProcesses = 0;
    int finishedProcesses = 0;

    // Count cores in use and process statistics
    vector<bool> coreInUse(totalCores, false);
    for (const auto& process : globalProcesses) {
        if (!process.isFinished) {
            runningProcesses++;
            if (process.core != -1 && process.core < totalCores) {
                coreInUse[process.core] = true;
            }
        }
        else {
            finishedProcesses++;
        }
    }

    // Count cores actually in use
    for (bool inUse : coreInUse) {
        if (inUse) coresUsed++;
    }

    int coresAvailable = totalCores - coresUsed;
    double cpuUtilization = totalCores > 0 ? (static_cast<double>(coresUsed) / totalCores) * 100.0 : 0.0;

    // Generate timestamp for the report
    time_t now = time(0);
    tm localtm;
#ifdef _WIN32
    localtime_s(&localtm, &now);
#else
    localtm = *localtime(&now);
#endif
    stringstream timestamp;
    timestamp << put_time(&localtm, "%m/%d/%Y, %I:%M:%S %p");

    // Write report to file
    ofstream reportFile("csopesy-log.txt");
    if (!reportFile.is_open()) {
        cout << "Error: Could not create csopesy-log.txt file." << endl;
        return;
    }

    // Write the exact same format as screen -ls
    reportFile << "CPU UTILIZATION REPORT" << endl;
    reportFile << "Generated: " << timestamp.str() << endl;
    reportFile << "======================================" << endl;
    reportFile << "CPU Utilization: " << fixed << setprecision(2) << cpuUtilization << "%" << endl;
    reportFile << "Cores used: " << coresUsed << endl;
    reportFile << "Cores available: " << coresAvailable << endl;
    reportFile << "Total cores: " << totalCores << endl;
    reportFile << endl;

    // Write running processes
    reportFile << "Running processes:" << endl;
    if (runningProcesses == 0) {
        reportFile << "No running processes." << endl;
    }
    else {
        for (const auto& p : globalProcesses) {
            if (!p.isFinished) {
                tm startTime;
                string startTimeStr = "Waiting...            ";
                if (p.startTime != 0) {
#ifdef _WIN32
                    localtime_s(&startTime, &p.startTime);
#else
                    startTime = *localtime(&p.startTime);
#endif
                    stringstream ss;
                    ss << put_time(&startTime, "%m/%d/%Y %I:%M:%S%p");
                    startTimeStr = ss.str();
                }

                reportFile << left << setw(12) << p.name;
                reportFile << " (" << setw(25) << startTimeStr << ")";
                reportFile << right << setw(8) << "Core: " << (p.core == -1 ? "N/A" : to_string(p.core));
                reportFile << setw(8) << p.tasksCompleted << " / " << p.totalTasks << endl;
            }
        }
    }

    reportFile << endl << "Finished processes:" << endl;
    if (finishedProcesses == 0) {
        reportFile << "No finished processes." << endl;
    }
    else {
        for (const auto& p : globalProcesses) {
            if (p.isFinished) {
                tm endTime;
#ifdef _WIN32
                localtime_s(&endTime, &p.endTime);
#else
                endTime = *localtime(&p.endTime);
#endif
                stringstream ss;
                ss << put_time(&endTime, "%m/%d/%Y %I:%M:%S%p");

                reportFile << left << setw(12) << p.name << " (";
                reportFile << setw(25) << ss.str() << ")";
                reportFile << right << setw(8) << "Core: " << p.core;
                reportFile << right << setw(12) << "Finished";
                reportFile << setw(8) << p.tasksCompleted << " / " << p.totalTasks << endl;
            }
        }
    }

    reportFile << endl << "======================================" << endl;
    reportFile << "Total processes: " << globalProcesses.size() << endl;
    reportFile << "Running: " << runningProcesses << endl;
    reportFile << "Finished: " << finishedProcesses << endl;
    reportFile << "======================================" << endl;

    reportFile.close();

    cout << "CPU utilization report generated and saved to csopesy-log.txt" << endl;
}
// =================== Functions - END =================== //

// ===================== Main ===================== //
int main() {
    string command;
    unordered_map<string, Screen> screens;
    bool inScreen = false;
    string currentScreen;

    enableUTF8Console();
    displayMainMenu();

    // Main command loop
    while (true) {
        if (inScreen) {
            cout << "\nAJEL OS [" << currentScreen << "]> ";
        }
        else {
            // Display main prompt, but only if scheduler isn't running.
            // When scheduler is active, let output from other threads show.
            if (!isSchedulerRunning) {
                cout << "\nAJEL OS> ";
            }
        }

        getline(cin, command);

        if (command == "exit") {
            if (inScreen) {
                inScreen = false;
                displayMainMenu();
            }
            else {
                // If scheduler is running, stop it first.
                if (isSchedulerRunning) {
                    isSchedulerRunning = false;
                    scheduler_cv.notify_all();
                    if (schedulerThread.joinable()) {
                        schedulerThread.join();
                    }
                }
                cout << "Exiting application." << endl;
                break;
            }
        }
        // == Initialize Command ==
        else if (command == "initialize") {
            initializeSystem();
        }
        // Check if system is initialized before allowing other commands
        else if (!isSystemInitialized) {
            cout << "Please initialize the OS first." << endl;
            continue;
        }
        else if (command == "clear") {
            if (inScreen) {
                screens[currentScreen].display();
            }
            else {
                displayMainMenu();
            }
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
            }
            else {
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
            }
            else {
                cout << "Screen \"" << name << "\" not found." << endl;
            }
        }
        // == List Scheduler Processes Command ==
        else if (command == "screen -ls") {
            lock_guard<mutex> lock(processMutex);
            displaySchedulerUI(globalProcesses);
        }
        // == Scheduler-test Command ==
        else if (command == "scheduler-start") {
            if (isSchedulerRunning) {
                cout << "Scheduler is already running." << endl;
                continue;
            }
            /*
            // --- TEST CASE: Create 10 processes, each with 100 print commands ---
            lock_guard<mutex> lock(processMutex);
            globalProcesses.clear();
            for (int i = 1; i <= 10; ++i) {
                stringstream ss;
                ss << "process" << setfill('0') << setw(2) << i;
                globalProcesses.push_back({ ss.str(), 0, 0, -1, 0, 100, false });
            }*/

            isSchedulerRunning = true;
            schedulerThread = thread(FCFSScheduler);
            cout << "Scheduler started with 10 processes on 4 cores." << endl;
            cout << "Type 'screen -ls' to view progress or 'scheduler-stop' to stop." << endl;
        }
        // == Scheduler-stop Command ==
        else if (command == "scheduler-stop") {
            if (!isSchedulerRunning) {
                cout << "Scheduler is not running." << endl;
                continue;
            }

            cout << "Stopping scheduler..." << endl;
            isSchedulerRunning = false;
            scheduler_cv.notify_all(); // Wake up all waiting threads to allow them to exit
            if (schedulerThread.joinable()) {
                schedulerThread.join();
            }

            cout << "Scheduler stopped." << endl;

            lock_guard<mutex> lock(processMutex);
            displaySchedulerUI(globalProcesses);
        }
        else if (command == "report-util") {
            generateUtilizationReport();
        }
        else if (!command.empty()) {
            if (inScreen) {
                screens[currentScreen].advance(); // Advance screen on any command
            }
            else {
                cout << "Command not recognized." << endl;
            }
        }
    }

    return 0;

}
