#include <iostream>
#include <algorithm>
#include <string>
#include <cstring>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <glob.h>
#include <readline/readline.h>
#include <ext/stdio_filebuf.h>
#include <memory>
#include <limits.h>

#include "delep.hpp"
#include "history.hpp"
#include "squashbug.hpp"

using namespace std;

// Constants
const size_t MAX_BUFFER_SIZE = 4096;
const size_t DEFAULT_CAPACITY = 256;

static sigjmp_buf env;
size_t job_number = 1;
volatile bool is_background;
pid_t foreground_pid;
set<pid_t> background_pids;
history h;
char *curr_line = nullptr;

class Command
{
public:
    string command;
    vector<string> arguments;
    int input_fd, output_fd;
    string input_file, output_file;
    pid_t pid;
    bool pipe_mode = false;

    Command(const string &cmd) : command(cmd), input_fd(STDIN_FILENO), output_fd(STDOUT_FILENO), input_file(""), output_file(""), pid(-1)
    {
        if (!parse_command()) {
            throw runtime_error("Failed to parse command: " + cmd);
        }
    }

    ~Command()
    {
        if (input_fd != STDIN_FILENO && input_fd != -1)
            close(input_fd);
        if (output_fd != STDOUT_FILENO && output_fd != -1)
            close(output_fd);
    }

private:
    bool parse_command()
    {
        try {
            parse_arguments();
            handle_wildcards();
            setup_io_redirection();
            return true;
        } catch (const exception& e) {
            cerr << "Error parsing command: " << e.what() << endl;
            return false;
        }
    }

    void parse_arguments()
    {
        stringstream ss(command);
        string arg;
        string temp = "";
        bool backslash = false;
        
        while (ss >> arg)
        {
            if (arg == "<")
            {
                if (!(ss >> input_file)) {
                    throw runtime_error("Expected input file after '<'");
                }
                backslash = false;
            }
            else if (arg == ">")
            {
                if (!(ss >> output_file)) {
                    throw runtime_error("Expected output file after '>'");
                }
                backslash = false;
            }
            else if (arg.length() > 0 && arg[arg.size() - 1] == '\\')
            {
                temp = temp + arg;
                temp[temp.size() - 1] = ' ';
                backslash = true;
            }
            else
            {
                if (backslash)
                {
                    temp = temp + arg;
                    arguments.push_back(temp);
                    temp = "";
                    backslash = false;
                }
                else
                    arguments.push_back(arg);
            }
        }

        if (arguments.empty()) {
            throw runtime_error("No command specified");
        }
        
        command = arguments[0];
    }

    void handle_wildcards()
    {
        vector<string> temp_args;
        for (auto &arg : arguments)
        {
            if (arg.find('*') != string::npos || arg.find('?') != string::npos)
            {
                glob_t glob_result;
                memset(&glob_result, 0, sizeof(glob_result));
                
                int ret = glob(arg.c_str(), GLOB_TILDE, NULL, &glob_result);
                if (ret != 0)
                {
                    if (ret == GLOB_NOMATCH) {
                        // No matches found, keep original argument
                        temp_args.push_back(arg);
                    } else {
                        globfree(&glob_result);
                        throw runtime_error("Glob error for pattern: " + arg);
                    }
                }
                else
                {
                    for (size_t i = 0; i < glob_result.gl_pathc; ++i)
                    {
                        temp_args.push_back(string(glob_result.gl_pathv[i]));
                    }
                    globfree(&glob_result);
                }
            }
            else
                temp_args.push_back(arg);
        }

        arguments = temp_args;
    }

