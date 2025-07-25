/** === CSOPESY MCO2 Multitasking OS ===
 * Group members: (alphabetical)
 * - Abendan, Ashley
 * - Ladrido, Eryl
 * - Rodriguez, Joaquin Andres
 * - Tiu, Lance Wilem
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
#include <algorithm>
#include <cstdint> // For uint16_t


// ===================== Libraries - END ===================== //

using namespace std;

void displayHeader();
void displayMainMenu();
// === [NEW] === Forward declaration for the instruction parser
bool parseInstructionsString(const string& raw_instructions, vector<struct ProcessInstruction>& instructions);


// ======================= Global Variables ======================= //

bool isSchedulerRunning = false;
bool isSystemInitialized = false; // New flag to track system initialization
thread schedulerThread;
vector<thread> cpu_workers;

mutex processMutex; // Used for protecting shared resources like globalProcesses and cout
vector<struct Process> globalProcesses; // List of all processes
bool screenActive = false;

// === [NEW] === Added mutex for the screens map to allow access from worker threads
mutex screensMutex;
unordered_map<string, class Screen> screens;


// --- Threading & Scheduling Variables ---
queue<struct Process*> ready_queue; // Queue of processes ready for CPU execution
mutex queue_mutex;                  // Mutex to protect the ready_queue
condition_variable scheduler_cv;    // Notifies worker threads about new processes

// --- Memory Management Variables (REVISED/ADDED) ---
int current_memory_used = 0;
mutex memory_mutex;
condition_variable memory_cv; // Notifies the admission scheduler about freed memory
queue<struct Process*> waiting_for_memory_queue; // Processes waiting for memory allocation
mutex waiting_queue_mutex;

int quantumCycleCounter = 0;

// --- Configuration Variables ---
struct SystemConfig {
    int num_cpu;
    string scheduler;
    int quantum_cycles;
    int batch_process_freq;
    int min_ins;
    int max_ins;
    int delay_per_exec;
    // Memory allocation parameters
    int max_overall_mem;
    int mem_per_frame;
    int mem_per_proc;

    // Constructor
    SystemConfig() :
        num_cpu(0),
        scheduler(""),
        quantum_cycles(0),
        batch_process_freq(0),
        min_ins(0),
        max_ins(0),
        delay_per_exec(0),
        max_overall_mem(0),
        mem_per_frame(0),
        mem_per_proc(0) {
    }

    // Method to validate configuration
    bool isValid() const {
        return num_cpu > 0 &&
            !scheduler.empty() &&
            quantum_cycles > 0 &&
            batch_process_freq > 0 &&
            min_ins > 0 &&
            max_ins > 0 &&
            max_ins >= min_ins &&
            delay_per_exec >= 0 &&
            // New memory validation
            max_overall_mem > 0 &&
            mem_per_frame > 0 &&
            mem_per_proc > 0 &&
            mem_per_frame <= max_overall_mem &&
            mem_per_proc <= max_overall_mem;
    }
} systemConfig;

// ===================== Global Variables - END ===================== //

// ===================== Structures ===================== //

/**
 * Represents a single process instruction
 */
struct ProcessInstruction {
    enum Type {
        PRINT, DECLARE, ADD, SUBTRACT, SLEEP, FOR_LOOP, READ, WRITE
    };

    Type type{}; // FIX: Initialize the enum
    string var_name;
    int value = 0;
    string message;
    vector<ProcessInstruction> loop_body;
    int loop_count = 0;
    int memory_address = 0;
};

/**
 * Defines the process structure.
 */
struct Process {
    string name;
    time_t startTime;
    time_t endTime;
    int core; // Core that is executing or executed the process
    int tasksCompleted;
    int totalTasks; // Total number of instructions to execute
    bool isFinished;
    vector<ProcessInstruction> instructions; // Process instructions
    unordered_map<string, int> variables;   // Process variables (Symbol Table)

    // === [NEW] === Memory and violation tracking members
    int memorySize; // Process-specific memory allocation
    unordered_map<int, uint16_t> memory_space; // Emulated memory space
    bool has_violation;
    string violation_address;

    int currentInstructionIndex = 0; // track instruction being executed

    // === [MODIFIED] === Updated constructor to include memory size
    Process(const string& processName, int memSize) :
        name(processName), memorySize(memSize), startTime(0), endTime(0), core(-1),
        tasksCompleted(0), totalTasks(0), isFinished(false), has_violation(false), currentInstructionIndex(0) {
    }

    // === [MODIFIED] === Updated default constructor
    Process()
        : name("unnamed"), memorySize(0), startTime(0), endTime(0), core(-1),
        tasksCompleted(0), totalTasks(0), isFinished(false), has_violation(false), currentInstructionIndex(0) {
    }

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
    int memorySize;  // Memory allocated to the process in bytes
    string timestamp;

private:
    bool memoryViolation;        // Flag indicating if memory violation occurred
    string violationTime;        // Time when violation occurred (HH:MM:SS format)
    string violationAddress;     // Hex address that caused the violation

public:
    // Default constructor
    Screen() : name(""), totalLines(100), currentLine(1), memorySize(64), memoryViolation(false) {}

    // Updated constructor to include memory allocation
    Screen(const string& name, int memorySize, int totalLines = 100)
        : name(name), currentLine(1), totalLines(totalLines), memorySize(memorySize), memoryViolation(false) {
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
        cout << "│ Memory Allocated: " << setw(6) << memorySize << " bytes"
            << setw(24) << " " << "│\n";
        cout << "├──────────────────────────────────────────────────────────────┤\n";
        cout << "│ Current Instruction Line: " << setw(6) << currentLine
            << " of " << setw(6) << totalLines
            << " (" << setw(3) << (totalLines > 0 ? (currentLine * 100 / totalLines) : 0) << "%)      │\n";
        cout << "├──────────────────────────────────────────────────────────────┤\n";
        cout << "│ Timestamp: " << left << setw(53) << timestamp << "│\n";
        cout << "└──────────────────────────────────────────────────────────────┘\n";

        cout << "\nProgress:\n[";
        int progressWidth = 50;
        int pos = (totalLines > 0) ? (progressWidth * currentLine / totalLines) : 0;
        for (int i = 0; i < progressWidth; ++i) {
            if (i < pos) cout << "=";
            else if (i == pos) cout << ">";
            else cout << " ";
        }
        cout << "] " << (totalLines > 0 ? (currentLine * 100 / totalLines) : 0) << "%\n";

        cout << "\nType \"exit\" to return to main menu" << endl;
    }

