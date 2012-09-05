#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define  BUFFER_SIZE  4096
unsigned char  buf[BUFFER_SIZE];

int main(int argc, char *argv[])
{
	int  in_count, out_count;
	int  result;
	int  in_would_blocks = 0;
	int  out_would_blocks = 0;

	in_count = 0;	/* use as temporary counter */
	while((result = getopt(argc, argv, "hio")) != EOF)
		switch(result) {
		case 'i':
			fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
			in_count++;
			break;

		case 'o':
			fcntl(STDOUT_FILENO, F_SETFL, O_NONBLOCK);
			in_count++;
			break;

		case 'h':
		default:
	fputs("the following options are understood:\n" \
	"	-h  Display this usage message\n" \
	"	-i  Use non-blocking I/O on stdin\n" \
	"	-o  Use non-blocking I/O on stdout\n", stderr);
			exit(0);
	}
	if(!in_count) {
		fputs("Must specify -i and/or -o\n", stderr);
		exit(1);
	}

	do {
		for(in_count = 0; in_count < BUFFER_SIZE;) {
			result = read(STDIN_FILENO, buf + in_count, BUFFER_SIZE - in_count);
			if(result > 0)
				in_count += result;
			else if(errno == EAGAIN)
				in_would_blocks++;
			else
				break;
		}

		for(out_count = 0; out_count < in_count;) {
			result = write(STDOUT_FILENO, buf + out_count, in_count - out_count);
			if(result >= 0)
				out_count += result;
			else if(errno == EAGAIN)
				out_would_blocks++;
			else
				break;
		}
	} while(in_count == BUFFER_SIZE);

	fprintf(stderr, "Number of times EAGAIN was returned by input stream:  %d\n", in_would_blocks);
	fprintf(stderr, "Number of times EAGAIN was returned by output stream: %d\n", out_would_blocks);

	exit(0);
}
