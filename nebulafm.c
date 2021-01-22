#include <stdio.h>
#include <curses.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>

/* globals */
int term_max_x, term_max_y;
int win_start_x, win_start_y;
char *current_dir_path = NULL;
int current_select = 0;
WINDOW *current_win;

/* function prototypes */
void init(int, char *[]);
void curses_init(void);
int get_number_of_files(char *);
void get_files_in_array(char *, char *[]);
void make_windows(void);
WINDOW *create_newwin(int height, int width, int starty, int startx);
void print_files(int, char *[]);
void highlight_selection(int, int);
void scroll_down(void);
void scroll_up(void);
int compare_elements(const void *, const void *);

int main(int argc, char *argv[])
{

    int keypress;

    init(argc, argv);
    curses_init();

    do
    {
        int current_files_num = get_number_of_files(current_dir_path);
        char *current_dir_files[current_files_num];
        get_files_in_array(current_dir_path, current_dir_files);

        // sorting files in dir alphabetically
        qsort(current_dir_files, current_files_num, sizeof (char*), compare_elements);

        getmaxyx(stdscr, term_max_y, term_max_x); // get term size
        make_windows(); // make two windows + status

        print_files(current_files_num, current_dir_files);

        box(current_win, 0, 0);
        //wrefresh(current_win);

        // keybindings
        keypress = wgetch(current_win);
        if (keypress == 'j')
            scroll_down();
        if (keypress == 'k')
            scroll_up();

    } while (keypress != 'q');

    free(current_dir_path);
    endwin ();
    return 0;
}

void init(int argc, char *argv[])
{
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));
    int allocSize = snprintf(NULL, 0, "%s", cwd);
    current_dir_path = malloc(allocSize + 1);
    if (current_dir_path == NULL)
    {
        printf("directory initialization error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(current_dir_path, allocSize + 1, "%s", cwd);
}

void curses_init()
{
    initscr(); // start curses mode
    noecho();
    curs_set(0); // hide the cursor
}

int get_number_of_files(char *directory)
{
    DIR *pDir;
    struct dirent *pDirent;
    int len = 0;

    pDir = opendir(directory);
    while ((pDirent = readdir(pDir)) != NULL)
    {
        if (strcmp(pDirent->d_name, "..") == 0 || strcmp(pDirent->d_name, ".") == 0)
            continue;
        len++;
    }
    closedir(pDir);
    return len;
}

void get_files_in_array(char *directory, char *files[])
{
    DIR *pDir;
    struct dirent *pDirent;
    int i = 0;

    pDir = opendir(directory);
    while ((pDirent = readdir(pDir)) != NULL)
    {
        if (strcmp(pDirent->d_name, "..") == 0 || strcmp(pDirent->d_name, ".") == 0)
            continue;
        files[i] = strdup(pDirent->d_name);
        i++;
    }
    closedir(pDir);
}

void make_windows()
{
    current_win = create_newwin(term_max_y, term_max_x / 2, 0, 0);
    // later create preview_win and status_win ...
}

WINDOW *create_newwin(int height, int width, int starty, int startx)
{
    WINDOW *local_win;
    local_win = newwin(height, width, starty, startx);
    return local_win;
}

void print_files(int num_files_dir, char *dir_files[])
{
    for (int i = 0; i < num_files_dir; i++)
    {
        highlight_selection(current_select, i);
        wmove(current_win, i + 1, 1);
        wprintw(current_win, "%.*s", term_max_x / 2 - 3, dir_files[i]);
    }
}

void highlight_selection(int current_select, int file_number)
{
    if (file_number == current_select)
        wattron(current_win, A_STANDOUT);
    else
        wattroff(current_win, A_STANDOUT); 
}

void scroll_down()
{
    current_select++; 
    current_select = (current_select < 0) ? 0 : current_select;
}

void scroll_up()
{
    current_select--; 
    current_select = (current_select < 0) ? 0 : current_select;
}

int compare_elements(const void *arg1, const void *arg2)
{
    char *const *p1 = arg1;
    char *const *p2 = arg2;
    return strcasecmp(*p1, *p2);
}
