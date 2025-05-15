#include <iostream>
#include <string>
#include <cstdlib> // For system("clear") or system("cls")

using namespace std;

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
            break; // Exit the loop and end the program
        }
        if(command == "clear") { // clears screen & displays header
            system("cls");
            displayHeader();
            cout << "Hello, Welcome to AJEL OS command.net" << endl;
            cout << "Type \"exit\" to quit, \"clear\" to clear the screen" << endl;
        }
        else {
            cout << command << " command recognized. Doing something." << endl;
        }
    }

    return 0;
}
