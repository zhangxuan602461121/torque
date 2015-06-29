#include "license_pbs.h" /* See here for the software license */
/*
 *
 * pbs_track - (TORQUE) start tracking a session not spawned through the usual TM interface
 *
 * Authors:
 *      Josh Butikofer
 *      David Jackon
 *      Alan Taufer
 *      Cluster Resources, Inc.
 */

#include <errno.h>
#include "cmds.h"
#include "tm.h"
#include "net_cache.h"
#include <pbs_config.h>   /* the master config generated by configure */

#define MAXARGS 64
#define NO_SERVER_SUFFIX "NO_SERVER_SUFFIX"

/*
 * parse_commandline_opts() - evaluate the pbs_track command line options
 *
 */
int parse_commandline_opts(

  int          argc,
  char       **argv,
  std::string &tmpAdopteeID,
  char        *tmpJobID,
  int         &DoBackground)
  {
  int ArgIndex;
  int NumErrs = 0;

#define GETOPT_ARGS "a:bj:"

  while ((ArgIndex = getopt(argc, argv, GETOPT_ARGS)) != EOF)
    {
    switch (ArgIndex)
      {
      /* -a: adopt a process */
      case 'a':
        /* If we have already read a -b option, we know that there is an error */
        if (DoBackground == 1)
          {
          NumErrs++;
          break;
          }

        tmpAdopteeID = optarg;

        break;

      case 'b':
        /* If we have already read an -a option, we know that there is an error */
        if (tmpAdopteeID.size() != 0)
          {
          NumErrs++;
          break;
          }

        /* background process */

        DoBackground = 1;

        break;

      case 'j':

        snprintf(tmpJobID, sizeof(tmpJobID), "%s", optarg);

        break;

      default:


        NumErrs++;

        break;
      }
    }

  /* Initial sanity check of arguments passed by user
   * e.g. there should not be a executable specified if we are using
   * the adopt option.
   */
  if ((NumErrs > 0) ||
      ((optind >= argc) && (tmpAdopteeID.size() == 0)) ||
      ((tmpJobID[0] == '\0') && (tmpAdopteeID.size() == 0)) ||
      ((tmpAdopteeID.size() > 0) && (tmpJobID[0] == '\0')))
    {
    fprintf(stdout, "NumErrs %d tmpJobID[0] %d tmpAdopteeID.size() %d\n", NumErrs, tmpJobID[0], (int)tmpAdopteeID.size());
    fprintf(stdout, "argc %d argv[0] %s argv[1] %s argv[2] %s", argc, argv[0], argv[1], argv[2]);
    static char Usage[] = "USAGE: pbs_track -j <JOBID> [-b] -- a.out arg1 arg2 ... argN\n" \
                          " OR    pbs_track -j <JOBID> -a <PID>\n";
    fprintf(stderr, "%s", Usage);
    return 2;
    }

  return PBSE_NONE;

  }



/*
 * adopt_process() - adopt a running process into a running PBS job
 *
 * This function will only be called if the calling function (main)
 * determines that the user wants to adopt an existing process
 */
int adopt_process(

  char        *JobID,
  std::string  tmpAdopteeID)
  {
  int   rc;
  int   adoptee_pid;

  adoptee_pid = strtol(tmpAdopteeID.c_str(), NULL, 10);

  if (errno == ERANGE || tmpAdopteeID.find_first_not_of("0123456789") != std::string::npos)
    {
    fprintf(stderr, "Invalid PID to adopt: %s\n", tmpAdopteeID.c_str());
    return PBSE_RMBADPARAM;
    }

  rc = tm_adopt(JobID, TM_ADOPT_JOBID, adoptee_pid);

  return rc;
  }



/*
 * fork_process() - fork process if user has requested such behavior
 *
 * If the user passed the -b option, we will need to fork this process.
 * Also, arguments for the soon-to-be created new process are gathered.
 */
int fork_process(

  int           argc,
  char        **argv,
  int           DoBackground,
  int          &this_pid,
  char         *JobID,
  char        **Args)
  {
  int aindex = 0;
  int rc = -100;

  /* gather a.out and other arguments */

  aindex = 0;

  for (;optind < argc;optind++)
    {
    Args[aindex++] = strdup(argv[optind]);
    printf("Got arg: %s\n",
           Args[aindex-1]);
    }

  Args[aindex] = NULL;

  /* decide if we should fork or not */

  this_pid = 1;

  if (DoBackground == 1)
    {
    printf("FORKING!\n");

    this_pid = fork();
    }

  if ((DoBackground == 0) || (this_pid == 0))
    {
    rc = tm_adopt(JobID, TM_ADOPT_JOBID, getpid());
    }
  else if (this_pid > 0)
    {
    /* parent*/

    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    }
  else if (this_pid < 0)
    {
    fprintf(stderr, "pbs_track: could not fork (%d:%s)\n",
            errno,
            strerror(errno));
    }

  return rc;
  }