    void setup_io_redirection()
    {
        if (!input_file.empty())
        {
            input_fd = open(input_file.c_str(), O_RDONLY);
            if (input_fd == -1)
            {
                throw runtime_error("Error opening input file: " + input_file + " - " + strerror(errno));
            }
        }

        if (!output_file.empty())
        {
            output_fd = open(output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (output_fd == -1)
            {
                throw runtime_error("Error opening output file: " + output_file + " - " + strerror(errno));
            }
        }
    }
};

// Utility functions
string get_safe_string(const char* str) {
    return str ? string(str) : string("");
}

string get_current_directory() {
    vector<char> buffer(DEFAULT_CAPACITY);
    char* result = nullptr;
    
    while (!(result = getcwd(buffer.data(), buffer.size()))) {
        if (errno == ERANGE) {
            buffer.resize(buffer.size() * 2);
        } else {
            throw runtime_error("Failed to get current directory: " + string(strerror(errno)));
        }
    }
    
    return string(result);
}

string get_hostname() {
    vector<char> buffer(DEFAULT_CAPACITY);
    
    while (gethostname(buffer.data(), buffer.size()) < 0) {
        if (errno == ENAMETOOLONG) {
            buffer.resize(buffer.size() * 2);
        } else {
            throw runtime_error("Failed to get hostname: " + string(strerror(errno)));
        }
    }
    
    return string(buffer.data());
}

string shell_prompt()
{
    try {
        string user = get_safe_string(getenv("USER"));
        string pcname = get_hostname();
        string current_directory = get_current_directory();
        
        return user + "@" + pcname + ":" + current_directory + "$ ";
    } catch (const exception& e) {
        cerr << "Error creating prompt: " << e.what() << endl;
        return "shell$ ";
    }
}

void delim_remove(string &command)
{
    // Remove leading spaces
    size_t start = command.find_first_not_of(" \t");
    if (start == string::npos) {
        command = "";
        return;
    }
    
    // Remove trailing spaces
    size_t end = command.find_last_not_of(" \t");
    command = command.substr(start, end - start + 1);
}

int execute_command(Command &command, bool background)
{
    // Validate command
    if (command.arguments.empty()) {
        cerr << "No command to execute" << endl;
        return -1;
    }

    // Prepare arguments for execvp
    vector<char*> args;
    for (const auto& arg : command.arguments) {
        args.push_back(const_cast<char*>(arg.c_str()));
    }
    args.push_back(nullptr);

    // Redirect input and output
    if (dup2(command.input_fd, STDIN_FILENO) == -1) {
        perror("dup2 input");
        return -1;
    }
    if (dup2(command.output_fd, STDOUT_FILENO) == -1) {
        perror("dup2 output");
        return -1;
    }

    // Execute the command
    int ret = execvp(command.command.c_str(), args.data());
    if (ret == -1) {
        cerr << "Error executing command: " << command.command << " - " << strerror(errno) << endl;
        return -1;
    }
    
    return 0;
}

// Signal handlers
void ctrl_c_handler(int signum)
{
    if (foreground_pid == 0) {
        siglongjmp(env, 42);
    }
    cout << endl;
    if (kill(foreground_pid, SIGTERM) == -1) {
        kill(foreground_pid, SIGKILL);
    }
    foreground_pid = 0;
}

void ctrl_z_handler(int signum)
{
    if (foreground_pid == 0) {
        siglongjmp(env, 42);
    }
    cout << endl << "[" << job_number++ << "] " << foreground_pid << endl;
    kill(foreground_pid, SIGSTOP);
    background_pids.insert(foreground_pid);
    foreground_pid = 0;
}

void child_signal_handler(int signum)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        background_pids.erase(pid);
    }
}

// Readline key bindings
static int key_up_arrow(int count, int key)
{
    if (count == 0) return 0;
    
    if (h.curr_ind == h.get_size()) {
        free(curr_line);
        curr_line = strdup(rl_line_buffer);
    }
    
    h.decrement_history();
    string line = h.get_curr();
    rl_replace_line(line.c_str(), 0);
    rl_point = line.size();
    return 0;
}

