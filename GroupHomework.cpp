#include <iostream>
#include <string>
#include <cstdlib> // For system("clear") or system("cls")
#include <map>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace std;

class Screen {
public:
    string name;
    int currentLine;
    int totalLines;
    string timestamp;

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
            break; // Exit the loop and end the program
        }
        if(command == "clear") { // clears screen & displays header
            system("cls");
            displayHeader();
            cout << "Hello, Welcome to AJEL OS command.net" << endl;
            cout << "Type \"exit\" to quit, \"clear\" to clear the screen" << endl;
        }
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
        }
        else {
            cout << command << " command recognized. Doing something." << endl;
        }
    }

    return 0;
}
