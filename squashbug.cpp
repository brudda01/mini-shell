#include "squashbug.hpp"

squashbug::squashbug(pid_t pid, bool suggest) : sbpid(pid), suggest(suggest)
{   
    if (pid <= 0) {
        throw invalid_argument("Invalid PID: " + to_string(pid));
    }
    
    build_process_map();
}

squashbug::~squashbug()
{
    // Cleanup handled by STL containers
}

void squashbug::build_process_map()
{
    DIR *dirp = opendir("/proc");
    if (!dirp) {
        throw runtime_error("Failed to open /proc directory: " + string(strerror(errno)));
    }

    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || entry->d_type != DT_DIR) {
            continue;
        }

        // Validate that directory name is numeric (PID)
        if (!is_numeric(entry->d_name)) {
            continue;
        }

        try {
            parse_process_status(entry->d_name);
        } catch (const exception& e) {
            // Skip processes we can't read (common for security reasons)
            continue;
        }
    }
    closedir(dirp);
}

bool squashbug::is_numeric(const string& str)
{
    if (str.empty()) return false;
    return all_of(str.begin(), str.end(), ::isdigit);
}

void squashbug::parse_process_status(const string& pid_str)
{
    string status_file = "/proc/" + pid_str + "/status";
    ifstream file_stream(status_file);
    
    if (!file_stream.is_open()) {
        return; // Process might have terminated
    }
    
    string line;
    statusMap status_values;
    
    while (getline(file_stream, line)) {
        size_t colon_pos = line.find(':');
        if (colon_pos == string::npos) {
            continue;
        }
        
        string key = line.substr(0, colon_pos);
        string value = line.substr(colon_pos + 1);
        
        // Trim whitespace from value
        size_t start = value.find_first_not_of(" \t");
        if (start != string::npos) {
            size_t end = value.find_last_not_of(" \t");
            value = value.substr(start, end - start + 1);
        } else {
            value = "";
        }
        
        status_values[key] = value;
    }
    
    if (!status_values.empty()) {
        int pid = stoi(pid_str);
        pidMap[pid] = status_values;
    }
}

void squashbug::returnChildren(pid_t pid, set<int> &pids)
{   
    for (const auto& entry : pidMap) {
        auto ppid_it = entry.second.find("PPid");
        if (ppid_it != entry.second.end()) {
            try {
                int parent_pid = stoi(ppid_it->second);
                if (parent_pid == pid) {
                    pids.insert(entry.first);
                    returnChildren(entry.first, pids);
                }
            } catch (const exception& e) {
                // Skip invalid PPid values
                continue;
            }
        }
    }
}

int squashbug::countChildren(pid_t pid)
{
    int count = 0;
    for (const auto& entry : pidMap) {
        auto ppid_it = entry.second.find("PPid");
        if (ppid_it != entry.second.end()) {
            try {
                int parent_pid = stoi(ppid_it->second);
                if (parent_pid == pid) {
                    count++;
                    count += countChildren(entry.first);
                }
            } catch (const exception& e) {
                // Skip invalid PPid values
                continue;
            }
        }
    }
    return count;
}

string squashbug::get_process_field(pid_t pid, const string& field)
{
    auto pid_it = pidMap.find(pid);
    if (pid_it == pidMap.end()) {
        return "";
    }
    
    auto field_it = pid_it->second.find(field);
    if (field_it == pid_it->second.end()) {
        return "";
    }
    
    return field_it->second;
    }

void squashbug::print_process_info(pid_t pid, int process_number)
{
    string name = get_process_field(pid, "Name");
    string state = get_process_field(pid, "State");
    int children = countChildren(pid);
    
    cout << "Process " << process_number << ": ";
    cout << left << setw(20) << setfill(' ') << name;
        cout << "PID: ";
    cout << left << setw(10) << setfill(' ') << pid;
        cout << "State: ";
    cout << left << setw(20) << setfill(' ') << state;
        cout << "Children: ";
    cout << left << setw(15) << setfill(' ') << children << endl;
}