static int key_down_arrow(int count, int key)
{
    if (count == 0) return 0;
    
    if (h.curr_ind == h.get_size()) {
        free(curr_line);
        curr_line = strdup(rl_line_buffer);
    }
    
    h.increment_history();
    if (h.curr_ind < h.get_size()) {
        string line = h.get_curr();
        rl_replace_line(line.c_str(), 0);
        rl_point = line.size();
    } else {
        rl_replace_line(curr_line ? curr_line : "", 0);
        rl_point = rl_end;
    }
    return 0;
}

static int key_ctrl_a(int count, int key)
{
    if (count == 0) return 0;
    rl_point = 0;
    return 0;
}

static int key_ctrl_e(int count, int key)
{
    if (count == 0) return 0;
    rl_point = rl_end;
    return 0;
}

// Built-in command handlers
bool handle_builtin_command(Command& shell_command)
{
    if (shell_command.command == "exit") {
        cout << "exit" << endl;
        exit(EXIT_SUCCESS);
    }
    else if (shell_command.command == "cd") {
        if (shell_command.arguments.size() == 1) {
            const char* home = getenv("HOME");
            if (home && chdir(home) != 0) {
                perror("cd");
            }
        }
        else if (shell_command.arguments.size() == 2) {
            if (chdir(shell_command.arguments[1].c_str()) != 0) {
                perror("cd");
            }
        }
        else {
            cerr << "cd: too many arguments" << endl;
        }
        return true;
    }
    else if (shell_command.command == "pwd") {
        try {
            string cwd = get_current_directory();
            if (write(shell_command.output_fd, cwd.c_str(), cwd.length()) == -1 ||
                write(shell_command.output_fd, "\n", 1) == -1) {
                perror("pwd");
            }
        } catch (const exception& e) {
            cerr << "pwd: " << e.what() << endl;
        }
        return true;
    }
    
    return false;
}

// Command execution wrapper
int execute_child_process(Command& shell_command, bool is_background, int pipe_write_fd)
{
    // Set up signal handlers for child
    signal(SIGINT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);

    // Handle special commands
    if (shell_command.command == "delep") {
        if (shell_command.arguments.size() != 2) {
            cerr << "delep: usage: delep <filepath>" << endl;
            return -1;
        }
        delep(const_cast<char*>(shell_command.arguments[1].c_str()), pipe_write_fd);
        return 0;
    }
    else if (shell_command.command == "sb") {
        if (shell_command.arguments.size() < 2 || shell_command.arguments.size() > 3) {
            cerr << "sb: usage: sb <PID> [-suggest]" << endl;
            return -1;
        }
        
        bool suggest = (shell_command.arguments.size() == 3 && 
                       shell_command.arguments[2] == "-suggest");
        
        try {
            pid_t target_pid = stoi(shell_command.arguments[1]);
            squashbug sb(target_pid, suggest);
            sb.run();
            return 0;
        } catch (const exception& e) {
            cerr << "sb: invalid PID: " << shell_command.arguments[1] << endl;
            return -1;
        }
    }
    
    // Execute regular command
    return execute_command(shell_command, is_background);
}

