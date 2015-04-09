// Copyright: sees LICENSE. (BSD-style)

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#include <SDL2/SDL.h>

#define MAXVAL 1024

static char *progname;
static int sample_len = 20000;
static int *samples;
static uint64_t *sample_times;
static int sample_head = 0, sample_tail = 0;
static volatile int sample_busy = 0;
static int verbose = 0;

void
usage(retcode)
{
	FILE *out;
	out = retcode == 0 ? stdout : stderr;
	fprintf(out,
	   "%s -v -W <width> -H <height> -X <sample-ms> -Y <max-Y> -s <baudrate> -r <refresh-rate> input-dev\n"
	   "  x-axis sample-ms the number of milliseconds to take one\n"
	   "  snapshot sample of the input data\n", progname);
	exit(retcode);
}

struct plotarg {
	int width;
	int height;
	int sample_msecs;
	int max_y;

	int refresh_rate;
	int terminated;

	SDL_Renderer *renderer;
};


void
plot_clear(SDL_Renderer *renderer)
{
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
}

void
draw_dashed(SDL_Renderer *renderer, int x1, int y1, int x2, int y2, int len)
{
	int to;
	if (x1 != x2) {
		/* horizontal */
		while (x1 < x2) {
			to = x1 + len;
			if (to > x2)
				to = x2;
			SDL_RenderDrawLine(renderer, x1, y1, to, y2);
			x1 += len + 3;
		}
	} else {
		/* vertical */
		while (y1 < y2) {
			to = y1 + len;
			if (to > y2)
				to = y2;
			SDL_RenderDrawLine(renderer, x1, y1, x1, to);
			y1 += len + 3;
		}

	}
}

#define GUTTER 40

void
draw_grid(struct plotarg *opts, SDL_Renderer *renderer)
{
	int ygap;
	int i;

	plot_clear(renderer);

	/* Draw Y-grid (dashed) */
	SDL_SetRenderDrawColor(renderer, 7, 65, 110, 255);
	ygap = (opts->height - GUTTER) / 11;
	for (i = 0; i < 11; i++) {
		draw_dashed(renderer, GUTTER, opts->height - (GUTTER + i * ygap),
		            opts->width - 10, opts->height - (GUTTER + i * ygap), 5);
	}

	SDL_SetRenderDrawColor(renderer, 7, 41, 176, 255);

	/* Draw Y-axis */
	SDL_RenderDrawLine(renderer, GUTTER-1, opts->height - 10, GUTTER-1, 20);
	SDL_RenderDrawLine(renderer, GUTTER-2, opts->height - 10, GUTTER-2, 20);
	SDL_RenderDrawLine(renderer, GUTTER-3, opts->height - 10, GUTTER-3, 20);

	/* Draw X-axis */
	SDL_RenderDrawLine(renderer, 10, opts->height - GUTTER+1, opts->width - 10, opts->height - GUTTER+1);
	SDL_RenderDrawLine(renderer, 10, opts->height - GUTTER+2, opts->width - 10, opts->height - GUTTER+2);
	SDL_RenderDrawLine(renderer, 10, opts->height - GUTTER+3, opts->width - 10, opts->height - GUTTER+3);
}

void
plot_points(struct plotarg *opts, SDL_Renderer *renderer, int end)
{
	int i;
	int n = 0;
	int y;
	int x, x0;
	int len;
	int xrange = opts->width - GUTTER;
	int yrange = opts->height - GUTTER;
	int prev = opts->height - GUTTER;

	if (end < sample_head) {
		len = sample_len - sample_head + end - 1;
	} else {
		len = end - sample_head;
	}

	SDL_SetRenderDrawColor(renderer, 0, 200, 0, 255);

	// Compress data to ranges
	x = GUTTER;
	x0 = GUTTER;
	for (i = sample_head; i != end;) {
		x = GUTTER + (xrange * n / len);
		y = opts->height - GUTTER -
		    (yrange * samples[i] / opts->max_y);

		SDL_RenderDrawLine(renderer, x0, prev, x, y);
		SDL_RenderDrawPoint(renderer, x, y);
		prev = y;
		x0 = x;

		n++;

		i++;
		if (i >= sample_len)
			i = 0;
	}
}

int
doplot(struct plotarg *opts)
{
	uint64_t te;
	int end;

	if (sample_busy) {
		return sample_head;
	}

	// render sample
	sample_busy = 1;

	// refresh plot
	draw_grid(opts, opts->renderer);

	if (sample_head == sample_tail) {
		// no samples yet
		sample_busy = 0;
		return sample_head;
	}

	// find range to fit inside sample_msecs
	te = sample_times[sample_head] + (opts->sample_msecs * 1000);
	for (end = sample_head+1; end != sample_tail; ) {
		if (sample_times[end] >= te) {
			break;
		}
		end++;
		if (end >= sample_len)
			end = 0;
	}

	plot_points(opts, opts->renderer, end);

	SDL_RenderPresent(opts->renderer);

	sample_head = end;
	sample_busy = 0;

	return end;
}