    void advance() {
        if (currentLine < totalLines)
            currentLine++;
    }

    // Getter for memory size
    int getMemorySize() const {
        return memorySize;
    }

    // Memory violation related methods
    bool hasMemoryViolation() const {
        return memoryViolation;
    }

    string getViolationTime() const {
        return violationTime;
    }

    string getViolationAddress() const {
        return violationAddress;
    }

    // Method to trigger a memory access violation
    void triggerMemoryViolation(const string& hexAddress) {
        memoryViolation = true;
        violationAddress = hexAddress;

        // Get current time in HH:MM:SS format
        time_t now = time(0);
        tm localtm;
#ifdef _WIN32
        localtime_s(&localtm, &now);
#else
        localtm = *localtime(&now);
#endif
        stringstream ss;
        ss << put_time(&localtm, "%H:%M:%S");
        violationTime = ss.str();
    }

    // Method to simulate a random memory violation (for testing purposes)
    void simulateMemoryViolation() {
        // Generate a random hex address outside the allocated memory range
        srand(static_cast<unsigned int>(time(nullptr)));
        int invalidAddress = memorySize + (rand() % 1000) + 1;

        stringstream ss;
        ss << "0x" << hex << uppercase << invalidAddress;
        triggerMemoryViolation(ss.str());
    }
};
// =================== Classes - END =================== //

// ===================== Functions ===================== //


void generateMemorySnapshot(int quantumCycle) {
    time_t now = time(0);
    tm localtm;
#ifdef _WIN32
    localtime_s(&localtm, &now);
#else
    localtm = *localtime(&now);
#endif
    stringstream timestamp;
    timestamp << put_time(&localtm, "(%m/%d/%Y %I:%M:%S%p)");

    vector<tuple<int, string, int>> memoryLayout;
    int nextAddress = 0;

    {
        lock_guard<mutex> lock(processMutex);
        for (const auto& proc : globalProcesses) {
            if (!proc.isFinished && proc.startTime != 0) {
                int start = nextAddress;
                int end = start + proc.memorySize; // Use process-specific memory size
                memoryLayout.emplace_back(end, proc.name, start);
                nextAddress = end;
            }
        }
    }

    int totalExternalFragmentation = systemConfig.max_overall_mem - nextAddress;

    stringstream filename;
    filename << "memory_stamp_" << setfill('0') << setw(2) << quantumCycle << ".txt";
    ofstream outfile(filename.str());
    if (!outfile.is_open()) return;

    outfile << "Timestamp: " << timestamp.str() << endl;
    outfile << "Number of processes in memory: " << memoryLayout.size() << endl;
    outfile << "Total external fragmentation in KB: " << totalExternalFragmentation << endl;
    outfile << "----end---- = " << systemConfig.max_overall_mem << endl;

    sort(memoryLayout.begin(), memoryLayout.end(), [](const tuple<int, string, int>& a, const tuple<int, string, int>& b) {
        return get<0>(a) > get<0>(b); // Sort by end address in descending order
        });

    for (const auto& [end, name, start] : memoryLayout) {
        outfile << end << endl;
        outfile << name << endl;
        outfile << start << endl;
    }

    outfile << "----start-- = 0" << endl;
    outfile.close();
}


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
        cout << "max-overall-mem=8192" << endl;
        cout << "mem-per-frame=256" << endl;
        cout << "mem-per-proc=1024" << endl;
        return false;
    }

    string line;
    vector<string> missingKeys;
    vector<string> requiredKeys = { "num-cpu", "scheduler", "quantum-cycles",
                                   "batch-process-freq", "min-ins", "max-ins", "delay-per-exec",
        // New required keys
        "max-overall-mem", "mem-per-frame", "mem-per-proc" };
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
            }
            else if (key == "scheduler") {
                systemConfig.scheduler = value;
                keyFound[1] = true;
                cout << "  ✓ scheduler: " << systemConfig.scheduler << endl;
            }
            else if (key == "quantum-cycles") {
                systemConfig.quantum_cycles = stoi(value);
                keyFound[2] = true;
                cout << "  ✓ quantum-cycles: " << systemConfig.quantum_cycles << endl;
            }
            else if (key == "batch-process-freq") {
                systemConfig.batch_process_freq = stoi(value);
                keyFound[3] = true;
                cout << "  ✓ batch-process-freq: " << systemConfig.batch_process_freq << endl;
            }
            else if (key == "min-ins") {
                systemConfig.min_ins = stoi(value);
                keyFound[4] = true;
                cout << "  ✓ min-ins: " << systemConfig.min_ins << endl;
            }
            else if (key == "max-ins") {
                systemConfig.max_ins = stoi(value);
                keyFound[5] = true;
                cout << "  ✓ max-ins: " << systemConfig.max_ins << endl;
            }
            else if (key == "delay-per-exec") {
                systemConfig.delay_per_exec = stoi(value);
                keyFound[6] = true;
                cout << "  ✓ delay-per-exec: " << systemConfig.delay_per_exec << " ms" << endl;
            }
            else if (key == "max-overall-mem") {
                systemConfig.max_overall_mem = stoi(value);
                keyFound[7] = true;
                cout << "  ✓ max-overall-mem: " << systemConfig.max_overall_mem << endl;
            }
            else if (key == "mem-per-frame") {
                systemConfig.mem_per_frame = stoi(value);
                keyFound[8] = true;
                cout << "  ✓ mem-per-frame: " << systemConfig.mem_per_frame << endl;
            }
            else if (key == "mem-per-proc") {
                systemConfig.mem_per_proc = stoi(value);
                keyFound[9] = true;
                cout << "  ✓ mem-per-proc: " << systemConfig.mem_per_proc << endl;
            }
            else {
                cout << "Warning: Unknown configuration key ignored: " << key << endl;
            }
        }
        catch (const exception) {
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
    cout << "├── Delay per Execution: " << systemConfig.delay_per_exec << " ms" << endl;
    // Display memory parameters
    cout << "├── Max Overall Memory: " << systemConfig.max_overall_mem << " KB" << endl;
    cout << "├── Memory per Frame: " << systemConfig.mem_per_frame << " KB" << endl;
    cout << "└── Memory per Process: " << systemConfig.mem_per_proc << " KB" << endl;
    cout << string(50, '=') << endl;

    // Initialize system components
    globalProcesses.clear();

    // Mark system as initialized
    isSystemInitialized = true;
    cout << "\nSystem initialized successfully!" << endl;
    cout << "You can now use scheduler-start to begin process scheduling." << endl;
}



