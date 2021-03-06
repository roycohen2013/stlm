/* -*- c -*- */
/*
 * Copyright (c) 2013 Peter Scott, OZ2ABA
 *
 * Strx correlator is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * Strx correlator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with strx; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "viterbi.h"

/** @brief Trellis encoder table.
 */
uint8_t trellis_encoder [0x8000];


typedef enum {INIT, HUNT, COLLECT_HEAD, COLLECT_ALL} state_t;

typedef struct _correlator_t {
	state_t		state;			/* Engine state. */
	uint32_t	sr;			/* Shift register for asembling bytes. */
	uint32_t	flag;			/* Flag value. */

	unsigned int	sampleno;		/* Number of samples collected. */
	unsigned int	total_samples;		/* Total number of samples to collect for the full packet. */

	uint8_t		packet_buf [1024];	/* Byte buffer array for the decoded data. */
	unsigned int	packet_len;		/* Length of the packet in the buffer. */
	unsigned int	pbit;
	unsigned int	flag_err;		/* Number of error bits in flag. */
	unsigned int	trellis_err;		/* Number of error bits corrected by the Trellis encoding. */

	struct v	*vp;			/* Viterbi instance. */
	COMPUTETYPE	*symbols;		/* Pointer to a symbol buffer for the Viterbi decoder. */
	uint8_t		raw_buf [2048];		/* Buffer for storing the raw packet in packed format (for trellis check). */
} correlator_t;


fd_set	fixed_read_fds;
int	fixed_nfds;

struct client {
	struct client	*prev;	/* Pointer to the previous one in the chain. */
	struct client	*next;	/* Pointer to the next one in the chain. */
	int		fd;	/* Socket file descriptor. */
};

struct client_set {
	int			listen_fd;	/* Accept socket. */
	unsigned long long	packets;	/* Number of packets received on this channel. */
	unsigned long long	bytes;		/* Number of bytes received on this channel. */
	struct client		*list;		/* List of connected sockets. */
};

struct client_set client_set [270];	/* Array of client sockets. */

uint8_t	last_housekeeping [100];	/* Buffer to hold the last received housekeeping packet. */

/* Macro to insert a entry into a list. */
#define INSERT_INTO_LIST(list,element) do { \
	element->next = list; \
	if (element->next) element->next->prev = element; \
	list = element; \
} while (0)

/* Macro to remove a entry from a list. */
#define REMOVE_FROM_LIST(list,element) do { \
	if (element->prev == NULL) {	/* First element in the list. */ \
		list = element->next; \
		if (element->next) element->next->prev = NULL; \
	} else {	/* Not the first element. */ \
		element->prev->next = element->next; \
		if (element->next) element->next->prev = element->prev; \
	} \
} while (0)



static void init_sockets (void)
{
	int			x;
	struct sockaddr_in	addr;
	size_t			addr_size;
	int			opt_val;

	memset (client_set, 0, sizeof (client_set));
	FD_ZERO (&fixed_read_fds);
	fixed_nfds = 0;

	/* Create a listening socket for the first 32 slots. */
	for (x = 0; x < 32; x++) {
		client_set [x].listen_fd = socket (AF_INET, SOCK_STREAM, 0);
		if (client_set [x].listen_fd < 0) {
			perror ("socket: ");
			exit (1);
		}

		opt_val = 1;
		setsockopt (client_set [x].listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof (opt_val));

		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons (4000 + x);
		addr.sin_family = AF_INET;
		addr_size = sizeof (addr);
		if (bind (client_set [x].listen_fd, (struct sockaddr *)&addr, addr_size) < 0) {
			perror ("bind: ");
			exit (1);
		}

		if (listen (client_set [x].listen_fd, 5) < 0) {
			perror ("listen: ");
			exit (1);
		}

		/* Add the listening handle to the fixed read_fds. */
		FD_SET (client_set [x].listen_fd, &fixed_read_fds);
		if (client_set [x].listen_fd >= fixed_nfds) {
			fixed_nfds = client_set [x].listen_fd + 1;
		}
	}

	/* Create a listening socket for the command port. */
	client_set [260].listen_fd = socket (AF_INET, SOCK_STREAM, 0);
	if (client_set [x].listen_fd < 0) {
		perror ("socket: ");
		exit (1);
	}

	opt_val = 1;
	setsockopt (client_set [260].listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof (opt_val));

	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons (5000);
	addr.sin_family = AF_INET;
	addr_size = sizeof (addr);
	if (bind (client_set [260].listen_fd, (struct sockaddr *)&addr, addr_size) < 0) {
		perror ("bind: ");
		exit (1);
	}

	if (listen (client_set [260].listen_fd, 5) < 0) {
		perror ("listen: ");
		exit (1);
	}

	/* Add the listening handle to the fixed read_fds. */
	FD_SET (client_set [260].listen_fd, &fixed_read_fds);
	if (client_set [260].listen_fd >= fixed_nfds) {
		fixed_nfds = client_set [260].listen_fd + 1;
	}
}


