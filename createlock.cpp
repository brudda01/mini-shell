#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <signal.h>

using namespace std;

volatile bool should_exit = false;

void signal_handler(int signum) {
    cout << "\nReceived signal " << signum << ". Cleaning up and exiting..." << endl;
    should_exit = true;
}

int main()
{	
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    cout << "Process PID: " << getpid() << endl;
    
    FILE *fp = fopen("lock.txt", "w");
    if (!fp) {
        cerr << "Error: Cannot create/open lock.txt: " << strerror(errno) << endl;
        return 1;
    }
    
    int fd = fileno(fp);
    if (fd == -1) {
        cerr << "Error: Cannot get file descriptor: " << strerror(errno) << endl;
        fclose(fp);
        return 1;
    }
    
    cout << "Attempting to acquire exclusive lock..." << endl;
    int ret = flock(fd, LOCK_EX | LOCK_NB); // Non-blocking lock
    
    if (ret == 0) {
        cout << "Lock acquired successfully!" << endl;
        cout << "Holding lock indefinitely. Press Ctrl+C to release and exit." << endl;
        
        // Write some data to the file
        fprintf(fp, "This file is locked by process %d\n", getpid());
        fflush(fp);
        
        // Keep the lock until interrupted
        while (!should_exit) {
            sleep(1);
        }
        
        cout << "Releasing lock..." << endl;
        flock(fd, LOCK_UN);
        cout << "Lock released." << endl;
    } else {
        if (errno == EWOULDBLOCK) {
            cerr << "Error: File is already locked by another process" << endl;
        } else {
            cerr << "Error: Lock acquisition failed: " << strerror(errno) << endl;
        }
        fclose(fp);
        return 1;
    }
    
    fclose(fp);
    cout << "Program exiting normally." << endl;
    return 0;
}