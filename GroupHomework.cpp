#include <iostream>
#include <string>
<<<<<<< HEAD
#include <cstdlib> // For system("clear") or system("cls")
#include <map>
#include <ctime>
#include <iomanip>
#include <sstream>
=======
#include <unordered_map>
#include <ctime>
#include <iomanip>  // For put_time
#include <sstream>  // For stringstream
#include <cstdlib>  // For system("clear") or system("cls")
>>>>>>> 19be02ec7e4b51382839d78c731bd73e3ced6eac

using namespace std;

class Screen {
public:
    string name;
    int currentLine;
    int totalLines;
    string timestamp;

<<<<<<< HEAD
    Screen(string n) {
        name = n;
        currentLine = 1;
        totalLines = 100; // placeholder
        timestamp = getCurrentTimestamp();
    }

    void display() {
        cout << "\n=== Screen: " << name << " ===\n";
        cout << "Process name: " << name << endl;
        cout << "Instruction line: " << currentLine << " / " << totalLines << endl;
        cout << "Created at: " << timestamp << endl;
        cout << "Type 'exit' to return to main menu.\n";
    }

private:
    string getCurrentTimestamp() {
        time_t now = time(nullptr);
        tm* localTime = localtime(&now);
        stringstream ss;
        ss << put_time(localTime, "%m/%d/%Y, %I:%M:%S %p");
        return ss.str();
    }
};


=======
    // Constructor for screen
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

    // Display function
    void display() const {
        system("cls");  // or use "clear" if you're on Linux/Mac
        
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
        cout << "\nProgress:\n";
        cout << "[";
        int progressWidth = 50;
        int pos = progressWidth * currentLine / totalLines;
        for (int i = 0; i < progressWidth; ++i) {
            if (i < pos) cout << "=";
            else if (i == pos) cout << ">";
            else cout << " ";
        }
        cout << "] " << (currentLine * 100 / totalLines) << "%\n";
    }

    // Progress number in instruction line
    void advance() {
        if (currentLine < totalLines)
            currentLine++;
    }
};

>>>>>>> 19be02ec7e4b51382839d78c731bd73e3ced6eac
// Function to display the CSOPESY ASCII header
void displayHeader() {
    cout << R"(
   _____         ____. ___________ .____       ________      _________
  /  _  \       |    | \_   _____/ |    |      \_____  \    /   _____/
 /  /_\  \      |    |  |    __)_  |    |       /   |   \   \_____  \ 
/    |    \ /\__|    |  |        \ |    |___   /    |    \  /        \
\____|____/ \________| /_________/ |________\  \_________/ /_________/
)" << endl;
}

<<<<<<< HEAD
void showCommands() {
    cout << "\nAvailable commands:\n";
    cout << "  help   - Show available commands\n";
    cout << "  clear  - Clear the screen\n";
    cout << "  exit   - Exit the program\n";
}



int main() {
    string command;
    map<string, Screen> screens;
    bool inScreen = false;
    string currentScreen = "";

=======
int main() {
    string command;
    unordered_map<string, Screen> screens;
>>>>>>> 19be02ec7e4b51382839d78c731bd73e3ced6eac

    // Display initial messages
    displayHeader();
    cout << "Hello, Welcome to AJEL OS command.net" << endl;
    cout << "Type \"exit\" to quit, \"clear\" to clear the screen" << endl;

    // Main command loop
    while (true) {
        cout << "\nEnter a command: ";
        getline(cin, command);

        if (inScreen) {
            if (command == "exit") {
                inScreen = false;
                 currentScreen = "";
                 displayHeader();
                cout << "Back to main menu." << endl;
            } else {
                cout << "[" << currentScreen << "] " << command << " command received.\n";
                screens[currentScreen].currentLine++;
             }
            continue;
        }   

        if (command == "exit") {
            cout << "exit command recognized. Exiting application." << endl;
            break;
        }
        else if (command == "clear") {
            system("cls");
            displayHeader();
            cout << "Hello, Welcome to AJEL OS command.net" << endl;
            cout << "Type \"exit\" to quit, \"clear\" to clear the screen" << endl;
        }
<<<<<<< HEAD
        else if (command.substr(0, 9) == "screen -r") {
            string name = command.substr(10);
            if (screens.find(name) != screens.end()) {
                inScreen = true;
                currentScreen = name;
                screens[name].display();
            } else {
                cout << "No screen found with name '" << name << "'." << endl;
            }
        }
        else if (command.substr(0, 9) == "screen -s") {
            string name = command.substr(10);
            if (screens.find(name) == screens.end()) {
                screens[name] = Screen(name);
                cout << "New screen '" << name << "' created." << endl;
            } else {
                cout << "Screen '" << name << "' already exists." << endl;
            }
=======
        else if (command == "initialize") {
            cout << "initialize command recognized. Doing something." << endl;
        }
        else if (command == "screen") {
            cout << "screen command recognized. Doing something." << endl;
        }
        else if (command == "scheduler-test") {
            cout << "scheduler-test command recognized. Doing something." << endl;
        }
        else if (command == "scheduler-stop") {
            cout << "scheduler-stop command recognized. Doing something." << endl;
        }
        else if (command == "report-util") {
            cout << "report-util command recognized. Doing something." << endl;
>>>>>>> 19be02ec7e4b51382839d78c731bd73e3ced6eac
        }
        else {
            cout << "Command not recognized." << endl;
        }
    }

    return 0;
}
