// % gcc nebulafm.c -o nebulafm $(ncursesw5-config --cflags --libs)

#include <stdio.h>
#include <curses.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>

/* globals */
int term_max_x, term_max_y;
int win_start_x, win_start_y;
char *current_dir_path = NULL;
int current_files_num = 0; 
int current_select = 0;
WINDOW *current_win;

/* function prototypes */
void init(int, char *[]);
void init_curses(void);
int get_number_of_files(char *);
void get_files_in_array(char *, char *[]);
int compare_elements(const void *, const void *);
void make_windows(void);
WINDOW *create_newwin(int height, int width, int starty, int startx);
void print_files(int, char *[]);
void go_down(void);
void go_up(void);
void go_back(void);


int main(int argc, char *argv[])
{

    int keypress;

    init(argc, argv);
    init_curses();

    do
    {
        current_files_num = get_number_of_files(current_dir_path);
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
            go_down();
        if (keypress == 'k')
            go_up();
        if (keypress == 'h' && current_dir_path[1] != '\0') // if not root dir
            go_back();
    } while (keypress != 'q');

    free(current_dir_path);
    endwin ();
    return EXIT_SUCCESS;
}


void init(int argc, char *argv[])
{
    setlocale(LC_ALL, ""); // for wide characters
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));
    int alloc_size = snprintf(NULL, 0, "%s", cwd);
    current_dir_path = malloc(alloc_size + 1);
    if (current_dir_path == NULL)
    {
        printf("directory initialization error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(current_dir_path, alloc_size + 1, "%s", cwd);
}

void init_curses()
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

int compare_elements(const void *arg1, const void *arg2)
{
    char * const *p1 = arg1;
    char * const *p2 = arg2;
    return strcasecmp(*p1, *p2);
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
        if (i == current_select)
            wattron(current_win, A_STANDOUT); // highlighting
        wmove(current_win, i + 1, 0);
        wclrtoeol(current_win); // pre-erase the string for too long file names
        wmove(current_win, i + 1, 2);
        wprintw(current_win, "%s", dir_files[i]);
        wattroff(current_win, A_STANDOUT); 
    }
}

void go_down()
{
    current_select++; 
    if (current_select > current_files_num - 1)
        current_select = current_files_num -1;
}

void go_up()
{
    current_select--; 
    current_select = (current_select < 0) ? 0 : current_select;
}

void go_back()
{
    current_dir_path[strrchr(current_dir_path, '/') - current_dir_path] = '\0';
    if (current_dir_path[0] == '\0') // for root dir
    {
        current_dir_path[0] = '/';
        current_dir_path[1] = '\0';
    }
    current_select = 0;
}
