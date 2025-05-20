#include <iostream>
#include <string>
#include <unordered_map>
#include <ctime>
#include <iomanip>  // For put_time
#include <sstream>  // For stringstream
#include <cstdlib>  // For system("clear") or system("cls")

using namespace std;

class Screen {
public:
    string name;
    int currentLine;
    int totalLines;
    string timestamp;

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
        //Display process name, instruction line number, and timestamp
        cout << "+Insert display here+\n";
    }

    // Progress number in instruction line
    void advance() {
        if (currentLine < totalLines)
            currentLine++;
    }
};

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

int main() {
    string command;
    unordered_map<string, Screen> screens;

    // Display initial messages
    displayHeader();
    cout << "Hello, Welcome to AJEL OS command.net" << endl;
    cout << "Type \"exit\" to quit, \"clear\" to clear the screen" << endl;

    // Main command loop
    while (true) {
        cout << "\nEnter a command: ";
        getline(cin, command);

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
        }
        else {
            cout << "Command not recognized." << endl;
        }
    }

    return 0;
}