static int dump_telemetry (int fd)
{
	char		buf [10000];
	char		*bc = buf;
	uint32_t	upt = last_housekeeping [1] + (last_housekeeping [2] << 8) + (last_housekeeping [3] << 16) + (last_housekeeping [4] << 24);
	float		vbat = ((24.9+4.7)/4.7) * (last_housekeeping [5] + (last_housekeeping [6] << 8)) * (3.3 / 4095.0);
	int		x;

	/* Forst dump some info from the housekeeping block. */
	bc += sprintf (bc, "TX: %d  Uptime: %d.%d  Vbat: %5.2f  Flags: %04X",
			   last_housekeeping [0],
			   upt / 10, upt % 10,
			   vbat,
			   last_housekeeping [7] + (last_housekeeping [8] << 8));

	for (x = 0; x < 256; x++) {
		if (client_set [x].packets > 0) {
			bc += sprintf (bc, "\t%d:%llu,%llu", x, client_set [x].packets, client_set [x].bytes);
		}
	}
	bc += sprintf (bc, "\n");

	return send (fd, buf, (bc - buf), MSG_NOSIGNAL);
}


static void service_sockets (fd_set *read_fds)
{
	int	x;

	/* Find the new client (if any). */
	for (x = 0; x < 32; x++) {
		if (FD_ISSET (client_set [x].listen_fd, read_fds)) {
			/* Create a new client. */
			struct client	*c = calloc (1, sizeof (*c));
			struct sockaddr	addr;
			socklen_t	addr_size = sizeof (addr);

			c->fd = accept (client_set [x].listen_fd, &addr, &addr_size);

			/* Make socket non-blocking. */
			fcntl (c->fd, F_SETFL, O_NONBLOCK);

			/* Add the listening handle to the fixed read_fds. */
			FD_SET (c->fd, &fixed_read_fds);
			if (c->fd >= fixed_nfds) {
				fixed_nfds = c->fd + 1;
			}

			/* Insert at the head of the list. */
			INSERT_INTO_LIST (client_set [x].list, c);
		}
	}

	/*  Check if any client have transmitted data - if so just flush it. */
	for (x = 0; x < 32; x++) {
		struct client	*cnext = client_set [x].list;
		struct client	*c = cnext;

		while ((c = cnext)) {
			cnext = c->next;

			if (FD_ISSET (c->fd, read_fds)) {
				char 	buf [4096];
				int	y;

				y = recv (c->fd, &buf, sizeof (buf), 0);
				if (y < 0 && errno != EINTR && errno != EAGAIN) {
					/* Read error. Ditch this user. */
					close (c->fd);
					FD_CLR (c->fd, &fixed_read_fds);
					REMOVE_FROM_LIST (client_set [x].list, c);
					free (c);
				} else if (y == 0) {
					/* EOF. */
					close (c->fd);
					FD_CLR (c->fd, &fixed_read_fds);
					REMOVE_FROM_LIST (client_set [x].list, c);
					free (c);
				}
			}
		}
	}

	/* Check if any client have transmitted data on the control channel. */
	if (FD_ISSET (client_set [260].listen_fd, read_fds)) {
		/* Create a new client. */
		struct client	*c = calloc (1, sizeof (*c));
		struct sockaddr	addr;
		socklen_t	addr_size = sizeof (addr);

		c->fd = accept (client_set [260].listen_fd, &addr, &addr_size);
		if (c->fd >= 0) {
			/* Make socket non-blocking. */
			fcntl (c->fd, F_SETFL, O_NONBLOCK);

			/* Add the real handle to the fixed read_fds. */
			FD_SET (c->fd, &fixed_read_fds);
			if (c->fd >= fixed_nfds) {
				fixed_nfds = c->fd + 1;
			}

			/* Insert at the head of the list. */
			INSERT_INTO_LIST (client_set [260].list, c);
		}
	}

	/* Check if any data is received on the monitor port. */
	{
		struct client	*cnext = client_set [260].list;
		struct client	*c;

		while ((c = cnext)) {
			cnext = c->next;

			if (FD_ISSET (c->fd, read_fds)) {
				char 	buf [4096];
				int	y;

				y = recv (c->fd, &buf, sizeof (buf), 0);
				if (y < 0) {
					if (errno != EINTR && errno != EAGAIN) {
						/* Monitor client disappeared. Close down. */
						close (c->fd);
						FD_CLR (c->fd, &fixed_read_fds);
						REMOVE_FROM_LIST (client_set [260].list, c);
						free (c);
					}
				} else if (y == 0) {
					/* EOF. */
					close (c->fd);
					FD_CLR (c->fd, &fixed_read_fds);
					REMOVE_FROM_LIST (client_set [260].list, c);
					free (c);
				} else if (y > 0) {
					/* Something received. Dump telemetry status. */
					if (dump_telemetry (c->fd) < 0) {
						/* Monitor client disappeared. Close down. */
						close (c->fd);
						FD_CLR (c->fd, &fixed_read_fds);
						REMOVE_FROM_LIST (client_set [260].list, c);
						free (c);
					}
				}
			}
		}
	}
}


