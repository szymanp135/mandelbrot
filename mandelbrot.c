#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <string.h>
#include <math.h>
#include "cmplx.h"

#define ERR(s) { fprintf(stderr, "%s:%d ", __FILE__, __LINE__); perror(s); exit(EXIT_FAILURE); }

struct thread_arg {
	/* Locking semaphore to prevent multiple access to pixel iterator */
	sem_t* sem; 
	/* Pixel iterator */
	int* px_itr;
	/* Image size */
	int height;
	int width;
	/* Maximum number of iterations per pixel */
	int max_itr;
	/* Array of iterations for each pixel */
	double* img_mags;
	int* img_iters;
	int img_iters_size;
	/* Central point of image */
	double x0;
	double y0;
	/* Width zoomed view of image */
	double zoom_width;
	/* Rate at which animation zooms in */
	/* Unused for threads but used by "prepare_to_save" */
	double zoom_change_rate;
};

cmplx translate_num_to_pixel(int px, struct thread_arg* tharg) {
	
	cmplx translated = { 0 };

	translated.x = tharg->x0 + ((px % tharg->width) /
		(double)tharg->width - 0.5) * tharg->zoom_width;
	translated.y = tharg->y0 + ((px / tharg->width) /
		(double)tharg->height - 0.5) * tharg->zoom_width;
	
	return translated;
}

void* thread_work(void* arg) {
	
	struct thread_arg* tharg;
	sem_t* sem;
	int* px_itr;
	int i = 0, px;
	int img_size;
	int img_height;
	int img_width;
	double* img_mags;
	int* img_iters;
	int max_itr;
	int do_work = 1;
	cmplx z, z2;
	cmplx z0;

	tharg = arg;
	sem = tharg->sem;
	px_itr = tharg->px_itr;
	img_height = tharg->height;
	img_width = tharg->width;
	img_size = img_width * img_height;
	img_iters = tharg->img_iters;
	img_mags = tharg->img_mags;
	max_itr = tharg->max_itr;
	
	while(do_work) {
		/* Lock semaphore */
		if(sem_wait(sem))
			ERR("sem_wait");
		/* Read current pixel iterator value and increment it.*/
		/* If value is greater than image size then quit loop */
		if((px = (*px_itr)++) > img_size) {
			do_work = 0;
			if(sem_post(sem))
				ERR("sem_post");
			break;
		}
		/* Unlock semaphore */
		if(sem_post(sem))
			ERR("sem_post");

		z0 = translate_num_to_pixel(px, tharg);
		z.x = z.y = z2.x = z2.y = 0.0;

		/* Loop computations */
		for(i = 0; z2.x + z2.y < 4.0 && i < max_itr; ++i) {
			/* Compute next z value */
			z.y = 2.0 * z.x * z.y + z0.y;
			z.x = z2.x - z2.y + z0.x;
			z2.x = z.x * z.x;
			z2.y = z.y * z.y;
		}

		/* Save iterations count */
		img_iters[px] = i;
		img_mags[px] = sqrt(z2.x + z2.y);
	}

	return NULL;
}

/*
 * Creates file and writes provided data to it.
 *
 * Returns 0 if succeeded and non-zero otherwise.
 *
 * Arguments:
 * name		: Base file name
 * data_itr	: Iteration data to store
 * data_mag	: Magnitude data to store
 * data_size	: Size of data to store
 */
