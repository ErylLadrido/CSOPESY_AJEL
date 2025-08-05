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
#include <atomic> // For VMStat
#include <regex> // For backing store


// ===================== Libraries - END ===================== //

using namespace std;

// ===================== Forward Declarations & Core Structs ===================== //

void displayHeader();
void displayMainMenu();
bool parseInstructionsString(const string& raw_instructions, vector<struct ProcessInstruction>& instructions);


/**
 * Represents a physical memory frame in the system.
 */
struct FrameInfo {
    bool isFree = true;
    int ownerPID = -1;
    int virtualPageNumber = -1; // Which virtual page is stored here
    bool dirty = false;
    bool referenced = false;
};

/**
 * Represents an entry in a process's page table.
 */
struct PageTableEntry {
    int virtualPageNumber = -1;
    int frameNumber = -1;
    bool valid = false;
    bool dirty = false;
    bool referenced = false;
};

// --- Configuration Struct ---
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
    int min_mem_per_proc;
    int max_mem_per_proc;
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
        min_mem_per_proc(0),
        max_mem_per_proc(0) {
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
            min_mem_per_proc > 0 &&
            max_mem_per_proc > 0 &&
            max_mem_per_proc >= min_mem_per_proc &&
            mem_per_frame <= max_overall_mem;
            //max_mem_per_proc <= max_overall_mem;
    }
};

// --- Global System Configuration and Memory Structures ---
SystemConfig systemConfig;
vector<FrameInfo> frameTable;
mutex frameTableMutex;
std::queue<int> frameEvictionQueue;

/**
 * Page replacement class definition. Needs frameTable to be declared first.
 */
class ImprovedPageReplacement {
private:
    queue<pair<int, int>> pageQueue; // <frameIndex, timestamp>
    int timestamp_counter = 0;
    mutex replacement_mutex;

public:
    void addPage(int frameIndex) {
        lock_guard<mutex> lock(replacement_mutex);
        pageQueue.push({ frameIndex, timestamp_counter++ });
    }

    int evictPage() {
        lock_guard<mutex> lock(replacement_mutex);

        while (!pageQueue.empty()) {
            auto [frameIndex, timestamp] = pageQueue.front();
            pageQueue.pop();

            // Verify frame is still valid for eviction
            if (frameIndex >= 0 && frameIndex < frameTable.size() && !frameTable[frameIndex].isFree) {
                return frameIndex;
            }
        }
        return -1;
    }

    bool isEmpty() {
        lock_guard<mutex> lock(replacement_mutex);
        return pageQueue.empty();
    }
};


// ======================= Global Variables ======================= //

bool isSchedulerRunning = false;
bool isSystemInitialized = false; // New flag to track system initialization
thread schedulerThread;
vector<thread> cpu_workers;

mutex processMutex; // Used for protecting shared resources like globalProcesses and cout
vector<struct Process> globalProcesses; // List of all processes
bool screenActive = false;

mutex screensMutex;
unordered_map<string, class Screen> screens;

// --- Threading & Scheduling Variables ---
queue<struct Process*> ready_queue; // Queue of processes ready for CPU execution
mutex queue_mutex;                  // Mutex to protect the ready_queue
condition_variable scheduler_cv;    // Notifies worker threads about new processes

// --- Memory Management Variables ---
int current_memory_used = 0;
mutex memory_mutex;
condition_variable memory_cv; // Notifies the admission scheduler about freed memory
queue<struct Process*> waiting_for_memory_queue; // Processes waiting for memory allocation
mutex waiting_queue_mutex;

int quantumCycleCounter = 0;
int nextPID = 1;
ImprovedPageReplacement pageReplacer;

// --- For VMStat ---
atomic<int> pageFaults{ 0 };
atomic<int> pageReplacements{ 0 };
atomic<int> totalCpuTicks{ 0 };
atomic<int> activeCpuTicks{ 0 };
atomic<int> idleCpuTicks{ 0 };

mutex backing_store_mutex;

// ===================== Global Variables - END ===================== //


// ===================== Structures ===================== //

/**
 * Represents a single process instruction
 */
struct ProcessInstruction {
    enum Type {
        PRINT, DECLARE, ADD, SUBTRACT, FOR_LOOP, READ, WRITE
    };

    Type type{}; // FIX: Initialize the enum
    string var_name;
    int value = 0;
    string message;
    vector<ProcessInstruction> loop_body;
    int loop_count = 0;
    int memory_address = 0;

    bool is_three_operand = false; // For ADD var1 var2 var3
    string arg1_var;
    string arg2_var;
    bool print_has_variable = false; // For PRINT "message" + var
};

/**
 * Defines the process structure.
 */
struct Process {
    string name;
    int pid;
    time_t startTime;
    time_t endTime;
    int core; // Core that is executing or executed the process
    int tasksCompleted;
    int totalTasks; // Total number of instructions to execute
    bool isFinished;
    vector<ProcessInstruction> instructions; // Process instructions
    unordered_map<string, int> variable_offsets; // Maps variable name to its offset in the symbol table (0-62)
    int next_variable_offset; // Tracks the next available offset in the symbol table segment

    // === [NEW] === Memory and violation tracking members
    int memorySize; // Process-specific memory allocation
    unordered_map<int, uint16_t> memory_space; // Emulated memory space
    bool has_violation;
    string violation_address;

    int currentInstructionIndex = 0; // track instruction being executed

    unordered_map<int, PageTableEntry> pageTable; // For page allocator

    Process(const string& processName, int memSize, int id = -1) :
        name(processName), memorySize(memSize), pid(id), startTime(0), endTime(0), core(-1),
        tasksCompleted(0), totalTasks(0), isFinished(false), has_violation(false),
        currentInstructionIndex(0), next_variable_offset(0) { // Initialize new member
    }