/**
 * Generate random process instructions based on config parameters
 */
vector<ProcessInstruction> generateProcessInstructions(int minInstructions, int maxInstructions) {
    vector<ProcessInstruction> instructions;
    int totalInstructions = minInstructions + (rand() % (maxInstructions - minInstructions + 1));

    // Generate random instructions
    for (int i = 0; i < totalInstructions; ++i) {
        ProcessInstruction instr;
        // === [MODIFIED] === Added READ/WRITE to random instruction generation
        int instrType = rand() % 8; // 0-7 for different instruction types

        switch (instrType) {
        case 0: // PRINT
            instr.type = ProcessInstruction::PRINT;
            instr.message = "Hello world from process!";
            break;

        case 1: // DECLARE
            instr.type = ProcessInstruction::DECLARE;
            instr.var_name = "var" + to_string(rand() % 10 + 1); // var1-var10
            instr.value = rand() % 100;
            break;

        case 2: // ADD
            instr.type = ProcessInstruction::ADD;
            instr.var_name = "var" + to_string(rand() % 10 + 1);
            instr.value = rand() % 50 + 1;
            break;

        case 3: // SUBTRACT
            instr.type = ProcessInstruction::SUBTRACT;
            instr.var_name = "var" + to_string(rand() % 10 + 1);
            instr.value = rand() % 50 + 1;
            break;

        case 4: // SLEEP
            instr.type = ProcessInstruction::SLEEP;
            instr.value = rand() % 5 + 1; // Sleep for 1-5 cycles
            break;

        case 5: { // FOR_LOOP (simple, max 3 iterations)
            instr.type = ProcessInstruction::FOR_LOOP;
            instr.loop_count = rand() % 3 + 1; // 1-3 iterations
            // Add a simple PRINT instruction inside the loop
            ProcessInstruction loopInstr;
            loopInstr.type = ProcessInstruction::PRINT;
            loopInstr.message = "Loop iteration";
            instr.loop_body.push_back(loopInstr);
            break;
        } // <--- Closing brace for the new scope

            // === [NEW] === Logic for generating READ/WRITE instructions
        case 6: {// READ
            instr.type = ProcessInstruction::READ;
            instr.var_name = "var" + to_string(rand() % 10 + 1);
            // Generate a random address within a 1KB space for testing
            instr.memory_address = rand() % 1024;
            break;
        }
        case 7: // WRITE
            instr.type = ProcessInstruction::WRITE;
            // First, ensure a variable is declared to write from it
            {
                ProcessInstruction decl_instr;
                decl_instr.type = ProcessInstruction::DECLARE;
                decl_instr.var_name = "write_var" + to_string(rand() % 5);
                decl_instr.value = rand() % 500;
                instructions.push_back(decl_instr);
                // Now setup the WRITE instruction
                instr.var_name = decl_instr.var_name;
            }
            instr.memory_address = rand() % 1024;
            break;
        }

        instructions.push_back(instr);
    }

    return instructions;
}

/**
 * Execute a single process instruction
 */
