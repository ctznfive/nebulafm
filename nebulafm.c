// % gcc nebulafm.c -o nebulafm -Wall -ggdb -lmagic $(ncursesw5-config --cflags --libs)

#include <stdio.h>
#include <curses.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <magic.h>

/* globals */
int term_max_x, term_max_y;
int win_start_x, win_start_y;
char *editor = NULL; // default editor
char *current_dir_path = NULL;
char *current_select_path = NULL;
char *previous_select = NULL; // highlighting after returning to parent directory
int back_flag = 0; // changes to 1 after returning to parent directory
int pane_flag = 0; // 0 - the active panel on the left; 1 - the active panel on the right
int dirs_num;
int files_num;
int current_select = 1; // position of the selected line in the window
int top_file_index = 0; // file index to print the first line of the current window
WINDOW *left_pane;
WINDOW *right_pane;
WINDOW *status_bar;
sigset_t signal_set; // represent a signal set to specify what signals are affected

/* function prototypes */
void init(int, char *[]);
void init_curses(void);
void get_number_of_files(char *, int *, int *);
void get_files_in_array(char *, char *[], char *[]);
int compare_elements(const void *, const void *);
void make_windows(void);
void refresh_windows(void);
WINDOW *create_window(int, int, int, int);
void restore_indexes(char *[], int);
void print_files(WINDOW *, char *[], char *[], int, int);
int print_list(WINDOW *, char *[], int, int, int);
char *get_select_path(int, char *[]);
void print_line(WINDOW *, int, char *);
void go_down(void);
void go_up(void);
void go_previous(void);
int is_dir(const char *);
void open_dir();
void open_file();
pid_t fork_execlp(char *, char*);
void print_status(char *);
char *get_human_filesize(double, char *);
void take_action(int);

int main(int argc, char *argv[])
{
    int keypress;
    init(argc, argv);
    init_curses();

    do
    {
        dirs_num = 0;
        files_num = 0;
        get_number_of_files(current_dir_path, &dirs_num, &files_num);
        char *dirs_list[dirs_num];
        char *files_list[files_num];
        get_files_in_array(current_dir_path, dirs_list, files_list);

        /* sorting files in dir alphabetically */
        qsort(dirs_list, dirs_num, sizeof(char *), compare_elements);
        qsort(files_list, files_num, sizeof(char *), compare_elements);

        getmaxyx(stdscr, term_max_y, term_max_x); // get term size
        term_max_y--; // for status bar
        sigprocmask(SIG_UNBLOCK, &signal_set, NULL); // unblock SIGWINCH
        make_windows(); // make two panes + status bar

        /* update current_select and top_file_index after go_previous() */
        if (back_flag == 1)
            restore_indexes(dirs_list, dirs_num);

        /* print and refresh */
        print_files(left_pane, dirs_list, files_list, dirs_num, files_num);
        print_status(current_select_path);
        refresh_windows();

        /* keybindings */
        if (pane_flag == 0)
            keypress = wgetch(left_pane);
        if (pane_flag == 1)
            keypress = wgetch(right_pane);
        take_action(keypress);

    } while (keypress != 'q');

    free(current_dir_path);
    free(current_select_path);
    free(previous_select);
    free(editor);
    endwin();
    clear();
    return EXIT_SUCCESS;
}

