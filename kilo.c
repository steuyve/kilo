/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h> 	/* Character handling functions */
#include <unistd.h> 	/* Standard symbolic constants and types */
#include <termios.h> 	/* Terminal interface */
#include <sys/ioctl.h>	/* IO on streams devices */
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>	/* For implementing variadic functions - functions with variable number of arguments. */
#include <fcntl.h>	/* for file control. */

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

/* convert key 'char' to CTRL-char */
#define CTRL_KEY(k) ((k) & 0x1f) /* bitwise AND with 00011111, setting last 3 bits to 0. */

/*** data ***/

enum editor_key {
	BACKSPACE = 127,	/* ASCII code for backspace. */
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

typedef struct erow {
	int size;
	int rsize;
	char *chars;	/* the literal characters in the row. */
	char *render;	/* the characters to render in this row - for dealing with tabs and nonprintable characters. */
} erow;

struct editor_config {
	int cx, cy;
	int rx;
	int rowoff;	/* row offset - the row of the file the user is currently scrolled to. */
	int coloff;	/* column offset - the column of the file the cursor is currently on. */
	int screenrows;
	int screencols;
	int numrows;
	erow *row;	/* pointer to the first element of an array of erows. */
	int dirty;	/* flag for whether file has been modified. */
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct termios orig_termios;
};

struct editor_config E;

typedef struct abuf {
	char *b;
	int len;
} abuf;

#define ABUF_INIT {NULL, 0}

/*** declarations ***/

void die(const char *);
void restore_termios_config(void);
void raw_mode(void);
int read_key(void);
int row_cx_to_rx(erow *, int);
void update_row(erow *);
void append_row(char *, size_t);
void free_row(erow *);
void delete_row(int);
void row_insert_char(erow *, int, int);
void row_delete_char(erow *, int);
void row_append_string(erow *, char *, size_t);
void insert_char(int);
void delete_char(void);
void editor_open(char *);
char *rows_to_string(int *);
void editor_save(void);
void ab_append(abuf *, const char *, int);
void ab_free(abuf *);
void refresh_screen(void);
int get_cursor_pos(int *, int *);
void editor_scroll(void);
int get_windowsize(int *, int *);
void draw_status(abuf *);
void set_status_msg(const char *, ...);
void draw_status_msg(abuf *);
void draw_rows(abuf *);
void move_cursor(int);
void process_keypress(void);
void init_editor(void);

/*** terminal ***/

void die(const char *s)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	perror(s);
	exit(1);
}

void restore_termios_config(void)
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

void raw_mode(void)
{
	// save termios config
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	atexit(restore_termios_config);

	struct termios raw = E.orig_termios;
	tcgetattr(STDIN_FILENO, &raw);
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

	/* set timeout for read */
	raw.c_cc[VMIN] = 0;	/* min number of bytes of input needed before read() can return. */
	raw.c_cc[VTIME] = 1;	/* max time to wait before read() returns, in tenths of a question. */

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int read_key(void)
{
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}

	if (c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			} else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'F': return END_KEY;
					case 'H': return HOME_KEY;
				}
			}
		} else if (seq[0] == 'O') {
			switch (seq[1]) {
				case 'F': return END_KEY;
				case 'H': return HOME_KEY;
			}
		}

		return '\x1b';
	} else {
		return c;
	}
}

/*** row operations ***/

int row_cx_to_rx(erow *row, int cx)
{
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t') rx+= (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
		rx++;
	}
	return rx;
}

void update_row(erow *row)
{
	int tabs = 0;
	int j;
	/* count the number of tabs in the current row. */
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == '\t') tabs++;

	free(row->render);
	row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) +  1); /* include space for tabs. */

	int idx = 0;
	for (j = 0; j < row->size; j++) {
		/* replace tab character with spaces. */
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			/* append spaces until we get to a tab stop, which is a column divisible by 8. */
			while (idx % 8 !=0) row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}

	row->render[idx] = '\0';
	row->rsize = idx;
}

void append_row(char *s, size_t len)
{
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	update_row(&E.row[at]);

	E.numrows++;
	E.dirty++;
}

void free_row(erow *row)
{
	free(row->render);
	free(row->chars);
}

void delete_row(int at)
{
	if (at < 0 || at >= E.numrows) return;
	free_row(&E.row[at]);
	memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
	E.numrows--;
	E.dirty++;
}

void row_insert_char(erow *row, int at, int c)
{
	if (at < 0 || at > row->size) at = row->size;
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	update_row(row);
	E.dirty++;
}