    // === [MODIFIED] === Updated default constructor
    Process()
        : name("unnamed"), memorySize(0), pid(-1), startTime(0), endTime(0), core(-1),
        tasksCompleted(0), totalTasks(0), isFinished(false), has_violation(false),
        currentInstructionIndex(0), next_variable_offset(0) { // Initialize new member
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


bool checkMemoryViolation(int address, int processMemorySize, const string& operation, int pid, int coreId, ofstream& logFile) {
    if (address < 0 || address >= processMemorySize) {
        time_t now = time(0);
        tm localtm;
#ifdef _WIN32
        localtime_s(&localtm, &now);
#else
        localtm = *localtime(&now);
#endif
        stringstream timestamp;
        timestamp << put_time(&localtm, "(%m/%d/%Y %I:%M:%S %p)");

        stringstream violationAddr;
        violationAddr << "0x" << hex << uppercase << address;

        logFile << timestamp.str() << " Core:" << coreId
            << " MEMORY VIOLATION: " << operation << " at address "
            << violationAddr.str() << " (Process memory: 0x0 - 0x"
            << hex << uppercase << (processMemorySize - 1) << dec << ")" << endl;

        return true;
    }
    return false;
}

void savePageToBackingStore(int pid, int vpn, const unordered_map<int, uint16_t>& memory, int frameBaseAddr) {
    lock_guard<mutex> lock(backing_store_mutex);

    // Read existing entries
    unordered_map<string, string> entries;
    ifstream inFile("csopesy-backing-store.txt");
    string line;
    regex entryPattern(R"(PID=(\d+)\s+VPN=(\d+)\s+DATA=.*)");

    while (getline(inFile, line)) {
        smatch match;
        if (regex_match(line, match, entryPattern)) {
            string key = match[1].str() + "_" + match[2].str();
            entries[key] = line;
        }
    }
    inFile.close();

    // Create new entry
    stringstream newEntry;
    newEntry << "PID=" << pid << " VPN=" << vpn << " DATA=";
    for (int offset = 0; offset < systemConfig.mem_per_frame; offset += 2) {
        int addr = frameBaseAddr + offset;
        uint16_t val = memory.count(addr) ? memory.at(addr) : 0;
        newEntry << setw(4) << setfill('0') << hex << uppercase << val << " ";
    }

    // Update entry
    string key = to_string(pid) + "_" + to_string(vpn);
    entries[key] = newEntry.str();

    // Write all entries back
    ofstream outFile("csopesy-backing-store.txt");
    for (const auto& entry : entries) {
        outFile << entry.second << endl;
    }
    outFile.close();
}

bool loadPageFromBackingStore(int pid, int vpn, unordered_map<int, uint16_t>& memory, int frameBaseAddr) {
    lock_guard<mutex> lock(backing_store_mutex);

    ifstream backingFile("csopesy-backing-store.txt");
    if (!backingFile) return false;

    string key = to_string(pid) + "_" + to_string(vpn);
    string line;
    regex entryPattern(R"(PID=(\d+)\s+VPN=(\d+)\s+DATA=(.*))");
    bool found = false;

    while (getline(backingFile, line)) {
        smatch match;
        if (regex_match(line, match, entryPattern)) {
            string currentKey = match[1].str() + "_" + match[2].str();
            if (currentKey == key) {
                istringstream dataStream(match[3]);
                string hexVal;
                int offset = 0;

                while (dataStream >> hexVal && offset < systemConfig.mem_per_frame) {
                    int addr = frameBaseAddr + offset;
                    uint16_t val = static_cast<uint16_t>(stoi(hexVal, nullptr, 16));
                    memory[addr] = val;
                    offset += 2;
                }
                found = true;
                break;
            }
        }
    }
    backingFile.close();
    return found;
}

int assignFrameToPage(Process& process, int virtualPageNumber, int frameIndex) {
    FrameInfo& frame = frameTable[frameIndex];

    frame.isFree = false;
    frame.ownerPID = process.pid;
    frame.virtualPageNumber = virtualPageNumber;
    frame.dirty = false;
    frame.referenced = true;

    // Update page table entry
    PageTableEntry entry;
    entry.virtualPageNumber = virtualPageNumber;
    entry.frameNumber = frameIndex;
    entry.valid = true;
    entry.dirty = false;
    entry.referenced = true;

    process.pageTable[virtualPageNumber] = entry;

    // Add frame to eviction queue
    frameEvictionQueue.push(frameIndex);

    // Load from backing store
    int baseAddr = virtualPageNumber * systemConfig.mem_per_frame;
    loadPageFromBackingStore(process.pid, virtualPageNumber, process.memory_space, baseAddr);

    return frameIndex;
}


/**
 * Evicts a frame using FIFO policy and returns the freed frame number.
 * Also updates the corresponding process's page table.
 */
int evictFrame() {
    lock_guard<mutex> lock(frameTableMutex);

    while (!frameEvictionQueue.empty()) {
        int evictedFrame = frameEvictionQueue.front();
        frameEvictionQueue.pop();

        if (evictedFrame < 0 || evictedFrame >= static_cast<int>(frameTable.size())) {
            continue;
        }

        FrameInfo& evicted = frameTable[evictedFrame];
        int evictedPID = evicted.ownerPID;
        int evictedVPN = evicted.virtualPageNumber;

        auto it = find_if(globalProcesses.begin(), globalProcesses.end(),
            [evictedPID](const Process& p) { return p.pid == evictedPID; });

        if (it != globalProcesses.end()) {
            Process& evictedProcess = *it;

            // Save page to backing store if dirty
            if (evicted.dirty) {
                int baseAddr = evictedVPN * systemConfig.mem_per_frame;
                savePageToBackingStore(evictedProcess.pid, evictedVPN, evictedProcess.memory_space, baseAddr);
            }

            // Invalidate the page in the page table
            if (evictedProcess.pageTable.count(evictedVPN)) {
                evictedProcess.pageTable[evictedVPN].valid = false;
            }
        }

        // Reset the frame
        evicted.isFree = true;
        evicted.ownerPID = -1;
        evicted.virtualPageNumber = -1;
        evicted.dirty = false;
        evicted.referenced = false;

        return evictedFrame;
    }

    return -1; // No frame to evict
}

/**
 * Allocates a frame for the given virtual page of a process.
 * Returns the frame number if successful, or -1 if no free frame is available.
 */
int allocateFrameForPage(Process& process, int virtualPageNumber) {
    // First, search for a free frame
    for (size_t i = 0; i < frameTable.size(); ++i) {
        if (frameTable[i].isFree) {
            // Use the free frame
            return assignFrameToPage(process, virtualPageNumber, static_cast<int>(i));
        }
    }

    // Evict if no free frame found 
    int evictedFrame = evictFrame();
    if (evictedFrame != -1) {
        return assignFrameToPage(process, virtualPageNumber, evictedFrame);
    }

    return -1;
}

/**
 * Allocates a frame or evicts an old one to make space.
 */
int ensureFrameAvailable(Process& process, int virtualPageNumber) {
    int frame = allocateFrameForPage(process, virtualPageNumber);

    if (frame != -1) {
        return frame; // Success
    }

    // Need to evict
    int evictedFrame = evictFrame();
    if (evictedFrame == -1) return -1;

    // Retry allocation after eviction
    return allocateFrameForPage(process, virtualPageNumber);
}

void printVMStat() {
    lock_guard<mutex> procLock(processMutex);
    lock_guard<mutex> queueLock(queue_mutex);
    lock_guard<mutex> waitLock(waiting_queue_mutex);
    lock_guard<mutex> memLock(memory_mutex);

    int totalMemBytes = systemConfig.max_overall_mem * 1024; // Converts Kb to b
    int usedMemBytes = current_memory_used * 1024;
    int freeMemBytes = totalMemBytes - usedMemBytes;

    cout << "\n=== VM STATISTICS ===" << endl;

    cout << "\n[Memory Usage]" << endl;
    cout << "Total Memory (bytes)  : " << totalMemBytes << endl;
    cout << "Used Memory (bytes)   : " << usedMemBytes << endl;
    cout << "Free Memory (bytes)   : " << freeMemBytes << endl;

    cout << "\n[CPU]" << endl;
    cout << "Total CPU Ticks       : " << totalCpuTicks.load() << endl;
    cout << "Active CPU Ticks      : " << activeCpuTicks.load() << endl;
    cout << "Idle CPU Ticks        : " << idleCpuTicks.load() << endl;

    cout << "\n[Paging]" << endl;
    cout << "Pages Paged In        : " << pageFaults.load() << endl;
    cout << "Pages Paged Out       : " << pageReplacements.load() << endl;
}

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
                                   "max-overall-mem", "mem-per-frame", "min-mem-per-proc", "max-mem-per-proc" };
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
            else if (key == "min-mem-per-proc") {
                systemConfig.min_mem_per_proc = stoi(value);
                keyFound[9] = true;
                cout << "  ✓ min-mem-per-proc: " << systemConfig.min_mem_per_proc << endl;
            }
            else if (key == "max-mem-per-proc") {
                systemConfig.max_mem_per_proc = stoi(value);
                keyFound[10] = true;
                cout << "  ✓ max-mem-per-proc: " << systemConfig.max_mem_per_proc << endl;
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
    cout << "├── Min Memory per Process: " << systemConfig.min_mem_per_proc << " KB" << endl;
    cout << "└── Max Memory per Process: " << systemConfig.max_mem_per_proc << " KB" << endl;
    cout << string(50, '=') << endl;

    // Initialize Frame Table
    int totalFrames = systemConfig.max_overall_mem / systemConfig.mem_per_frame;
    frameTable.resize(totalFrames);

    for (int i = 0; i < totalFrames; ++i) {
        frameTable[i] = FrameInfo(); // Default isFree = true
    }

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
 // In generateProcessInstructions function:
vector<ProcessInstruction> generateProcessInstructions(int minInstructions, int maxInstructions, int processMemorySize) {
    vector<ProcessInstruction> instructions;
    int totalInstructions = minInstructions + (rand() % (maxInstructions - minInstructions + 1));

    // Define available instruction types (excluding FOR_LOOP)
    vector<ProcessInstruction::Type> availableTypes = {
        ProcessInstruction::PRINT,
        ProcessInstruction::DECLARE,
        ProcessInstruction::ADD,
        ProcessInstruction::SUBTRACT,
        ProcessInstruction::READ,
        ProcessInstruction::WRITE
    };

    // Generate random instructions
    for (int i = 0; i < totalInstructions; ++i) {
        ProcessInstruction instr;
        instr.type = availableTypes[rand() % availableTypes.size()];

        switch (instr.type) {
        case ProcessInstruction::PRINT:
            instr.message = "Hello world from process!";
            break;

        case ProcessInstruction::DECLARE:
            instr.var_name = "var" + to_string(rand() % 10 + 1);
            instr.value = rand() % 100;
            break;

        case ProcessInstruction::ADD:
            instr.var_name = "var" + to_string(rand() % 10 + 1);
            instr.value = rand() % 50 + 1;
            break;

        case ProcessInstruction::SUBTRACT:
            instr.var_name = "var" + to_string(rand() % 10 + 1);
            instr.value = rand() % 50 + 1;
            break;

        case ProcessInstruction::READ:
            instr.var_name = "var" + to_string(rand() % 10 + 1);
            instr.memory_address = rand() % processMemorySize;
            break;

        case ProcessInstruction::WRITE:
            instr.var_name = "write_var" + to_string(rand() % 5);
            instr.memory_address = rand() % processMemorySize;
            // Add declaration for write variable
            ProcessInstruction decl_instr;
            decl_instr.type = ProcessInstruction::DECLARE;
            decl_instr.var_name = instr.var_name;
            decl_instr.value = rand() % 500;
            instructions.push_back(decl_instr);
            break;
        }
        instructions.push_back(instr);
    }
    return instructions;
}

bool ensureSymbolTablePageLoaded(Process* process, ofstream& logFile, int coreId) {
    int vpn = 0; // The symbol table is always located in Virtual Page Number 0.

    // Check if the page is not valid (not in a frame)
    if (process->pageTable.count(vpn) == 0 || !process->pageTable[vpn].valid) {
        time_t now = time(0);
        tm localtm;
#ifdef _WIN32
        localtime_s(&localtm, &now);
#else
        localtm = *localtime(&now);
#endif
        stringstream timestamp;
        timestamp << put_time(&localtm, "(%m/%d/%Y %I:%M:%S %p)");

        logFile << timestamp.str() << " Core:" << coreId
            << " SYMBOL TABLE PAGE FAULT. Attempting to load page " << vpn << "." << endl;
        pageFaults++;

        // Attempt to allocate a frame for this page
        if (allocateFrameForPage(*process, vpn) == -1) {
            logFile << timestamp.str() << " Core:" << coreId
                << " FATAL: Page fault failed. No frame available. Process terminated." << endl;
            process->isFinished = true;
            process->has_violation = true; // Mark for termination
            return false; // Fatal error
        }
        logFile << timestamp.str() << " Core:" << coreId << " Page " << vpn << " loaded successfully." << endl;
    }
    return true; // Page is now loaded and valid
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

    switch (instr.type) {
    case ProcessInstruction::PRINT: {
        string output = instr.message;
        if (instr.print_has_variable) {
            // This is the new format: message + variable
            if (process->variable_offsets.count(instr.var_name)) {
                int offset = process->variable_offsets.at(instr.var_name);
                if (process->memory_space.count(offset)) {
                    output += to_string(process->memory_space.at(offset));
                }
                else {
                    output += "[uninitialized]";
                }
            }
            else {
                output += "[undeclared]";
            }
        }
        // Log the final composed message
        logFile << timestamp.str() << " Core:" << coreId << " \"" << output << "\"" << endl;
        this_thread::sleep_for(chrono::milliseconds(systemConfig.delay_per_exec));
        {
            lock_guard<mutex> lock(processMutex);
            process->tasksCompleted++;
        }
        break;
    }

    case ProcessInstruction::DECLARE: {
        // A variable declaration is a WRITE to the symbol table. Ensure page is loaded.
        if (!ensureSymbolTablePageLoaded(process, logFile, coreId)) return false;

        // Symbol table is 64 bytes. Each var is 2 bytes (uint16_t). Max 32 vars.
        if (process->next_variable_offset >= 64) {
            logFile << timestamp.str() << " Core:" << coreId << " DECLARE " << instr.var_name << " ignored. Symbol table full." << endl;
        }
        else {
            int offset = process->next_variable_offset;
            process->variable_offsets[instr.var_name] = offset;
            process->memory_space[offset] = instr.value; // Write value to virtual memory
            process->next_variable_offset += 2; // Move to next slot

            // Mark the page as dirty since we wrote to it
            process->pageTable[0].dirty = true;
            {
                lock_guard<mutex> lock(frameTableMutex);
                int frameNum = process->pageTable[0].frameNumber;
                if (frameNum != -1) frameTable[frameNum].dirty = true;
            }

            logFile << timestamp.str() << " Core:" << coreId << " DECLARE " << instr.var_name
                << " = " << instr.value << " at offset " << offset << endl;
            this_thread::sleep_for(chrono::milliseconds(systemConfig.delay_per_exec));
            {
                lock_guard<mutex> lock(processMutex);
                process->tasksCompleted++;
            }
        }
        break;
    }

    case ProcessInstruction::ADD:
    case ProcessInstruction::SUBTRACT: {
        // Accessing a variable requires reading and writing to the symbol table.
        if (!ensureSymbolTablePageLoaded(process, logFile, coreId)) return false;

        // Auto-declare if variable doesn't exist
        if (process->variable_offsets.find(instr.var_name) == process->variable_offsets.end()) {
            if (process->next_variable_offset >= 64) {
                logFile << timestamp.str() << " Core:" << coreId << " "
                    << (instr.type == ProcessInstruction::ADD ? "ADD" : "SUBTRACT")
                    << " on " << instr.var_name << " ignored. Symbol table full." << endl;
                break; // Don't complete the instruction
            }
            int offset = process->next_variable_offset;
            process->variable_offsets[instr.var_name] = offset;
            process->memory_space[offset] = 0; // Initialize with 0
            process->next_variable_offset += 2;
        }

        int offset = process->variable_offsets[instr.var_name];
        uint16_t currentValue = process->memory_space[offset];

        if (instr.type == ProcessInstruction::ADD) {
            if (instr.is_three_operand) {
                // New format: ADD dest src1 src2
                uint16_t val1 = 0, val2 = 0;
                if (process->variable_offsets.count(instr.arg1_var)) {
                    val1 = process->memory_space.at(process->variable_offsets.at(instr.arg1_var));
                }
                if (process->variable_offsets.count(instr.arg2_var)) {
                    val2 = process->memory_space.at(process->variable_offsets.at(instr.arg2_var));
                }
                currentValue = val1 + val2;
                logFile << timestamp.str() << " Core:" << coreId << " ADD " << instr.arg1_var << " + " << instr.arg2_var
                    << " into " << instr.var_name;
            }
            else {
                // Original format: ADD var value
                currentValue += instr.value;
                logFile << timestamp.str() << " Core:" << coreId << " ADD " << instr.value
                    << " to " << instr.var_name;
            }
        }
        else {
            currentValue -= instr.value;
            logFile << timestamp.str() << " Core:" << coreId << " SUBTRACT " << instr.value
                << " from " << instr.var_name;
        }

        process->memory_space[offset] = currentValue; // Write back the result

        // Mark the page as dirty
        process->pageTable[0].dirty = true;
        {
            lock_guard<mutex> lock(frameTableMutex);
            int frameNum = process->pageTable[0].frameNumber;
            if (frameNum != -1) frameTable[frameNum].dirty = true;
        }

        logFile << " (result: " << currentValue << ")" << endl;

        this_thread::sleep_for(chrono::milliseconds(systemConfig.delay_per_exec));
        {
            lock_guard<mutex> lock(processMutex);
            process->tasksCompleted++;
        }
        break;
    }

  

    case ProcessInstruction::READ: {
        int addr = instr.memory_address;

        // Fix: Memory size is in KB, convert to bytes properly
        int memoryInBytes = process->memorySize; // memorySize is already in bytes based on screen creation

        // Check for memory violation before proceeding
        if (addr < 0 || addr >= memoryInBytes) {
            process->isFinished = true;
            process->has_violation = true;
            stringstream ss;
            ss << "0x" << hex << uppercase << addr;
            process->violation_address = ss.str();
            logFile << timestamp.str() << " Core:" << coreId << " MEMORY VIOLATION on READ at " << process->violation_address
                << ". Valid range: 0x0 - 0x" << hex << uppercase << (memoryInBytes - 1) << dec << ". Process terminated." << endl;
            return false;
        }

        // 1. Handle page fault for the source memory address
        int vpn_source = addr / systemConfig.mem_per_frame;
        if (process->pageTable.count(vpn_source) == 0 || !process->pageTable[vpn_source].valid) {
            pageFaults++;
            if (allocateFrameForPage(*process, vpn_source) == -1) {
                logFile << timestamp.str() << " Core:" << coreId << " PAGE FAULT FAILED on READ. Process terminated." << endl;
                process->isFinished = true;
                process->has_violation = true;
                return false;
            }
        }
        process->pageTable[vpn_source].referenced = true;

        // Read value from the source address
        uint16_t value_read = process->memory_space.count(addr) ? process->memory_space.at(addr) : 0;

        // 2. Handle page fault for the destination (the symbol table)
        if (!ensureSymbolTablePageLoaded(process, logFile, coreId)) return false;

        // Auto-declare if variable doesn't exist
        int offset;
        if (process->variable_offsets.find(instr.var_name) == process->variable_offsets.end()) {
            if (process->next_variable_offset >= 64) {
                logFile << timestamp.str() << " Core:" << coreId << " READ into " << instr.var_name << " ignored. Symbol table full." << endl;
                break;
            }
            offset = process->next_variable_offset;
            process->variable_offsets[instr.var_name] = offset;
            process->next_variable_offset += 2;
        }
        else {
            offset = process->variable_offsets.at(instr.var_name);
        }

        // Write the value to the symbol table in memory
        process->memory_space[offset] = value_read;

        // Mark the symbol table page as dirty
        process->pageTable[0].dirty = true;
        {
            lock_guard<mutex> lock(frameTableMutex);
            int frameNum = process->pageTable[0].frameNumber;
            if (frameNum != -1) frameTable[frameNum].dirty = true;
        }

        logFile << timestamp.str() << " Core:" << coreId << " READ " << value_read << " from 0x" << hex << setw(4) << setfill('0') << addr << dec << " into " << instr.var_name << endl;

        this_thread::sleep_for(chrono::milliseconds(systemConfig.delay_per_exec));
        {
            lock_guard<mutex> lock(processMutex);
            process->tasksCompleted++;
        }
        break;
    }

    case ProcessInstruction::WRITE: {
        int addr = instr.memory_address;

        // Fix: Memory size is in KB, convert to bytes properly  
        int memoryInBytes = process->memorySize; // memorySize is already in bytes based on screen creation

        // Bounds check for destination address
        if (addr < 0 || addr >= memoryInBytes) {
            process->isFinished = true;
            process->has_violation = true;
            stringstream ss;
            ss << "0x" << hex << uppercase << addr;
            process->violation_address = ss.str();
            logFile << timestamp.str() << " Core:" << coreId << " MEMORY VIOLATION on WRITE at " << process->violation_address
                << ". Valid range: 0x0 - 0x" << hex << uppercase << (memoryInBytes - 1) << dec << ". Process terminated." << endl;
            return false;
        }

        // 1. Get value from variable, which requires accessing the symbol table
        if (!ensureSymbolTablePageLoaded(process, logFile, coreId)) return false;

        uint16_t valueToWrite = 0;
        if (process->variable_offsets.count(instr.var_name)) {
            int offset = process->variable_offsets.at(instr.var_name);
            if (process->memory_space.count(offset)) {
                valueToWrite = process->memory_space.at(offset);
            }
        }

        // 2. Page fault check for the destination address
        int vpn_dest = addr / systemConfig.mem_per_frame;
        if (process->pageTable.count(vpn_dest) == 0 || !process->pageTable[vpn_dest].valid) {
            pageFaults++;
            if (allocateFrameForPage(*process, vpn_dest) == -1) {
                logFile << timestamp.str() << " Core:" << coreId << " PAGE FAULT FAILED on WRITE. Process terminated." << endl;
                process->isFinished = true;
                process->has_violation = true;
                return false;
            }
        }

        // Write the value to the destination address in memory
        process->memory_space[addr] = valueToWrite;

        // Mark destination page as dirty and referenced
        process->pageTable[vpn_dest].referenced = true;
        process->pageTable[vpn_dest].dirty = true;
        {
            lock_guard<mutex> lock(frameTableMutex);
            int frameNum = process->pageTable[vpn_dest].frameNumber;
            if (frameNum != -1) frameTable[frameNum].dirty = true;
        }

        logFile << timestamp.str() << " Core:" << coreId << " WRITE " << dec << valueToWrite << " (from " << instr.var_name << ") to 0x" << hex << setw(4) << setfill('0') << addr << dec << endl;

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

void displayProcessSMI() {
    vector<tuple<string, int, int, int, bool, bool, string, int, int>> processInfos;
    int totalMemUsed = 0;

    {
        lock_guard<mutex> proc_lock(processMutex);
        lock_guard<mutex> mem_lock(memory_mutex);
        totalMemUsed = current_memory_used;

        for (const auto& proc : globalProcesses) {
            int framesUsed = 0;
            int validPages = 0;

            // Count frames and valid pages for this process
            for (const auto& [vpn, pageEntry] : proc.pageTable) {
                if (pageEntry.valid) {
                    validPages++;
                    framesUsed++;
                }
            }

            processInfos.emplace_back(
                proc.name,
                proc.core,
                proc.tasksCompleted,
                proc.totalTasks,
                proc.isFinished,
                proc.has_violation,
                proc.violation_address,
                proc.memorySize,
                validPages
            );
        }
    }

    cout << "\n=== PROCESS-SMI REPORT ===" << endl;
    cout << "Total Memory Used: " << totalMemUsed << " / " << systemConfig.max_overall_mem << " KB" << endl;
    cout << "Total Frames: " << (systemConfig.max_overall_mem / systemConfig.mem_per_frame) << endl;
    cout << "Frame Size: " << systemConfig.mem_per_frame << " KB" << endl;
    cout << string(80, '=') << endl;

    int idx = 1;
    for (const auto& info : processInfos) {
        auto& [name, core, completed, total, isFinished, has_violation, violation_addr, memSize, validPages] = info;

        cout << "\n== Process " << idx++ << " ==" << endl;
        cout << "Name: " << name << endl;
        cout << "PID: " << (idx - 1) << endl;
        cout << "Core: " << (core == -1 ? "N/A" : to_string(core)) << endl;
        cout << "Memory Allocated: " << memSize << " KB" << endl;
        cout << "Pages in Memory: " << validPages << " / " << (memSize / systemConfig.mem_per_frame) << endl;
        cout << "Progress: " << completed << " / " << total << " instructions" << endl;

        string status;
        if (isFinished) {
            status = has_violation ? "Terminated (Memory Violation)" : "Finished";
            if (has_violation) {
                cout << "Violation Address: " << violation_addr << endl;
            }
        }
        else {
            status = (core == -1) ? "Waiting for CPU" : "Running";
        }
        cout << "Status: " << status << endl;

        // Show recent log entries
        cout << "-- Recent Log Entries --" << endl;
        string logFileName = name + ".txt";
        ifstream infile(logFileName);
        if (infile.is_open()) {
            vector<string> lines;
            string line;
            while (getline(infile, line)) {
                lines.push_back(line);
            }
            infile.close();

            int startIdx = max(0, static_cast<int>(lines.size()) - 3);
            for (int i = startIdx; i < lines.size(); i++) {
                cout << "  " << lines[i] << endl;
            }
        }
        else {
            cout << "  (No log file found)" << endl;
        }
        cout << string(50, '-') << endl;
    }
}

void generateDetailedMemorySnapshot(int quantumCycle) {
    time_t now = time(0);
    tm localtm;
#ifdef _WIN32
    localtime_s(&localtm, &now);
#else
    localtm = *localtime(&now);
#endif
    stringstream timestamp;
    timestamp << put_time(&localtm, "(%m/%d/%Y %I:%M:%S%p)");

    // Collect memory layout information
    vector<tuple<int, string, int, int, int>> memoryLayout; // end, name, start, pid, pages_in_memory
    int nextAddress = 0;
    int totalProcesses = 0;
    int totalPagesInMemory = 0;

    {
        lock_guard<mutex> lock(processMutex);
        lock_guard<mutex> frame_lock(frameTableMutex);

        for (const auto& proc : globalProcesses) {
            if (!proc.isFinished && proc.startTime != 0) {
                int start = nextAddress;
                int end = start + proc.memorySize;

                // Count pages in memory for this process
                int pagesInMemory = 0;
                for (const auto& [vpn, pageEntry] : proc.pageTable) {
                    if (pageEntry.valid) {
                        pagesInMemory++;
                    }
                }
                totalPagesInMemory += pagesInMemory;

                memoryLayout.emplace_back(end, proc.name, start, proc.pid, pagesInMemory);
                nextAddress = end;
                totalProcesses++;
            }
        }
    }

    int totalExternalFragmentation = systemConfig.max_overall_mem - nextAddress;
    int totalFrames = systemConfig.max_overall_mem / systemConfig.mem_per_frame;

    // Count free frames
    int freeFrames = 0;
    {
        lock_guard<mutex> frame_lock(frameTableMutex);
        for (const auto& frame : frameTable) {
            if (frame.isFree) freeFrames++;
        }
    }

    stringstream filename;
    filename << "memory_stamp_" << setfill('0') << setw(2) << quantumCycle << ".txt";
    ofstream outfile(filename.str());
    if (!outfile.is_open()) return;

    outfile << "Memory Snapshot " << timestamp.str() << endl;
    outfile << "Quantum Cycle: " << quantumCycle << endl;
    outfile << "Number of processes in memory: " << totalProcesses << endl;
    outfile << "Total pages in memory: " << totalPagesInMemory << " / " << totalFrames << endl;
    outfile << "Free frames: " << freeFrames << endl;
    outfile << "External fragmentation: " << totalExternalFragmentation << " KB" << endl;
    outfile << string(60, '=') << endl;

    // Sort by end address (descending for the format requirement)
    sort(memoryLayout.begin(), memoryLayout.end(), [](const tuple<int, string, int, int, int>& a, const tuple<int, string, int, int, int>& b) {
        return get<0>(a) > get<0>(b);
        });

    outfile << "----end---- = " << systemConfig.max_overall_mem << endl;
    for (const auto& [end, name, start, pid, pagesInMem] : memoryLayout) {
        outfile << end << endl;
        outfile << name << " (PID:" << pid << ", Pages:" << pagesInMem << ")" << endl;
        outfile << start << endl;
    }
    outfile << "----start-- = 0" << endl;

    outfile.close();
}

void printEnhancedVMStat() {
    lock_guard<mutex> procLock(processMutex);
    lock_guard<mutex> queueLock(queue_mutex);
    lock_guard<mutex> waitLock(waiting_queue_mutex);
    lock_guard<mutex> memLock(memory_mutex);
    lock_guard<mutex> frameLock(frameTableMutex);

    int totalMemBytes = systemConfig.max_overall_mem * 1024;
    int usedMemBytes = current_memory_used * 1024;
    int freeMemBytes = totalMemBytes - usedMemBytes;

    // Count frame statistics
    int totalFrames = frameTable.size();
    int usedFrames = 0;
    int dirtyFrames = 0;

    for (const auto& frame : frameTable) {
        if (!frame.isFree) {
            usedFrames++;
            if (frame.dirty) dirtyFrames++;
        }
    }

    int freeFrames = totalFrames - usedFrames;

    // Count process statistics
    int runningProcs = 0, waitingProcs = 0, finishedProcs = 0;
    for (const auto& proc : globalProcesses) {
        if (proc.isFinished) finishedProcs++;
        else if (proc.startTime != 0) runningProcs++;
        else waitingProcs++;
    }

    cout << "\n" << string(50, '=') << endl;
    cout << "           VIRTUAL MEMORY STATISTICS" << endl;
    cout << string(50, '=') << endl;

    cout << "\n[MEMORY USAGE]" << endl;
    cout << "Total Memory         : " << setw(10) << totalMemBytes << " bytes" << endl;
    cout << "Used Memory          : " << setw(10) << usedMemBytes << " bytes" << endl;
    cout << "Free Memory          : " << setw(10) << freeMemBytes << " bytes" << endl;
    cout << "Memory Utilization   : " << setw(9) << fixed << setprecision(1)
        << (totalMemBytes > 0 ? (double)usedMemBytes / totalMemBytes * 100 : 0) << "%" << endl;

    cout << "\n[FRAME STATISTICS]" << endl;
    cout << "Total Frames         : " << setw(10) << totalFrames << endl;
    cout << "Used Frames          : " << setw(10) << usedFrames << endl;
    cout << "Free Frames          : " << setw(10) << freeFrames << endl;
    cout << "Dirty Frames         : " << setw(10) << dirtyFrames << endl;
    cout << "Frame Size           : " << setw(10) << systemConfig.mem_per_frame << " KB" << endl;

    cout << "\n[CPU STATISTICS]" << endl;
    cout << "Total CPU Ticks      : " << setw(10) << totalCpuTicks.load() << endl;
    cout << "Active CPU Ticks     : " << setw(10) << activeCpuTicks.load() << endl;
    cout << "Idle CPU Ticks       : " << setw(10) << idleCpuTicks.load() << endl;
    cout << "CPU Utilization      : " << setw(9) << fixed << setprecision(1)
        << (totalCpuTicks.load() > 0 ? (double)activeCpuTicks.load() / totalCpuTicks.load() * 100 : 0) << "%" << endl;

    cout << "\n[PAGING STATISTICS]" << endl;
    cout << "Page Faults          : " << setw(10) << pageFaults.load() << endl;
    cout << "Pages Paged Out      : " << setw(10) << pageReplacements.load() << endl;
    cout << "Page Fault Rate      : " << setw(9) << fixed << setprecision(3)
        << (totalCpuTicks.load() > 0 ? (double)pageFaults.load() / totalCpuTicks.load() : 0) << endl;

    cout << "\n[PROCESS STATISTICS]" << endl;
    cout << "Running Processes    : " << setw(10) << runningProcs << endl;
    cout << "Waiting Processes    : " << setw(10) << waitingProcs << endl;
    cout << "Finished Processes   : " << setw(10) << finishedProcs << endl;
    cout << "Total Processes      : " << setw(10) << globalProcesses.size() << endl;

    cout << "\n" << string(50, '=') << endl;
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
    lock_guard<mutex> proc_lock(processMutex);
    lock_guard<mutex> screen_lock(screensMutex);

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

void releaseProcessFrames(Process* process) {
    lock_guard<mutex> lock(frameTableMutex);
    for (auto const& [vpn, page_entry] : process->pageTable) {
        if (page_entry.valid) {
            int frameNum = page_entry.frameNumber;
            if (frameNum >= 0 && frameNum < frameTable.size()) {
                frameTable[frameNum].isFree = true;
                frameTable[frameNum].ownerPID = -1;
                frameTable[frameNum].virtualPageNumber = -1;
                frameTable[frameNum].dirty = false;
                frameTable[frameNum].referenced = false;
            }
        }
    }
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

            if (!isSchedulerRunning && ready_queue.empty()) {
                return;
            }
            if (!ready_queue.empty()) {
                currentProcess = ready_queue.front();
                ready_queue.pop();
            }
            else {
                // Idle tick
                totalCpuTicks++;
                idleCpuTicks++;
                continue;
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
                totalCpuTicks++;
                activeCpuTicks++;
                generateDetailedMemorySnapshot(quantumCycleCounter++);
            }

            outfile.close();

            // Check if process finished
            bool finished = false;
            bool violation_occurred = false;
            {
                lock_guard<mutex> lock(processMutex);
                if (currentProcess->currentInstructionIndex >= currentProcess->instructions.size() || currentProcess->has_violation) {
                    currentProcess->endTime = time(nullptr);
                    currentProcess->isFinished = true;
                    finished = true;
                    if (currentProcess->has_violation) {
                        violation_occurred = true;
                    }
                }
            }

            // If a violation occurred, we now lock BOTH mutexes in the correct order to update the screen
            if (violation_occurred) {
                lock_guard<mutex> proc_lock(processMutex);
                lock_guard<mutex> screen_lock(screensMutex);
                if (screens.count(currentProcess->name)) {
                    screens.at(currentProcess->name).triggerMemoryViolation(currentProcess->violation_address);
                }
            }

            // If finished, release memory and frames
            // If finished, release memory and frames
            if (finished) {
                releaseProcessFrames(currentProcess);
                {
                    lock_guard<mutex> mem_lock(memory_mutex);
                    //current_memory_used -= currentProcess->memorySize;
                }
                memory_cv.notify_one(); // Signal that memory has been freed
            }
            // === [FIX] ===
            // If the process is NOT finished and scheduler is RR, put it back on the queue.
            else if (systemConfig.scheduler == "rr") {
                {
                    lock_guard<mutex> lock(queue_mutex);
                    ready_queue.push(currentProcess);
                }
                // Don't need to notify here, the worker will just loop and grab the next process.
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
    cpu_workers.clear();
    for (int i = 0; i < systemConfig.num_cpu; ++i) {
        cpu_workers.emplace_back(cpu_worker_main, i);
    }

    // --- Main admission loop ---
    while (isSchedulerRunning) {
        Process* proc_to_admit = nullptr;
        {
            // This lock is to safely check the waiting queue and memory usage
            lock_guard<mutex> mem_lock(memory_mutex);
            lock_guard<mutex> wait_lock(waiting_queue_mutex);

            if (!waiting_for_memory_queue.empty()) {
                Process* next_proc = waiting_for_memory_queue.front();
                // ALWAYS admit the next process. Let the page allocator handle memory.
                proc_to_admit = next_proc;
                waiting_for_memory_queue.pop();
            }
        }

        if (proc_to_admit) {
            // If we found a process to admit, update memory and push to ready queue
            // The memory_mutex is already locked from the outer scope, so we can safely update this.
            //current_memory_used += proc_to_admit->memorySize;

            {
                lock_guard<mutex> ready_lock(queue_mutex);
                ready_queue.push(proc_to_admit);
            }
            scheduler_cv.notify_one(); // Notify one worker
        }

        else {
            // If we couldn't admit a process, sleep for a short time to prevent
            // this loop from running at 100% CPU when memory is full.
            this_thread::sleep_for(chrono::milliseconds(50));
        }
    }

    // --- Cleanup: Wait for all worker threads to complete ---
    scheduler_cv.notify_all();
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
            string arg1, arg2, arg3;
            line_ss >> arg1 >> arg2;

            // Check if there's a third argument
            if (line_ss >> arg3) {
                // Handle three-operand ADD: ADD dest src1 src2
                instr.type = ProcessInstruction::ADD;
                instr.is_three_operand = true;
                instr.var_name = arg1; // Destination
                instr.arg1_var = arg2; // Source 1
                instr.arg2_var = arg3; // Source 2
            }
            else {
                // Handle original two-operand ADD: ADD var value
                instr.type = ProcessInstruction::ADD;
                instr.is_three_operand = false;
                instr.var_name = arg1;
                try {
                    instr.value = stoi(arg2);
                }
                catch (...) { success = false; }
            }
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
            instr.type = ProcessInstruction::PRINT;
            // A more robust regex to parse: PRINT("message" + variable)
            // It specifically captures characters that are NOT a quote for the message part.
            static const regex print_pattern(R"(PRINT\s*\(\s*\"([^"]*)\"\s*\+\s*(\w+)\s*\))");
            smatch match;

            if (regex_match(segment, match, print_pattern)) {
                // A successful match will have 3 parts:
                // match[0]: The entire matched string
                // match[1]: The message (any character except a quote)
                // match[2]: The variable name
                if (match.size() == 3) {
                    instr.print_has_variable = true;
                    instr.message = match[1].str();
                    instr.var_name = match[2].str();
                }
                else {
                    success = false;
                }
            }
            else {
                success = false;
            }
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
                lock_guard<mutex> screen_lock(screensMutex);
                screens[currentScreen].display();
            }
            else {
                displayMainMenu();
            }
        }
        // In the 'process-smi' command block:
        else if (command == "process-smi") {
            displayProcessSMI();
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

            // Check for name conflicts - lock mutexes in consistent order
            bool name_exists = false;
            {
                lock_guard<mutex> create_check_lock(processMutex); // Changed from proc_lock
                lock_guard<mutex> screen_lock(screensMutex);

                if (screens.count(name)) name_exists = true;
                for (const auto& p : globalProcesses) {
                    if (p.name == name) name_exists = true;
                }
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

            // Create the Screen and Process - lock mutexes in consistent order
            {
                lock_guard<mutex> create_add_lock(processMutex); // Changed from proc_lock
                lock_guard<mutex> screen_lock(screensMutex);


                screens[name] = Screen(name, memorySize, countTotalInstructions(instructions));

                int pid = nextPID++;
                Process newProc(name, memorySize, pid);
                newProc.instructions = instructions;
                newProc.totalTasks = countTotalInstructions(newProc.instructions);

                // Initialize Page Table
                int numPages = memorySize / systemConfig.mem_per_frame;
                for (int vpn = 0; vpn < numPages; ++vpn) {
                    PageTableEntry entry;
                    entry.virtualPageNumber = vpn;
                    entry.valid = false; // Page not yet in memory
                    entry.frameNumber = -1;
                    entry.dirty = false;
                    entry.referenced = false;
                    newProc.pageTable[vpn] = entry;
                }

                globalProcesses.push_back(move(newProc));
            }

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

                // This check now correctly uses info from the Screen object,
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

            // Lock mutexes in consistent order
            {
                lock_guard<mutex> start_lock(processMutex); // Changed from proc_lock
                lock_guard<mutex> screen_lock(screensMutex);
                lock_guard<mutex> wait_lock(waiting_queue_mutex);
                lock_guard<mutex> queue_lock(queue_mutex);
                lock_guard<mutex> mem_lock(memory_mutex);

                screens.clear();
                while (!waiting_for_memory_queue.empty()) waiting_for_memory_queue.pop();
                while (!ready_queue.empty()) ready_queue.pop();
                current_memory_used = 0;

                if (globalProcesses.empty()) {
                    // Generate 10 processes with randomized instructions

                    for (int i = 1; i <= 10; ++i) {
                        stringstream name;
                        name << "process" << setfill('0') << setw(2) << i;

                        // Calculate a random, power-of-2 memory size
                        int min_exp = static_cast<int>(log2(systemConfig.min_mem_per_proc));
                        int max_exp = static_cast<int>(log2(systemConfig.max_mem_per_proc));
                        int rand_exp = min_exp + (rand() % (max_exp - min_exp + 1));
                        int random_mem_size = static_cast<int>(pow(2, rand_exp));

                        // === [FIXED] === Assign a proper PID to the new process
                        int pid = nextPID++;
                        Process newProc(name.str(), random_mem_size, pid);
                        newProc.instructions = generateProcessInstructions(systemConfig.min_ins, systemConfig.max_ins, random_mem_size);
                        newProc.totalTasks = countTotalInstructions(newProc.instructions);
                        newProc.currentInstructionIndex = 0;

                        // === [FIXED] === Initialize the Page Table for the new process
                        int numPages = random_mem_size / systemConfig.mem_per_frame;
                        for (int vpn = 0; vpn < numPages; ++vpn) {
                            PageTableEntry entry;
                            entry.virtualPageNumber = vpn;
                            entry.valid = false; // Page not yet in memory
                            entry.frameNumber = -1;
                            entry.dirty = false;
                            entry.referenced = false;
                            newProc.pageTable[vpn] = entry;
                        }

                        globalProcesses.push_back(std::move(newProc));
                    }
                }

                // Add all new processes to the waiting queue
                for (auto& proc : globalProcesses) {
                    if (!proc.isFinished) {
                        waiting_for_memory_queue.push(&proc);
                    }
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
        else if (command == "vmstat") {
            printEnhancedVMStat();
        }
        
        else if (!command.empty()) {
            if (inScreen) {
                // This just advances the UI
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
