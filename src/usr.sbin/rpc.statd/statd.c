/*
 * Copyright (c) 1995
 *	A.R. Gordon (andrew.gordon@net-tel.co.uk).  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the FreeBSD project
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ANDREW GORDON AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/* main() function for status monitor daemon.  Some of the code in this	*/
/* file was generated by running rpcgen /usr/include/rpcsvc/sm_inter.x	*/
/* The actual program logic is in the file procs.c			*/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: release/10.0.0/usr.sbin/rpc.statd/statd.c 222627 2011-06-02 20:15:32Z rmacklem $");

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <rpc/rpc.h>
#include <rpc/rpc_com.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include "statd.h"

#define	GETPORT_MAXTRY	20	/* Max tries to get a port # */

int debug = 0;		/* Controls syslog() calls for debug messages	*/

char **hosts, *svcport_str = NULL;
int nhosts = 0;
int xcreated = 0;
static int	mallocd_svcport = 0;
static int	*sock_fd;
static int	sock_fdcnt;
static int	sock_fdpos;

static int	create_service(struct netconfig *nconf);
static void	complete_service(struct netconfig *nconf, char *port_str);
static void	clearout_service(void);
static void handle_sigchld(int sig);
void out_of_mem(void);

static void usage(void);

int
main(int argc, char **argv)
{
  struct sigaction sa;
  struct netconfig *nconf;
  void *nc_handle;
  in_port_t svcport;
  int ch, i, s;
  char *endptr, **hosts_bak;
  int have_v6 = 1;
  int maxrec = RPC_MAXDATASIZE;
  int attempt_cnt, port_len, port_pos, ret;
  char **port_list;

  while ((ch = getopt(argc, argv, "dh:p:")) != -1)
    switch (ch) {
    case 'd':
      debug = 1;
      break;
    case 'h':
      ++nhosts;
      hosts_bak = hosts;
      hosts_bak = realloc(hosts, nhosts * sizeof(char *));
      if (hosts_bak == NULL) {
	      if (hosts != NULL) {
		      for (i = 0; i < nhosts; i++) 
			      free(hosts[i]);
		      free(hosts);
		      out_of_mem();
	      }
      }
      hosts = hosts_bak;
      hosts[nhosts - 1] = strdup(optarg);
      if (hosts[nhosts - 1] == NULL) {
	      for (i = 0; i < (nhosts - 1); i++) 
		      free(hosts[i]);
	      free(hosts);
	      out_of_mem();
      }
      break;
    case 'p':
      endptr = NULL;
      svcport = (in_port_t)strtoul(optarg, &endptr, 10);
      if (endptr == NULL || *endptr != '\0' || svcport == 0 || 
          svcport >= IPPORT_MAX)
	usage();
      
      svcport_str = strdup(optarg);
      break;
    default:
      usage();
    }
  argc -= optind;
  argv += optind;

  (void)rpcb_unset(SM_PROG, SM_VERS, NULL);

  /*
   * Check if IPv6 support is present.
   */
  s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if (s < 0)
      have_v6 = 0;
  else 
      close(s);

  rpc_control(RPC_SVC_CONNMAXREC_SET, &maxrec);

  /*
   * If no hosts were specified, add a wildcard entry to bind to
   * INADDR_ANY. Otherwise make sure 127.0.0.1 and ::1 are added to the
   * list.
   */
  if (nhosts == 0) {
	  hosts = malloc(sizeof(char**));
	  if (hosts == NULL)
		  out_of_mem();

	  hosts[0] = "*";
	  nhosts = 1;
  } else {
	  hosts_bak = hosts;
	  if (have_v6) {
		  hosts_bak = realloc(hosts, (nhosts + 2) *
		      sizeof(char *));
		  if (hosts_bak == NULL) {
			  for (i = 0; i < nhosts; i++)
				  free(hosts[i]);
			  free(hosts);
			  out_of_mem();
		  } else
			  hosts = hosts_bak;

		  nhosts += 2;
		  hosts[nhosts - 2] = "::1";
	  } else {
		  hosts_bak = realloc(hosts, (nhosts + 1) * sizeof(char *));
		  if (hosts_bak == NULL) {
			  for (i = 0; i < nhosts; i++)
				  free(hosts[i]);

			  free(hosts);
			  out_of_mem();
		  } else {
			  nhosts += 1;
			  hosts = hosts_bak;
		  }
	  }
	  hosts[nhosts - 1] = "127.0.0.1";
  }

  attempt_cnt = 1;
  sock_fdcnt = 0;
  sock_fd = NULL;
  port_list = NULL;
  port_len = 0;
  nc_handle = setnetconfig();
  while ((nconf = getnetconfig(nc_handle))) {
	  /* We want to listen only on udp6, tcp6, udp, tcp transports */
	  if (nconf->nc_flag & NC_VISIBLE) {
		  /* Skip if there's no IPv6 support */
		  if (have_v6 == 0 && strcmp(nconf->nc_protofmly, "inet6") == 0) {
	      /* DO NOTHING */
		  } else {
			ret = create_service(nconf);
			if (ret == 1)
				/* Ignore this call */
				continue;
			if (ret < 0) {
				/*
				 * Failed to bind port, so close off
				 * all sockets created and try again
				 * if the port# was dynamically
				 * assigned via bind(2).
				 */
				clearout_service();
				if (mallocd_svcport != 0 &&
				    attempt_cnt < GETPORT_MAXTRY) {
					free(svcport_str);
					svcport_str = NULL;
					mallocd_svcport = 0;
				} else {
					errno = EADDRINUSE;
					syslog(LOG_ERR,
					    "bindresvport_sa: %m");
					exit(1);
				}

				/* Start over at the first service. */
				free(sock_fd);
				sock_fdcnt = 0;
				sock_fd = NULL;
				nc_handle = setnetconfig();
				attempt_cnt++;
			} else if (mallocd_svcport != 0 &&
			    attempt_cnt == GETPORT_MAXTRY) {
				/*
				 * For the last attempt, allow
				 * different port #s for each nconf
				 * by saving the svcport_str and
				 * setting it back to NULL.
				 */
				port_list = realloc(port_list,
				    (port_len + 1) * sizeof(char *));
				if (port_list == NULL)
					out_of_mem();
				port_list[port_len++] = svcport_str;
				svcport_str = NULL;
				mallocd_svcport = 0;
			}
		  }
	  }
  }

  /*
   * Successfully bound the ports, so call complete_service() to
   * do the rest of the setup on the service(s).
   */
  sock_fdpos = 0;
  port_pos = 0;
  nc_handle = setnetconfig();
  while ((nconf = getnetconfig(nc_handle))) {
	  /* We want to listen only on udp6, tcp6, udp, tcp transports */
	  if (nconf->nc_flag & NC_VISIBLE) {
		  /* Skip if there's no IPv6 support */
		  if (have_v6 == 0 && strcmp(nconf->nc_protofmly, "inet6") == 0) {
	      /* DO NOTHING */
		  } else if (port_list != NULL) {
			if (port_pos >= port_len) {
				syslog(LOG_ERR, "too many port#s");
				exit(1);
			}
			complete_service(nconf, port_list[port_pos++]);
		  } else
			complete_service(nconf, svcport_str);
	  }
  }
  endnetconfig(nc_handle);
  free(sock_fd);
  if (port_list != NULL) {
  	for (port_pos = 0; port_pos < port_len; port_pos++)
  		free(port_list[port_pos]);
  	free(port_list);
  }

  init_file("/var/db/statd.status");

  /* Note that it is NOT sensible to run this program from inetd - the 	*/
  /* protocol assumes that it will run immediately at boot time.	*/
  daemon(0, 0);
  openlog("rpc.statd", 0, LOG_DAEMON);
  if (debug) syslog(LOG_INFO, "Starting - debug enabled");
  else syslog(LOG_INFO, "Starting");

  /* Install signal handler to collect exit status of child processes	*/
  sa.sa_handler = handle_sigchld;
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGCHLD);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGCHLD, &sa, NULL);

  /* Initialisation now complete - start operating			*/
  notify_hosts();	/* Forks a process (if necessary) to do the	*/
			/* SM_NOTIFY calls, which may be slow.		*/

  svc_run();	/* Should never return					*/
  exit(1);
}