void squashbug::print_process_tree()
{
    cout << "Process Tree:" << endl;
    
    string ppid_str = get_process_field(sbpid, "PPid");
    if (ppid_str.empty()) {
        cout << "Cannot find parent process information" << endl;
        return;
    }
    
    try {
        pid_t current_pid = stoi(ppid_str);
        int counter = 1;
        
        // Walk up the process tree
        while (current_pid > 0 && counter <= 10) { // Limit depth to prevent infinite loops
            print_process_info(current_pid, counter);
            
            string parent_ppid = get_process_field(current_pid, "PPid");
            if (parent_ppid.empty()) {
                    break;
                }
            
            pid_t next_pid = stoi(parent_ppid);
            if (next_pid == current_pid) { // Prevent infinite loop
                break;
            }
            
            current_pid = next_pid;
            counter++;
        }
    } catch (const exception& e) {
        cout << "Error walking process tree: " << e.what() << endl;
    }
}

pid_t squashbug::suggest_malicious_process()
{
    vector<pid_t> candidate_pids;
    
    // Build list of candidate PIDs (process and its parents)
    string ppid_str = get_process_field(sbpid, "PPid");
    candidate_pids.push_back(sbpid);
    
    if (!ppid_str.empty()) {
        try {
            pid_t parent_pid = stoi(ppid_str);
            if (parent_pid > 0) {
                candidate_pids.push_back(parent_pid);
                
                string grandparent_ppid = get_process_field(parent_pid, "PPid");
                if (!grandparent_ppid.empty()) {
                    pid_t grandparent_pid = stoi(grandparent_ppid);
                    if (grandparent_pid > 0) {
                        candidate_pids.push_back(grandparent_pid);
                    }
                }
            }
        } catch (const exception& e) {
            // Continue with just the original PID
        }
    }
    
    // Find processes with the same name as the target
    string target_name = get_process_field(sbpid, "Name");
    set<pid_t> same_name_pids;
    
    for (pid_t pid : candidate_pids) {
        string name = get_process_field(pid, "Name");
        if (name == target_name) {
            same_name_pids.insert(pid);
        }
    }
    
    // First, look for sleeping processes with the same name
    for (pid_t pid : same_name_pids) {
        string state = get_process_field(pid, "State");
        if (!state.empty() && state[0] == 'S') {
            return pid;
        }
    }
    
    // If no sleeping process found, return the one with most children
    pid_t best_candidate = 0;
    int max_children = -1;
    
    for (pid_t pid : same_name_pids) {
        int children = countChildren(pid);
        if (children > max_children) {
            max_children = children;
            best_candidate = pid;
                    }
                }
    
    return best_candidate > 0 ? best_candidate : sbpid;
        }

bool squashbug::confirm_kill()
{
    cout << "Do you want to kill this process and all its children? (y/n): ";
    string response;
    
    if (!(cin >> response)) {
        cout << "Error reading input" << endl;
        return false;
    }
    
    return (response == "y" || response == "yes" || response == "Y" || response == "YES");
}

void squashbug::kill_process_tree(pid_t pid)
        {
            set<int> children;
    returnChildren(pid, children);
    
    cout << "Killing process tree..." << endl;
    
    // Kill children first
    for (int child_pid : children) {
        if (kill(child_pid, SIGKILL) == 0) {
            cout << "Killed child process " << child_pid << endl;
        } else {
            cerr << "Failed to kill child process " << child_pid << ": " << strerror(errno) << endl;
        }
    }
    
    // Kill the main process
    if (kill(pid, SIGKILL) == 0) {
        cout << "Killed main process " << pid << endl;
    } else {
        cerr << "Failed to kill main process " << pid << ": " << strerror(errno) << endl;
    }
}

void squashbug::run()
{   
    if (pidMap.find(sbpid) == pidMap.end()) {
        cout << "PID " << sbpid << " not found in process table" << endl;
        return;
    }
    
    print_process_tree();
    
    if (suggest) {
        pid_t suggested_pid = suggest_malicious_process();
        cout << "Suggested Trojan PID is: " << suggested_pid << endl;
        
        if (confirm_kill()) {
            kill_process_tree(suggested_pid);
        } else {
            cout << "Operation cancelled." << endl;
        }
    }
    
    cout << "Done." << endl;
    }
}