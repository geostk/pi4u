/* --------------------------------------------------------- */
/* --------------- A Very Short Example -------------------- */
/* --------------------------------------------------------- */

#define _XOPEN_SOURCE 500
#define _BSD_SOURCE

#include <stdio.h>
#include <stdlib.h> /* free() */
#include "cmaes_interface.h"
#include <mpi.h>
#include <torc.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#define VERBOSE 1
#define _STEALING_
/*#define _RESTART_*/
#define _IODUMP_ 1
#define JOBMAXTIME	0
#include "fitfun.c" 

/* the objective (fitness) function to be minimized */
void taskfun(double *x, int *pn, double *res, int *info)
{
	int n = *pn;
#if 0
	int gen, chain, step, task;
	gen = info[0]; chain = info[1]; step = info[2]; task = info[3];
	printf("executing task (%d,%d,%d,%d)\n", gen, chain, step, task);
#endif

	double f = -fitfun(x, n, (void *)NULL, info);	/* CMA-ES needs this minus sign */

	*res = f;
	return;
}

double *lower_bound;	//double lower_bound[] = {-6.0, -6.0};
double *upper_bound;	//double upper_bound[] = {+6.0, +6.0};

int is_feasible(double *pop, int dim)
{
	int i, good;
	for (i = 0; i < dim; i++) {
		good = (lower_bound[i] <= pop[i]) && (pop[i] <= upper_bound[i]);
		if (!good) {
			return 0;
		}
	}
	return 1;
}

static int checkpoint_restart = 0;