/*
 * This routine creates and binds sockets on the appropriate
 * addresses. It gets called one time for each transport.
 * It returns 0 upon success, 1 for ingore the call and -1 to indicate
 * bind failed with EADDRINUSE.
 * Any file descriptors that have been created are stored in sock_fd and
 * the total count of them is maintained in sock_fdcnt.
 */
static int
create_service(struct netconfig *nconf)
{
	struct addrinfo hints, *res = NULL;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	struct __rpc_sockinfo si;
	int aicode;
	int fd;
	int nhostsbak;
	int r;
	u_int32_t host_addr[4];  /* IPv4 or IPv6 */
	int mallocd_res;

	if ((nconf->nc_semantics != NC_TPI_CLTS) &&
	    (nconf->nc_semantics != NC_TPI_COTS) &&
	    (nconf->nc_semantics != NC_TPI_COTS_ORD))
		return (1);	/* not my type */

	/*
	 * XXX - using RPC library internal functions.
	 */
	if (!__rpc_nconf2sockinfo(nconf, &si)) {
		syslog(LOG_ERR, "cannot get information for %s",
		    nconf->nc_netid);
		return (1);
	}

	/* Get rpc.statd's address on this transport */
	memset(&hints, 0, sizeof hints);
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = si.si_af;
	hints.ai_socktype = si.si_socktype;
	hints.ai_protocol = si.si_proto;

	/*
	 * Bind to specific IPs if asked to
	 */
	nhostsbak = nhosts;
	while (nhostsbak > 0) {
		--nhostsbak;
		sock_fd = realloc(sock_fd, (sock_fdcnt + 1) * sizeof(int));
		if (sock_fd == NULL)
			out_of_mem();
		sock_fd[sock_fdcnt++] = -1;	/* Set invalid for now. */
		mallocd_res = 0;

		/*	
		 * XXX - using RPC library internal functions.
		 */
		if ((fd = __rpc_nconf2fd(nconf)) < 0) {
			syslog(LOG_ERR, "cannot create socket for %s",
			    nconf->nc_netid);
			continue;
		}
		switch (hints.ai_family) {
		case AF_INET:
			if (inet_pton(AF_INET, hosts[nhostsbak],
			    host_addr) == 1) {
				hints.ai_flags |= AI_NUMERICHOST;
			} else {
				/*
				 * Skip if we have an AF_INET6 address.
				 */
				if (inet_pton(AF_INET6, hosts[nhostsbak],
				    host_addr) == 1) {
					close(fd);
					continue;
				}
			}
			break;
		case AF_INET6:
			if (inet_pton(AF_INET6, hosts[nhostsbak],
			    host_addr) == 1) {
				hints.ai_flags |= AI_NUMERICHOST;
			} else {
				/*
				 * Skip if we have an AF_INET address.
				 */
				if (inet_pton(AF_INET, hosts[nhostsbak],
				    host_addr) == 1) {
					close(fd);
					continue;
				}
			}
			break;
		default:
			break;
		}

		/*
		 * If no hosts were specified, just bind to INADDR_ANY
		 */
		if (strcmp("*", hosts[nhostsbak]) == 0) {
			if (svcport_str == NULL) {
				res = malloc(sizeof(struct addrinfo));
				if (res == NULL) 
					out_of_mem();
				mallocd_res = 1;
				res->ai_flags = hints.ai_flags;
				res->ai_family = hints.ai_family;
				res->ai_protocol = hints.ai_protocol;
				switch (res->ai_family) {
				case AF_INET:
					sin = malloc(sizeof(struct sockaddr_in));
					if (sin == NULL) 
						out_of_mem();
					sin->sin_family = AF_INET;
					sin->sin_port = htons(0);
					sin->sin_addr.s_addr = htonl(INADDR_ANY);
					res->ai_addr = (struct sockaddr*) sin;
					res->ai_addrlen = (socklen_t)
					    sizeof(struct sockaddr_in);
					break;
				case AF_INET6:
					sin6 = malloc(sizeof(struct sockaddr_in6));
					if (sin6 == NULL)
						out_of_mem();
					sin6->sin6_family = AF_INET6;
					sin6->sin6_port = htons(0);
					sin6->sin6_addr = in6addr_any;
					res->ai_addr = (struct sockaddr*) sin6;
					res->ai_addrlen = (socklen_t)
					    sizeof(struct sockaddr_in6);
					break;
				default:
					syslog(LOG_ERR, "bad addr fam %d",
					    res->ai_family);
					exit(1);
				}
			} else { 
				if ((aicode = getaddrinfo(NULL, svcport_str,
				    &hints, &res)) != 0) {
					syslog(LOG_ERR,
					    "cannot get local address for %s: %s",
					    nconf->nc_netid,
					    gai_strerror(aicode));
					close(fd);
					continue;
				}
			}
		} else {
			if ((aicode = getaddrinfo(hosts[nhostsbak], svcport_str,
			    &hints, &res)) != 0) {
				syslog(LOG_ERR,
				    "cannot get local address for %s: %s",
				    nconf->nc_netid, gai_strerror(aicode));
				close(fd);
				continue;
			}
		}

		/* Store the fd. */
		sock_fd[sock_fdcnt - 1] = fd;

		/* Now, attempt the bind. */
		r = bindresvport_sa(fd, res->ai_addr);
		if (r != 0) {
			if (errno == EADDRINUSE && mallocd_svcport != 0) {
				if (mallocd_res != 0) {
					free(res->ai_addr);
					free(res);
				} else
					freeaddrinfo(res);
				return (-1);
			}
			syslog(LOG_ERR, "bindresvport_sa: %m");
			exit(1);
		}

		if (svcport_str == NULL) {
			svcport_str = malloc(NI_MAXSERV * sizeof(char));
			if (svcport_str == NULL)
				out_of_mem();
			mallocd_svcport = 1;

			if (getnameinfo(res->ai_addr,
			    res->ai_addr->sa_len, NULL, NI_MAXHOST,
			    svcport_str, NI_MAXSERV * sizeof(char),
			    NI_NUMERICHOST | NI_NUMERICSERV))
				errx(1, "Cannot get port number");
		}
		if (mallocd_res != 0) {
			free(res->ai_addr);
			free(res);
		} else
			freeaddrinfo(res);
		res = NULL;
	}
	return (0);
}

