#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

const char *sysname = "shellax"; // sysname points to obejct of type array of chars. It points to the address of the first char in the sring

enum return_codes { // data structure written by the user to assign intergers to names to make reading of the code easier
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t { //data type that can be used to group items of possibly different types into a single type.
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args; // t's a pointer to a char *. The value stored in a char ** is a memory address, and the value stored at that memory address is a char *.
  char *redirects[3];     // array of pointers for char type variables
  struct command_t *next; // If we have a pointer to structure, members are accessed using arrow ( -> ) operator.
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) { // defines a variable called command of type command_t* wich is a pointer to command_t*
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd)); // places an absolute pathname of the current working directory in the array pointed to by buf, and returns buf. 
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname); //searches the environment variables at runtime for an entry with the specified name, and returns a pointer to the variable's value.
  return 0;
}
/**
 * Parse a command string into a command struct       // parse means to read it into commands understood by the struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;

  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c = malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before


    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;
  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}

int process_command(struct command_t *command, int piping_input_fd);
void resolve_command(char *command_name, char *pth);

int main() {
  while (1) {
    struct command_t *command = malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0
    int code;
    code = prompt(command);
    if (code == EXIT)
      break;
    code = process_command(command, 0);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}

bool is_builtin(char* name) {
  return strcmp(name, "uniq") == 0 || strcmp(name, "chatroom") == 0 || strcmp(name, "wiseman") == 0 || strcmp(name, "myfactor") == 0;
}

void execute_builtin(struct command_t* command);

int process_command(struct command_t *command, int piping_input_fd) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;
  if (strcmp(command->name, "exit") == 0)
    return EXIT;
  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[0]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }
  pid_t pid = fork();
  if (pid == 0) {
    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // increase args size by 2
    command->args = (char **)realloc(
        command->args, sizeof(char *) * (command->arg_count += 2));
    // shift everything forward by 1
    for (int i = command->arg_count - 2; i > 0; --i)
      command->args[i] = command->args[i - 1];
    // set args[0] as a copy of name
    command->args[0] = strdup(command->name);
    // set args[arg_count-1] (last) to NULL
    command->args[command->arg_count - 1] = NULL;
    char *command_path = malloc(1024);
    bool built_in = is_builtin(command->name);
    if (!built_in) {
      resolve_command(command->name, command_path); //try to find the path
    }
		if (built_in || strcmp(command_path, "")) {	
      // Handle I/O redirection		
      if (command->redirects[0]) {
        int fd = open(command->redirects[0], O_RDWR | O_CREAT);
        dup2(fd, 0); // copy the file descriptor of fd to stdin 0 so we close it after
        close(fd);
      }
      if (command->redirects[1]) {
        int fd = open(command->redirects[1], O_RDWR | O_CREAT | O_TRUNC);
        dup2(fd, 1);
        close(fd);
      } else if (command->redirects[2]) {
        int fd = open(command->redirects[2], O_RDWR | O_CREAT | O_APPEND);
        dup2(fd, 1);
        close(fd);
      }
      // Handle piping
      if (command->next) {
        int fd[2];
        pipe(fd);
        pid_t pipe_pid = fork();
        // the child will execute the first command and
        // the parent will recursively execurte the rest
        // when the child finishes.
        if (pipe_pid == 0) {
          close(fd[0]);
          if (piping_input_fd != 0) {
            dup2(piping_input_fd, 0);
            close(piping_input_fd);
          }
          if (fd[1] != 1) {
            dup2(fd[1], 1);
            close(fd[1]);
          }
          if (built_in) {
            execute_builtin(command);
          } else {
            execv(command_path, command->args);
          }
        } else {
          close(fd[1]);
          close(piping_input_fd);
          wait(0);
          process_command(command->next, fd[0]);
        }
      } else {
        if (piping_input_fd != 0) {
          dup2(piping_input_fd, 0);
          close(piping_input_fd);
        }
        if (built_in) {
          execute_builtin(command);
        } else {
          execv(command_path, command->args);
        }
      }
		} else {
      printf("command not found: %s\n", command->name);
    }
    exit(0);
  } else {
    if (!command->background) {
      wait(0);
    }
    return SUCCESS;
  }
  printf("-%s: %s: command not found\n", sysname, command->name);
  return UNKNOWN;
}

void solve_uniq(struct command_t* command);
void solve_chatroom(struct command_t* command);
void solve_wiseman(struct command_t* command);
void solve_myfactor(struct command_t* command);

void execute_builtin(struct command_t* command) {
  if (!strcmp(command->name, "uniq")) {
    solve_uniq(command);
  } else if (!strcmp(command->name, "chatroom")) {
    solve_chatroom(command);
  } else if (!strcmp(command->name, "wiseman")) {
    solve_wiseman(command);
  } else if (!strcmp(command->name, "myfactor")) {
    solve_myfactor(command);
  }
}

void solve_uniq(struct command_t* command) {
  bool print_count = ((command->arg_count >= 3) && ((!strcmp(command->args[1], "-c")) || (!strcmp(command->args[1], "--count"))));
  char * prev = 0;
  char * curr = 0;
  size_t len = 0;
  int count = 0;
  size_t slen;
  while((slen = getline(&curr, &len, stdin)) != -1) {
    if ((prev == NULL) || !strcmp(prev, curr)) {
      ++count;
    } else {
      if (print_count) {
        printf("%d %s", count, prev);
      } else {
        printf("%s", prev);
      }
      count = 1;
    }
    if (prev != NULL) {
      free(prev); // give back the memory space
    }
    prev = malloc(slen + 1); // allocate memory so that we can hold since it was pointing to0 (no memory space).
    strcpy(prev, curr);
  }
  if (print_count) {
    printf("%d %s", count, curr);
  } else {
    printf("%s", curr);
  }
}

void solve_chatroom(struct command_t* command) {

}

void solve_wiseman(struct command_t* command) {

}

// Diana's custom command
void solve_myfactor(struct command_t* command) {
  int n = command->args[2];
  printf("%d", n);
  for (int d = 2; d <= n-1; ++d) {
    if (n % d != 0) continue;
    while (n % d == 0) {
      printf("%d", d);
      n /= d;
    }
  }
  if (n != 1) printf("%d", n);
}

void resolve_command(char *command_name, char *pth) {
  const char* path = getenv("PATH");
  char* paths[32];
	char *cpy = strdup(path);
	char *tkn = strtok(cpy, ":");
	int j = 0;
	while (tkn != NULL) { 
		paths[j] = malloc(strlen(tkn) + 1);
		strcpy(paths[j], tkn);
		tkn = strtok(NULL, ":");
		j++;
	}
	free(cpy);
	int i;
	for (i = 0; i < sizeof(paths); i++) {
		if (paths[i] != NULL) {	
			char *file_name;
			DIR *d;
			struct dirent *dir;
			d = opendir(paths[i]); // RECURSE THROUGH DIRECTORIES , OPENS DIR TO 
			if (d != NULL) { 
				while ((dir = readdir(d)) != NULL) {			
					file_name = dir->d_name;
					if (!strcmp(file_name, command_name)) {
						strcpy(pth, paths[i]);
						strcpy(pth + strlen(paths[i]), "/");
						strcpy(pth + strlen(paths[i]) + 1, file_name);
						pth[strlen(paths[i]) + 1 + strlen(file_name)] = 0;
						closedir(d);
						return;
					}				
				}
				closedir(d);
			}
		}
	}
	strcpy(pth, "");
	return;
}

