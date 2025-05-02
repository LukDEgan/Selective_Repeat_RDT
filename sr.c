#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Go Back N protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications:
   - removed bidirectional GBN code and other code not used by prac.
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/

#define RTT \
  16.0 /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE                                   \
  6 /* the maximum number of buffered unacked packet \
      MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE                                                        \
  (WINDOWSIZE * 2)    /* the min sequence space for SR must be at least \
                       windowsize_sender + windowsize_receiver */
#define NOTINUSE (-1) /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender
   and receiver the simulator will overwrite part of your packet with 'z's.  It
   will not overwrite your original checksum.  This procedure must generate a
   different checksum to the original if the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet) {
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for (i = 0; i < 20; i++) checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet) {
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}

/********* Sender (A) variables and functions ************/

static struct pkt
    a_window[WINDOWSIZE]; /* array for storing packets waiting for ACK */
bool a_acked[SEQSPACE];   /* array of bools of length sequence number, where it
                             tells which one is waiting on ack*/
static int a_windowfirst,
    a_windowlast; /* array indexes of the first/last packet awaiting ACK */
static int
    unacked_packet_count; /* the number of packets currently awaiting an ACK */
static int A_nextseqnum; /* the next sequence number to be used by the sender */

/* called from layer 5 (application layer), passed the message to be sent to
 * other side */
void A_output(struct msg message) {
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on ACK */
  if (unacked_packet_count < WINDOWSIZE) {
    if (TRACE > 1)
      printf(
          "----A: New message arrives, send window is not full, send new "
          "messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++) sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    /* put packet in window buffer */
    /* windowlast will always be 0 for alternating bit; but not for GoBackN */
    a_windowlast = (a_windowlast + 1) % WINDOWSIZE;
    a_window[a_windowlast] = sendpkt;
    unacked_packet_count++;
    a_acked[A_nextseqnum] = false;

    /* send out packet */
    if (TRACE > 0) printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);

    /* start timer if first packet in window */
    if (unacked_packet_count == 1) starttimer(A, RTT);

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
  }
  /* if blocked,  window is full */
  else {
    if (TRACE > 0) printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet) {
  int ackseq;
  bool window_slid;
  /* if received ACK is not corrupted */
  if (!IsCorrupted(packet)) {
    if (TRACE > 0) {
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    }
    total_ACKs_received++;
    /* check if new ACK or duplicate */
    if (unacked_packet_count != 0) {
      int seqfirst = a_window[a_windowfirst].seqnum;
      int seqlast = a_window[a_windowlast].seqnum;
      /* check case when seqnum has and hasn't wrapped */
      if (((seqfirst <= seqlast) &&
           (packet.acknum >= seqfirst && packet.acknum <= seqlast)) ||
          ((seqfirst > seqlast) &&
           (packet.acknum >= seqfirst || packet.acknum <= seqlast))) {
        /* packet is a new ACK */
        if (TRACE > 0)
          printf("----A: ACK %d is not a duplicate\n", packet.acknum);
        new_ACKs++;

        /* selective repeat doesn't have cumulative acks, need to see if ack
         * packet acks a waiting unacked packet (say that three times over lol)
         */

        ackseq = packet.acknum;
        if (!a_acked[ackseq]) {
          a_acked[ackseq] = true; /*packet has been acked, set to true*/
          unacked_packet_count--;
        }
        /* in selective repeat, the window slides up the first unacked packet*/
        window_slid = false;
        while (a_acked[a_window[a_windowfirst].seqnum]) {
          a_acked[a_window[a_windowfirst].seqnum] =
              false; /*setting this to false is the same as sliding the window
                        over*/
          a_windowfirst = (a_windowfirst + 1) % WINDOWSIZE;
          window_slid = true;
        }
        /* start timer again if the oldest is acked (the window mustve slid) or
           if the oldest times out
         */

        if (window_slid) {
          stoptimer(A);
          if (unacked_packet_count > 0) {
            starttimer(A, RTT);  // Start timer for the new oldest packet
          }
        }

        /* otherwise, if window didnt slide, oldest hasnt been acked, continue
              like normal. */
      }
    } else if (TRACE > 0)
      printf("----A: duplicate ACK received, do nothing!\n");
  } else if (TRACE > 0)
    printf("----A: corrupted ACK is received, do nothing!\n");
}

/* called when A's timer goes off */
void A_timerinterrupt(void) {
  int seq = a_window[(a_windowfirst) % WINDOWSIZE].seqnum;
  if (TRACE > 0) printf("----A: time out,resend packets!\n");

  if (TRACE > 0)
    printf("---A: resending packet %d\n",
           a_window[(a_windowfirst) % WINDOWSIZE].seqnum);

  tolayer3(A, a_window[(a_windowfirst) % WINDOWSIZE]);
  packets_resent++;
  starttimer(A, RTT);
}

/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void) {
  int i;
  /* initialise A's window, buffer and sequence number */
  A_nextseqnum = 0; /* A starts with seq num 0, do not change this */
  a_windowfirst = 0;
  a_windowlast = -1; /* windowlast is where the last packet sent is stored.
                   new packets are placed in winlast + 1
                   so initially this is set to -1
                 */
  unacked_packet_count = 0;

  for (i = 0; i < SEQSPACE; i++)
    a_acked[i] = 3; /*initially, all packets are not acked and set to 3*/
}