/*
 * Called after all the create_service() calls have succeeded, to complete
 * the setup and registration.
 */
static void
complete_service(struct netconfig *nconf, char *port_str)
{
	struct addrinfo hints, *res = NULL;
	struct __rpc_sockinfo si;
	struct netbuf servaddr;
	SVCXPRT	*transp = NULL;
	int aicode, fd, nhostsbak;
	int registered = 0;

	if ((nconf->nc_semantics != NC_TPI_CLTS) &&
	    (nconf->nc_semantics != NC_TPI_COTS) &&
	    (nconf->nc_semantics != NC_TPI_COTS_ORD))
		return;	/* not my type */

	/*
	 * XXX - using RPC library internal functions.
	 */
	if (!__rpc_nconf2sockinfo(nconf, &si)) {
		syslog(LOG_ERR, "cannot get information for %s",
		    nconf->nc_netid);
		return;
	}

	nhostsbak = nhosts;
	while (nhostsbak > 0) {
		--nhostsbak;
		if (sock_fdpos >= sock_fdcnt) {
			/* Should never happen. */
			syslog(LOG_ERR, "Ran out of socket fd's");
			return;
		}
		fd = sock_fd[sock_fdpos++];
		if (fd < 0)
			continue;

		if (nconf->nc_semantics != NC_TPI_CLTS)
			listen(fd, SOMAXCONN);

		transp = svc_tli_create(fd, nconf, NULL,
		RPC_MAXDATASIZE, RPC_MAXDATASIZE);

		if (transp != (SVCXPRT *) NULL) {
			if (!svc_register(transp, SM_PROG, SM_VERS,
			    sm_prog_1, 0)) {
				syslog(LOG_ERR, "can't register on %s",
				    nconf->nc_netid);
			} else {
				if (!svc_reg(transp, SM_PROG, SM_VERS,
				    sm_prog_1, NULL)) 
					syslog(LOG_ERR,
					    "can't register %s SM_PROG service",
					    nconf->nc_netid);
			}
		} else 
			syslog(LOG_WARNING, "can't create %s services",
			    nconf->nc_netid);

		if (registered == 0) {
			registered = 1;
			memset(&hints, 0, sizeof hints);
			hints.ai_flags = AI_PASSIVE;
			hints.ai_family = si.si_af;
			hints.ai_socktype = si.si_socktype;
			hints.ai_protocol = si.si_proto;


			if ((aicode = getaddrinfo(NULL, port_str, &hints,
			    &res)) != 0) {
				syslog(LOG_ERR, "cannot get local address: %s",
				    gai_strerror(aicode));
				exit(1);
			}

			servaddr.buf = malloc(res->ai_addrlen);
			memcpy(servaddr.buf, res->ai_addr, res->ai_addrlen);
			servaddr.len = res->ai_addrlen;

			rpcb_set(SM_PROG, SM_VERS, nconf, &servaddr);

			xcreated++;
			freeaddrinfo(res);
		}
	} /* end while */
}

