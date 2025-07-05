#ifndef __HISTORY_HPP
#define __HISTORY_HPP

#include <readline/readline.h>
#include <readline/history.h>
#include <deque>
#include <string>
#include <cstdio>

#define HISTORY_FILE ".history"
#define MAX_SIZE 1000

class history
{
private:
    std::deque<std::string> dequeue;
    int max_size;
    FILE *fp;
    
    // Private helper methods
    void load_history_from_file();
    void save_history_to_file();

public:
    history();
    ~history();
    
    int curr_ind;
    
    // Basic operations
    int get_size();
    bool isempty();
    void add_history(const std::string& line);
    void clear_history();
    
    // Navigation operations
    void decrement_history();
    void increment_history();
    std::string get_curr();
    std::string get_history_item(int index);
    
    // Display operations
    void print_history();
};

#endif