/********* Receiver (B)  variables and procedures ************/
static int b_windowbase; /* the base sequence number for the recv window */
static int
    B_nextseqnum; /* the sequence number for the next packets sent by B */
static struct pkt b_window[WINDOWSIZE]; /*the receiver's window*/
static bool
    b_acked[SEQSPACE]; /*an array keeping track of what has been acked by b*/

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet) {
  struct pkt sendpkt;
  int i;
  int upper_bound;
  bool inwindow;
  int sequencenum = packet.seqnum;
  /* if not corrupted, check if its within the window, if it
   * is, and its not a duplicate, buffer it*/
  if (!IsCorrupted(packet)) {
    if (TRACE > 0) {
      printf("----B: packet %d is correctly received, send ACK!\n",
             packet.seqnum);
    }
    packets_received++;
    /* check if its in the window*/
    upper_bound = (b_windowbase + WINDOWSIZE) % SEQSPACE;
    inwindow = false;
    if (b_windowbase < upper_bound) { /* no wrap around, easy check*/
      if (sequencenum >= b_windowbase && sequencenum < upper_bound) {
        inwindow = true;
      }
    } else /*wrap around*/ {
      if (sequencenum >= b_windowbase || sequencenum < upper_bound) {
        inwindow = true;
      }
    }
    /*if its in the window and its new then buffer*/
    if (inwindow && !b_acked[sequencenum]) {
      b_acked[sequencenum] = true;
      b_window[sequencenum] = packet;
    }
    /*send ack to sender even if its not in order*/
    sendpkt.acknum = sequencenum;
    sendpkt.seqnum = B_nextseqnum;
    B_nextseqnum = (B_nextseqnum + 1) % 2;
    /* we don't have any data to send.  fill payload with 0's */
    for (i = 0; i < 20; i++) sendpkt.payload[i] = '0';
    /* slide window to next non-received packet */
    while (b_acked[b_windowbase]) {
      /*  deliver to receiving application */
      tolayer5(B, b_window[b_windowbase].payload);
      b_acked[b_windowbase] = false; /* reset */
      b_windowbase = (b_windowbase + 1) % SEQSPACE;
    }
    /* packet is corrupted or out of order resend last ACK */
  } else {
    if (TRACE > 0) {
      printf(
          "----B: packet corrupted or not expected sequence number, resend "
          "ACK!\n");
    }
  }

  /* create packet */
  sendpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % 2;

  /* computer checksum */
  sendpkt.checksum = ComputeChecksum(sendpkt);

  /* send out packet */
  tolayer3(B, sendpkt);
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void) {
  int i;
  B_nextseqnum = 1;
  b_windowbase = 0;
  for (i = 0; i < SEQSPACE; i++) {
    b_acked[i] = false;
  }
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message) {}

/* called when B's timer goes off */
void B_timerinterrupt(void) {}
