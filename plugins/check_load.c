/*****************************************************************************
*
* Monitoring check_load plugin
*
* License: GPL
* Copyright (c) 1999-2007 Monitoring Plugins Development Team
*
* Description:
*
* This file contains the check_load plugin
*
* This plugin tests the current system load average.
*
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*
*****************************************************************************/

const char *progname = "check_load";
const char *copyright = "1999-2022";
const char *email = "devel@monitoring-plugins.org";

#include "./common.h"
#include "./runcmd.h"
#include "./utils.h"
#include "./popen.h"

#include <string.h>

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>
#endif

/* needed for compilation under NetBSD, as suggested by Andy Doran */
#ifndef LOADAVG_1MIN
#define LOADAVG_1MIN	0
#define LOADAVG_5MIN	1
#define LOADAVG_15MIN	2
#endif /* !defined LOADAVG_1MIN */


static int process_arguments (int argc, char **argv);
static int validate_arguments (void);
void print_help (void);
void print_usage (void);
static int print_top_consuming_processes();

static int n_procs_to_show = 0;

/* strictly for pretty-print usage in loops */
static const int nums[3] = { 1, 5, 15 };

/* provide some fairly sane defaults */
double wload[3] = { 0.0, 0.0, 0.0 };
double cload[3] = { 0.0, 0.0, 0.0 };
#define la1 la[0]
#define la5 la[1]
#define la15 la[2]

char *status_line;
bool take_into_account_cpus = false;

static void
get_threshold(char *arg, double *th)
{
	size_t i, n;
	int valid = 0;
	char *str = arg, *p;

	n = strlen(arg);
	for(i = 0; i < 3; i++) {
		th[i] = strtod(str, &p);
		if(p == str) break;

		valid = 1;
		str = p + 1;
		if(n <= (size_t)(str - arg)) break;
	}

	/* empty argument or non-floatish, so warn about it and die */
	if(!i && !valid) usage (_("Warning threshold must be float or float triplet!\n"));

	if(i != 2) {
		/* one or more numbers were given, so fill array with last
		 * we got (most likely to NOT produce the least expected result) */
		for(n = i; n < 3; n++) th[n] = th[i];
	}
}


int
main (int argc, char **argv)
{
	int result = -1;
	int i;
	long numcpus;

	double la[3] = { 0.0, 0.0, 0.0 };	/* NetBSD complains about uninitialized arrays */
#ifndef HAVE_GETLOADAVG
	char input_buffer[MAX_INPUT_BUFFER];
#endif

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);
	setlocale(LC_NUMERIC, "POSIX");

	/* Parse extra opts if any */
	argv = np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

#ifdef HAVE_GETLOADAVG
	result = getloadavg (la, 3);
	if (result != 3)
		return STATE_UNKNOWN;
#else
	child_process = spopen (PATH_TO_UPTIME);
	if (child_process == NULL) {
		printf (_("Error opening %s\n"), PATH_TO_UPTIME);
		return STATE_UNKNOWN;
	}
	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf (_("Could not open stderr for %s\n"), PATH_TO_UPTIME);
	}
	fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);
    if(strstr(input_buffer, "load average:")) {
	    sscanf (input_buffer, "%*[^l]load average: %lf, %lf, %lf", &la1, &la5, &la15);
    }
    else if(strstr(input_buffer, "load averages:")) {
	    sscanf (input_buffer, "%*[^l]load averages: %lf, %lf, %lf", &la1, &la5, &la15);
    }
    else {
		printf (_("could not parse load from uptime %s: %d\n"), PATH_TO_UPTIME, result);
		return STATE_UNKNOWN;
    }

	result = spclose (child_process);
	if (result) {
		printf (_("Error code %d returned in %s\n"), result, PATH_TO_UPTIME);
		return STATE_UNKNOWN;
	}
#endif

	if ((la[0] < 0.0) || (la[1] < 0.0) || (la[2] < 0.0)) {
#ifdef HAVE_GETLOADAVG
		printf (_("Error in getloadavg()\n"));
#else
		printf (_("Error processing %s\n"), PATH_TO_UPTIME);
#endif
		return STATE_UNKNOWN;
	}

	/* we got this far, so assume OK until we've measured */
	result = STATE_OK;

	xasprintf(&status_line, _("load average: %.2f, %.2f, %.2f"), la1, la5, la15);
	xasprintf(&status_line, ("total %s"), status_line);


	double scaled_la[3] = { 0.0, 0.0, 0.0 };
	bool is_using_scaled_load_values = false;

	if (take_into_account_cpus == true && (numcpus = GET_NUMBER_OF_CPUS()) > 0) {
		is_using_scaled_load_values = true;

		scaled_la[0] = la[0] / numcpus;
		scaled_la[1] = la[1] / numcpus;
		scaled_la[2] = la[2] / numcpus;

		char *tmp = NULL;
		xasprintf(&tmp, _("load average: %.2f, %.2f, %.2f"), scaled_la[0], scaled_la[1], scaled_la[2]);
		xasprintf(&status_line, "scaled %s - %s", tmp, status_line);
	}

	for(i = 0; i < 3; i++) {
		if (is_using_scaled_load_values) {
			if(scaled_la[i] > cload[i]) {
				result = STATE_CRITICAL;
				break;
			}
			else if(scaled_la[i] > wload[i]) result = STATE_WARNING;
		} else {
			if(la[i] > cload[i]) {
				result = STATE_CRITICAL;
				break;
			}
			else if(la[i] > wload[i]) result = STATE_WARNING;
		}
	}

	printf("LOAD %s - %s|", state_text(result), status_line);
	for(i = 0; i < 3; i++) {
		if (is_using_scaled_load_values) {
			printf("load%d=%.3f;;;0; ", nums[i], la[i]);
			printf("scaled_load%d=%.3f;%.3f;%.3f;0; ", nums[i], scaled_la[i], wload[i], cload[i]);
		} else {
			printf("load%d=%.3f;%.3f;%.3f;0; ", nums[i], la[i], wload[i], cload[i]);
		}
	}

	putchar('\n');
	if (n_procs_to_show > 0) {
		print_top_consuming_processes();
	}
	return result;
}


