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

void restore_termios_config()
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
		die("tcsetattr");
}

void raw_mode()
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
	raw.c_cc[VMIN] = 0;	/* min number of bytes of input needed before read() can return. */
	raw.c_cc[VTIME] = 1;	/* max time to wait before read() returns, in tenths of a question. */

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/*** init ***/

int main()
{
	raw_mode();
	while (1) {
		char c = '\0';
		if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
		if (iscntrl(c)) {
			printf("%d\r\n", c);
		} else {
			printf("%d ('%c')\r\n", c, c);
		}
		if (c == CTRL_KEY('q'))
			break;
	}

	return 0;
}