void write_socket (int sockno, int length, uint8_t *data)
{
	/* Send the packet to all subscribers of the sockno value. */
	struct client	*cnext = client_set [sockno].list;
	struct client	*c = cnext;
	int		x;

	client_set [sockno].packets++;
	client_set [sockno].bytes += length;

	while ((c = cnext)) {
		cnext = c->next;

		x = send (c->fd, data, length, MSG_NOSIGNAL);
		if (x < 0) {
			/* The socket died. Clean up. */
			close (c->fd);
			FD_CLR (c->fd, &fixed_read_fds);

			/* Remove it from the list. */
			if (c->prev == NULL) {
				/* First element in the list. */
				client_set [sockno].list = c->next;
				if (c->next) {
					c->next->prev = NULL;
				}

				free (c);
			} else {
				/* Not the first element. */
				c->prev->next = c->next;
				if (c->next) {
					c->next->prev = c->prev;
				}

				free (c);
			}
		}
	}
}


/** @brief  Create the contents of the trellis_encoder table.
 */
static void init_trellis_encoder (void)
{
	unsigned int	partab [256];	/* Parity lookup table. */
	unsigned int	w, sr, s, bit, res;
	uint8_t		*vt = trellis_encoder;

	/* Initialize the parity lookup table. */
	for (w = 0; w < 256; w++) {
		partab [w] = 0;
		s = w;
		for (bit = 0; bit < 8; bit++) {
			partab [w] ^= s & 0x01;
			s >>= 1;
		}
	}

	/* Walk through all possible combinations of a 6 bit history + 8 bits of new data. */
	for (w = 0; w < 0x4000; w++) {
		sr = (w >> 8) & 0xFF;		/* Load the shift register with the history part. */
		s = w & 0xFF;			/* Load the new byte into s. */

		res = 0;			/* Clear the result field. */
		for (bit = 0; bit < 8; bit++) {
			sr = (sr << 1) | (s & 0x80 ? 1 : 0);;
			s <<= 1;

			res <<= 1;
			if (partab [sr & 109] >  0) res |= 1;
			res <<= 1;
			if (partab [sr &  79] == 0) res |= 1;	/* Second bit is inverted. */
		}

		/* Store the result. */
		*(vt++) = (res >> 8) & 0xFF;	/* High byte first. */
		*(vt++) = res & 0xFF;		/* followed by low byte. */
	}
}