int save_data(char* name, int* data_itr, double* data_mag, int data_size) {
	
	int fd;
	int data_to_write;
	int data_written;
	int wr = 0;

	/* Save iterations data */
	name[strlen(name) + 1] = 0;
	name[strlen(name) + 0] = 'i';
	data_to_write = data_size * sizeof(int);
	data_written = 0;
	/* Open file */
	if((fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0)
		return -1;
	/* Write data to file */
	while(data_to_write) {
		if((wr = write(fd, data_itr + data_written, data_to_write)) < 0)
			return -2;
		data_written += wr;
		data_to_write -= wr;
	}
	if(close(fd))
		return -3;

	/* Save magnitude data */
	name[strlen(name) - 1] = 'm';
	data_to_write = data_size * sizeof(double);
	data_written = 0;
	/* Open file */
	if((fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0)
		return -11;
	/* Write data to  */
	while(data_to_write) {
		if((wr = write(fd, data_mag + data_written, data_to_write)) < 0)
			return -12;
		data_written += wr;
		data_to_write -= wr;
	}
	if(close(fd))
		return -13;

	return 0;
}

/*
 * Prepares program to store generated data. Prepares by creating directory
 * and changing current working directory to newly created one.
 *
 * Returns 0 if succeeded and non-zero otherwise.
 *
 * Arguemnts;
 * dirname	: Directory name
 */
int prepare_to_save(char* dirname, struct thread_arg* tharg, int frame_count, int fps) {
	
	int mallocated = 0;
	time_t t;
	struct tm tm;
	int fd;
	char stats[256];

	if(!dirname) {
		dirname = malloc(256);
		if(!dirname)
			return -3;
		mallocated = 1;
		t = time(0);
		tm = *localtime(&t);
		sprintf(dirname, "data/Mandelbrot %04d-%02d-%02d %02d:%02d:%02d",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec);
	}

	printf("Data will be saved in directory: '%s'\n", dirname);

	if(mkdir(dirname, 0755))
		return -1;
	if(chdir(dirname))
		return -2;
	
	/* Create file with statistics */
	if((fd = open(".stats", O_WRONLY | O_CREAT, 0644)) < 0)
		return -4;

	sprintf(stats, "%d\n%d\n%d\n%d\n%d\n%15.12e\n \n%40.37e\n%40.37e\n",
		tharg->width, tharg->height, frame_count, fps,
		tharg->max_itr, tharg->zoom_change_rate, tharg->x0,
		tharg->y0);
	if(write(fd, stats, strlen(stats)) < 0)
		return -5;

	if(close(fd))
		return -6;

	if(mallocated)
		free(dirname);
	return 0;
}

/*
 * Create work threads
 *
 * Returns 0 if succeeded and non-zero otherwise.
 *
 * Arguments:
 * threads	: Array of pointers to created threads ids
 * num		: Number of threads to create
 * tharg	: Threads argument
 */
int create_threads(pthread_t* threads, int num, struct thread_arg* tharg) {
	
	int i;

	for(i = 0; i < num; ++i) { 
		if(pthread_create(threads + i, 0, &thread_work, tharg))
			return -1;
	}
	return 0;
}

/*
 * Wait for threads to finish their work and join them.
 *
 * Returns 0 if succeeded and non-zero otherwise.
 *
 * Arguemnts:
 * threads	: Array of thread ids
 * num		: Number of threads
 */
int join_threads(pthread_t* threads, int num) {
	
	int i;

	for(i = 0; i < num; ++i) {
		if(pthread_join(threads[i], 0))
			return -1;
	}

	return 0;
}

int main(int argc, char** argv) {

	int			i;
	double			t;		/* Loop time indicator */
	sem_t			sem;		/* Image pixel index semaphore */
	int			numCPU;		/* CPU count */
	int			px_itr = 0;	/* Image pixel iterator */
	double*			img_mags;	/* Array of magnitudes of points just after bailout */
	int*			img_iters;	/* Array of iterations per pixel */
	int			img_iters_size;	/* Length of two declared above arrays */
	int			max_itr = 1000;	/* Maximum iterations per pixel */
	int			width = 4096;	/* Image width */
	int			height = 4096;	/* Image height */
	double			x0 = 0.0;	/* Image center point on X axis */
	double			y0 = 0.0;	/* Image center point on Y axis */
	double			zoom_width = 2.0;/* Width of displayed area in coordinate units */
	double			zoom_ch_rt = 1.1;/* Zoom change rate (per second) */
	pthread_t*		threads;	/* Array of thread ids */
	struct thread_arg	tharg;		/* Threads args structure */
	char 			framename[16];	/* String for framenames */

	int 			fps = 5;	/* Frames per second count */
	double 			spf;		/* Seconds per frame (inverse of fps) */
	double 			duration = 1.0;	/* Animation duration (in seconds) */
	
	if(argc < 2) {
		/* Print usage */
		printf("Usage: %s <width> <height> <max_itr> <x0> <y0> <zoom> <fps> "
			"<duration [sec]> <zoom change rate>\n", argv[0]);
		return -2137;
	}

	/* Get number of CPUs in system */
	numCPU = sysconf(_SC_NPROCESSORS_ONLN);
	printf("Detected number of CPUs: %d.\n", numCPU);

	/* Allocate memory for thread ids */
	if(!(threads = malloc(sizeof(pthread_t) * numCPU)))
		ERR("malloc threads");
	
	/* Initialise semaphore */
	if(sem_init(&sem, 0, 1))
		ERR("sem_init");

	/* Gather args */
	if(argc > 1) {
		width = strtol(argv[1], 0, 10);
		if(argc > 2) {
			height = strtol(argv[2], 0, 10);
			if(argc > 3) {
				max_itr = strtol(argv[3], 0, 10);
				if(argc > 5) {
					x0 = atof(argv[4]);
					y0 = atof(argv[5]);
					if(argc > 6) {
						zoom_width = atof(argv[6]);
						if(argc > 7) {
							fps = strtol(argv[7], 0, 10);
							if(argc > 8) {
								duration = atof(argv[8]);
								if(argc > 9) {
									zoom_ch_rt = atof(argv[9]);
								}
							}
						}
					}
				}
			}
		}
	}

	/* Print parameters info */
	printf("Params:\nwidth = %d\nheight = %d\nmax iterations = %d\nx0 = %e"
		"\ny0 = %e\nzoom = %f\nzoom change rate = %f\nfps = %d\n"
		"duration = %f\nestimated frame count = %d\n\n", width, height,
		max_itr, x0, y0, zoom_width, zoom_ch_rt, fps, duration,
		(int)(duration * fps));
	
	/* Initialise array of iterations */
	img_iters_size = width * height;
	if(!(img_iters = malloc(img_iters_size * sizeof(int))))
		ERR("malloc img_iters");
	/*Initialise array of magnitudes */
	if(!(img_mags = malloc(img_iters_size * sizeof(double))))
		ERR("malloc img_mags");
	
	/* Prepare threads args */
	tharg.sem = &sem;
	tharg.px_itr = &px_itr;
	tharg.width = width;
	tharg.height = height;
	tharg.max_itr = max_itr;
	tharg.img_mags = img_mags;
	tharg.img_iters = img_iters;
	tharg.img_iters_size = img_iters_size;
	tharg.x0 = x0;
	tharg.y0 = y0;
	tharg.zoom_width = zoom_width;
	tharg.zoom_change_rate = zoom_ch_rt;

	/* Prepare program to save data */
	if(prepare_to_save(0, &tharg, (int)(duration * fps), fps))
		ERR("prepare_to_save");

	/* Calculate seconds per frame (time step size) */
	spf = 1.0 / (double)fps;

	/* Compute per-frame zoom change rate to achieve
	 * zoom of zoom_ch_rt per second */
	zoom_ch_rt = pow(1.0 / zoom_ch_rt, spf);

	/* Main generation loop */
	for(t = 0.0, i = 0; t < duration; t += spf, ++i,
		tharg.zoom_width *= zoom_ch_rt, px_itr = 0) {
		/* Craete threads and run them */
		if(create_threads(threads, numCPU, &tharg))
			ERR("create_threads");

		/* Wait for them to finish */
		if(join_threads(threads, numCPU))
			ERR("join_threads");

		/* Save generated data to file */
		sprintf(framename, "%05d", i);
		if(save_data(framename, img_iters, img_mags, img_iters_size))
			ERR("save_data");

		printf("Frame %05d finished generation.\n", i);
	}

	sem_destroy(&sem);
	free(img_iters);
	free(img_mags);
	free(threads);
	return EXIT_SUCCESS;
}