void row_delete_char(erow *row, int at)
{
	if (at < 0 || at >= row->size) return;
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	update_row(row);
	E.dirty++;
}

void row_append_string(erow *row, char *s, size_t len)
{
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	update_row(row);
	E.dirty++;
}

/*** editor operations ***/

void insert_char(int c)
{
	if (E.cy == E.numrows) {
		append_row("", 0);
	}
	row_insert_char(&E.row[E.cy], E.cx, c);
	E.cx++;
}

void delete_char(void)
{
	if (E.cy == E.numrows) return;
	if (E.cx == 0 && E.cy == 0) return;

	erow *row = &E.row[E.cy];
	if (E.cx > 0) {
		row_delete_char(row, E.cx - 1);
		E.cx--;
	} else {
		E.cx = E.row[E.cy - 1].size;
		row_append_string(&E.row[E.cy - 1], row->chars, row->size);
		delete_row(E.cy);
		E.cy--;
	}
}

/*** file IO ***/

void editor_open(char *filename)
{
	free(E.filename);
	E.filename = strdup(filename);
	FILE *fp = fopen(filename, "r");
	if (!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		append_row(line, linelen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}

char *rows_to_string(int *buflen)
{
	int totlen = 0;
	int j;
	for (j = 0; j < E.numrows; j++) {
		totlen += E.row[j].size + 1;
	}
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *p = buf;
	for (j = 0; j < E.numrows; j++) {
		memcpy(p, E.row[j].chars, E.row[j].size);
		p += E.row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editor_save(void)
{
	if (E.filename == NULL) return;

	int len;
	char *buf = rows_to_string(&len);

	/* O_RDWR - means open it for reading and writing.
	 * O_CREAT - means create a new file if it doesn't already exist.
	 * Permissions 0644 means owner of the file can read and write, everyone else read.
	 */
	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		/* ftruncate sets the file's size to the specified length. */
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);
				E.dirty = 0;
				set_status_msg("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	set_status_msg("Can't save! I/O error: %s", strerror(errno));
}

/*** append buffer ***/
/* Collect planned writes to a buffer to be written to STDOUT_FILENO all at once. */

void ab_append(abuf *ab, const char *s, int len)
{
	/* first make sure we have enough space. */
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL) return;
	/* then append given string into new buffer. */
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void ab_free(abuf *ab)
{
	free(ab->b);
	ab->len = 0;
}

/*** output ***/

void refresh_screen(void)
{
	editor_scroll();	/* figure out which row of the file we are currently on. */

	abuf ab = ABUF_INIT;

	ab_append(&ab, "\x1b[?25l", 6);	/* l and h commands hide and show the cursor respectively. */

	/* Write an escape sequence to the terminal.
	 * We are using VT100 escape sequences here.
	 * \x1b is the escape character (27 in decimal).
	 * Escape sequences are always the escape character followed by [
	 * J means to erase in display.
	 * The argument 2 to the J command means clear the entire screen.
	 */
	/* ab_append(&ab, "\x1b[2J", 4); */ 	/* clear the screen */
	/* H command means to position the cursor.
	 * Normally takes two arguments [row;colH for (row, col).
	 */
	ab_append(&ab, "\x1b[H", 3);	/* reposition the cursor to position 1;1 */

	draw_rows(&ab);
	draw_status(&ab);
	draw_status_msg(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
	ab_append(&ab, buf, strlen(buf));

	ab_append(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	ab_free(&ab);
}

int get_cursor_pos(int *rows, int *cols)
{
	char buf[32];
	unsigned int i = 0;
	/* n command used to query the terminal for status information.
	 * 6 argument asks for cursor position.
	 */
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	printf("\r\n");
	/* reply is an escape sequence of the form \x1b[row;colR. */
	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	/* parse the reply position with sscanf. */
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int get_windowsize(int *rows, int *cols)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		/* C command moves cursor forward.
		 * B command moves cursor down.
		 * Both commands stop cursor from going past edge of screen, so we use 999.
		 */
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return get_cursor_pos(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

void editor_scroll(void)
{
	E.rx = 0;
	if (E.cy < E.numrows) E.rx = row_cx_to_rx(&E.row[E.cy], E.cx);

	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	}
	if (E.cy >= E.rowoff + E.screenrows) {
		E.rowoff = E.cy - E.screenrows + 1;
	}
	if (E.rx < E.coloff) {
		E.coloff = E.rx;
	}
	if (E.rx >= E.coloff + E.screencols) {
		E.coloff = E.rx - E.screencols + 1;
	}
}

void draw_rows(abuf *ab)
{
	int y;
	for (y = 0; y < E.screenrows; y++) {
		int filerow = y + E.rowoff;
		if (filerow >= E.numrows) {
			if (E.numrows == 0 && y == E.screenrows / 3) {
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
				if (welcomelen > E.screencols) welcomelen = E.screencols;
				int padding = (E.screencols - welcomelen) / 2;
				if (padding) {
					ab_append(ab, "~", 1);
					padding--;
				}
				while (padding--) ab_append(ab, " ", 1);
				ab_append(ab, welcome, welcomelen);
			} else {
				ab_append(ab, "~", 1);
			}
		} else {
			int len = E.row[filerow].rsize - E.coloff;
			if (len < 0) len = 0;
			if (len > E.screencols) len = E.screencols;
			ab_append(ab, &E.row[filerow].render[E.coloff], len);
		}
			/* K command erases the current line.
			 * Default argument (0) erases to the right of the cursor.
			 */
			ab_append(ab, "\x1b[K", 3);
			ab_append(ab, "\r\n", 2);
		} 
}

void draw_status(abuf *ab)
{
	/* m command means select graphic rendition.
	 * 7 means invert colors.
	 */
	ab_append(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
			E.filename ? E.filename : "[No Name]", E.numrows,
			E.dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
	if (len > E.screencols) len = E.screencols;
	ab_append(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			ab_append(ab, rstatus, rlen);
			break;
		} else {
			ab_append(ab, " ", 1);
			len++;
		}
	}
	ab_append(ab, "\x1b[m", 3);
	ab_append(ab, "\r\n", 2);
}

void set_status_msg(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

void draw_status_msg(abuf *ab)
{
	ab_append(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols) msglen = E.screencols;
	if(msglen && time(NULL) - E.statusmsg_time < 5)	/* set timeout to 5 seconds. */
		ab_append(ab, E.statusmsg, msglen);
}

/*** input ***/

void process_keypress(void) {
	static int quit_times = KILO_QUIT_TIMES;
	int c = read_key();

	switch (c) {
		case '\r':
			/* TODO */
			break;
		case CTRL_KEY('q'):
			if (E.dirty && quit_times > 0) {
				set_status_msg("WARNING: File has unsaved changes. Press CTRL-Q %d more times to force quit.", quit_times);
				quit_times--;
				return;
			}
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
		case CTRL_KEY('s'):
			editor_save();
			break;
		case HOME_KEY:
			E.cx = 0;
			break;
		case END_KEY:
			if (E.cy < E.numrows)
				E.cx = E.row[E.cy].size;
			break;
		case BACKSPACE:
		case CTRL_KEY('h'):	/* CTRL-h sends ASCII code 8 which is what the backspace character used to send. */
		case DEL_KEY:
			if (c == DEL_KEY) move_cursor(ARROW_RIGHT);
			delete_char();
			break;
		case PAGE_UP:
		case PAGE_DOWN:
			{
				if (c == PAGE_UP) {
					E.cy = E.rowoff;
				} else if (c == PAGE_DOWN) {
					E.cy = E.rowoff + E.screenrows - 1;
					if (E.cy > E.numrows) E.cy = E.numrows;
				}

				int times = E.screenrows;
				while (times--) move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;
		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			move_cursor(c);
			break;
		case CTRL_KEY('l'):	/* CTRL-l traditionally used to refresh the screen in terminal programs. */
		case '\x1b':
			break;
		default:
			insert_char(c);
			break;
	}

	quit_times = KILO_QUIT_TIMES;
}

void move_cursor(int key)
{
	/* get the current row. */
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	
	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) {
				E.cx--;
			} else if (E.cy > 0) {
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (row && E.cx < row->size) {
				E.cx++;
			} else if (row && E.cx == row->size) {
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0) E.cy--;
			break;
		case ARROW_DOWN:
			if (E.cy < E.numrows) E.cy++;
			break;
	}

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen) E.cx = rowlen;
}

/*** init ***/

void init_editor(void)
{
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.numrows = 0;
	E.row = NULL;
	E.dirty = 0;
	E.filename = NULL;
	// E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;

	if (get_windowsize(&E.screenrows, &E.screencols) == -1) die("get_windowsize");
	E.screenrows -= 2;
}

int main(int argc, char *argv[])
{
	raw_mode();
	init_editor();
	if (argc >= 2) {
		editor_open(argv[1]);
	}

	set_status_msg("HELP: CTRL-S to save | CTRL-Q to quit.");

	while (1) {
		refresh_screen();
		process_keypress();
	}

	return 0;
}