inline int popcount_8 (unsigned int v)
{
	v = ((v >> 1) & 0x55) + (v & 0x55);
	v = ((v >> 2) & 0x33) + (v & 0x33);
	v = ((v >> 4) & 0x0F) + (v & 0x0F);
	return v;
}

inline int popcount_16 (unsigned int v)
{
	return popcount_8 (v >> 8) + popcount_8 (v);
}

inline int popcount_32 (unsigned int v)
{
	return popcount_16 (v >> 16) + popcount_16 (v);
}

inline int popcount_64 (uint64_t v)
{
	return popcount_32 (v >> 32) + popcount_32 (v);
}



static correlator_t *new_correlator (void)
{
	correlator_t	*new = calloc (sizeof (*new), 1);

	new->state = HUNT;
	new->flag = 0x374FE2DA;

	/* Allocate memory for the viterbi symbol buffer. */
	if (posix_memalign((void**)&new->symbols, 16, RATE*(FRAMEBITS+(K-1))*sizeof(COMPUTETYPE))){
    		printf ("Allocation of symbols array failed\n");
    		exit (1);
  	}

	/* Create the viterbi instance. */
	new->vp = create_viterbi (FRAMEBITS);
	if (! new->vp) {
		printf ("create_viterbi failed\n");
		exit (1);
	}

	return new;
}


/** @brief  Deliver the collected packet.
 * @param[io]  Pointer to the correlator instance.
 */
void deliver_packet (correlator_t *cor)
{
	int		x;
	char		t [100];
	struct timeval	tv;

	/* Print a timestamp. */
	gettimeofday (&tv, NULL);
	strftime (t, sizeof (t), "%F %T", gmtime (&tv.tv_sec));
	printf ("%s.%03ld ", t, tv.tv_usec / 1000);

	printf ("pbit: %5d  flag err: %1d  trellis err: %2u  ", cor->pbit, cor->flag_err, cor->trellis_err);
	printf ("Len: %3d  Len2: %3d  CRC: %04X  ID: %3u", cor->packet_buf [0], cor->packet_buf [1] ^ 0xFF, (cor->packet_buf [cor->packet_len - 2]<<8) | cor->packet_buf [cor->packet_len - 1], cor->packet_buf [2]);
	printf ("  Packet:");
	for (x = 0; x  < cor->packet_len; x++) {
		printf (" %02X", cor->packet_buf [x]);
	}
	printf ("\n");

	/* Deliver data to network socket. */
	write_socket (cor->packet_buf [2], cor->packet_len - 5, &cor->packet_buf [3]);

	if (cor->packet_buf [2] <= 3) {
		/* This is a housekeeping packet. Keep the last one around. */
		memcpy (last_housekeeping, &cor->packet_buf [2], cor->packet_len - 4);
	}
}


/** @brief  Add a sample to the state machine.
 * @param[io]  Pointer to the correlator instance.
 * @param[in]  Sample value. Value set: 0..127, 128..255.
 */
