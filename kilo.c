/*** includes ***/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h> 	/* Character handling functions */
#include <unistd.h> 	/* Standard symbolic constants and types */
#include <termios.h> 	/* Terminal interface */
#include <sys/ioctl.h>	/* IO on streams devices */
#include <string.h>

/*** defines ***/

/* convert key 'char' to CTRL-char */
#define CTRL_KEY(k) ((k) & 0x1f) /* bitwise AND with 00011111, setting last 3 bits to 0. */

/*** data ***/

struct editor_config {
	int screenrows;
	int screencols;
	struct termios orig_termios;
};

struct editor_config E;

struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

/*** declarations ***/

void die(const char *);
void restore_termios_config(void);
void raw_mode(void);
char read_key(void);
void ab_append(struct abuf *, const char *, int);
void ab_free(struct abuf *);
void refresh_screen(void);
int get_cursor_pos(int *, int *);
int get_windowsize(int *, int *);
void draw_rows(struct abuf *);
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

char read_key(void)
{
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	return c;
}

/*** append buffer ***/
/* Collect planned writes to a buffer to be written to STDOUT_FILENO all at once. */

void ab_append(struct abuf *ab, const char *s, int len)
{
	/* first make sure we have enough space. */
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL) return;
	/* then append given string into new buffer. */
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void ab_free(struct abuf *ab)
{
	free(ab->b);
	ab->len = 0;
}

/*** output ***/

void refresh_screen(void)
{
	struct abuf ab = ABUF_INIT;
	/* Write an escape sequence to the terminal.
	 * We are using VT100 escape sequences here.
	 * \x1b is the escape character (27 in decimal).
	 * Escape sequences are always the escape character followed by [
	 * J means to erase in display.
	 * The argument 2 to the J command means clear the entire screen.
	 */
	ab_append(&ab, "\x1b[2J", 4); 	/* clear the screen */
	/* H command means to position the cursor.
	 * Normally takes two arguments [row;colH for (row, col).
	 */
	ab_append(&ab, "\x1b[H", 3);	/* reposition the cursor to position 1;1 */

	draw_rows(&ab);
	ab_append(&ab, "\x1b[H", 3);

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

void draw_rows(struct abuf *ab)
{
	int y;
	for (y = 0; y < E.screenrows; y++) {
		ab_append(ab, "~", 1);
		if (y < E.screenrows - 1) ab_append(ab, "\r\n", 2);
	}
}

/*** input ***/

void process_keypress(void) {
	char c = read_key();

	switch (c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
	}
}

/*** init ***/

void init_editor(void)
{
	if (get_windowsize(&E.screenrows, &E.screencols) == -1) die("get_windowsize");
}

int main(void)
{
	raw_mode();
	init_editor();

	while (1) {
		refresh_screen();
		process_keypress();
	}

	return 0;
}