bool executeInstruction(Process* process, const ProcessInstruction& instr, int coreId, ofstream& logFile) {
    // Generate timestamp
    time_t now = time(0);
    tm localtm;
#ifdef _WIN32
    localtime_s(&localtm, &now);
#else
    localtm = *localtime(&now);
#endif
    stringstream timestamp;
    timestamp << put_time(&localtm, "(%m/%d/%Y %I:%M:%S %p)");

    // === [MODIFIED] === Added logic for new instructions and symbol table limit
    switch (instr.type) {
    case ProcessInstruction::PRINT:
        logFile << timestamp.str() << " Core:" << coreId << " \"" << instr.message << "\"" << endl;
        this_thread::sleep_for(chrono::milliseconds(systemConfig.delay_per_exec));
        {
            lock_guard<mutex> lock(processMutex);
            process->tasksCompleted++;
        }
        break;

    case ProcessInstruction::DECLARE:
        // Spec: Symbol table has fixed size of 64 bytes for max 32 variables.
        if (process->variables.size() >= 32) {
            logFile << timestamp.str() << " Core:" << coreId << " DECLARE " << instr.var_name << " ignored. Symbol table full." << endl;
        }
        else {
            process->variables[instr.var_name] = instr.value;
            logFile << timestamp.str() << " Core:" << coreId << " DECLARE " << instr.var_name
                << " = " << instr.value << endl;
            this_thread::sleep_for(chrono::milliseconds(systemConfig.delay_per_exec));
            {
                lock_guard<mutex> lock(processMutex);
                process->tasksCompleted++;
            }
        }
        break;

    case ProcessInstruction::ADD:
        if (process->variables.find(instr.var_name) == process->variables.end()) {
            process->variables[instr.var_name] = 0; // Auto-declare with 0
        }
        process->variables[instr.var_name] += instr.value;
        logFile << timestamp.str() << " Core:" << coreId << " ADD " << instr.var_name
            << " " << instr.value << " (result: " << process->variables[instr.var_name] << ")" << endl;
        this_thread::sleep_for(chrono::milliseconds(systemConfig.delay_per_exec));
        {
            lock_guard<mutex> lock(processMutex);
            process->tasksCompleted++;
        }
        break;

    case ProcessInstruction::SUBTRACT:

        if (process->variables.find(instr.var_name) == process->variables.end()) {
            process->variables[instr.var_name] = 0; // Auto-declare with 0
        }
        process->variables[instr.var_name] -= instr.value;
        logFile << timestamp.str() << " Core:" << coreId << " SUBTRACT " << instr.var_name
            << " " << instr.value << " (result: " << process->variables[instr.var_name] << ")" << endl;
        this_thread::sleep_for(chrono::milliseconds(systemConfig.delay_per_exec));
        {
            lock_guard<mutex> lock(processMutex);
            process->tasksCompleted++;
        }
        break;

    case ProcessInstruction::SLEEP:
        logFile << timestamp.str() << " Core:" << coreId << " SLEEP " << instr.value << endl;
        {
            lock_guard<mutex> lock(processMutex);
            process->tasksCompleted++;
        }
        // Sleep for the specified number of cycles
        for (int i = 0; i < instr.value; ++i) {
            this_thread::sleep_for(chrono::milliseconds(systemConfig.delay_per_exec));
            if (!isSchedulerRunning) return false; // Allow early termination
        }
        break;

    case ProcessInstruction::FOR_LOOP:
        logFile << timestamp.str() << " Core:" << coreId << " FOR_LOOP " << instr.loop_count << " iterations" << endl;
        {
            lock_guard<mutex> lock(processMutex);
            process->tasksCompleted++;
        }
        for (int i = 0; i < instr.loop_count; ++i) {
            for (const auto& loopInstr : instr.loop_body) {
                if (!executeInstruction(process, loopInstr, coreId, logFile)) {
                    return false; // Early termination
                }
                if (!isSchedulerRunning) return false;
            }
        }
        break;

        // === [NEW] === Execution logic for READ and WRITE
    case ProcessInstruction::READ: {
        // Spec: check symbol table limit before adding a new variable via READ
        if (process->variables.find(instr.var_name) == process->variables.end() && process->variables.size() >= 32) {
            logFile << timestamp.str() << " Core:" << coreId << " READ into " << instr.var_name << " ignored. Symbol table full." << endl;
            break; // Don't execute, don't count as a completed task
        }

        // Check for memory access violation
        if (instr.memory_address < 0 || instr.memory_address >= process->memorySize) {
            process->isFinished = true;
            process->has_violation = true;
            stringstream ss;
            ss << "0x" << hex << uppercase << instr.memory_address;
            process->violation_address = ss.str();
            logFile << timestamp.str() << " Core:" << coreId << " MEMORY VIOLATION on READ at " << process->violation_address << ". Process terminated." << endl;
            return false; // Stop execution for this process
        }

        // Read value. Spec: returns 0 if uninitialized.
        uint16_t value_read = 0;
        if (process->memory_space.count(instr.memory_address)) {
            value_read = process->memory_space.at(instr.memory_address);
        }
        process->variables[instr.var_name] = value_read;
        logFile << timestamp.str() << " Core:" << coreId << " READ " << instr.var_name << " from 0x" << hex << instr.memory_address << " (Value: " << dec << value_read << ")" << endl;
        this_thread::sleep_for(chrono::milliseconds(systemConfig.delay_per_exec));
        {
            lock_guard<mutex> lock(processMutex);
            process->tasksCompleted++;
        }
        break;
    }

    case ProcessInstruction::WRITE: {
        // Check if source variable exists
        if (process->variables.find(instr.var_name) == process->variables.end()) {
            logFile << timestamp.str() << " Core:" << coreId << " WRITE failed. Source variable '" << instr.var_name << "' not declared." << endl;
            break; // Skip instruction
        }

        // Check for memory access violation
        if (instr.memory_address < 0 || instr.memory_address >= process->memorySize) {
            process->isFinished = true;
            process->has_violation = true;
            stringstream ss;
            ss << "0x" << hex << uppercase << instr.memory_address;
            process->violation_address = ss.str();
            logFile << timestamp.str() << " Core:" << coreId << " MEMORY VIOLATION on WRITE at " << process->violation_address << ". Process terminated." << endl;
            return false; // Stop execution for this process
        }

        int value_from_var = process->variables.at(instr.var_name);
        // Spec: Clamp value to uint16 range (0, 65535)
        uint16_t value_to_write = static_cast<uint16_t>(max(0, min(value_from_var, 65535)));

        process->memory_space[instr.memory_address] = value_to_write;
        logFile << timestamp.str() << " Core:" << coreId << " WRITE " << dec << value_to_write << " to 0x" << hex << instr.memory_address << endl;
        this_thread::sleep_for(chrono::milliseconds(systemConfig.delay_per_exec));
        {
            lock_guard<mutex> lock(processMutex);
            process->tasksCompleted++;
        }
        break;
    }

    }

    return true;
}

/**
 * Count total instructions (including loop iterations)
 */
int countTotalInstructions(const vector<ProcessInstruction>& instructions) {
    int total = 0;
    for (const auto& instr : instructions) {
        if (instr.type == ProcessInstruction::FOR_LOOP) {
            total += 1; // For the loop declaration
            total += instr.loop_count * countTotalInstructions(instr.loop_body);
        }
        else {
            total += 1;
        }
    }
    return total;
}

/**
 * Displays the Scheduler UI, showing running and finished processes.
 * REVISED to show memory usage and waiting processes.
 */
