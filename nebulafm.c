// % gcc nebulafm.c -o nebulafm -Wall -ggdb -lmagic $(ncursesw5-config --cflags --libs)

#include <stdio.h>
#include <curses.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <signal.h>
#include <magic.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define KEY_TAB 9
#define KEY_RETURN 10
#define KEY_BACKWARD 'h'
#define KEY_DOWNWARD 'j'
#define KEY_UPWARD 'k'
#define KEY_FORWARD 'l'
#define KEY_CUT 'd'
#define KEY_DELETE 'D'
#define KEY_HIDE 'z'
#define LEFT 0
#define RIGHT 1
#define REFRESH 50 // Refresh every 5 seconds
#define HIDDENVIEW 1 // Display or hide hidden files

typedef struct pane
{
    WINDOW *win;
    char *path;
    char *select_path;
    char *parent_dirname; // The name of the parent directory
    int dirs_num;
    int files_num;
    int top_index; // The file index to print the first line of the current window
    int select; // The position of the selected line in the window
}
pane;

/* Globals */
pane left_pane  = { .select = 1 };
pane right_pane = { .select = 1 };
int pane_flag = LEFT; // 0 - the active panel on the left; 1 - the active panel on the right
int termsize_x, termsize_y;
struct passwd *user_data;
char *conf_path = NULL; // The path to the configuration directory
char *clipboard_path = NULL;
char *editor = NULL; // Default editor
sigset_t signal_set; // Represent a signal set to specify what signals are affected
WINDOW *status_bar;
int back_flag = 0; // Changes to 1 after returning to parent directory
int hide_flag = HIDDENVIEW;

/* Prototypes */
void init_common(void);
void set_editor(void);
void init_paths(void);
void make_conf_dir(char *);
void init_curses(void);
void get_number_of_files(pane *);
void get_files_in_array(char *, char *[], char *[]);
int compare_elements(const void *, const void *);
void make_windows(void);
void refresh_windows(void);
WINDOW *create_window(int, int, int, int);
void restore_indexes(char *[], pane *);
void print_files(pane *, char *[], char *[]);
int print_list(pane *, char *[], int, int, int);
char *get_select_path(int, char *[], pane *);
void print_line(WINDOW *, int, char *);
void go_down(pane *);
void go_up(pane *);
void go_previous(pane *);
int is_dir(const char *);
void open_dir(pane *);
void open_file(pane *);
pid_t fork_exec(char *, char **);
void highlight_active_pane(int, int);
void print_status(pane *);
void print_notification(char *);
char *get_human_filesize(double, char *);
void append_clipboard(char *);
void rm_files(pane *);
void take_action(int, pane *);

int main(int argc, char *argv[])
{
    int keypress;

    /* Initialization */
    init_common();
    init_curses();

    do
    {
        getmaxyx(stdscr, termsize_y, termsize_x); // Get term size
        termsize_y--; // For status bar
        make_windows();

        get_number_of_files(&left_pane);
        get_number_of_files(&right_pane);

        /* Fill in variable-length arrays of all files in the current directory */
        char *dirs_list_l[left_pane.dirs_num];
        char *dirs_list_r[right_pane.dirs_num];
        char *files_list_l[left_pane.files_num];
        char *files_list_r[right_pane.files_num];
        get_files_in_array(left_pane.path, dirs_list_l, files_list_l);
        get_files_in_array(right_pane.path, dirs_list_r, files_list_r);

        /* Sorting files in dir alphabetically */
        qsort(dirs_list_l, left_pane.dirs_num, sizeof(char *), compare_elements);
        qsort(files_list_l, left_pane.files_num, sizeof(char *), compare_elements);
        qsort(dirs_list_r, right_pane.dirs_num, sizeof(char *), compare_elements);
        qsort(files_list_r, right_pane.files_num, sizeof(char *), compare_elements);

        /* Update 'select' and 'top_index' after go_previous() */
        if (back_flag == 1)
        {
            if (pane_flag == LEFT)
                restore_indexes(dirs_list_l, &left_pane);
            else if (pane_flag == RIGHT)
                restore_indexes(dirs_list_r, &right_pane);
            else
            {
                perror("pane_flag initialization error\n");
                exit(EXIT_FAILURE);
            }
        }

        /* Print and refresh */
        print_files(&left_pane, dirs_list_l, files_list_l);
        print_files(&right_pane, dirs_list_r, files_list_r);

        if (pane_flag == LEFT)
        {
            highlight_active_pane(0, 0);
            print_status(&left_pane);
            refresh_windows();

            /* Keybindings */
            keypress = wgetch(left_pane.win);
            if (keypress == ERR)
                continue;
            take_action(keypress, &left_pane);
        }
        else if (pane_flag == RIGHT)
        {
            highlight_active_pane(0, termsize_x / 2);
            print_status(&right_pane);
            refresh_windows();

            /* Keybindings */
            keypress = wgetch(right_pane.win);
            if (keypress == ERR)
                continue;
            take_action(keypress, &right_pane);
        }
        else
        {
            perror("pane_flag initialization error\n");
            exit(EXIT_FAILURE);
        }

    }
    while (keypress != 'q');

    free(left_pane.path);
    free(left_pane.select_path);
    free(left_pane.parent_dirname);
    free(right_pane.path);
    free(right_pane.select_path);
    free(right_pane.parent_dirname);
    free(editor);
    free(conf_path);
    free(clipboard_path);

    endwin();
    clear();
    return EXIT_SUCCESS;
}