/*
 * handle_adoption_results() - Determine if call to tm_adopt was successful
 *
 * The results of the tm_adopt call are evaluated and the user is informed
 * of its status. If we need to call a new command (i.e. we are not adopting
 * an existing process), the command is also called and this process is replaced.
 */
int handle_adoption_results(
  int          rc,
  int          DoBackground,
  int          this_pid,
  char        *JobID,
  std::string  tmpAdopteeID,
  char       **Args)
  {

  if ((DoBackground == 0) || (this_pid == 0) || tmpAdopteeID.size() > 0)
    {
    switch (rc)
      {

      case TM_SUCCESS:

        /* success! */
        fprintf(stderr, "Success!\n");

        break;

      case TM_ENOTFOUND:

        fprintf(stderr, "pbs_track: MOM could not find job %s\n",
                JobID);

        break;

      case TM_ESYSTEM:

      case TM_ENOTCONNECTED:

        fprintf(stderr, "pbs_track: error occurred while trying to communication with pbs_mom: %s (%d)\n",
                pbse_to_txt(rc),
                rc);

        break;

      case TM_EPERM:

        fprintf(stderr, "pbs_track: permission denied: %s (%d)\n",
                pbse_to_txt(rc),
                rc);

        break;

      default:

        /* Unexpected error occurred */

        fprintf(stderr, "pbs_track: unexpected error %s (%d) occurred\n",
                pbse_to_txt(rc),
                rc);

        break;
      }  /* END switch(rc) */

    if (rc != TM_SUCCESS)
      {
      return -1;
      }

    /* do the exec */

    if (tmpAdopteeID.size() == 0 &&
        execvp(Args[0], Args) == -1)
      {
      fprintf(stderr,"execvp failed with error %d, message:\n%s\n",
        errno,
        strerror(errno));
      return errno;
      }
    }

  return 0;
  }



int main(

  int    argc,
  char **argv) /* pbs_track */

  {
  char *Args[MAXARGS];

  int   rc;
  int   this_pid;

  char tmpJobID[PBS_MAXCLTJOBID];        /* from the command line */
  std::string tmpAdopteeID;

  char JobID[PBS_MAXCLTJOBID];  /* modified job ID for MOM/server consumption */
  char ServerName[MAXSERVERNAME];

  int  DoBackground = 0;

  tmpJobID[0] = '\0';

  /* USAGE: pbs_track [-j <JOBID>] -- a.out arg1 arg2 ... argN
   *  OR    pbs_track -j <JOBID> -a <PID>\n
   */
  rc = parse_commandline_opts(argc, argv, tmpAdopteeID, tmpJobID, DoBackground);
  if (rc)
    {
    exit(rc);
    }

  /* Append server name to job number. Thus we create a fully qualified
   * job name to use when we check if that job exists.
   */
  if (getenv(NO_SERVER_SUFFIX) != NULL)
    {
    snprintf(JobID, sizeof(JobID), "%s", tmpJobID);
    }
  else
    {
    if (get_server(tmpJobID, JobID, sizeof(JobID), ServerName, sizeof(ServerName)))
      {
      fprintf(stderr, "pbs_track: illegally formed job identifier: '%s'\n", JobID);
      exit(1);
      }
    }

  /* Check whether we are adopting a previously-existing process,
   * or creating a new one. If we are adopting (tmpAdopteeID.size() > 0)
   * just check if the given pid is valid, then adopt specified process.
   * Otherwise, we will fork the process, if necessary.
   */
  if (tmpAdopteeID.size() > 0)
    {
    rc = adopt_process(JobID, tmpAdopteeID);
    if (rc == PBSE_RMBADPARAM)
      {
      return 1;
      }
    }
  else
    {
    rc = fork_process(argc, argv, DoBackground, this_pid, JobID, Args);
    }

  rc = handle_adoption_results(rc, DoBackground, this_pid, JobID, tmpAdopteeID, Args);

  exit(rc);
  }  /* END main() */