/*
 * Clear out sockets after a failure to bind one of them, so that the
 * cycle of socket creation/binding can start anew.
 */
static void
clearout_service(void)
{
	int i;

	for (i = 0; i < sock_fdcnt; i++) {
		if (sock_fd[i] >= 0) {
			shutdown(sock_fd[i], SHUT_RDWR);
			close(sock_fd[i]);
		}
	}
}

static void
usage()
{
      fprintf(stderr, "usage: rpc.statd [-d] [-h <bindip>] [-p <port>]\n");
      exit(1);
}

/* handle_sigchld ---------------------------------------------------------- */
/*
   Purpose:	Catch SIGCHLD and collect process status
   Retruns:	Nothing.
   Notes:	No special action required, other than to collect the
		process status and hence allow the child to die:
		we only use child processes for asynchronous transmission
		of SM_NOTIFY to other systems, so it is normal for the
		children to exit when they have done their work.
*/

static void handle_sigchld(int sig __unused)
{
  int pid, status;
  pid = wait4(-1, &status, WNOHANG, (struct rusage*)0);
  if (!pid) syslog(LOG_ERR, "Phantom SIGCHLD??");
  else if (status == 0)
  {
    if (debug) syslog(LOG_DEBUG, "Child %d exited OK", pid);
  }
  else syslog(LOG_ERR, "Child %d failed with status %d", pid,
    WEXITSTATUS(status));
}

/*
 * Out of memory, fatal
 */
void
out_of_mem()
{

	syslog(LOG_ERR, "out of memory");
	exit(2);
}
