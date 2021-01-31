// % gcc nebulafm.c -o nebulafm $(ncursesw5-config --cflags --libs)

#include <stdio.h>
#include <curses.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <sys/stat.h>
#include <sys/types.h>

/* globals */
int term_max_x, term_max_y;
int win_start_x, win_start_y;
char *current_dir_path = NULL;
char *dir_name_select = NULL; // highlighting after returning to parent directory
int back_flag = 0; // changes to 1 after returning to parent directory
int current_dirs_num;
int current_files_num;
int current_select = 1; // position of the selected line in the window
int top_file_index = 0; // file index to print the first line of the current window
WINDOW *current_win;

/* function prototypes */
void init(int, char *[]);
void init_curses(void);
void get_number_of_files(char *, int *, int *);
void get_files_in_array(char *, char *[], char *[]);
int compare_elements(const void *, const void *);
void make_windows(void);
WINDOW *create_newwin(int, int, int, int);
int print_files(char *[], int, int, int);
void print_line(WINDOW *, int, char *);
int is_dir(const char *);
void go_down(void);
void go_up(void);
void go_back(void);
void go_forward_opendir(char *[]);

int main(int argc, char *argv[])
{
    int keypress;
    init(argc, argv);
    init_curses();

    do
    {
        current_dirs_num = 0;
        current_files_num = 0;
        get_number_of_files(current_dir_path, &current_dirs_num, &current_files_num);
        char *current_dir_dirs[current_dirs_num];
        char *current_dir_files[current_files_num];
        get_files_in_array(current_dir_path, current_dir_dirs, current_dir_files);

        /* sorting files in dir alphabetically */
        qsort(current_dir_dirs, current_dirs_num, sizeof(char *), compare_elements);
        qsort(current_dir_files, current_files_num, sizeof(char *), compare_elements);

        getmaxyx(stdscr, term_max_y, term_max_x); // get term size
        make_windows(); // make two windows + status bar

        /* update current_select and top_file_index after go_back() */
        if (back_flag == 1)
        {
            for (int i = 0; i < current_dirs_num; i++)
            {
                if (strcmp(current_dir_dirs[i], dir_name_select) == 0)
                {
                    if (term_max_y > current_dirs_num)
                    {
                        top_file_index = 0;
                        current_select = i + 1;
                        break;
                    }
                    else if (i <= current_dirs_num - 1 - term_max_y)
                    {
                        top_file_index = i;
                        current_select = 1;
                        break;
                    }
                    else
                    {
                        top_file_index = i - (term_max_y - 3);
                        current_select = term_max_y - 2;
                        break;
                    }
                }
            }
            back_flag = 0;
        }

        /* print directories */
        wattron(current_win, COLOR_PAIR(1));
        wattron(current_win, A_BOLD);
        int line_index = print_files(current_dir_dirs, current_dirs_num, top_file_index, 1);

        /* print other types of files */
        wattroff(current_win, COLOR_PAIR(1));
        wattroff(current_win, A_BOLD);
        if (top_file_index < current_dirs_num)
            print_files(current_dir_files, current_files_num, 0, line_index);
        else
            print_files(current_dir_files, current_files_num, top_file_index - current_dirs_num, 1);

        box(current_win, 0, 0);
        wrefresh(current_win);

        // keybindings
        keypress = wgetch(current_win);
        if (keypress == 'j')
            go_down();
        if (keypress == 'k')
            go_up();
        if (keypress == 'h' && current_dir_path[1] != '\0') // if not root dir
            go_back();
        if (keypress == 'l')
            if (top_file_index + current_select <= current_dirs_num)
                go_forward_opendir(current_dir_dirs);
    } while (keypress != 'q');

    free(current_dir_path);
    free(dir_name_select);
    endwin ();
    return EXIT_SUCCESS;
}