// Process delep command output
void handle_delep_output(int pipe_read_fd, const string& filename)
{
    vector<char> buffer(MAX_BUFFER_SIZE);
    string pids_data;
    ssize_t bytes_read;
    
    while ((bytes_read = read(pipe_read_fd, buffer.data(), buffer.size() - 1)) > 0) {
        buffer[bytes_read] = '\0';
        pids_data += buffer.data();
    }
    
    if (bytes_read < 0) {
        perror("read delep output");
        return;
    }
    
    set<int> pids_lock, pids_nolock;
    stringstream ss(pids_data);
    string entry;
    
    while (getline(ss, entry, ',')) {
        if (entry.empty()) continue;
        
        size_t colon_pos = entry.find(':');
        if (colon_pos == string::npos) continue;
        
        string type = entry.substr(0, colon_pos);
        string pid_str = entry.substr(colon_pos + 1);
        
        try {
            int pid = stoi(pid_str);
            if (type == "Lock") {
                pids_lock.insert(pid);
            } else if (type == "NoLock") {
                pids_nolock.insert(pid);
            }
        } catch (const exception& e) {
            cerr << "Warning: Invalid PID in delep output: " << pid_str << endl;
        }
    }
    
    // Combine all PIDs
    set<int> all_pids(pids_lock.begin(), pids_lock.end());
    all_pids.insert(pids_nolock.begin(), pids_nolock.end());
    
    if (all_pids.empty()) {
        cout << "No process has the file open" << endl;
        return;
    }
    
    // Display results
    cout << "Following PIDs have opened the given file in lock mode:" << endl;
    for (int pid : pids_lock) {
        cout << pid << endl;
    }
    
    cout << "Following PIDs have opened the given file in normal mode:" << endl;
    for (int pid : pids_nolock) {
        cout << pid << endl;
    }
    
    // Ask for confirmation
    cout << "Do you want to kill all the processes using the file? (yes/no): ";
    string response;
    if (!(cin >> response)) {
        cout << "Error reading response" << endl;
        return;
    }
    
    if (response == "yes") {
        for (int pid : all_pids) {
            if (kill(pid, SIGKILL) == 0) {
                cout << "Killed process " << pid << endl;
            } else {
                cerr << "Failed to kill process " << pid << ": " << strerror(errno) << endl;
            }
        }
        
        if (remove(filename.c_str()) == 0) {
            cout << "Deleted file " << filename << endl;
        } else {
            cerr << "Error deleting file " << filename << ": " << strerror(errno) << endl;
        }
    } else {
        cout << "Exiting..." << endl;
    }
}

// Parse and execute commands
vector<string> parse_pipeline(const string& command)
{
    vector<string> commands;
    stringstream ss(command);
    string cmd;
    
    while (getline(ss, cmd, '|')) {
        delim_remove(cmd);
        if (!cmd.empty()) {
            commands.push_back(cmd);
        }
    }
    
    return commands;
}