void init(int argc, char *argv[])
{
    setlocale(LC_ALL, ""); // unicode, etc

    /* setting a mask to block/unblock SIGWINCH (term window size changed) */
    sigemptyset (&signal_set);
    sigaddset(&signal_set, SIGWINCH);

    /* set preferred editor */
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

    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));
    int alloc_size = snprintf(NULL, 0, "%s", cwd);
    current_dir_path = (char*) malloc(alloc_size + 1);
    if (current_dir_path == NULL)
    {
        perror("directory initialization error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(current_dir_path, alloc_size + 1, "%s", cwd);

    char *ptr = strrchr(current_dir_path, '/');
    alloc_size = snprintf(NULL, 0, "%s", ptr);
    previous_select = (char*) malloc(alloc_size + 1);
    if (previous_select == NULL)
    {
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(previous_select, alloc_size + 1, "%s", ptr + 1);
}

void init_curses()
{
    initscr();
    noecho();
    curs_set(0); // hide the cursor
    start_color();
    init_pair(1, COLOR_CYAN, 0); // directory highlighting colors
    init_pair(2, COLOR_RED, 0); // active pane highlighting colors
}

void get_number_of_files(char *directory, int *dirs, int *files)
{
    DIR *pDir;
    struct dirent *pDirent;

    if ((pDir = opendir(directory)) != NULL)
    {
        while ((pDirent = readdir(pDir)) != NULL)
        {
            if (strcmp(pDirent->d_name, "..") == 0 || strcmp(pDirent->d_name, ".") == 0)
                continue;
            if (pDirent->d_type == DT_DIR)
                *dirs += 1;
            else
                *files += 1;
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
    left_pane  = create_window(term_max_y, term_max_x / 2 + 1, 0, 0);
    right_pane = create_window(term_max_y, term_max_x / 2 + 1, 0, term_max_x / 2);
    status_bar = create_window(2, term_max_x, term_max_y - 1, 0);
}

void refresh_windows()
{
    wrefresh(left_pane);
    wrefresh(right_pane);
    wrefresh(status_bar); 
}

WINDOW *create_window(int height, int width, int starty, int startx)
{
    WINDOW *win;
    win = newwin(height, width, starty, startx);
    return win;
}

void restore_indexes(char *dirs[], int num)
{
    for (int i = 0; i < num; i++)
    {
        if (strcmp(dirs[i], previous_select) == 0)
        {
            if (term_max_y > num)
            {
                top_file_index = 0;
                current_select = i + 1;
                break;
            }
            else if (i < num - (term_max_y - 2))
            {
                top_file_index = i;
                current_select = 1;
                break;
            }
            else
            {
                top_file_index = num - (term_max_y - 2);
                current_select = term_max_y - 1 - (num - i);
                break;
            }
        }
    }
    back_flag = 0;
}

void print_files(WINDOW *win, char *dirs_list[], char *files_list[], int dirs_num, int files_num)
{
    /* print directories */
    wattron(win, COLOR_PAIR(1));
    wattron(win, A_BOLD);
    int index = print_list(win, dirs_list, dirs_num, top_file_index, 1);

    /* print other types of files */
    wattroff(win, COLOR_PAIR(1));
    wattroff(win, A_BOLD);
    if (top_file_index < dirs_num)
        print_list(win, files_list, files_num, 0, index);
    else
        print_list(win, files_list, files_num, top_file_index - dirs_num, 1);
}

int print_list(WINDOW *win, char *list[], int num, int start_index, int line_pos)
{
    for (int i = start_index; i < num; i++)
    {
        if (line_pos == current_select)
        {
            wattron(win, A_STANDOUT); // highlighting
            free(current_select_path);
            current_select_path = get_select_path(i, list); // change the selection path
        }
        print_line(win, line_pos, list[i]);
        wattroff(win, A_STANDOUT); 
        line_pos++;
    }

    /* erase the string if last filename is too long */
    for (int i = line_pos; i < term_max_y; i++)
    {
        wmove(win, i, 0);
        wclrtoeol(win);
    }
    return line_pos;
}

char *get_select_path(int index, char *list[])
{
    int alloc_size = snprintf(NULL, 0, "%s/%s", current_dir_path, list[index]);
    char *path = (char*) malloc(alloc_size + 1);
    if (path == NULL)
    {
        endwin();
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    if (current_dir_path[1] == '\0') // for root dir
        snprintf(path, alloc_size + 1, "%s%s", current_dir_path, list[index]);
    else
        snprintf(path, alloc_size + 1, "%s/%s", current_dir_path, list[index]);
    return path;
}

void print_line(WINDOW *window, int pos, char *str)
{
    wmove(window, pos, 0);
    wclrtoeol(window); // pre-erase the string for too long file names
    wmove(window, pos, 2);
    wprintw(window, "%s\n", str);
}

void go_down()
{
    int num = dirs_num + files_num;
    current_select++; 
    if (current_select > num)
        current_select = num;

    /* scrolling */
    if (current_select > term_max_y - 2)
    {
        if (num - top_file_index > term_max_y - 2)
            top_file_index++;
        current_select--;
        wclear(left_pane);
    }
    
}

void go_up()
{
    current_select--; 

    /* scrolling */
    if (current_select < 1)
    {
        if (top_file_index > 0)
            top_file_index--;
        current_select = 1; 
        wclear(left_pane);
    }
}

void go_previous()
{
    free(previous_select);
    char *ptr = strrchr(current_dir_path, '/');
    int alloc_size = snprintf(NULL, 0, "%s", ptr);
    previous_select = (char*) malloc(alloc_size + 1);
    if (previous_select == NULL)
    {
        endwin();
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(previous_select, alloc_size + 1, "%s", ptr + 1);

    current_dir_path[strrchr(current_dir_path, '/') - current_dir_path] = '\0';
    if (current_dir_path[0] == '\0') // for root dir
    {
        current_dir_path[0] = '/';
        current_dir_path[1] = '\0';
    }

    back_flag = 1;
}

int is_dir(const char *path)
{
    struct stat st;
    stat(path, &st);
    return S_ISDIR(st.st_mode); // returns non-zero if the file is a directory
}

void open_dir()
{
    free(current_dir_path);
    int alloc_size = snprintf(NULL, 0, "%s", current_select_path);
    current_dir_path = (char*) malloc(alloc_size + 1);
    if (current_dir_path == NULL)
    {
        endwin();
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(current_dir_path, alloc_size + 1, "%s", current_select_path);

    current_select = 1;
    top_file_index = 0;
}

void open_file()
{
    magic_t magic = magic_open(MAGIC_MIME_TYPE);
    magic_load(magic, NULL);
    const char *filetype = magic_file(magic, current_select_path);
    if (filetype != NULL)
    {
        if (strstr(filetype, "text/")     != NULL ||
            strstr(filetype, "empty")     != NULL ||
            strstr(filetype, "sharedlib") != NULL ||
            strstr(filetype, "symlink")   != NULL ||
            strstr(filetype, "octet")     != NULL)
        {
            /* open a file in default text editor */
            endwin();
            sigprocmask(SIG_BLOCK, &signal_set, NULL); // block SIGWINCH
            pid_t pid = fork_execlp(editor, current_select_path);
            int status;
            waitpid(pid, &status, 0);
            refresh();
            clear();
        }
        else
        {
            /* open a file in the user's preferred application */
            fork_execlp("xdg-open", current_select_path);
        }
    }
    else
        perror("an magic_error occurred:\n");
    magic_close(magic);
}

pid_t fork_execlp(char *cmd, char *path)
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
        execlp(cmd, cmd, path, (char *)0);
        perror("EXEC:\n");
        exit(EXIT_FAILURE);
    }
    return pid;
}

void print_status(char *path)
{
    /* highlight the active pane*/
    if (pane_flag == 0)
    {
        wattron(status_bar, COLOR_PAIR(2));
        mvwhline(status_bar, 0, 0, ACS_HLINE, term_max_x / 2);
        wattroff(status_bar, COLOR_PAIR(2));
    }
    else if (pane_flag == 1)
    {
        wattron(status_bar, COLOR_PAIR(2));
        mvwhline(status_bar, 0, term_max_x / 2, ACS_HLINE, term_max_x / 2);
        wattroff(status_bar, COLOR_PAIR(2));
    }

    int num = dirs_num + files_num;
    int file_number = 0;
    if (num != 0)
        file_number = top_file_index + current_select;
    if (is_dir(path) == 0)
    {
        char buf[10];
        struct stat st;
        double size = (stat(path, &st) == 0) ? st.st_size : 0;
        char *human_size = get_human_filesize(size, buf);
        wmove(status_bar, 1, 0);
        wprintw(status_bar, "[%02d/%02d]  %s  %s", file_number, num, human_size, path);
    }
    else
    {
        wmove(status_bar, 1, 0);
        wprintw(status_bar, "[%02d/%02d]  %s", file_number, num, path);
    }
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

void take_action(int key)
{
    if (key == 9) // press "tab" to change the active panel
        pane_flag = (pane_flag == 0) ? 1 : 0;
    if (key == 'j')
        go_down();
    if (key == 'k')
        go_up();
    if (key == 'h' && current_dir_path[1] != '\0') // if not root dir
        go_previous();
    if (key == 'l')
        (is_dir(current_select_path) == 0) ? open_file() : open_dir();
}
