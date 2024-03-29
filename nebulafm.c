/* ----- NebulaFM ----- */
/* See LICENSE for license details. */

#include <stdio.h>
#include <curses.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <limits.h>
#include <linux/limits.h>
#include <signal.h>
#include <magic.h>
#include <fcntl.h>
#include <pwd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "config.h"

#define KEY_CHPANE 9 // Tab key to change the pane
#define KEY_RETURN 10
#define LEFT 0
#define RIGHT 1

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
WINDOW *status_bar;
WINDOW *bookmarks;
int pane_flag = LEFT; // 0 - the active panel on the left; 1 - the active panel on the right
int termsize_x, termsize_y;
struct passwd *user_data;
char *conf_path = NULL; // The path to the configuration directory
char *clipboard_path = NULL;
char *bookmarks_path = NULL;
int clipboard_num = 0; // The number of files on the clipboard
char *editor = NULL; // Default editor
char *shell = NULL; // Default shell
int bookmarks_num = 0; // The number of bookmarks
sigset_t signal_set; // Represent a signal set to specify what signals are affected
int back_flag = 0; // Changes to 1 after returning to parent directory
int hide_flag = HIDDENVIEW;
char *search_substr = NULL; // Substring to search
int search_dir_index = -1; // The pointer to the search result in the directory list
int search_file_index = -1; // The pointer to the search result in the file list

/* Prototypes */
void init_common(int, char *[]);
void set_editor(void);
void set_shell(void);
void init_paths(int, char *[]);
void init_current_dir(char *);
void init_parent_dir(char *);
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
int print_list(pane *, char *[], int, int, int, int);
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
int exist_clipboard(char *);
void append_clipboard(char *);
void remove_clipboard(char *);
void remove_files(pane *);
int rm_file(char *);
void yank_files(pane *);
int cp_file(char *, char *);
void move_files(pane *);
int mv_file(char *, char *);
void rename_file(pane *);
int is_empty_str(const char *);
void open_shell(pane *);
void select_all(pane *);
void add_list_clipboard(char *[], char *, int);
void make_new(pane *, char *);
void preview_select(pane *);
int get_bookmarks_num(void);
int exist_bookmark(char);
void add_bookmark(char *, char);
void print_bookmarks(void);
void open_bookmark(char, pane *);
void remove_bookmark(char);
int search_dir(pane *, char *, int);
int search_file(pane *, char *, int);
int search_list(char *, char *[], int, int);
void free_array(char *[], int);
void take_action(int, pane *);

int main(int argc, char *argv[])
{
    int keypress;

    /* Initialization */
    init_common(argc, argv);
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
                endwin();
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
            {
                free_array(dirs_list_l, left_pane.dirs_num);
                free_array(files_list_l, left_pane.files_num);
                free_array(dirs_list_r, right_pane.dirs_num);
                free_array(files_list_r, right_pane.files_num);
                continue;
            }
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
            {
                free_array(dirs_list_l, left_pane.dirs_num);
                free_array(files_list_l, left_pane.files_num);
                free_array(dirs_list_r, right_pane.dirs_num);
                free_array(files_list_r, right_pane.files_num);
                continue;
            }
            take_action(keypress, &right_pane);
        }
        else
        {
            endwin();
            perror("pane_flag initialization error\n");
            exit(EXIT_FAILURE);
        }

        free_array(dirs_list_l, left_pane.dirs_num);
        free_array(files_list_l, left_pane.files_num);
        free_array(dirs_list_r, right_pane.dirs_num);
        free_array(files_list_r, right_pane.files_num);
    }
    while (keypress != 'q');

    /* Emptying the clipboard */
    remove(clipboard_path);

    free(left_pane.path);
    free(left_pane.select_path);
    free(left_pane.parent_dirname);
    free(right_pane.path);
    free(right_pane.select_path);
    free(right_pane.parent_dirname);
    free(editor);
    free(shell);
    free(conf_path);
    free(clipboard_path);
    free(bookmarks_path);
    free(search_substr);

    endwin();
    clear();
    return EXIT_SUCCESS;
}