void init(int argc, char *argv[])
{
    setlocale(LC_ALL, ""); // unicode, etc

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

    char *ptr = strrchr(current_dir_path, '/');
    alloc_size = snprintf(NULL, 0, "%s", ptr);
    dir_name_select = malloc(alloc_size + 1);
    if (dir_name_select == NULL)
    {
        endwin();
        printf("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(dir_name_select, alloc_size + 1, "%s", ptr + 1);
}

void init_curses()
{
    initscr();
    noecho();
    curs_set(0); // hide the cursor
    start_color();
    init_pair(1, COLOR_CYAN, 0); // directory highlighting colors
}

void get_number_of_files(char *directory, int *dirs, int *files)
{
    DIR *pDir;
    struct dirent *pDirent;

    pDir = opendir(directory);
    while ((pDirent = readdir(pDir)) != NULL)
    {
        if (strcmp(pDirent->d_name, "..") == 0 || strcmp(pDirent->d_name, ".") == 0)
            continue;
        if (pDirent->d_type == DT_DIR)
            *dirs += 1;
        else
            *files += 1;
    }
    closedir(pDir);
}

void get_files_in_array(char *directory, char *dirs[], char *files[])
{
    DIR *pDir;
    struct dirent *pDirent;
    int i = 0;
    int j = 0;

    pDir = opendir(directory);
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

int print_files(char *dir_files[], int files_num, int start_index, int line_pos)
{
    for (int i = start_index; i < files_num; i++)
    {
        if (line_pos == current_select)
            wattron(current_win, A_STANDOUT); // highlighting
        print_line(current_win, line_pos, dir_files[i]);
        wattroff(current_win, A_STANDOUT); 
        line_pos++;
    }

    // erase the string if last filename is too long
    wmove(current_win, line_pos, 0);
    wclrtoeol(current_win);
    return line_pos;
}

void print_line(WINDOW *window, int pos, char *str)
{
    wmove(window, pos, 0);
    wclrtoeol(window); // pre-erase the string for too long file names
    wmove(window, pos, 2);
    wprintw(window, "%s\n", str);
}

int is_dir(const char *dir)
{
    struct stat stat_path;
    if (stat(dir, &stat_path) != 0)
        return 0;
    return S_ISDIR(stat_path.st_mode); // non-zero if the file is a directory
}

void go_down()
{
    int files_num = current_dirs_num + current_files_num;
    current_select++; 
    if (current_select > files_num)
        current_select = files_num;

    /* scrolling */
    if (current_select > term_max_y - 2)
    {
        if (files_num - top_file_index > term_max_y - 2)
            top_file_index++;
        current_select--;
        wclear(current_win);
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
        wclear(current_win);
    }
}

void go_back()
{
    free(dir_name_select);
    char *ptr = strrchr(current_dir_path, '/');
    int alloc_size = snprintf(NULL, 0, "%s", ptr);
    dir_name_select = malloc(alloc_size + 1);
    if (dir_name_select == NULL)
    {
        endwin();
        printf("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(dir_name_select, alloc_size + 1, "%s", ptr + 1);

    current_dir_path[strrchr(current_dir_path, '/') - current_dir_path] = '\0';
    if (current_dir_path[0] == '\0') // for root dir
    {
        current_dir_path[0] = '/';
        current_dir_path[1] = '\0';
    }

    back_flag = 1;
}

void go_forward_opendir(char *dir_files[])
{
    int index = top_file_index + current_select;
    int alloc_size = snprintf(NULL, 0, "%s/%s", current_dir_path, dir_files[index - 1]);
    char *tmp_path = malloc(alloc_size + 1);
    if (tmp_path == NULL)
    {
        endwin();
        printf("memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    snprintf(tmp_path, alloc_size + 1, "%s/%s", current_dir_path, dir_files[index - 1]);

    if (is_dir(tmp_path) != 0)
    {
        free(current_dir_path);
        alloc_size = snprintf(NULL, 0, "%s/%s", tmp_path);
        current_dir_path = malloc(alloc_size + 1);
        if (current_dir_path == NULL)
        {
            endwin();
            printf("memory allocation error\n");
            exit(EXIT_FAILURE);
        }
        snprintf(current_dir_path, alloc_size + 1, "%s", tmp_path);
        current_select = 1;
        top_file_index = 0;
    }
    free(tmp_path);
}
