#include <iostream>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/file.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <signal.h>

using namespace std;

volatile bool should_exit = false;

void signal_handler(int signum) {
    cout << "\nReceived signal " << signum << ". Exiting..." << endl;
    should_exit = true;
}

int main() {
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    cout << "Process PID: " << getpid() << endl;
    cout << "Opening lock.txt without file locking..." << endl;
    
    FILE *fp = fopen("lock.txt", "w");
    if (!fp) {
        cerr << "Error: Cannot create/open lock.txt: " << strerror(errno) << endl;
        return 1;
    }
    
    cout << "File opened successfully (no lock applied)" << endl;
    
    // Write some data to the file
    int write_count = 0;
    while (!should_exit) {
        fprintf(fp, "Write #%d from process %d (no lock)\n", ++write_count, getpid());
        fflush(fp);
        
        if (write_count == 1) {
            cout << "Writing to file without lock. Press Ctrl+C to stop." << endl;
        }
        
        sleep(2);
        
        // Limit writes to prevent infinite file growth
        if (write_count >= 100) {
            cout << "Maximum writes reached. Exiting..." << endl;
            break;
        }
    }
    
    fclose(fp);
    cout << "File closed. Program exiting normally." << endl;
    return 0;
}