void init_common(int argc, char *argv[])
{
    uid_t pw_uid = getuid();
    user_data = getpwuid(pw_uid);
    setlocale(LC_ALL, ""); // Unicode, etc
    set_editor();
    set_shell();
    init_paths(argc, argv);
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
        editor = malloc(alloc_size + 1);
        if (editor == NULL)
        {
            perror("editor initialization error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(editor, alloc_size + 1, "%s", getenv("EDITOR"));
    }
    else
    {
        editor = malloc(4);
        if (editor == NULL)
        {
            perror("editor initialization error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(editor, 4, "vim");
    }
}

void set_shell()
{
    if (getenv("SHELL") != NULL)
    {
        int alloc_size = snprintf(NULL, 0, "%s", getenv("SHELL"));
        shell = malloc(alloc_size + 1);
        if (shell == NULL)
        {
            perror("shell initialization error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(shell, alloc_size + 1, "%s", getenv("SHELL"));
    }
    else
    {
        shell = malloc(10);
        if (shell == NULL)
        {
            perror("shell initialization error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(shell, 10, "/bin/bash");
    }
}

void init_paths(int argc, char *argv[])
{
    char cwd[PATH_MAX];
    int alloc_size;

    if (argc == 1)
    {
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            perror("current directory initialization error\n");
            exit(EXIT_FAILURE);
        }
        init_current_dir(cwd);
        init_parent_dir(cwd);
    }

    /* Argument as an absolute path */
    if (argc == 2 && argv[1][0] == '/')
    {
        if(strlen(argv[1]) > 1 && argv[1][strlen(argv[1]) - 1] == '/')
            argv[1][strlen(argv[1]) - 1] = '\0';
        if (access(argv[1], R_OK) == 0)
        {
            init_current_dir(argv[1]);
            init_parent_dir(argv[1]);
        }
        else
        {
            printf("The directory does not exist.\n");
            exit(EXIT_FAILURE);
        }
    }

    /* Argument as a relative path */
    if (argc == 2 && argv[1][0] != '/')
    {
        if(strlen(argv[1]) > 1 && argv[1][strlen(argv[1]) - 1] == '/')
            argv[1][strlen(argv[1]) - 1] = '\0';
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            perror("current directory initialization error\n");
            exit(EXIT_FAILURE);
        }
        int alloc_size_wd = snprintf(NULL, 0, "%s", cwd);
        alloc_size = snprintf(NULL, 0, "%s", argv[1]);
        left_pane.path = malloc(alloc_size_wd + alloc_size + 2);
        if (left_pane.path == NULL)
        {
            perror("directory initialization error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(left_pane.path, alloc_size_wd + alloc_size + 2, "%s/%s", cwd, argv[1]);
        right_pane.path = malloc(alloc_size_wd + alloc_size + 2);
        if (right_pane.path == NULL)
        {
            perror("directory initialization error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(right_pane.path, alloc_size_wd + alloc_size + 2, "%s/%s", cwd, argv[1]);

        if (access(left_pane.path, R_OK) != 0)
        {
            printf("The directory does not exist.\n");
            exit(EXIT_FAILURE);
        }
        init_parent_dir(left_pane.path);
    }

    if (argc > 2)
    {
        printf("Incorrect arguments. Use `man nebulafm` for help.\n");
        exit(EXIT_FAILURE);
    }

    /* If the initialization directory is empty */
    alloc_size = snprintf(NULL, 0, "%s", left_pane.path);
    left_pane.select_path = malloc(alloc_size + 1);
    if (left_pane.select_path == NULL)
    {
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(left_pane.select_path, alloc_size + 1, "%s", left_pane.path);
    right_pane.select_path = malloc(alloc_size + 1);
    if (right_pane.select_path == NULL)
    {
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(right_pane.select_path, alloc_size + 1, "%s", left_pane.path);

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

    /* Set the path for the bookmarks file */
    alloc_size = snprintf(NULL, 0, "%s/bookmarks", conf_path);
    bookmarks_path = malloc(alloc_size + 1);
    if (bookmarks_path == NULL)
    {
        perror("bookmarks initialization error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(bookmarks_path, alloc_size + 1, "%s/bookmarks", conf_path);
}

void init_current_dir(char *path)
{
    int alloc_size = snprintf(NULL, 0, "%s", path);
    left_pane.path = malloc(alloc_size + 1);
    if (left_pane.path == NULL)
    {
        perror("directory initialization error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(left_pane.path, alloc_size + 1, "%s", path);
    right_pane.path = malloc(alloc_size + 1);
    if (right_pane.path == NULL)
    {
        perror("directory initialization error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(right_pane.path, alloc_size + 1, "%s", path);
}

void init_parent_dir(char *path)
{
    char *ptr = strrchr(path, '/');
    int alloc_size = snprintf(NULL, 0, "%s", ptr);
    left_pane.parent_dirname = malloc(alloc_size + 1);
    if (left_pane.parent_dirname == NULL)
    {
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(left_pane.parent_dirname, alloc_size + 1, "%s", ptr + 1);
    right_pane.parent_dirname = malloc(alloc_size + 1);
    if (right_pane.parent_dirname == NULL)
    {
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(right_pane.parent_dirname, alloc_size + 1, "%s", ptr + 1);
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
    init_pair(2, COLOR_RED, 0);  // Colors : active pane; files from clipboard
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
    wattron(pane->win, A_BOLD);
    int index = print_list(pane, dirs_list, pane->dirs_num, pane->top_index, 1, 1);
    wattroff(pane->win, A_BOLD);

    /* Print other types of files */
    if (pane->top_index < pane->dirs_num)
        print_list(pane, files_list, pane->files_num, 0, index, 0);
    else
        print_list(pane, files_list, pane->files_num, pane->top_index - pane->dirs_num, 1, 0);
}

int print_list(pane *pane, char *list[], int num, int start_index, int line_pos, int color)
{
    for (int i = start_index; i < num; i++)
    {
        if (line_pos == pane->select)
        {
            wattron(pane->win, A_STANDOUT); // Highlighting
            free(pane->select_path);
            pane->select_path = get_select_path(i, list, pane);
        }

        char *print_path = NULL;
        int alloc_size = snprintf(NULL, 0, "%s/%s", pane->path, list[i]);
        print_path = malloc(alloc_size + 1);
        if (print_path == NULL)
        {
            endwin();
            perror("memory allocation error\n");
            exit(EXIT_FAILURE);
        }
        if (pane->path[1] == '\0') // For root dir
            snprintf(print_path, alloc_size + 1, "%s%s", pane->path, list[i]);
        else
            snprintf(print_path, alloc_size + 1, "%s/%s", pane->path, list[i]);

        /* selecting files on the clipboard */
        if (exist_clipboard(print_path) == 0)
        {
            wattron(pane->win, COLOR_PAIR(2));
            print_line(pane->win, line_pos, list[i]);
            wmove(pane->win, line_pos, 0);
            wprintw(pane->win, ">");
            wattroff(pane->win, COLOR_PAIR(2));
        }
        else
        {
            wattron(pane->win, COLOR_PAIR(color));
            print_line(pane->win, line_pos, list[i]);
            wattroff(pane->win, COLOR_PAIR(color));
        }

        wattroff(pane->win, A_STANDOUT);
        free(print_path);
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
    char *path = malloc(alloc_size + 1);
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
    pane->parent_dirname = malloc(alloc_size + 1);
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
    pane->path = malloc(alloc_size + 1);
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
            char *argv[] = { editor, pane->select_path, (char *)0 };
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
        wprintw(status_bar, "[%02d/%02d]  [*%d]  %s  %s", file_number, num, clipboard_num,
                human_size, pane->select_path);
    }
    else
    {
        wmove(status_bar, 1, 0);
        wprintw(status_bar, "[%02d/%02d]  [*%d]  %s", file_number, num, clipboard_num,
                pane->select_path);
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
    usleep(1000000);
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

int exist_clipboard(char *path)
{
    FILE *file = fopen(clipboard_path, "r");
    if (file != NULL)
    {
        char buf[PATH_MAX];
        while(fgets(buf, PATH_MAX, file))
        {
            buf[strcspn(buf, "\r\n")] = 0;
            if (strcmp(path, buf) == 0)
            {
                fclose(file);
                return 0;
            }
        }
        fclose(file);
    }
    return -1;
}

void append_clipboard(char *path)
{
    FILE *file = fopen(clipboard_path, "a+");
    if (file == NULL)
    {
        endwin();
        perror("clipboard access error\n");
        exit(EXIT_FAILURE);
    }
    fprintf(file, "%s\n", path);
    clipboard_num++;
    fclose(file);
}

void remove_clipboard(char *path)
{
    FILE *file = fopen(clipboard_path, "r");
    if (file != NULL)
    {
        char *tmp_clipboard_path = NULL;
        int alloc_size = snprintf(NULL, 0, "%s/.clipboard", conf_path);
        tmp_clipboard_path = malloc(alloc_size + 1);
        if (tmp_clipboard_path == NULL)
        {
            endwin();
            perror("temp clipboard initialization error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(tmp_clipboard_path, alloc_size + 1, "%s/.clipboard", conf_path);

        char buf[PATH_MAX];
        FILE *tmp_file = fopen(tmp_clipboard_path, "a+");
        if (tmp_file == NULL)
        {
            endwin();
            perror("temp clipboard access error\n");
            exit(EXIT_FAILURE);
        }
        while (fgets(buf, PATH_MAX, file))
        {
            buf[strcspn(buf, "\r\n")] = 0;
            if (strcmp(path, buf) != 0)
                fprintf(tmp_file, "%s\n", buf);
            else
                clipboard_num--;
        }

        fclose(tmp_file);
        fclose(file);
        char *argv[] = { "mv", tmp_clipboard_path, clipboard_path, (char *)0 };
        pid_t pid = fork_exec(argv[0], argv);
        int status;
        waitpid(pid, &status, 0);
        free(tmp_clipboard_path);
    }
}

void remove_files(pane *pane)
{
    if (clipboard_num != 0)
    {
        FILE *file = fopen(clipboard_path, "r");
        char buf[PATH_MAX];
        int del_num = 0;
        while(fgets(buf, PATH_MAX, file))
        {
            buf[strcspn(buf, "\r\n")] = 0;
            if (rm_file(buf) == 0)
                del_num++;
        }
        if (del_num != clipboard_num)
            print_notification("Some files aren't deleted. Permission denied!");
        fclose(file);
        remove(clipboard_path);
        clipboard_num = 0;
        pane->select = 1;
    }
    else
    {
        if (rm_file(pane->select_path) == 0)
            pane->select = 1;
        else
            print_notification("Permission denied!");
    }
}

int rm_file(char *path)
{
    if (access(path, W_OK) == 0)
    {
        char *argv[] = { "rm", "-f", "-r", path, (char *)0 };
        pid_t pid = fork_exec(argv[0], argv);
        int status;
        waitpid(pid, &status, 0);
        return 0;
    }
    return -1;
}

void yank_files(pane *pane)
{
    if (clipboard_num != 0)
    {
        FILE *file = fopen(clipboard_path, "r");
        char buf[PATH_MAX];
        int cp_num = 0;
        while(fgets(buf, PATH_MAX, file))
        {
            buf[strcspn(buf, "\r\n")] = 0;
            if (cp_file(buf, pane->path) == 0)
                cp_num++;
        }
        if (cp_num != clipboard_num)
            print_notification("Some files aren't copied. Permission denied!");
        fclose(file);
        remove(clipboard_path);
        clipboard_num = 0;
        pane->select = 1;
    }
    else
    {
        /* Make a copy of the selected file in the current directory. */
        if (access(pane->path, W_OK) == 0)
        {
            int alloc_size = snprintf(NULL, 0, "%s~", pane->select_path);
            char *cpy_path = malloc(alloc_size + 1);
            if (cpy_path == NULL)
            {
                endwin();
                perror("memory allocation error\n");
                exit(EXIT_FAILURE);
            }
            snprintf(cpy_path, alloc_size + 1, "%s~", pane->select_path);
            char *argv[] = { "cp", "-b", "-r", pane->select_path, cpy_path, (char *)0 };
            pid_t pid = fork_exec(argv[0], argv);
            int status;
            waitpid(pid, &status, 0);
            free(cpy_path);
        }
        else
            print_notification("Permission denied!");
    }
}

int cp_file(char *path, char *dir)
{
    if (access(dir, W_OK) == 0)
    {
        char *argv[] = { "cp", "-b", "-r", path, dir, (char *)0 };
        pid_t pid = fork_exec(argv[0], argv);
        int status;
        waitpid(pid, &status, 0);
        return 0;
    }
    return -1;
}

void move_files(pane *pane)
{
    if (clipboard_num != 0)
    {
        FILE *file = fopen(clipboard_path, "r");
        char buf[PATH_MAX];
        int mv_num = 0;
        while(fgets(buf, PATH_MAX, file))
        {
            buf[strcspn(buf, "\r\n")] = 0;
            if (mv_file(buf, pane->path) == 0)
                mv_num++;
        }
        if (mv_num != clipboard_num)
            print_notification("Some files aren't moved. Permission denied!");
        fclose(file);
        remove(clipboard_path);
        clipboard_num = 0;
        pane->select = 1;
    }
    else
        print_notification("The clipboard is empty. Please select the files.");
}

int mv_file(char *path, char *dir)
{
    if (access(dir, W_OK) == 0 && access(path, W_OK) == 0)
    {
        char *argv[] = { "mv", "-b", path, dir, (char *)0 };
        pid_t pid = fork_exec(argv[0], argv);
        int status;
        waitpid(pid, &status, 0);
        return 0;
    }
    return -1;
}

void rename_file(pane *pane)
{
    char *new_name = malloc(NAME_MAX);
    if (new_name == NULL)
    {
        endwin();
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    echo();
    curs_set(1);
    wgetnstr(status_bar, new_name, NAME_MAX);
    noecho();
    curs_set(0);
    if (strlen(new_name) != 0 && is_empty_str(new_name) != 0)
    { if (access(pane->select_path, W_OK) == 0)
        {
            int alloc_size = snprintf(NULL, 0, "%s/%s", pane->path, new_name);
            char *new_path = malloc(alloc_size + 1);
            if (new_path == NULL)
            {
                endwin();
                perror("memory allocation error\n");
                exit(EXIT_FAILURE);
            }
            snprintf(new_path, alloc_size + 1, "%s/%s", pane->path, new_name);
            char *argv[] = { "mv", "-b", pane->select_path, new_path, (char *)0 };
            pid_t pid = fork_exec(argv[0], argv);
            int status;
            waitpid(pid, &status, 0);
            free(new_path);
            pane->select = 1;
        }
        else
            print_notification("Permission denied!");
    }
    else
        print_notification("Please enter the correct name!");
    free(new_name);
}

int is_empty_str(const char *str)
{
    while (*str != '\0')
    {
        if (isspace((unsigned char) *str) == 0)
            return -1;
        str++;
    }
    return 0;
}

void open_shell(pane *pane)
{
    endwin();
    sigprocmask(SIG_BLOCK, &signal_set, NULL); // block SIGWINCH
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
        chdir(pane->path);
        char *cmd[] = { shell, (char *)0 };
        execvp(cmd[0], cmd);
        perror("EXEC:\n");
        exit(EXIT_FAILURE);
    }
    int status;
    waitpid(pid, &status, 0);

    /* refresh */
    left_pane.select = 1;
    right_pane.select = 1;
    left_pane.top_index = 0;
    right_pane.top_index = 0;
    refresh();
}

void select_all(pane *pane)
{
    remove(clipboard_path);
    clipboard_num = 0;
    char *dirs_list[pane->dirs_num];
    char *files_list[pane->files_num];
    get_files_in_array(pane->path, dirs_list, files_list);
    add_list_clipboard(dirs_list, pane->path, pane->dirs_num);
    add_list_clipboard(files_list, pane->path, pane->files_num);
    free_array(dirs_list, pane->dirs_num);
    free_array(files_list, pane->files_num);
}

void add_list_clipboard(char *list[], char *path, int num)
{
    int alloc_size;
    char *filepath = NULL;
    for (int index = 0; index < num; index++)
    {
        alloc_size = snprintf(NULL, 0, "%s/%s", path, list[index]);
        filepath = malloc(alloc_size + 1);
        if (filepath == NULL)
        {
            endwin();
            perror("memory allocation error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(filepath, alloc_size + 1, "%s/%s", path, list[index]);
        append_clipboard(filepath);
        free(filepath);
    }
}

void make_new(pane *pane, char *cmd)
{
    char *new = malloc(NAME_MAX);
    if (new == NULL)
    {
        endwin();
        perror("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    echo();
    curs_set(1);
    wgetnstr(status_bar, new, NAME_MAX);
    noecho();
    curs_set(0);
    if (strlen(new) != 0 && is_empty_str(new) != 0)
    {
        if (access(pane->path, W_OK) == 0)
        {
            int alloc_size = snprintf(NULL, 0, "%s/%s", pane->path, new);
            char *new_path = malloc(alloc_size + 1);
            if (new_path == NULL)
            {
                endwin();
                perror("memory allocation error\n");
                exit(EXIT_FAILURE);
            }
            snprintf(new_path, alloc_size + 1, "%s/%s", pane->path, new);
            if (access(new_path, R_OK) != 0)
            {
                char *argv[] = { cmd, new_path, (char *)0 };
                pid_t pid = fork_exec(argv[0], argv);
                int status;
                waitpid(pid, &status, 0);
                free(new_path);
                pane->select = 1;
            }
            else
                print_notification("File/directory exists!");
        }
        else
            print_notification("Permission denied!");
    }
    else
        print_notification("Please enter the correct name!");
    free(new);
}

void preview_select(pane *pane)
{
    if (access(pane->select_path, R_OK) == 0)
    {
        endwin();
        sigprocmask(SIG_BLOCK, &signal_set, NULL); // block SIGWINCH
        if (is_dir(pane->select_path) == 0)
        {
            char *argv[] = { "less", pane->select_path, (char *)0 };
            pid_t pid = fork_exec(argv[0], argv);
            int status;
            waitpid(pid, &status, 0);
        }
        else
        {
            /* ls -lAh | less */
            int pipefd[2];
            if (pipe(pipefd) == -1)
            {
                endwin();
                perror("pipe error\n");
                exit(EXIT_FAILURE);
            }

            pid_t pid1 = fork();
            if (pid1 == -1)
            {
                endwin();
                perror("fork error\n");
                exit(EXIT_FAILURE);
            }
            if (pid1 == 0)
            {
                int fd = open("/dev/null", O_WRONLY);
                dup2(fd, STDERR_FILENO);
                close(fd);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
                char *argv[] = { "ls", "-l", "-A", "-h", pane->select_path, (char *)0 };
                execvp(argv[0], argv);
                perror("EXEC:\n");
                exit(EXIT_FAILURE);
            }

            pid_t pid2 = fork();
            if (pid2 == -1)
            {
                endwin();
                perror("fork error\n");
                exit(EXIT_FAILURE);
            }
            if (pid2 == 0)
            {
                int fd = open("/dev/null", O_WRONLY);
                dup2(fd, STDERR_FILENO);
                close(fd);
                dup2(pipefd[0], STDIN_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
                char *argv[] = { "less", (char *)0 };
                execvp(argv[0], argv);
                perror("EXEC:\n");
                exit(EXIT_FAILURE);
            }

            close(pipefd[0]);
            close(pipefd[1]);
            int status;
            waitpid(pid1, &status, 0);
            waitpid(pid2, &status, 0);
        }
    }
    else
        print_notification("Permission denied!");
}

int get_bookmarks_num()
{
    int num = 0;
    FILE *file = fopen(bookmarks_path, "r");
    if (file != NULL)
    {
        char buf[PATH_MAX];
        while(fgets(buf, PATH_MAX, file))
            num++;
        fclose(file);
        return num;
    }
    return -1;
}

int exist_bookmark(char key)
{
    FILE *file = fopen(bookmarks_path, "r");
    if (file != NULL)
    {
        char buf[PATH_MAX];
        while(fgets(buf, PATH_MAX, file))
        {
            if (buf[0] == key)
            {
                fclose(file);
                return 0;
            }
        }
        fclose(file);
    }
    return -1;
}

void add_bookmark(char *path, char key)
{
    FILE *file = fopen(bookmarks_path, "a+");
    if (file == NULL)
    {
        endwin();
        perror("bookmarks access error\n");
        exit(EXIT_FAILURE);
    }
    fprintf(file, "%c:%s\n", key, path);
    fclose(file);
}

void print_bookmarks()
{
    int i = 0;
    char buf[PATH_MAX];
    FILE *file = fopen(bookmarks_path, "r");
    if (file == NULL)
    {
        endwin();
        perror("bookmarks access error\n");
        exit(EXIT_FAILURE);
    }
    bookmarks = create_window(bookmarks_num + 3, termsize_x, termsize_y - bookmarks_num - 4, 0);
    wattron(bookmarks, COLOR_PAIR(2));
    wmove(bookmarks, 1, 1);
    wprintw(bookmarks, "mark\tpath\n");
    wattroff(bookmarks, COLOR_PAIR(2));
    while(fgets(buf, PATH_MAX, file))
    {
        wmove(bookmarks, i + 2, 1);
        wprintw(bookmarks, " %c", buf[0]);
        wprintw(bookmarks, "\t%s", &buf[2]);
        i++;
    }
    fclose(file);
    box(bookmarks, 0, 0);
    wrefresh(bookmarks);
}

void open_bookmark(char key, pane *pane)
{
    FILE *file = fopen(bookmarks_path, "r");
    if (file == NULL)
    {
        endwin();
        perror("bookmarks access error\n");
        exit(EXIT_FAILURE);
    }
    char buf[PATH_MAX];
    while(fgets(buf, PATH_MAX, file))
    {
        if (buf[0] == key)
        {
            buf[strcspn(buf, "\r\n")] = 0;
            int alloc_size = snprintf(NULL, 0, "%s", buf);
            char *new_path = malloc(alloc_size + 1);
            if (new_path == NULL)
            {
                endwin();
                perror("memory allocation error\n");
                exit(EXIT_FAILURE);
            }
            strncpy(new_path, buf + 2, strlen(buf) - 1);

            if (access(new_path, R_OK) == 0)
            {
                free(pane->path);
                alloc_size = snprintf(NULL, 0, "%s", new_path);
                pane->path = malloc(alloc_size + 1);
                if (pane->path == NULL)
                {
                    endwin();
                    perror("memory allocation error\n");
                    exit(EXIT_FAILURE);
                }
                snprintf(pane->path, alloc_size + 1, "%s", new_path);

                pane->select = 1;
                pane->top_index = 0;
                /* If the directory is empty */
                alloc_size = snprintf(NULL, 0, "%s", pane->path);
                free(pane->select_path);
                pane->select_path = malloc(alloc_size + 1);
                if (pane->select_path == NULL)
                {
                    perror("memory allocation error\n");
                    exit(EXIT_FAILURE);
                }
                snprintf(pane->select_path, alloc_size + 1, "%s", pane->path);
            }
            else
                print_notification("The directory doesn't exist.");
            free(new_path);
            break;
        }
    }
    fclose(file);
}

void remove_bookmark(char key)
{
    FILE *file = fopen(bookmarks_path, "r");
    if (file != NULL)
    {
        char *tmp_bookmarks_path = NULL;
        int alloc_size = snprintf(NULL, 0, "%s/.bookmarks", conf_path);
        tmp_bookmarks_path = malloc(alloc_size + 1);
        if (tmp_bookmarks_path == NULL)
        {
            endwin();
            perror("temp bookmarks initialization error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(tmp_bookmarks_path, alloc_size + 1, "%s/.bookmarks", conf_path);

        char buf[PATH_MAX];
        FILE *tmp_file = fopen(tmp_bookmarks_path, "a+");
        if (tmp_file == NULL)
        {
            endwin();
            perror("temp bookmarks access error\n");
            exit(EXIT_FAILURE);
        }
        while (fgets(buf, PATH_MAX, file))
        {
            buf[strcspn(buf, "\r\n")] = 0;
            if (buf[0] != key)
                fprintf(tmp_file, "%s\n", buf);
            else
                print_notification("Bookmark deleted.");
        }

        fclose(tmp_file);
        fclose(file);
        char *argv[] = { "mv", tmp_bookmarks_path, bookmarks_path, (char *)0 };
        pid_t pid = fork_exec(argv[0], argv);
        int status;
        waitpid(pid, &status, 0);
        free(tmp_bookmarks_path);
    }
}

int search_dir(pane *pane, char *substr, int start)
{
    char *dirs_list[pane->dirs_num];
    char *files_list[pane->files_num];
    get_files_in_array(pane->path, dirs_list, files_list);
    qsort(dirs_list, pane->dirs_num, sizeof(char *), compare_elements);
    int dir_found = search_list(substr, dirs_list, pane->dirs_num, start);
    if (dir_found != -1)
    {
        if (termsize_y > pane->dirs_num)
        {
            pane->top_index = 0;
            pane->select = dir_found + 1;
        }
        else if (dir_found < pane->dirs_num - (termsize_y - 2))
        {
            pane->top_index = dir_found;
            pane->select = 1;
        }
        else
        {
            pane->top_index = pane->dirs_num - (termsize_y - 2);
            pane->select = termsize_y - 1 - (pane->dirs_num - dir_found);
        }
    }
    free_array(dirs_list, pane->dirs_num);
    free_array(files_list, pane->files_num);
    return dir_found;
}

int search_file(pane *pane, char *substr, int start)
{
    char *dirs_list[pane->dirs_num];
    char *files_list[pane->files_num];
    get_files_in_array(pane->path, dirs_list, files_list);
    qsort(files_list, pane->files_num, sizeof(char *), compare_elements);
    int file_found = search_list(substr, files_list, pane->files_num, start);
    if (file_found != -1)
    {
        if (termsize_y > pane->dirs_num + pane->files_num)
        {
            pane->top_index = 0;
            pane->select = pane->dirs_num + file_found + 1;
        }
        else if (file_found < pane->files_num - (termsize_y - 2))
        {
            pane->top_index = pane->dirs_num + file_found;
            pane->select = 1;
        }
        else
        {
            pane->top_index = pane->dirs_num + pane->files_num - (termsize_y - 2);
            pane->select = termsize_y - 1 - (pane->files_num - file_found);
        }
    }
    free_array(dirs_list, pane->dirs_num);
    free_array(files_list, pane->files_num);
    return file_found;
}

int search_list(char *substr, char *list[], int num, int start)
{
    for (int index = start; index < num; index++)
    {
        char *found = strcasestr(list[index], substr);
        if (found != NULL)
            return index;
    }
    return -1;
}

void free_array(char *array[], int num)
{
    for (int i = 0; i < num; i++)
        free(array[i]);
}

void take_action(int key, pane *pane)
{
    int confirm_key;

    switch (key)
    {
        case KEY_CHPANE:
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

        case KEY_HIDE:
            hide_flag = (hide_flag == 1) ? 0 : 1;
            left_pane.top_index = 0;
            right_pane.top_index = 0;
            left_pane.select = 1;
            right_pane.select = 1;
            break;

        case KEY_MULT:
            if (exist_clipboard(pane->select_path) != 0)
                append_clipboard(pane->select_path);
            else
                remove_clipboard(pane->select_path);
            go_down(pane);
            break;

        case KEY_DEL:
            wattron(status_bar, COLOR_PAIR(2));
            print_line(status_bar, 1, "Delete?  Press ");
            wprintw(status_bar, "%c  ", KEY_DEL_CONF);
            wattroff(status_bar, COLOR_PAIR(2));
            confirm_key = wgetch(status_bar);
            if (confirm_key == KEY_DEL_CONF)
                remove_files(pane);
            break;

        case KEY_CPY:
            wattron(status_bar, COLOR_PAIR(2));
            print_line(status_bar, 1, "Yank?  Press ");
            wprintw(status_bar, "%c  ", KEY_CPY);
            wattroff(status_bar, COLOR_PAIR(2));
            confirm_key = wgetch(status_bar);
            if (confirm_key == KEY_CPY)
                yank_files(pane);
            break;

        case KEY_MV:
            wattron(status_bar, COLOR_PAIR(2));
            print_line(status_bar, 1, "Move?  Press ");
            wprintw(status_bar, "%c  ", KEY_MV);
            wattroff(status_bar, COLOR_PAIR(2));
            confirm_key = wgetch(status_bar);
            if (confirm_key == KEY_MV)
                move_files(pane);
            break;

        case KEY_RNM:
            wattron(status_bar, COLOR_PAIR(2));
            print_line(status_bar, 1, "Rename: ");
            wattroff(status_bar, COLOR_PAIR(2));
            rename_file(pane);
            break;

        case KEY_TOP:
            confirm_key = wgetch(status_bar);
            if (confirm_key == KEY_TOP)
            {
                pane->top_index = 0;
                pane->select = 1;
            }
            break;

        case KEY_BTM:
            if (pane->dirs_num + pane->files_num > termsize_y - 2)
            {
                pane->top_index = pane->dirs_num + pane->files_num - termsize_y + 2;
                pane->select = termsize_y - 2;
            }
            else
                pane->select = pane->dirs_num + pane->files_num;
            break;

        case KEY_HIGH:
            pane->select = 1;
            break;

        case KEY_MIDDLE:
            if (pane->dirs_num + pane->files_num > (termsize_y - 2) / 2)
                pane->select = (termsize_y - 2) / 2 + 1;
            else
                pane->select = pane->dirs_num + pane->files_num;
            break;

        case KEY_LAST:
            if (pane->dirs_num + pane->files_num > termsize_y - 2)
                pane->select = termsize_y - 2;
            else
                pane->select = pane->dirs_num + pane->files_num;
            break;

        case KEY_PAGEDOWN:
            if (pane->dirs_num + pane->files_num > termsize_y - 2)
            {
                if (pane->dirs_num + pane->files_num - pane->top_index < 2 * (termsize_y - 2))
                    pane->top_index = pane->dirs_num + pane->files_num - termsize_y + 2;
                else
                {
                    pane->top_index += termsize_y - 2;
                    pane->select = 1;
                }
            }
            break;

        case KEY_PAGEUP:
            if (pane->dirs_num + pane->files_num > termsize_y - 2)
            {
                if (pane->top_index < termsize_y - 2)
                    pane->top_index = 0;
                else
                {
                    pane->top_index -= termsize_y - 2;
                    pane->select = 1;
                }
            }
            break;

        case KEY_SHELL:
            open_shell(pane);
            break;

        case KEY_SELALL:
            select_all(pane);
            break;

        case KEY_SELEMPTY:
            remove(clipboard_path);
            clipboard_num = 0;
            break;

        case KEY_MAKEDIR:
            wattron(status_bar, COLOR_PAIR(2));
            print_line(status_bar, 1, "mkdir: ");
            wattroff(status_bar, COLOR_PAIR(2));
            make_new(pane, "mkdir");
            break;

        case KEY_MAKEFILE:
            wattron(status_bar, COLOR_PAIR(2));
            print_line(status_bar, 1, "touch: ");
            wattroff(status_bar, COLOR_PAIR(2));
            make_new(pane, "touch");
            break;

        case KEY_VIEW:
            preview_select(pane);
            break;

        case KEY_ADDBKMR:
            wattron(status_bar, COLOR_PAIR(2));
            print_line(status_bar, 1, "Enter the key to add a new bookmark... ");
            wattroff(status_bar, COLOR_PAIR(2));
            confirm_key = wgetch(status_bar);
            if (confirm_key != ERR && isalnum(confirm_key) != 0)
            {
                if (exist_bookmark(confirm_key) != 0)
                    add_bookmark(pane->path, confirm_key);
                else
                    print_notification("The bookmark button already exists.");
            }
            else
                print_notification("A letter or number is expected.");
            break;

        case KEY_OPENBKMR:
            bookmarks_num = get_bookmarks_num();
            if (bookmarks_num == -1 || bookmarks_num == 0)
                print_notification("No bookmarks available.");
            else
            {
                print_bookmarks();
                confirm_key = wgetch(bookmarks);
                open_bookmark(confirm_key, pane);
                delwin(bookmarks);
            }
            break;

        case KEY_DELBKMR:
            wattron(status_bar, COLOR_PAIR(2));
            print_line(status_bar, 1, "Which bookmark to delete? ");
            wattroff(status_bar, COLOR_PAIR(2));
            wrefresh(status_bar);
            bookmarks_num = get_bookmarks_num();
            if (bookmarks_num == -1 || bookmarks_num == 0)
                print_notification("No bookmarks available.");
            else
            {
                print_bookmarks();
                confirm_key = wgetch(bookmarks);
                remove_bookmark(confirm_key);
                delwin(bookmarks);
            }
            break;

        case KEY_SEARCH:
            wattron(status_bar, COLOR_PAIR(2));
            print_line(status_bar, 1, "Search: ");
            wattroff(status_bar, COLOR_PAIR(2));
            free(search_substr);
            search_substr = malloc(NAME_MAX);
            if (search_substr == NULL)
            {
                endwin();
                perror("memory allocation error\n");
                exit(EXIT_FAILURE);
            }
            echo();
            curs_set(1);
            wgetnstr(status_bar, search_substr, NAME_MAX);
            noecho();
            curs_set(0);
            if (strlen(search_substr) != 0 && is_empty_str(search_substr) != 0)
            {
                search_dir_index = search_dir(pane, search_substr, 0);
                search_file_index = -1;
                if (search_dir_index == -1)
                    search_file_index = search_file(pane, search_substr, 0);
            }
            else
                print_notification("Please enter the correct name!");
            break;

        case KEY_SEARCHNEXT:
            if (search_substr != NULL)
            {
                if (search_file_index == -1)
                    search_dir_index = search_dir(pane, search_substr, search_dir_index + 1);
                if (search_dir_index == -1)
                    search_file_index = search_file(pane, search_substr, search_file_index + 1);
            }
            break;
    }
}
