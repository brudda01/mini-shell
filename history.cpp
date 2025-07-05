#include "history.hpp"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cstring>

history::history() : max_size(MAX_SIZE), curr_ind(0), fp(nullptr)
{
    load_history_from_file();
}

history::~history()
{   
    save_history_to_file();
}

void history::load_history_from_file()
{
    std::ifstream file(HISTORY_FILE);
    if (!file.is_open()) {
        // File doesn't exist yet, which is fine
        curr_ind = 0;
        return;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        // Remove trailing newline if present
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        
        // Maintain maximum size
        if (static_cast<int>(dequeue.size()) >= max_size) {
            dequeue.pop_front();
        }
        
        dequeue.push_back(line);
    }
    
    curr_ind = dequeue.size();
    file.close();
}

void history::save_history_to_file()
{
    std::ofstream file(HISTORY_FILE);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not save history to file " << HISTORY_FILE << std::endl;
        return;
    }
    
    for (const auto& command : dequeue) {
        file << command << std::endl;
    }
    
    file.close();
}

int history::get_size()
{
    return static_cast<int>(dequeue.size());
}

bool history::isempty()
{
    return dequeue.empty();
}

void history::add_history(const std::string& line)
{
    // Don't add empty lines or duplicate consecutive commands
    if (line.empty()) {
        return;
    }
    
    if (!dequeue.empty() && dequeue.back() == line) {
        // Don't add duplicate consecutive commands
        curr_ind = dequeue.size();
        return;
    }
    
    // Maintain maximum size
    if (static_cast<int>(dequeue.size()) >= max_size) {
        dequeue.pop_front();
    }
    
    dequeue.push_back(line);
    curr_ind = dequeue.size();
}

void history::decrement_history()
{
    if (curr_ind > 0) {
        curr_ind--;
    }
}

void history::increment_history()
{
    if (curr_ind < static_cast<int>(dequeue.size())) {
        curr_ind++;
    }
}

std::string history::get_curr()
{
    if (curr_ind >= static_cast<int>(dequeue.size())) {
        return "";
    }
    
    if (curr_ind < 0) {
        curr_ind = 0;
        if (dequeue.empty()) {
            return "";
        }
    }
    
    return dequeue[curr_ind];
}

void history::clear_history()
{
    dequeue.clear();
    curr_ind = 0;
}

std::string history::get_history_item(int index)
{
    if (index < 0 || index >= static_cast<int>(dequeue.size())) {
        return "";
    }
    
    return dequeue[index];
}

void history::print_history()
{
    for (int i = 0; i < static_cast<int>(dequeue.size()); i++) {
        std::cout << (i + 1) << ": " << dequeue[i] << std::endl;
    }
}