void init_common()
{
    uid_t pw_uid = getuid();
    user_data = getpwuid(pw_uid);
    setlocale(LC_ALL, ""); // Unicode, etc
    set_editor();
    init_paths();
    make_conf_dir(conf_path);

    /* Setting a mask to block/unblock SIGWINCH (term window size changed) */
    sigemptyset (&signal_set);
    sigaddset(&signal_set, SIGWINCH);
}

void set_editor()
{
    if (getenv("EDITOR") != NULL)
    {
        int alloc_size = snprintf(NULL, 0, "%s", getenv("EDITOR"));    
        editor = (char*) malloc(alloc_size + 1);
        if (editor == NULL)
        {
            perror("editor initialization error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(editor, alloc_size + 1, "%s", getenv("EDITOR"));
    }
    else
    {
        editor = (char*) malloc(4);
        if (editor == NULL)
        {
            perror("editor initialization error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(editor, 4, "vim");
    }
}

void init_paths()
{
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));
    int alloc_size = snprintf(NULL, 0, "%s", cwd);
    left_pane.path = (char*) malloc(alloc_size + 1);
    if (left_pane.path == NULL)
    {
        perror("directory initialization error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(left_pane.path, alloc_size + 1, "%s", cwd);
    right_pane.path = (char*) malloc(alloc_size + 1);
    if (right_pane.path == NULL)
    {
        perror("directory initialization error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(right_pane.path, alloc_size + 1, "%s", cwd);

    char *ptr = strrchr(cwd, '/');
    alloc_size = snprintf(NULL, 0, "%s", ptr);
    left_pane.parent_dirname = (char*) malloc(alloc_size + 1);
    if (left_pane.parent_dirname == NULL)
    {
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(left_pane.parent_dirname, alloc_size + 1, "%s", ptr + 1);
    right_pane.parent_dirname = (char*) malloc(alloc_size + 1);
    if (right_pane.parent_dirname == NULL)
    {
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(right_pane.parent_dirname, alloc_size + 1, "%s", ptr + 1);

    /* Set the path for the configuration directory */ 
    char *conf_dir = getenv("XDG_CONFIG_HOME");
    if (conf_dir != NULL)
    {
        alloc_size = snprintf(NULL, 0, "%s/nebulafm", conf_dir);
        conf_path = malloc(alloc_size + 1);
        if (conf_path == NULL)
        {
            perror("config directory initialization error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(conf_path, alloc_size + 1, "%s/nebulafm", conf_dir);
    }
    else
    {
        alloc_size = snprintf(NULL, 0, "%s/.config/nebulafm", user_data->pw_dir);
        conf_path = malloc(alloc_size + 1);
        if (conf_path == NULL)
        {
            perror("config directory initialization error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(conf_path, alloc_size + 1, "%s/.config/nebulafm", user_data->pw_dir);
    }

    /* Set the path for the clipboard file */ 
    alloc_size = snprintf(NULL, 0, "%s/clipboard", conf_path);
    clipboard_path = malloc(alloc_size + 1);
    if (clipboard_path == NULL)
    {
        perror("clipboard initialization error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(clipboard_path, alloc_size + 1, "%s/clipboard", conf_path);
}

void make_conf_dir(char *path)
{
    struct stat st = { 0 };
    if (stat(path, &st) == -1)
        mkdir(path, 0755);
}

void init_curses()
{
    initscr();
    noecho();
    curs_set(0); // Hide the cursor
    halfdelay(REFRESH); 
    start_color();
    init_pair(1, COLOR_CYAN, 0); // Colors : directory 
    init_pair(2, COLOR_RED, 0);  // Colors : active pane
}

void get_number_of_files(pane *pane)
{
    DIR *pDir;
    struct dirent *pDirent;
    pane->dirs_num = 0;
    pane->files_num = 0;

    if ((pDir = opendir(pane->path)) != NULL)
    {
        while ((pDirent = readdir(pDir)) != NULL)
        {
            if (strcmp(pDirent->d_name, "..") == 0 || strcmp(pDirent->d_name, ".") == 0)
                continue;
            if (hide_flag == 0 && pDirent->d_name[0] == '.')
                continue;
            if (pDirent->d_type == DT_DIR)
                pane->dirs_num += 1;
            else
                pane->files_num += 1;
        }
    }
    closedir(pDir);
}

void get_files_in_array(char *directory, char *dirs[], char *files[])
{
    DIR *pDir;
    struct dirent *pDirent;
    int i = 0;
    int j = 0;

    if ((pDir = opendir(directory)) != NULL)
    {
        while ((pDirent = readdir(pDir)) != NULL)
        {
            if (strcmp(pDirent->d_name, "..") == 0 || strcmp(pDirent->d_name, ".") == 0)
                continue;
            if (hide_flag == 0 && pDirent->d_name[0] == '.')
                continue;
            if (pDirent->d_type == DT_DIR)
            {
                dirs[i] = strdup(pDirent->d_name);
                i++;
            }
            else
            {
                files[j] = strdup(pDirent->d_name);
                j++;
            }
        }
    }
    closedir(pDir);
}

int compare_elements(const void *arg1, const void *arg2)
{
    char * const *p1 = arg1;
    char * const *p2 = arg2;
    return strcasecmp(*p1, *p2);
}

void make_windows()
{
    left_pane.win  = create_window(termsize_y, termsize_x / 2 + 1, 0, 0);
    right_pane.win = create_window(termsize_y, termsize_x / 2 + 1, 0, termsize_x / 2);
    status_bar = create_window(2, termsize_x, termsize_y - 1, 0);
    keypad (left_pane.win, TRUE);
    keypad (right_pane.win, TRUE);
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL); // Unblock SIGWINCH
}

void refresh_windows()
{
    wrefresh(left_pane.win);
    wrefresh(right_pane.win);
    wrefresh(status_bar); 
}

WINDOW *create_window(int height, int width, int starty, int startx)
{
    WINDOW *win;
    win = newwin(height, width, starty, startx);
    return win;
}

void restore_indexes(char *dirs[], pane *pane)
{
    for (int i = 0; i < pane->dirs_num; i++)
    {
        if (strcmp(dirs[i], pane->parent_dirname) == 0)
        {
            if (termsize_y > pane->dirs_num)
            {
                pane->top_index = 0;
                pane->select = i + 1;
                break;
            }
            else if (i < pane->dirs_num - (termsize_y - 2))
            {
                pane->top_index = i;
                pane->select = 1;
                break;
            }
            else
            {
                pane->top_index = pane->dirs_num - (termsize_y - 2);
                pane->select = termsize_y - 1 - (pane->dirs_num - i);
                break;
            }
        }
    }
    back_flag = 0;
}

void print_files(pane *pane, char *dirs_list[], char *files_list[])
{
    /* Print directories */
    wattron(pane->win, COLOR_PAIR(1));
    wattron(pane->win, A_BOLD);
    int index = print_list(pane, dirs_list, pane->dirs_num, pane->top_index, 1);

    /* Print other types of files */
    wattroff(pane->win, COLOR_PAIR(1));
    wattroff(pane->win, A_BOLD);
    if (pane->top_index < pane->dirs_num)
        print_list(pane, files_list, pane->files_num, 0, index);
    else
        print_list(pane, files_list, pane->files_num, pane->top_index - pane->dirs_num, 1);
}

int print_list(pane *pane, char *list[], int num, int start_index, int line_pos)
{
    for (int i = start_index; i < num; i++)
    {
        if (line_pos == pane->select)
        {
            wattron(pane->win, A_STANDOUT); // Highlighting
            free(pane->select_path);
            pane->select_path = get_select_path(i, list, pane);
        }
        print_line(pane->win, line_pos, list[i]);
        wattroff(pane->win, A_STANDOUT); 
        line_pos++;
    }

    /* Erase the string if last filename is too long */
    for (int i = line_pos; i < termsize_y; i++)
    {
        wmove(pane->win, i, 0);
        wclrtoeol(pane->win);
    }
    return line_pos;
}

char *get_select_path(int index, char *list[], pane *pane)
{
    int alloc_size = snprintf(NULL, 0, "%s/%s", pane->path, list[index]);
    char *path = (char*) malloc(alloc_size + 1);
    if (path == NULL)
    {
        endwin();
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    if (pane->path[1] == '\0') // For root dir
        snprintf(path, alloc_size + 1, "%s%s", pane->path, list[index]);
    else
        snprintf(path, alloc_size + 1, "%s/%s", pane->path, list[index]);
    return path;
}

void print_line(WINDOW *window, int pos, char *str)
{
    wmove(window, pos, 0);
    wclrtoeol(window); // Pre-erase the string for too long file names
    wmove(window, pos, 2);
    wprintw(window, "%s\n", str);
}

void go_down(pane *pane)
{
    int num = pane->dirs_num + pane->files_num;
    pane->select++; 
    if (pane->select > num)
        pane->select = num;

    /* Scrolling */
    if (pane->select > termsize_y - 2)
    {
        if (num - pane->top_index > termsize_y - 2)
            pane->top_index++;
        pane->select--;
        wclear(pane->win);
    }
    
}

void go_up(pane *pane)
{
    pane->select--; 

    /* Scrolling */
    if (pane->select < 1)
    {
        if (pane->top_index > 0)
            pane->top_index--;
        pane->select = 1; 
        wclear(pane->win);
    }
}

void go_previous(pane *pane)
{
    free(pane->parent_dirname);
    char *ptr = strrchr(pane->path, '/');
    int alloc_size = snprintf(NULL, 0, "%s", ptr);
    pane->parent_dirname = (char*) malloc(alloc_size + 1);
    if (pane->parent_dirname == NULL)
    {
        endwin();
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(pane->parent_dirname, alloc_size + 1, "%s", ptr + 1);

    pane->path[strrchr(pane->path, '/') - pane->path] = '\0';
    if (pane->path[0] == '\0') // For root dir
    {
        pane->path[0] = '/';
        pane->path[1] = '\0';
    }

    back_flag = 1;
}

int is_dir(const char *path)
{
    struct stat st;
    stat(path, &st);
    return S_ISDIR(st.st_mode); // Returns non-zero if the file is a directory
}

void open_dir(pane *pane)
{
    free(pane->path);
    int alloc_size = snprintf(NULL, 0, "%s", pane->select_path);
    pane->path = (char*) malloc(alloc_size + 1);
    if (pane->path == NULL)
    {
        endwin();
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(pane->path, alloc_size + 1, "%s", pane->select_path);

    pane->select = 1;
    pane->top_index = 0;
}

void open_file(pane *pane)
{
    magic_t magic = magic_open(MAGIC_MIME_TYPE);
    magic_load(magic, NULL);
    const char *filetype = magic_file(magic, pane->select_path);
    if (filetype != NULL)
    {
        if (strstr(filetype, "text/")     != NULL ||
            strstr(filetype, "empty")     != NULL ||
            strstr(filetype, "sharedlib") != NULL ||
            strstr(filetype, "/zip")      != NULL ||
            strstr(filetype, "symlink")   != NULL ||
            strstr(filetype, "octet")     != NULL)
        {
            /* Open a file in default text editor */
            endwin();
            sigprocmask(SIG_BLOCK, &signal_set, NULL); // block SIGWINCH
            char *argv[] = { "editor", pane->select_path, (char *)0 };
            pid_t pid = fork_exec(argv[0], argv);
            int status;
            waitpid(pid, &status, 0);
        }
        else
        {
            /* Open a file in the user's preferred application */
            char *argv[] = { "xdg-open", pane->select_path, (char *)0 };
            fork_exec(argv[0], argv);
        }
    }
    else
        perror("an magic_error occurred:\n");
    magic_close(magic);
}

pid_t fork_exec(char *cmd, char **argv)
{
    pid_t pid;
    pid = fork();
    if (pid == -1)
    {
        endwin();
        perror("fork error\n");
        exit(EXIT_FAILURE);
    }
    if (pid == 0)
    {
        int fd = open("/dev/null", O_WRONLY);
        dup2(fd, STDERR_FILENO);
        close(fd); // Stderr now write to /dev/null
        execvp(cmd, argv);
        perror("EXEC:\n");
        exit(EXIT_FAILURE);
    }
    return pid;
}

void highlight_active_pane(int y, int x)
{
    wattron(status_bar, COLOR_PAIR(2));
    mvwhline(status_bar, y, x, ACS_HLINE, termsize_x / 2);
    wattroff(status_bar, COLOR_PAIR(2));
}

void print_status(pane *pane)
{
    int num = pane->dirs_num + pane->files_num;
    int file_number = 0;
    if (num != 0)
        file_number = pane->top_index + pane->select;
    if (is_dir(pane->select_path) == 0)
    {
        char buf[10];
        struct stat st;
        double size = (stat(pane->select_path, &st) == 0) ? st.st_size : 0;
        char *human_size = get_human_filesize(size, buf);
        wmove(status_bar, 1, 0);
        wprintw(status_bar, "[%02d/%02d]  %s  %s", file_number, num,
                human_size, pane->select_path);
    }
    else
    {
        wmove(status_bar, 1, 0);
        wprintw(status_bar, "[%02d/%02d]  %s", file_number, num, pane->select_path);
    }
}

void print_notification(char *str)
{
    wattron(status_bar, COLOR_PAIR(2));
    wattron(status_bar, A_BOLD);
    print_line(status_bar, 1, str);
    wattroff(status_bar, COLOR_PAIR(2));
    wattroff(status_bar, A_BOLD);
    wrefresh(status_bar);
    usleep(500000);
}

char *get_human_filesize(double size, char *buf)
{
    const char *units[] = {"B", "kB", "MB", "GB", "TB", "PB"};
    int i = 0;
    while (size > 1024)
    {
        size /= 1024;
        i++;
    }
    sprintf(buf, "%.*f %s", i, size, units[i]);
    return buf;
}

void append_clipboard(char *path)
{
    FILE *file = fopen(clipboard_path, "a+");
    if (file == NULL)
    {
        perror("clipboard access error\n");
        exit(EXIT_FAILURE);
    }
    fprintf(file, "%s\n", path);
    fclose(file);
}

void rm_files(pane *pane)
{
    char *argv[] = { "rm", "-f", "-r", pane->select_path, (char *)0 };
    pid_t pid = fork_exec(argv[0], argv);
    int status;
    waitpid(pid, &status, 0);
}

void take_action(int key, pane *pane)
{
    int confirm_key;

    switch (key)
    {
        case KEY_TAB: // Press "tab" to change the active panel
            pane_flag = (pane_flag == LEFT) ? RIGHT : LEFT;
            break;

        case KEY_UPWARD:
        case KEY_UP:
            go_up(pane);
            break;

        case KEY_DOWNWARD:
        case KEY_DOWN:
            go_down(pane);
            break;

        case KEY_BACKWARD:
        case KEY_LEFT:
            if (pane->path[1] != '\0') // If not root dir
                go_previous(pane);
            break;

        case KEY_FORWARD:
        case KEY_RIGHT:
        case KEY_RETURN:    
            if (access(pane->select_path, R_OK) == 0)
                (is_dir(pane->select_path) == 0) ? open_file(pane) : open_dir(pane);
            else
                print_notification("Permission denied");
            break;

        case KEY_CUT:
            wattron(status_bar, COLOR_PAIR(2));
            print_line(status_bar, 1, "Delete?  Press ");
            wprintw(status_bar, "%c  ", KEY_DELETE);
            wattroff(status_bar, COLOR_PAIR(2));
            confirm_key = wgetch(status_bar);
            if (confirm_key == KEY_DELETE)
            {
                if (access(pane->select_path, W_OK) == 0)
                {
                    rm_files(pane);
                    print_notification("Deleting!");
                }
                else
                    print_notification("Permission denied");
            }
            break;

        case KEY_HIDE:
            hide_flag = (hide_flag == 1) ? 0 : 1;
            left_pane.top_index = 0;
            right_pane.top_index = 0;
            left_pane.select = 1;
            right_pane.select = 1;
            break;
    }
}