void displaySchedulerUI(const vector<Process>& processes) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif

    displayHeader();

    // Lock memory and process data to ensure consistent reads
    lock_guard<mutex> mem_lock(memory_mutex);
    lock_guard<mutex> proc_lock(processMutex);

    // Calculate CPU utilization and process statistics
    int totalCores = systemConfig.num_cpu;
    int coresUsed = 0;
    int runningProcesses = 0;
    int waitingProcesses = 0;
    int finishedProcesses = 0;
    vector<bool> coreInUse(totalCores, false);

    for (const auto& process : processes) {
        if (process.isFinished) {
            finishedProcesses++;
        }
        else {
            // A process is running if it has a start time.
            if (process.startTime != 0) {
                runningProcesses++;
                if (process.core != -1 && process.core < totalCores) {
                    coreInUse[process.core] = true;
                }
            }
            else {
                // Otherwise, it's waiting for memory.
                waitingProcesses++;
            }
        }
    }

    for (bool inUse : coreInUse) {
        if (inUse) coresUsed++;
    }

    int coresAvailable = totalCores - coresUsed;
    double cpuUtilization = totalCores > 0 ? (static_cast<double>(coresUsed) / totalCores) * 100.0 : 0.0;
    double memUtilization = systemConfig.max_overall_mem > 0 ? (static_cast<double>(current_memory_used) / systemConfig.max_overall_mem) * 100.0 : 0.0;

    cout << "SYSTEM STATUS REPORT" << endl;
    cout << "======================================" << endl;
    cout << "CPU Utilization: " << fixed << setprecision(2) << cpuUtilization << "%" << endl;
    cout << "Memory Utilization: " << current_memory_used << " / " << systemConfig.max_overall_mem
        << " KB (" << fixed << setprecision(2) << memUtilization << "%)" << endl;
    cout << "Cores used: " << coresUsed << " | Cores available: " << coresAvailable << " | Total cores: " << totalCores << endl;
    cout << endl;

    cout << "Running processes:" << endl;
    if (runningProcesses == 0) {
        cout << "No running processes." << endl;
    }
    else {
        for (const auto& p : processes) {
            if (!p.isFinished && p.startTime != 0) {
                tm localtm;
                string startTimeStr;
#ifdef _WIN32
                localtime_s(&localtm, &p.startTime);
#else
                localtm = *localtime(&p.startTime);
#endif
                stringstream ss;
                ss << put_time(&localtm, "%m/%d/%Y %I:%M:%S%p");
                startTimeStr = ss.str();

                cout << left << setw(12) << p.name;
                cout << " (" << setw(25) << startTimeStr << ")";
                cout << right << setw(8) << "Core: " << (p.core == -1 ? "N/A" : to_string(p.core));
                cout << setw(8) << p.tasksCompleted << " / " << p.totalTasks << endl;
            }
        }
    }

    cout << endl << "Waiting for Memory:" << endl;
    if (waitingProcesses == 0) {
        cout << "No processes waiting for memory." << endl;
    }
    else {
        for (const auto& p : processes) {
            if (!p.isFinished && p.startTime == 0) {
                cout << left << setw(12) << p.name;
                cout << " (Requires: " << p.memorySize << " KB)" << endl;
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
                // === [MODIFIED] === Display memory violation status
                if (p.has_violation) {
                    cout << right << setw(12) << "VIOLATION";
                }
                else {
                    cout << right << setw(12) << "Finished";
                }
                cout << setw(8) << p.tasksCompleted << " / " << p.totalTasks << endl;
            }
        }
    }

    cout << endl << "======================================" << endl;
    cout << "Total processes: " << processes.size() << endl;
    cout << "Running: " << runningProcesses << " | Waiting: " << waitingProcesses << " | Finished: " << finishedProcesses << endl;
    cout << "======================================" << endl;
}


/**
 * @brief This is the main function for each CPU worker thread.
 * REVISED: When a process finishes, it now releases its memory and
 * notifies the admission scheduler.
 */
void cpu_worker_main(int coreId) {
    while (isSchedulerRunning) {
        Process* currentProcess = nullptr;

        // Get process from queue
        {
            unique_lock<mutex> lock(queue_mutex);
            scheduler_cv.wait(lock, [] {
                return !ready_queue.empty() || !isSchedulerRunning;
                });

            if (!isSchedulerRunning && ready_queue.empty()) return;
            if (!ready_queue.empty()) {
                currentProcess = ready_queue.front();
                ready_queue.pop();
            }
        }

        if (currentProcess) {
            // Set start time only once
            {
                lock_guard<mutex> lock(processMutex);
                if (currentProcess->startTime == 0) {
                    currentProcess->startTime = time(nullptr);
                }
                currentProcess->core = coreId;  // Update current core
            }

            // Open log file in append mode
            string logFileName = currentProcess->name + ".txt";
            ofstream outfile(logFileName, ios::app);

            int& index = currentProcess->currentInstructionIndex;
            int executedInstructions = 0;
            while (index < currentProcess->instructions.size() &&
                (!systemConfig.scheduler.compare("fcfs") || executedInstructions < systemConfig.quantum_cycles)) {

                if (!isSchedulerRunning) break;

                const ProcessInstruction& instr = currentProcess->instructions[index];
                if (!executeInstruction(currentProcess, instr, coreId, outfile)) {
                    // Instruction caused termination (e.g., memory violation)
                    break;
                }

                {
                    lock_guard<mutex> lock(processMutex);
                    currentProcess->currentInstructionIndex++;
                }

                executedInstructions++;
                generateMemorySnapshot(quantumCycleCounter++);
            }

            outfile.close();

            // Check if process finished
            bool finished = false;
            {
                lock_guard<mutex> lock(processMutex);
                // A process is finished if its instruction index is out of bounds OR it has a violation
                if (currentProcess->currentInstructionIndex >= currentProcess->instructions.size() || currentProcess->has_violation) {
                    currentProcess->endTime = time(nullptr);
                    currentProcess->isFinished = true;
                    finished = true;

                    // === [NEW] === If a violation occurred, update the associated Screen object for Eryl's task
                    if (currentProcess->has_violation) {
                        lock_guard<mutex> screen_lock(screensMutex);
                        if (screens.count(currentProcess->name)) {
                            screens.at(currentProcess->name).triggerMemoryViolation(currentProcess->violation_address);
                        }
                    }
                }
            }

            // If finished, release memory and notify admission scheduler. (REVISED)
            if (finished) {
                {
                    lock_guard<mutex> mem_lock(memory_mutex);
                    current_memory_used -= currentProcess->memorySize;
                }
                memory_cv.notify_one(); // Signal that memory has been freed
            }
            // Requeue if not finished (RR only)
            else if (systemConfig.scheduler == "rr" && isSchedulerRunning) {
                lock_guard<mutex> lock(queue_mutex);
                ready_queue.push(currentProcess);
                scheduler_cv.notify_one();
            }
        }
    }
}


/**
 * @brief NEW function: The main admission scheduler thread function.
 * This function admits processes from the waiting queue into the ready queue
 * if there is enough available memory. It then lets the CPU workers handle
 * execution.
 */
