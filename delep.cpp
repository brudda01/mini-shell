#include "delep.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cerrno>
#include <cstring>

using namespace std;

// Helper function to safely read a symbolic link
string safe_readlink(const string& path) {
    char buffer[PATH_MAX];
    ssize_t len = readlink(path.c_str(), buffer, sizeof(buffer) - 1);
    if (len == -1) {
        return "";
    }
    buffer[len] = '\0';
    return string(buffer);
}

// Helper function to check if a file has a lock
bool check_file_lock(const string& pid_str, const string& fd_str) {
    string fdinfo_path = "/proc/" + pid_str + "/fdinfo/" + fd_str;
    ifstream file(fdinfo_path);
    
    if (!file.is_open()) {
        return false;
    }
    
    string line;
    while (getline(file, line)) {
        if (line.find("lock:") == 0) {
            return true;
        }
    }
    
    return false;
}

// Helper function to validate PID string
bool is_valid_pid(const string& pid_str) {
    if (pid_str.empty()) return false;
    
    for (char c : pid_str) {
        if (!isdigit(c)) return false;
    }
    
    return true;
}

void delep(char *argpath, int fd)
{   
    if (!argpath) {
        string error_msg = "Error: NULL path argument";
        if (fd != -1) {
            write(fd, error_msg.c_str(), error_msg.length());
        }
        return;
    }
    
    // Validate the file path
    string target_path(argpath);
    if (target_path.empty()) {
        string error_msg = "Error: Empty path argument";
        if (fd != -1) {
            write(fd, error_msg.c_str(), error_msg.length());
        }
        return;
    }
    
    DIR *dirp = opendir("/proc");
    if (!dirp) {
        string error_msg = "Error: Cannot access /proc directory: " + string(strerror(errno));
        if (fd != -1) {
            write(fd, error_msg.c_str(), error_msg.length());
        }
        return;
    }

    ostringstream result_stream;
    vector<string> lock_pids;
    vector<string> nolock_pids;
    
    struct dirent *entry;
    while ((entry = readdir(dirp)) != NULL) {
        // Skip non-directory entries and special directories
        if (entry->d_type != DT_DIR || 
            strcmp(entry->d_name, ".") == 0 || 
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        // Validate PID directory name
        if (!is_valid_pid(entry->d_name)) {
            continue;
        }
        
        string pid_str(entry->d_name);
        string fd_dir_path = "/proc/" + pid_str + "/fd";
        
        DIR *fd_dirp = opendir(fd_dir_path.c_str());
        if (!fd_dirp) {
            // Process might have terminated or we don't have permission
            continue;
        }

        struct dirent *fd_entry;
        while ((fd_entry = readdir(fd_dirp)) != NULL) {
            if (strcmp(fd_entry->d_name, ".") == 0 || 
                strcmp(fd_entry->d_name, "..") == 0) {
                continue;
            }

            string fd_link_path = "/proc/" + pid_str + "/fd/" + fd_entry->d_name;
            string resolved_path = safe_readlink(fd_link_path);
            
            if (resolved_path.empty()) {
                continue;
            }
            
            // Check if this file descriptor points to our target file
            if (resolved_path == target_path) {
                try {
                    if (check_file_lock(pid_str, fd_entry->d_name)) {
                        lock_pids.push_back(pid_str);
                    } else {
                        nolock_pids.push_back(pid_str);
                    }
                } catch (const exception& e) {
                    // If we can't determine lock status, assume no lock
                    nolock_pids.push_back(pid_str);
                }
                break; // Found the file, no need to check other FDs for this PID
            }
        }
        closedir(fd_dirp);
    }
    closedir(dirp);
    
    // Build result string
    for (const string& pid : lock_pids) {
        result_stream << "Lock:" << pid << ",";
    }
    for (const string& pid : nolock_pids) {
        result_stream << "NoLock:" << pid << ",";
    }
    
    string result = result_stream.str();
    
    // Write result to file descriptor
    if (fd != -1) {
        if (write(fd, result.c_str(), result.length()) == -1) {
            cerr << "Error writing to file descriptor: " << strerror(errno) << endl;
        }
    }
}