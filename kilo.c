/*** includes ***/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h> 	/* Character handling functions */
#include <unistd.h> 	/* Standard symbolic constants and types */
#include <termios.h> 	/* Terminal interface */

/*** defines ***/

/* convert key 'char' to CTRL-char */
#define CTRL_KEY(k) ((k) & 0x1f) /* bitwise AND with 00011111, setting last 3 bits to 0. */

/*** data ***/

struct termios orig_termios;

/*** terminal ***/

void die(const char *s)
{
	perror(s);
	exit(1);
}

void restore_termios_config(void)
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
		die("tcsetattr");
}

void raw_mode(void)
{
	// save termios config
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
	atexit(restore_termios_config);

	struct termios raw = orig_termios;
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

void refresh_screen(void)
{
	/* Write an escape sequence to the terminal.
	 * We are using VT100 escape sequences here.
	 * \x1b is the escape character (27 in decimal).
	 * Escape sequences are always the escape character followed by [
	 * J means to erase in display.
	 * The argument 2 to the J command means clear the entire screen.
	 */
	write(STDOUT_FILENO, "\x1b[2J", 4); 	/* clear the screen */

	/* H command means to position the cursor.
	 * Normally takes two arguments [row;colH for (row, col).
	 */
	write(STDOUT_FILENO, "\x1b[H", 3);	/* reposition the cursor to position 1;1 */ 
}

/*** input ***/

void process_keypress(void) {
	char c = read_key();

	switch (c) {
		case CTRL_KEY('q'):
			exit(0);
			break;
	}
}

/*** init ***/

int main(void)
{
	raw_mode();
	while (1) {
		refresh_screen();
		process_keypress();
	}

	return 0;
}