void admissionScheduler() {
    // --- Start all CPU worker threads ---
    cpu_workers.clear(); // Clear any previous worker threads
    for (int i = 0; i < systemConfig.num_cpu; ++i) {
        cpu_workers.emplace_back(cpu_worker_main, i);
    }

    // --- Main admission loop ---
    while (isSchedulerRunning) {
        unique_lock<mutex> lock(memory_mutex);

        // === [MODIFIED] === Admission logic now checks process-specific memory size
        memory_cv.wait(lock, [] {
            if (waiting_for_memory_queue.empty()) return !isSchedulerRunning;
            Process* next_proc = waiting_for_memory_queue.front();
            bool can_admit = (current_memory_used + next_proc->memorySize <= systemConfig.max_overall_mem);
            return can_admit || !isSchedulerRunning;
            });

        if (!isSchedulerRunning) break;

        // Admit all possible processes while memory is available
        while (!waiting_for_memory_queue.empty()) {
            Process* proc_to_admit = nullptr;
            {
                // Check memory requirement before locking the waiting queue
                lock_guard<mutex> wait_lock(waiting_queue_mutex);
                if (!waiting_for_memory_queue.empty()) {
                    Process* next_proc = waiting_for_memory_queue.front();
                    if (current_memory_used + next_proc->memorySize <= systemConfig.max_overall_mem) {
                        proc_to_admit = next_proc;
                        waiting_for_memory_queue.pop();
                    }
                    else {
                        break; // Not enough memory for the next process, so stop trying
                    }
                }
            }

            if (proc_to_admit) {
                current_memory_used += proc_to_admit->memorySize;

                {
                    lock_guard<mutex> ready_lock(queue_mutex);
                    ready_queue.push(proc_to_admit);
                }
                scheduler_cv.notify_one(); // Notify one worker
            }
        }
    }

    // --- Cleanup: Wait for all worker threads to complete ---
    scheduler_cv.notify_all(); // Wake up any sleeping workers so they can exit
    for (auto& worker : cpu_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
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
    cout << "  initialize                         - Initialize the system with config parameters" << endl;
    cout << "  process-smi                        - Check the progress of your process" << endl;
    cout << "  screen -s <name> <memory>          - Create a new screen and declare memory allocation" << endl;
    // === [NEW] === Added screen -c to help menu
    cout << "  screen -c <name> <mem> \"<instr>\"   - Create a process with user-defined instructions" << endl;
    cout << "  screen -r <name>                   - Resume a screen" << endl;
    cout << "  screen -ls                         - List running/finished processes and system status" << endl;
    cout << "  scheduler-start                    - Start the scheduler" << endl;
    cout << "  scheduler-stop                     - Stop the scheduler" << endl;
    cout << "  report-util                        - Generate CPU and memory utilization report" << endl;
    cout << "  clear                              - Clear the screen" << endl;
    cout << "  exit                               - Exit the program" << endl;
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
    int waitingProcesses = 0;

    // Count cores in use and process statistics
    vector<bool> coreInUse(totalCores, false);
    for (const auto& process : globalProcesses) {
        if (process.isFinished) {
            finishedProcesses++;
        }
        else if (process.startTime != 0) {
            runningProcesses++;
            if (process.core != -1 && process.core < totalCores) {
                coreInUse[process.core] = true;
            }
        }
        else {
            waitingProcesses++;
        }
    }

    // Count cores actually in use
    for (bool inUse : coreInUse) {
        if (inUse) coresUsed++;
    }

    int coresAvailable = totalCores - coresUsed;
    double cpuUtilization = totalCores > 0 ? (static_cast<double>(coresUsed) / totalCores) * 100.0 : 0.0;
    double memUtilization = systemConfig.max_overall_mem > 0 ? (static_cast<double>(current_memory_used) / systemConfig.max_overall_mem) * 100.0 : 0.0;

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
    reportFile << "SYSTEM STATUS REPORT" << endl;
    reportFile << "Generated: " << timestamp.str() << endl;
    reportFile << "======================================" << endl;
    reportFile << "CPU Utilization: " << fixed << setprecision(2) << cpuUtilization << "%" << endl;
    reportFile << "Memory Utilization: " << current_memory_used << " / " << systemConfig.max_overall_mem
        << " KB (" << fixed << setprecision(2) << memUtilization << "%)" << endl;
    reportFile << "Cores used: " << coresUsed << " | Cores available: " << coresAvailable << " | Total cores: " << totalCores << endl;
    reportFile << endl;


    // Write running processes
    reportFile << "Running processes:" << endl;
    if (runningProcesses == 0) {
        reportFile << "No running processes." << endl;
    }
    else {
        for (const auto& p : globalProcesses) {
            if (!p.isFinished && p.startTime != 0) {
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

    reportFile << endl << "Waiting for Memory:" << endl;
    if (waitingProcesses == 0) {
        reportFile << "No processes waiting for memory." << endl;
    }
    else {
        for (const auto& p : globalProcesses) {
            if (!p.isFinished && p.startTime == 0) {
                reportFile << left << setw(12) << p.name;
                reportFile << " (Requires: " << p.memorySize << " KB)" << endl;
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
                if (p.has_violation) {
                    reportFile << right << setw(12) << "VIOLATION";
                }
                else {
                    reportFile << right << setw(12) << "Finished";
                }
                reportFile << setw(8) << p.tasksCompleted << " / " << p.totalTasks << endl;
            }
        }
    }

    reportFile << endl << "======================================" << endl;
    reportFile << "Total processes: " << globalProcesses.size() << endl;
    reportFile << "Running: " << runningProcesses << " | Waiting: " << waitingProcesses << " | Finished: " << finishedProcesses << endl;
    reportFile << "======================================" << endl;

    reportFile.close();

    cout << "System status report generated and saved to csopesy-log.txt" << endl;
}

// === [NEW] === Helper function to parse user-defined instruction strings
bool parseInstructionsString(const string& raw_instructions, vector<ProcessInstruction>& instructions) {
    stringstream ss(raw_instructions);
    string segment;

    while (getline(ss, segment, ';')) {
        // Trim leading/trailing whitespace from the segment
        segment.erase(0, segment.find_first_not_of(" \t\r\n"));
        segment.erase(segment.find_last_not_of(" \t\r\n") + 1);
        if (segment.empty()) continue;

        stringstream line_ss(segment);
        string command;
        line_ss >> command;

        ProcessInstruction instr;
        bool success = true;

        if (command == "DECLARE") {
            string var, val_str;
            if (line_ss >> var >> val_str) {
                instr.type = ProcessInstruction::DECLARE;
                instr.var_name = var;
                try {
                    instr.value = stoi(val_str);
                }
                catch (...) { success = false; }
            }
            else { success = false; }
        }
        else if (command == "ADD") {
            string var, val_str;
            if (line_ss >> var >> val_str) {
                instr.type = ProcessInstruction::ADD;
                instr.var_name = var;
                try {
                    instr.value = stoi(val_str);
                }
                catch (...) { success = false; }
            }
            else { success = false; }
        }
        else if (command == "SUBTRACT") {
            string var, val_str;
            if (line_ss >> var >> val_str) {
                instr.type = ProcessInstruction::SUBTRACT;
                instr.var_name = var;
                try {
                    instr.value = stoi(val_str);
                }
                catch (...) { success = false; }
            }
            else { success = false; }
        }
        else if (command == "READ") {
            string var, addr_str;
            if (line_ss >> var >> addr_str) {
                instr.type = ProcessInstruction::READ;
                instr.var_name = var;
                try {
                    instr.memory_address = stoul(addr_str, nullptr, 0); // 0 allows auto-detect (e.g. 0x)
                }
                catch (...) { success = false; }
            }
            else { success = false; }
        }
        else if (command == "WRITE") {
            string addr_str, var;
            if (line_ss >> addr_str >> var) {
                instr.type = ProcessInstruction::WRITE;
                instr.var_name = var;
                try {
                    instr.memory_address = stoul(addr_str, nullptr, 0);
                }
                catch (...) { success = false; }
            }
            else { success = false; }
        }
        else if (command == "PRINT") {
            // Simple version: PRINT "message here"
            string message;
            // find the first quote
            size_t first_quote = segment.find('"');
            size_t last_quote = segment.rfind('"');
            if (first_quote != string::npos && last_quote != string::npos && first_quote != last_quote) {
                instr.type = ProcessInstruction::PRINT;
                instr.message = segment.substr(first_quote + 1, last_quote - first_quote - 1);
            }
            else { success = false; }
        }
        else {
            success = false; // Unknown command
        }

        if (success) {
            instructions.push_back(instr);
        }
        else {
            cout << "Error parsing instruction: " << segment << endl;
            return false; // Stop parsing on error
        }
    }

    // Spec: 1-50 instructions
    if (instructions.size() < 1 || instructions.size() > 50) {
        cout << "Error: Instruction count must be between 1 and 50. Found: " << instructions.size() << endl;
        return false;
    }

    return true;
}


// =================== Functions - END =================== //

// ===================== Main ===================== //
int main() {
    string command;
    // unordered_map<string, Screen> screens; // Moved to global scope
    bool inScreen = false;
    string currentScreen;

    enableUTF8Console();
    displayMainMenu();

    while (true) {
        if (inScreen) {
            cout << "\nAJEL OS [" << currentScreen << "]> ";
        }
        else {
            cout << "\nAJEL OS> ";
        }

        getline(cin, command);

        if (command == "exit") {
            if (inScreen) {
                inScreen = false;
                displayMainMenu();
            }
            else {
                if (isSchedulerRunning) {
                    isSchedulerRunning = false;
                    memory_cv.notify_all(); // Wake up admission scheduler
                    scheduler_cv.notify_all(); // Wake up workers
                    if (schedulerThread.joinable()) {
                        schedulerThread.join();
                    }
                }
                cout << "Exiting application." << endl;
                break;
            }
        }
        else if (command == "initialize") {
            initializeSystem();
        }
        else if (!isSystemInitialized) {
            cout << "Please initialize the OS first." << endl;
            continue;
        }
        else if (command == "clear") {
            if (inScreen) {
                lock_guard<mutex> lock(screensMutex);
                screens[currentScreen].display();
            }
            else {
                displayMainMenu();
            }
        }
        else if (command == "process-smi") {
            lock_guard<mutex> lock(processMutex);
            int idx = 1;
            for (const auto& proc : globalProcesses) {
                cout << "\n== Process " << idx++ << " ==\n";
                cout << "Name: " << proc.name << "\n";
                cout << "Core: " << (proc.core == -1 ? "N/A" : to_string(proc.core)) << "\n";
                cout << "Progress: " << proc.tasksCompleted << " / " << proc.totalTasks << "\n";

                string status;
                if (proc.isFinished) {
                    status = proc.has_violation ? "Terminated (Violation)" : "Finished!";
                }
                else if (proc.startTime != 0) status = "Running";
                else status = "Waiting for Memory";
                cout << "Status: " << status << "\n";

                cout << "-- Log Output --\n";
                string logFileName = proc.name + ".txt";
                ifstream infile(logFileName);
                if (infile.is_open()) {
                    string line;
                    int count = 0;
                    while (getline(infile, line) && count < 5) {
                        cout << "  " << line << "\n";
                        count++;
                    }
                    if (count > 0) cout << "  (See " << logFileName << " for full log)\n";
                    infile.close();
                }
                else {
                    cout << "  (No log found)\n";
                }
            }
        }
        else if (command.rfind("screen -s ", 0) == 0) {
            if (inScreen) {
                cout << "Cannot create new screen while inside a screen. Type 'exit' first." << endl;
                continue;
            }

            // Parse the command to extract process name and memory size
            string params = command.substr(10); // Remove "screen -s "
            istringstream iss(params);
            string name;
            string memorySizeStr;

            // Extract process name and memory size
            if (!(iss >> name >> memorySizeStr)) {
                cout << "Usage: screen -s <process_name> <process_memory_size>" << endl;
                continue;
            }

            // Convert memory size string to integer
            int memorySize;
            try {
                memorySize = stoi(memorySizeStr);
            }
            catch (const invalid_argument) {
                cout << "Invalid memory allocation" << endl;
                continue;
            }
            catch (const out_of_range) {
                cout << "Invalid memory allocation" << endl;
                continue;
            }

            // Validate memory size is within range [2^6, 2^16] = [64, 65536]
            if (memorySize < 64 || memorySize > 65536) {
                cout << "Invalid memory allocation" << endl;
                continue;
            }

            // Validate memory size is a power of 2
            bool isPowerOfTwo = (memorySize > 0) && ((memorySize & (memorySize - 1)) == 0);
            if (!isPowerOfTwo) {
                cout << "Invalid memory allocation" << endl;
                continue;
            }

            // Check if screen already exists
            lock_guard<mutex> lock(screensMutex);
            if (screens.find(name) == screens.end()) {
                screens[name] = Screen(name, memorySize);
                cout << "Screen \"" << name << "\" created with " << memorySize << " bytes allocated." << endl;
            }
            else {
                cout << "Screen \"" << name << "\" already exists." << endl;
            }
        }
        // === [NEW] === Implementation of the screen -c command
        else if (command.rfind("screen -c ", 0) == 0) {
            if (!isSchedulerRunning) {
                cout << "Scheduler is not running. Cannot create new processes." << endl;
                continue;
            }

            // Parse: screen -c <name> <mem> "<instructions>"
            string cmd_part = command.substr(10);
            stringstream ss(cmd_part);
            string name, mem_str;

            ss >> name >> mem_str;

            size_t first_quote = cmd_part.find('"');
            size_t last_quote = cmd_part.rfind('"');

            if (name.empty() || mem_str.empty() || first_quote == string::npos || last_quote == string::npos || first_quote == last_quote) {
                cout << "Invalid command. Usage: screen -c <name> <mem> \"<instructions>\"" << endl;
                continue;
            }

            string instruction_str = cmd_part.substr(first_quote + 1, last_quote - first_quote - 1);

            int memorySize;
            try {
                memorySize = stoi(mem_str);
                if (memorySize < 64 || memorySize > 65536 || ((memorySize > 0) && (memorySize & (memorySize - 1)) != 0)) {
                    cout << "Invalid memory allocation" << endl;
                    continue;
                }
            }
            catch (...) {
                cout << "Invalid memory allocation" << endl;
                continue;
            }

            scoped_lock lock(screensMutex, processMutex);


            bool name_exists = false;
            if (screens.count(name)) name_exists = true;
            for (const auto& p : globalProcesses) {
                if (p.name == name) name_exists = true;
            }

            if (name_exists) {
                cout << "Process or screen with name \"" << name << "\" already exists." << endl;
                continue;
            }

            vector<ProcessInstruction> instructions;
            if (!parseInstructionsString(instruction_str, instructions)) {
                cout << "Failed to create process due to instruction parsing error." << endl;
                continue;
            }

            // Create the Screen and Process
            screens[name] = Screen(name, memorySize, countTotalInstructions(instructions));

            Process newProc(name, memorySize);
            newProc.instructions = instructions;
            newProc.totalTasks = countTotalInstructions(instructions);

            globalProcesses.push_back(move(newProc));

            // Add to waiting queue for admission
            {
                lock_guard<mutex> wait_lock(waiting_queue_mutex);
                waiting_for_memory_queue.push(&globalProcesses.back());
            }
            memory_cv.notify_one(); // Notify admission scheduler of new process

            cout << "Process \"" << name << "\" created with " << memorySize << " bytes and " << instructions.size() << " instructions. Now waiting for memory." << endl;

        }
        else if (command.rfind("screen -r ", 0) == 0) {
            if (inScreen) {
                cout << "Already in a screen. Type 'exit' first." << endl;
                continue;
            }

            string name = command.substr(10);
            lock_guard<mutex> lock(screensMutex);
            auto it = screens.find(name);

            if (it == screens.end()) {
                cout << "Process " << name << " not found." << endl;
            }
            else {
                Screen& screen = it->second;

                // === [MODIFIED] === This check now correctly uses info from the Screen object,
                // which is updated by the worker thread upon violation.
                if (screen.hasMemoryViolation()) {
                    cout << "Process " << name << " shut down due to memory access violation error that occurred at "
                        << screen.getViolationTime() << ". " << screen.getViolationAddress() << " invalid." << endl;
                }
                else {
                    // Normal screen access
                    inScreen = true;
                    currentScreen = name;
                    screen.display();
                }
            }
        }
        else if (command == "screen -ls") {
            displaySchedulerUI(globalProcesses);
        }
        else if (command == "scheduler-start") {
            if (isSchedulerRunning) {
                cout << "Scheduler is already running." << endl;
                continue;
            }

            {
                scoped_lock lock(processMutex, waiting_queue_mutex, queue_mutex, memory_mutex, screensMutex);



                globalProcesses.clear();
                screens.clear();
                while (!waiting_for_memory_queue.empty()) waiting_for_memory_queue.pop();
                while (!ready_queue.empty()) ready_queue.pop();
                current_memory_used = 0;


                // Generate 10 processes with randomized instructions
                for (int i = 1; i <= 10; ++i) {
                    stringstream name;
                    name << "process" << setfill('0') << setw(2) << i;

                    // === [MODIFIED] === Use new Process constructor
                    Process newProc(name.str(), systemConfig.mem_per_proc);
                    newProc.instructions = generateProcessInstructions(systemConfig.min_ins, systemConfig.max_ins);
                    newProc.totalTasks = countTotalInstructions(newProc.instructions);
                    newProc.currentInstructionIndex = 0;

                    globalProcesses.push_back(std::move(newProc));
                }

                // Add all new processes to the waiting queue
                for (auto& proc : globalProcesses) {
                    waiting_for_memory_queue.push(&proc);
                }
            }

            isSchedulerRunning = true;
            // Start the main admission scheduler thread (REVISED)
            schedulerThread = thread(admissionScheduler);
            memory_cv.notify_one(); // Kick-start the admission process

            cout << "Scheduler started (" << systemConfig.scheduler
                << ") with 10 processes on " << systemConfig.num_cpu
                << " cores." << endl;
        }
        else if (command == "scheduler-stop") {
            if (!isSchedulerRunning) {
                cout << "Scheduler is not running." << endl;
                continue;
            }

            cout << "Stopping scheduler..." << endl;
            isSchedulerRunning = false;
            memory_cv.notify_all(); // Wake up admission scheduler to terminate
            scheduler_cv.notify_all(); // Wake up all workers to terminate
            if (schedulerThread.joinable()) {
                schedulerThread.join();
            }

            cout << "Scheduler stopped." << endl;

            displaySchedulerUI(globalProcesses);
        }
        else if (command == "report-util") {
            generateUtilizationReport();
        }
        else if (!command.empty()) {
            if (inScreen) {
                // This doesn't do anything meaningful for the process, just advances the UI
                lock_guard<mutex> lock(screensMutex);
                screens[currentScreen].advance();
                screens[currentScreen].display();
            }
            else {
                cout << "Command not recognized." << endl;
            }
        }
    }

    return 0;
}