void stuff_sample (correlator_t *cor, unsigned int sample)
{
	switch (cor->state) {
		case INIT:	/* Reset state machines. */
			cor->pbit = 0;
			cor->sr = 0;

			/* Change state and fall down into HUNT mode. */
			cor->state = HUNT;

		case HUNT:	/* Look for a flag. */
			/* Shift the sample into the shift register. */
			cor->sr <<= 1;
			if (sample >= 128) {
				cor->sr |= 1;
			}
			cor->pbit++;
//printf ("HUNT: pbit: %6u  val: %3u  sr: %08X\n", cor->pbit, sample, cor->sr);

			/* Check for flag match. Allow one bit error in the flag. */
			cor->flag_err = popcount_32 (cor->sr ^ cor->flag);
			if ((cor->pbit == 72      && cor->flag_err < 5) ||		/* On time. Accept 4 errors. */
			    ((cor->pbit % 8) == 0 && cor->flag_err < 3) ||		/* On a byte boundary. Accept 2 errors. */
			    (cor->pbit != 76      && cor->flag_err < 1)) {		/* Otherwise need an exact match. */
				// printf ("\nFlag fundet efter %d  ", cor->pbit);
				cor->state = COLLECT_HEAD;
				cor->sampleno = 0;
				cor->total_samples = 0;
				cor->sr = 0;
				cor->raw_buf [0] = 0;
			}
			break;

		case COLLECT_HEAD:	/* Collect samples for interpreting the header. */
			/* Collect samples. */
			if (cor->sampleno & 0x01) {
				/* Invert every second sample. */
				cor->symbols [cor->sampleno] = 255 - sample;
			} else {
				cor->symbols [cor->sampleno] = sample;
			}
			// cor->sr = (cor->sr << 1) | (sample >= 128 ? 1 : 0);
			// cor->raw_buf [cor->sampleno >> 3] = cor->sr & 0xFF;
			cor->raw_buf [(cor->sampleno >> 3) + 0] = (cor->raw_buf [(cor->sampleno >> 3) + 0] << 1) | (sample >= 128 ? 1 : 0);
			cor->raw_buf [(cor->sampleno >> 3) + 1] = 0;
			cor->sampleno++;
//printf ("HEAD: sampleno: %6u  val: %3u\n", cor->sampleno, sample);

			if (cor->sampleno == (5*8 + (K-1)) * RATE) {
				/* Decode the samples to extract the length field. */
				memset (&cor->packet_buf, 0xFF, sizeof (cor->packet_buf));
				init_viterbi (cor->vp, 0);
				update_viterbi_blk_GENERIC (cor->vp, cor->symbols, 5*8 + (K-1));
				chainback_viterbi (cor->vp, cor->packet_buf, 5*8, 0);

				/* The length and the inverted length are stored as the first two bytes. */
				if (cor->packet_buf [0] == (cor->packet_buf [1] ^ 0xFF)) {
					/* Found a valid length. */
					cor->state = COLLECT_ALL;
					cor->packet_len = 5 + cor->packet_buf [0];

					/* Add one padding byte for flushing the trellis encoder. */
					cor->total_samples = RATE * (cor->packet_len + 1) * 8;
//printf ("Header OK: plen: %3d  samples: %5d\n", cor->packet_len, cor->total_samples);
				} else {
					/* Invalid length bytes. GO back to HUNT. */
printf ("Header error: len1: %3d  len2: %3d\n", cor->packet_buf [0], cor->packet_buf [1] ^ 0xFF);
					cor->state = INIT;
				}
			}
			break;

		case COLLECT_ALL:	/* Collect samples for interpreting the entire packet. */
			/* Collect samples. */
			if (cor->sampleno & 0x01) {
				/* Invert every second sample. */
				cor->symbols [cor->sampleno] = 255 - sample;
			} else {
				cor->symbols [cor->sampleno] = sample;
			}
			// cor->sr = (cor->sr << 1) | (sample >= 128 ? 1 : 0);
			// cor->raw_buf [cor->sampleno >> 3] = cor->sr & 0xFF;
			cor->raw_buf [(cor->sampleno >> 3) + 0] = (cor->raw_buf [(cor->sampleno >> 3) + 0] << 1) | (sample >= 128 ? 1 : 0);
			cor->raw_buf [(cor->sampleno >> 3) + 1] = 0;
			cor->sampleno++;
//printf ("ALL:  sampleno: %6u  val: %3u\n", cor->sampleno, sample);

			if (cor->sampleno == cor->total_samples) {
				/* Decode the samples to extract the whole packet. */
				cor->packet_buf [cor->packet_len] = 0;	/* Zero the tralier byte. */
				init_viterbi (cor->vp, 0);
				update_viterbi_blk_GENERIC (cor->vp, cor->symbols, cor->packet_len * 8 + (K-1));
				chainback_viterbi (cor->vp, cor->packet_buf, 8*cor->packet_len, 0);

				/* Re-encode the packet to count the number of errors corrected by the Trellis code. */
				cor->trellis_err = 0;
				/* NOTE: Does not check the last two bytes. */
				{
					unsigned int	x;
					unsigned int	b = 0;
					for (x = 0; x < cor->packet_len - 1; x++) {
						b = ((b << 8) | cor->packet_buf [x]) & 0x3FFF;
						cor->trellis_err += popcount_8 (cor->raw_buf [(x << 1) + 0] ^ trellis_encoder [(b << 1) + 0]);
						cor->trellis_err += popcount_8 (cor->raw_buf [(x << 1) + 1] ^ trellis_encoder [(b << 1) + 1]);
					}

					if (0 && cor->trellis_err) {
						b = 0;
						for (x = 0; x < cor->packet_len - 1; x++) {
							b = ((b << 8) | cor->packet_buf [x]) & 0x3FFF;
							printf ("Byte: %3u  %2X <> %2X => %d\n", (x << 1) + 0, cor->raw_buf [(x << 1) + 0], trellis_encoder [(b << 1) + 0], popcount_8 (cor->raw_buf [(x << 1) + 0] ^ trellis_encoder [(b << 1) + 0]));
							printf ("Byte: %3u  %2X <> %2X => %d\n", (x << 1) + 1, cor->raw_buf [(x << 1) + 1], trellis_encoder [(b << 1) + 1], popcount_8 (cor->raw_buf [(x << 1) + 1] ^ trellis_encoder [(b << 1) + 1]));
						}
					}
				}

				deliver_packet (cor);
				cor->state = INIT;
			}
			break;
	}
}


