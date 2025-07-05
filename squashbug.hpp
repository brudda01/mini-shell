#ifndef __MALWARE_HPP
#define __MALWARE_HPP

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <dirent.h>
#include <set>
#include <sys/types.h>
#include <signal.h>
#include <iomanip>
#include <algorithm>
#include <stdexcept>
#include <cerrno>

using namespace std;

typedef map<string, string> statusMap;
typedef map<int, statusMap> pid_Map;

#define SLEEP_DUR_MIN 2
#define NUM_CHILD 5
#define NUM_CHILD_CHILD 10

class squashbug
{
    public:
        squashbug(pid_t pid, bool suggest);
        ~squashbug();
        void run();
    private:
        pid_t sbpid;
        bool suggest;
        pid_Map pidMap;
        
        // Process map building
        void build_process_map();
        void parse_process_status(const string& pid_str);
        bool is_numeric(const string& str);
        
        // Process tree operations
        int countChildren(pid_t pid);
        void returnChildren(pid_t pid, set<int>& pids);
        string get_process_field(pid_t pid, const string& field);
        
        // Display functions
        void print_process_tree();
        void print_process_info(pid_t pid, int process_number);
        
        // Malware detection and elimination
        pid_t suggest_malicious_process();
        bool confirm_kill();
        void kill_process_tree(pid_t pid);
};

#endif