void execute_pipeline(const vector<string>& commands)
{
    vector<int> pipe_fds;
    vector<pid_t> child_pids;
    
    try {
        // Create pipes for pipeline
        for (size_t i = 0; i < commands.size() - 1; i++) {
            int pipefd[2];
            if (pipe(pipefd) == -1) {
                throw runtime_error("Failed to create pipe: " + string(strerror(errno)));
            }
            pipe_fds.push_back(pipefd[0]); // read end
            pipe_fds.push_back(pipefd[1]); // write end
        }
        
        // Execute each command in the pipeline
        for (size_t i = 0; i < commands.size(); i++) {
            Command shell_command(commands[i]);
            
            // Check for background execution
                is_background = false;
            if (!shell_command.arguments.empty() && 
                shell_command.arguments.back() == "&") {
                    is_background = true;
                    shell_command.arguments.pop_back();
                }

            // Handle built-in commands (only for single commands, not in pipelines)
            if (commands.size() == 1 && handle_builtin_command(shell_command)) {
                return;
            }
            
            // Set up pipes for command
            if (i > 0) {
                shell_command.input_fd = pipe_fds[(i-1)*2]; // read from previous pipe
            }
            if (i < commands.size() - 1) {
                shell_command.output_fd = pipe_fds[i*2 + 1]; // write to next pipe
            }
            
            // Create communication pipe for special commands
            int comm_pipe[2] = {-1, -1};
            if (shell_command.command == "delep") {
                if (pipe(comm_pipe) == -1) {
                    throw runtime_error("Failed to create communication pipe");
                }
            }
            
            // Fork child process
            pid_t pid = fork();
            if (pid == -1) {
                throw runtime_error("Failed to fork: " + string(strerror(errno)));
            }
            
            if (pid == 0) {
                // Child process
                
                // Close unused pipe ends
                for (size_t j = 0; j < pipe_fds.size(); j++) {
                    if (j != (i-1)*2 && j != i*2 + 1) {
                        close(pipe_fds[j]);
                    }
                }
                
                if (comm_pipe[0] != -1) close(comm_pipe[0]);
                
                exit(execute_child_process(shell_command, is_background, 
                                         comm_pipe[1] != -1 ? comm_pipe[1] : -1));
            } else {
                // Parent process
                child_pids.push_back(pid);
                
                if (is_background) {
                    cout << "[" << job_number++ << "] " << pid << endl;
                    background_pids.insert(pid);
                } else {
                    foreground_pid = pid;
                }
                
                // Close used pipe ends
                if (i > 0) {
                    close(pipe_fds[(i-1)*2]);
                }
                if (i < commands.size() - 1) {
                    close(pipe_fds[i*2 + 1]);
                }
                
                // Handle special command output
                if (shell_command.command == "delep" && comm_pipe[0] != -1) {
                    close(comm_pipe[1]);
                    if (!is_background) {
                        int status;
                        waitpid(pid, &status, 0);
                        if (shell_command.arguments.size() >= 2) {
                            handle_delep_output(comm_pipe[0], shell_command.arguments[1]);
                        }
                    }
                    close(comm_pipe[0]);
                }
            }
        }
        
        // Wait for foreground processes
        if (!is_background) {
            for (pid_t pid : child_pids) {
                int status;
                waitpid(pid, &status, 0);
            }
        }
        
        foreground_pid = 0;
        
    } catch (const exception& e) {
        cerr << "Pipeline execution error: " << e.what() << endl;
        
        // Clean up pipes
        for (int fd : pipe_fds) {
            close(fd);
        }
        
        // Kill any started processes
        for (pid_t pid : child_pids) {
            kill(pid, SIGKILL);
        }
    }
}

void setup_signal_handlers()
{
    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = &ctrl_c_handler;
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("sigaction SIGINT");
    }

    struct sigaction sa_tstp;
    memset(&sa_tstp, 0, sizeof(sa_tstp));
    sa_tstp.sa_handler = &ctrl_z_handler;
    if (sigaction(SIGTSTP, &sa_tstp, NULL) == -1) {
        perror("sigaction SIGTSTP");
    }

    struct sigaction sa_child;
    memset(&sa_child, 0, sizeof(sa_child));
    sa_child.sa_handler = &child_signal_handler;
    if (sigaction(SIGCHLD, &sa_child, NULL) == -1) {
        perror("sigaction SIGCHLD");
    }
}

void setup_readline()
{
    rl_initialize();
    rl_bind_keyseq("\\e[A", key_up_arrow);
    rl_bind_keyseq("\\e[B", key_down_arrow);
    rl_bind_keyseq("\\C-a", key_ctrl_a);
    rl_bind_keyseq("\\C-e", key_ctrl_e);
    rl_bind_key('\t', rl_insert);
}

int main()
{
    try {
        setup_readline();
        setup_signal_handlers();
        
        while (true) {
            if (sigsetjmp(env, 1) == 42) {
                cout << endl;
                continue;
            }

            string prompt = shell_prompt();
            char* input = readline(prompt.c_str());

            // Handle EOF (Ctrl+D)
            if (input == nullptr) {
                cout << "exit" << endl;
                break;
            }

            string command(input);
            free(input);

            delim_remove(command);
            if (command.empty()) {
                continue;
            }

            h.add_history(command);
            
            // Parse and execute pipeline
            vector<string> commands = parse_pipeline(command);
            if (!commands.empty()) {
                execute_pipeline(commands);
            }
        }
    } catch (const exception& e) {
        cerr << "Fatal error: " << e.what() << endl;
        return EXIT_FAILURE;
    }
    
    // Cleanup
    free(curr_line);
    return EXIT_SUCCESS;
}