int main (int argc, char **argv)
{
	uint8_t		input_buffer [65536];
	unsigned int	input_offset = 0;
	int		x;
	int		active_fds;
	correlator_t	*cor = new_correlator ();
	fd_set		read_fds;
	int		nfds;

	/* Ignore SIGPIPE interrupts. */
	signal (SIGPIPE, SIG_IGN);

	init_trellis_encoder ();
	init_sockets ();

	/* Read samples from stdin. */
	while (1) {
		/* Prepare the select. */
		read_fds = fixed_read_fds;
		nfds = fixed_nfds;
		FD_SET (0, &read_fds);

		active_fds = select (nfds, &read_fds, NULL, NULL, NULL);

		if (active_fds == -1) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;	/* Interrupted system call. Just retry. */
			}
			perror ("select: ");
			exit (2);
		}

		/* Service the main input socket. */
		if (FD_ISSET (0, &read_fds)) {
			float	*v;

			x = read (0, &input_buffer [input_offset], sizeof (input_buffer) - input_offset);
			if (x <= 0) break;

			v = (float *)&input_buffer [0];
			x += input_offset;
			while (x >= sizeof (*v)) {
				stuff_sample (cor, *v * 100.0 + 128);	/* Convert from -1..0..+1 format to 0..127,128..255 format. */
				x -= sizeof (*v);
				v++;
			}
			input_offset = 0;
			if (x > 0) {
				/* Partial read of the last value. Move it to the head of the buffer and prepare for the next batch. */
				uint8_t	*p = (void *)v;
				while (x-- > 0) {
					input_buffer [input_offset++] = *(p++);
				}
			}
			active_fds--;
		}

		/* Handle all the other sockets if any may be available. */
		if (active_fds > 0) {
			service_sockets (&read_fds);
		}
	}

	return 0;
}