/* the optimization loop */
int main(int argn, char **args)
{
	cmaes_t evo; /* an CMA-ES type struct or "object" */
	double *arFunvals, *const*pop, *xfinal;
	int i; 
	/* peh - start */
	int lambda, dim;
	double gt0, gt1, gt2, gt3;
	double tt0, tt1, stt = 0.0; 
	int step = 0;
	int info[4];	/* gen, chain, step, task */
	/* peh - end */

	torc_register_task(taskfun);

	/* Initialize everything into the struct evo, 0 means default */
	torc_init(argn, args, MODE_MS);

	if (argn == 2) {        /* new */
		if (!strcmp(args[1], "-cr")) {
			checkpoint_restart = 1;
		}
	}

	gt0 = torc_gettime();
	arFunvals = cmaes_init(&evo, 0, NULL, NULL, 0, 0, "cmaes_initials.par"); 
	printf("%s\n", cmaes_SayHello(&evo));
	cmaes_ReadSignals(&evo, "cmaes_signals.par");  /* write header and initial values */

	int d = cmaes_Get(&evo, "dim"); 
	lower_bound = malloc(d*sizeof(double));
	upper_bound = malloc(d*sizeof(double));
	for (i = 0; i < d; i++) {
		lower_bound[i] = -6;
		upper_bound[i] = +6;
	}


	/* Iterate until stop criterion holds */
	gt1 = torc_gettime();
	while(!cmaes_TestForTermination(&evo))
	{ 
		/* generate lambda new search points, sample population */
		pop = cmaes_SamplePopulation(&evo); /* do not change content of pop */

		/* Here we may resample each solution point pop[i] until it
		   becomes feasible. function is_feasible(...) needs to be
		   user-defined.  
		   Assumptions: the feasible domain is convex, the optimum is
		   not on (or very close to) the domain boundary, initialX is
		   feasible and initialStandardDeviations are sufficiently small
		   to prevent quasi-infinite looping.
		*/

		lambda = cmaes_Get(&evo, "lambda"); 
		dim = cmaes_Get(&evo, "dim"); 

		if (checkpoint_restart)
		{
			char filename[256];
			sprintf(filename, "curgen_db_%03d.txt", step);
			FILE *fp = fopen(filename, "r");
			if (fp != NULL)
			{
				tt0 = torc_gettime();
				for (i = 0; i < lambda; i++) {
					int j;
					for (j = 0; j < dim; j++) {
						int r = fscanf(fp, "%le", &pop[i][j]);
						printf("[%d] pop[%d][%d]=%f\n", r, i, j, pop[i][j]);
						if (r < 0) exit(1);
					}
					int r = fscanf(fp, "%le", &arFunvals[i]);
					//printf("[%d] arFunvals[%d] = %f\n", r, i, arFunvals[i]);
					if (r < 0) exit(1);
				}
				fclose(fp);
				tt1 = torc_gettime();
				stt += (tt1-tt0);
			}
			else
			{
				checkpoint_restart = 0;
			}
		}


		if (!checkpoint_restart)
		{
			for (i = 0; i < cmaes_Get(&evo, "popsize"); ++i) 
				while (!is_feasible(pop[i], dim)) 
					cmaes_ReSampleSingle(&evo, i); 

			/* evaluate the new search points using fitfun */ 
			tt0 = torc_gettime();
			for (i = 0; i < lambda; ++i) {
#if 0
				/*arFunvals[i] =*/ fitfun(pop[i], (int) dim, &arFunvals[i]);
#endif
				info[0] = 0; info[1] = 0; info[2] = step; info[3] = i; 	/* gen, chain, step, task */
				torc_create(-1, taskfun, 4,
					dim, MPI_DOUBLE, CALL_BY_VAL,
					1, MPI_INT, CALL_BY_COP,
					1, MPI_DOUBLE, CALL_BY_RES,
					4, MPI_INT, CALL_BY_COP,
					pop[i], &dim, &arFunvals[i], info);
			}
#if defined(_STEALING_)
			torc_enable_stealing();
#endif
			torc_waitall();
#if defined(_STEALING_)
			torc_disable_stealing();
#endif

			tt1 = torc_gettime();
			stt += (tt1-tt0);
		}

		/* update the search distribution used for cmaes_SampleDistribution() */
		cmaes_UpdateDistribution(&evo, arFunvals);

		/* read instructions for printing output or changing termination conditions */
		cmaes_ReadSignals(&evo, "cmaes_signals.par");
		fflush(stdout); /* useful in MinGW */

#if VERBOSE
		{
		const double *xbever = cmaes_GetPtr(&evo, "xbestever");
		double fbever = cmaes_Get(&evo, "fbestever");

		printf("BEST @ %5d: ", step);
		for (i = 0; i < dim; i++)
			printf("%25.16lf ", xbever[i]);
		printf("%25.16lf\n", fbever);
		}
//		printf("Step %4d: time = %.3lf seconds\n", step, tt1-tt0);
#endif

#if _IODUMP_
		if (!checkpoint_restart)
		{
		char filename[256];
		sprintf(filename, "curgen_db_%03d.txt", step);
		FILE *fp = fopen(filename, "w");
		for (i = 0; i < lambda; i++) {
			int j;
			for (j = 0; j < dim; j++) fprintf(fp, "%.6le ", pop[i][j]);
			fprintf(fp, "%.6le\n", arFunvals[i]);
		}
		fclose(fp);
		}
#endif


#if defined(_RESTART_)
		cmaes_WriteToFile(&evo, "resume", "allresumes.dat");         /* write resume data */
#endif


#if (JOBMAXTIME > 0)
		{
		double lastgen_time = tt1-tt0;
		static long maxt = -1;

		long runt, remt;
		if (maxt == -1) {
			maxt = JOBMAXTIME;	//get_job_maxTime();	// from lsf or provided by the user
			printf("job maxtime = %ld\n", maxt);
		}

		runt = torc_gettime()-gt0;	//runt = get_job_runTime();       // from lsf or provided by the application: runt = omp_get_wtime()-gt0;
		remt = maxt - runt;
		printf("job runtime = %ld remtime = %ld\n", runt, remt);

		if ((lastgen_time*1.1) > remt) {
			printf("No more available time, exiting...\n");
			evo.sp.stopMaxIter=step+1;
			break;
		}	

		}
#endif


		step++;
	}

	gt2 = torc_gettime();
	printf("Stop:\n%s\n",  cmaes_TestForTermination(&evo)); /* print termination reason */
	cmaes_WriteToFile(&evo, "all", "allcmaes.dat");         /* write final results */

	/* get best estimator for the optimum, xmean */
	xfinal = cmaes_GetNew(&evo, "xmean"); /* "xbestever" might be used as well */
	cmaes_exit(&evo); /* release memory */ 

	/* do something with final solution and finally release memory */
	free(xfinal); 

	gt3 = torc_gettime();
	printf("Total elapsed time = %.3lf  seconds\n", gt3-gt0);
	printf("Initialization time = %.3lf  seconds\n", gt1-gt0);
	printf("Processing time = %.3lf  seconds\n", gt2-gt1);
	printf("Funtion Evaluation time = %.3lf  seconds\n", stt);
	printf("Finalization time = %.3lf  seconds\n", gt3-gt2);

	torc_finalize();

	return 0;
}