void
plotrun(void *arg)
{
	struct plotarg *opts = arg;
	SDL_Event event;

	SDL_Init(SDL_INIT_VIDEO);

	SDL_Window *window = SDL_CreateWindow("Oscii",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		opts->width, opts->height, 0);

	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
	opts->renderer = renderer;

	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);

	draw_grid(opts, renderer);
	SDL_RenderPresent(renderer);

	for (;;) {
		SDL_WaitEvent(&event);

		switch (event.type) {
		case SDL_MOUSEBUTTONUP:
		case SDL_USEREVENT:
			doplot(opts);
			break;

		case SDL_QUIT:
			goto done;
		}

		SDL_RenderPresent(opts->renderer);
	}

done:
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	opts->terminated = 1;
	pthread_exit(0);
}

int
opendev(char *name, int speed)
{
	struct termios term;
	speed_t termspeed;
	int fd;
	int flags;
	int err;

	fd = open(name, O_RDWR | O_NOCTTY, 0666);
	if (fd < 0) {
		perror("open device");
		exit(1);
	}

	err = tcgetattr(fd, &term);
	if (err < 0) {
		perror("tcgetattr");
		exit(1);
	}

	if (speed < 19200) {
		termspeed = B9600;
	} else if (speed < 38400) {
		termspeed = B19200;
	} else if (speed < 57600) {
		termspeed = B38400;
	} else if (speed < 115200) {
		termspeed = B57600;
	} else {
		termspeed = B115200;
	}

	cfsetispeed(&term, termspeed);
	cfsetospeed(&term, termspeed);

	cfmakeraw(&term);

	term.c_cflag |= CLOCAL;

	tcsetattr(fd, TCSAFLUSH, &term);

	return fd;
}

static uint64_t
usec(struct timeval *tv)
{
	return (1000000 * tv->tv_sec + tv->tv_usec);
}

int
main(int argc, char *argv[])
{
	struct plotarg opts;
	int c;
	unsigned char v;
	ssize_t nread;
	int speed;
	int fd;
	char *devname;
	pthread_t thread;
	struct timeval tv;
	struct timeval tv_render;

	SDL_Event event;
	SDL_UserEvent ue;

	opts.width = 800;
	opts.height = 600;
	opts.max_y = 1200;
	opts.sample_msecs = 1000;
	opts.terminated = 0;
	opts.refresh_rate = 0;

	speed = 115200;

	progname = argv[0];
	while ((c = getopt(argc, argv, "hvW:H:X:Y:s:r:")) != -1) {
		switch (c) {
		case 'h':
			usage(0);
			break;

		case 'W':
			opts.width = atoi(optarg);
			break;

		case 'H':
			opts.height = atoi(optarg);
			break;

		case 'X':
			opts.sample_msecs = atoi(optarg);
			break;

		case 'Y':
			opts.max_y = atoi(optarg);
			break;

		case 's':
			speed = atoi(optarg);
			break;

		case 'r':
			opts.refresh_rate = atoi(optarg);
			break;

		case 'v':
			verbose = 1;
			break;

		default:
			usage(1);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage(1);

	devname = argv[0];

	fd = opendev(devname, speed);

	samples = malloc(sizeof(int) * sample_len);
	sample_times = malloc(sizeof(uint64_t) * sample_len);

	pthread_create(&thread, NULL, plotrun, &opts);

calibrate:
	// calibrate input (find 0xff)
	for (;;) {
		nread = read(fd, &v, 1);
		if (nread <= 0) {
			close(fd);
			perror("read from device (1)");
			SDL_Quit();
			exit(1);
		}

		if (v == 0xFF) {
			// Read 2 more and if 0xFF then it is it
			read(fd, &v, 1);
			read(fd, &v, 1);
			nread = read(fd, &v, 1);
			if (nread <= 0) {
				close(fd);
				perror("read from device (2)");
				SDL_Quit();
				exit(1);
			}
			if (v == 0xFF)
				break;
		}
	}

	ue.type = SDL_USEREVENT;
	ue.code = 0;
	event.type = SDL_USEREVENT;
	event.user = ue;

	gettimeofday(&tv_render, NULL);
	while (!opts.terminated) {
		// Read samples and put in circular buffer.
		// When the user clicks on the mouse, the current sample set
		// is printed to screen
		nread = read(fd, &v, 1);
		gettimeofday(&tv, NULL);
		if (nread == 1) {
			if (verbose)
				printf("%u ", 0xff & v);
			c = v << 8;
			nread = read(fd, &v, 1);
			if (verbose)
				printf("%u ... ", 0xff & v);
			c |= v;
			nread = read(fd, &v, 1);
			if (verbose)
				printf("%u\n", 0xff & v);
		}
		if (nread <= 0) {
			close(fd);
			perror("read from device (3)");
			SDL_Quit();
			exit(1);
		}

		// loss packets; recalibrate
		if (c > MAXVAL)
			goto calibrate;

		if (!sample_busy) {
			samples[sample_tail] = c;
			if (verbose) {
				printf("%d\n", c);
			}
			sample_times[sample_tail] = 1000000 * tv.tv_sec + tv.tv_usec;
			sample_tail++;
			if (sample_tail >= sample_len)
				sample_tail = 0;

			if (sample_tail == sample_head) {
				sample_head++;
				if (sample_head >= sample_len)
					sample_head = 0;
			}
		}

		if (opts.renderer > 0) {
			if (opts.refresh_rate > 0 &&
			    (usec(&tv) - usec(&tv_render)) >= opts.refresh_rate * 1000) {
				gettimeofday(&tv_render, NULL);
				SDL_PushEvent(&event);
				//doplot(&opts);
			}
		}
	}

	close(fd);

	return(0);
}