/* process command-line arguments */
static int
process_arguments (int argc, char **argv)
{
	int c = 0;

	int option = 0;
	static struct option longopts[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"percpu", no_argument, 0, 'r'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"procs-to-show", required_argument, 0, 'n'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	while (1) {
		c = getopt_long (argc, argv, "Vhrc:w:n:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'w': /* warning time threshold */
			get_threshold(optarg, wload);
			break;
		case 'c': /* critical time threshold */
			get_threshold(optarg, cload);
			break;
		case 'r': /* Divide load average by number of CPUs */
			take_into_account_cpus = true;
			break;
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (STATE_UNKNOWN);
		case 'n':
			n_procs_to_show = atoi(optarg);
			break;
		case '?':									/* help */
			usage5 ();
		}
	}

	c = optind;
	if (c == argc)
		return validate_arguments ();

	/* handle the case if both arguments are missing,
	 * but not if only one is given without -c or -w flag */
	if(c - argc == 2) {
		get_threshold(argv[c++], wload);
		get_threshold(argv[c++], cload);
	}
	else if(c - argc == 1) {
		get_threshold(argv[c++], cload);
	}

	return validate_arguments ();
}


static int
validate_arguments (void)
{
	int i = 0;

	/* match cload first, as it will give the most friendly error message
	 * if user hasn't given the -c switch properly */
	for(i = 0; i < 3; i++) {
		if(cload[i] < 0)
			die (STATE_UNKNOWN, _("Critical threshold for %d-minute load average is not specified\n"), nums[i]);
		if(wload[i] < 0)
			die (STATE_UNKNOWN, _("Warning threshold for %d-minute load average is not specified\n"), nums[i]);
		if(wload[i] > cload[i])
			die (STATE_UNKNOWN, _("Parameter inconsistency: %d-minute \"warning load\" is greater than \"critical load\"\n"), nums[i]);
	}

	return OK;
}


void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Felipe Gustavo de Almeida <galmeida@linux.ime.usp.br>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("This plugin tests the current system load average."));

	printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (" %s\n", "-w, --warning=WLOAD1,WLOAD5,WLOAD15");
	printf ("    %s\n", _("Exit with WARNING status if load average exceeds WLOADn"));
	printf (" %s\n", "-c, --critical=CLOAD1,CLOAD5,CLOAD15");
	printf ("    %s\n", _("Exit with CRITICAL status if load average exceed CLOADn"));
	printf ("    %s\n", _("the load average format is the same used by \"uptime\" and \"w\""));
	printf (" %s\n", "-r, --percpu");
	printf ("    %s\n", _("Divide the load averages by the number of CPUs (when possible)"));
	printf (" %s\n", "-n, --procs-to-show=NUMBER_OF_PROCS");
	printf ("    %s\n", _("Number of processes to show when printing the top consuming processes."));
	printf ("    %s\n", _("NUMBER_OF_PROCS=0 disables this feature. Default value is 0"));

	printf (UT_SUPPORT);
}

void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
	printf ("%s [-r] -w WLOAD1,WLOAD5,WLOAD15 -c CLOAD1,CLOAD5,CLOAD15 [-n NUMBER_OF_PROCS]\n", progname);
}

#ifdef PS_USES_PROCPCPU
int cmpstringp(const void *p1, const void *p2) {
	int procuid = 0;
	int procpid = 0;
	int procppid = 0;
	int procvsz = 0;
	int procrss = 0;
	float procpcpu = 0;
	char procstat[8];
#ifdef PS_USES_PROCETIME
	char procetime[MAX_INPUT_BUFFER];
#endif /* PS_USES_PROCETIME */
	char procprog[MAX_INPUT_BUFFER];
	int pos;
	sscanf (* (char * const *) p1, PS_FORMAT, PS_VARLIST);
	float procpcpu1 = procpcpu;
	sscanf (* (char * const *) p2, PS_FORMAT, PS_VARLIST);
	return procpcpu1 < procpcpu;
}
#endif /* PS_USES_PROCPCPU */

static int print_top_consuming_processes() {
	int i = 0;
	struct output chld_out, chld_err;
	if(np_runcmd(PS_COMMAND, &chld_out, &chld_err, 0) != 0){
		fprintf(stderr, _("'%s' exited with non-zero status.\n"), PS_COMMAND);
		return STATE_UNKNOWN;
	}
	if (chld_out.lines < 2) {
		fprintf(stderr, _("some error occurred getting procs list.\n"));
		return STATE_UNKNOWN;
	}
#ifdef PS_USES_PROCPCPU
	qsort(chld_out.line + 1, chld_out.lines - 1, sizeof(char*), cmpstringp);
#endif /* PS_USES_PROCPCPU */
	int lines_to_show = chld_out.lines < (size_t)(n_procs_to_show + 1)
			? (int)chld_out.lines : n_procs_to_show + 1;
	for (i = 0; i < lines_to_show; i += 1) {
		printf("%s\n", chld_out.line[i]);
	}
	return OK;
}
