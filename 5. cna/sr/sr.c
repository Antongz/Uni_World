#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Go Back N protocol.  Adapted from
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.1  J.F.Kurose

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications (6/6/2008 - CLP): 
   - removed bidirectional GBN code and other code not used by prac. 
   - fixed C style to adhere to current programming style
   (24/3/2013 - CLP)
   - added GBN implementation
**********************************************************************/

#define RTT  15.0       /* round trip time.  MUST BE SET TO 15.0 when submitting assignment */
#define WINDOWSIZE 6    /* Maximum number of buffered unacked packet */
#define SEQSPACE 12      /* min sequence space for SR must be at least windowsize*2 */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver  
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your 
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ ) 
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}

/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE];  /* array for storing packets waiting for ACK */
static int windowlast;    /* array indexes of the first/last packet awaiting ACK */
static int windowcount;                /* the number of packets currently awaiting an ACK */
static int A_nextseqnum;               /* the next sequence number to be used by the sender */

static struct pkt timer[SEQSPACE];   /* packets timer */
static int timercount;

void printBuffer(){
  int i;
  printf("Buffer\t");
  printf("windowcount %i: \t", windowcount);
  for (i=0; i<windowcount; i++){
    printf("|%i:%i|", buffer[i].seqnum,buffer[i].acknum);
  }
  printf("\n");
}

void printTimer(){
  int i;
  printf("Timer\t");
  printf("timercount %i: \t", timercount);
  for (i=0; i<timercount; i++){
    printf("|%i:%i|", timer[i].seqnum,timer[i].acknum);
  }
  printf("\n");
}

int BuffIndexPacket(int num){
  int i;
  for (i=0; i<windowcount; i++){
    if (num == buffer[i].seqnum){
      return i;
    }
  }
  return -1;
}

int dupTimer(int num){
  int i;
  for (i=num+1; i<timercount; i++){
    if (timer[num].seqnum == timer[i].seqnum){
      return 0;
    }
  }
  return -1;
}

void UpdateTimer(){
  struct pkt temp[SEQSPACE];
  int i;

  int j=0;
  for (i=0; i<timercount; i++){
    int index = BuffIndexPacket(timer[i].seqnum);
    if (buffer[index].acknum == -1 && (dupTimer(i) == -1)){

      temp[j] = timer[i];
      j++;
    }
  }

  for (i=0; i<j; i++){
    timer[i] = temp[i];
  }
  timercount = j;
}

int SearchWindowFirst(){ 
  int i;
  for (i = 0; i< windowcount; i++){
    if (buffer[i].acknum == -1){
      return i;
    }
  }
  return -1;
}

void UpdateBuffer(int wf){
    struct pkt temp[WINDOWSIZE];
    int i;  
    for (i=0; i<windowcount; i++){
      temp[i] = buffer[i];
    }

    int j=0;
    for (i=wf; i<windowcount; i++){
      buffer[j] = temp[i];
      j++;
    }
    
    windowcount = j;
    windowlast = j-1;
}

int TimerIndexPacket(int num){
  int i;
  for (i=0; i<timercount; i++){
    if (num == buffer[i].seqnum){
      return i;
    }
  }
  return -1;
}

void addTimer(int num){
  //printBuffer();
  int index = BuffIndexPacket(num);
  int intimer = TimerIndexPacket(num);
  if ((buffer[index].acknum == -1) && (intimer == -1)){
    timer[timercount] = buffer[index];
    timercount = (timercount +1) % SEQSPACE; 
    
  }
  //printTimer();
}

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;

  /* if not blocked waiting on ACK */
  if ( windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    /* create packet */
    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for ( i=0; i<20 ; i++ ) 
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt); 

    /* put packet in window buffer */
    /* windowlast will always be 0 for alternating bit; but not for GoBackN */
    windowlast = (windowlast + 1) % WINDOWSIZE; 
    buffer[windowlast] = sendpkt;
    windowcount++;

    addTimer(A_nextseqnum);

    /* send out packet */
    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3 (A, sendpkt);

    /* start timer if first packet in window */
    if (windowcount == 1)
      starttimer(A,RTT);

    /* get next sequence number, wrap back to 0 */
    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;  
  }
  /* if blocked,  window is full */
  else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

/* called from layer 3, when a packet arrives for layer 4 
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{

  /* if received ACK is not corrupted */ 
  if ((!IsCorrupted(packet))&& (packet.seqnum == 0)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n",packet.acknum);
    total_ACKs_received++;

    int index = BuffIndexPacket(packet.acknum);
    /* check if new ACK or duplicate */
    if (windowcount != 0) {
        if (buffer[index].acknum == -1){ 
            /* packet is a new ACK */
            if (TRACE > 0)
              printf("----A: ACK %d is not a duplicate\n",packet.acknum);
            new_ACKs++;

            /* deliver to receiving application */
            tolayer5(B, packet.payload);

            /* cumulative acknowledgement - determine how many packets are ACKed */
            if (index != -1)
              buffer[index].acknum = 0; 

      /* find the first unACK packt in the window */
            if (buffer[0].acknum == -1) {
            
            } else {
            
            /* delete the acked packets from window buffer */
              int windowfirst = SearchWindowFirst();
            
              if ( windowfirst == -1 ){ 
            
                windowlast = -1;
                windowcount = 0;
            
              } else {
            
                UpdateBuffer(windowfirst);
            
              }
      /* start timer again if there are still more unacked packets in window */
            stoptimer(A);

            if (windowcount > 0)
              starttimer(A, RTT);
            }
          
        }
        else
          if (TRACE > 0)
        printf ("----A: duplicate ACK received, do nothing!\n");
    }
  }
  else{ 
    if (TRACE > 0)
      printf ("----A: corrupted ACK is received, do nothing!\n");
  }
  
  //addTimer(packet.acknum);
  
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  int i;

  if (TRACE > 0)
    printf("----A: time out,resend packets!\n");

    UpdateTimer();

    if (TRACE > 0)
      printf ("---A: resending packet %d\n", timer[0].seqnum);

    tolayer3(A,timer[0]);
    packets_resent++;
    
    starttimer(A,RTT);
  
}       


/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  /* initialise A's window, buffer and sequence number */
  A_nextseqnum = 0;  /* A starts with seq num 0, do not change this */
  windowlast = -1;   /* windowlast is where the last packet sent is stored.  
         new packets are placed in winlast + 1 
         so initially this is set to -1
       */
  windowcount = 0;
  timercount = 0; 
}

/********* Receiver (B)  variables and procedures ************/

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;

  /* if not corrupted and received packet is in order */
  if  ( (!IsCorrupted(packet))  && (packet.acknum == -1) ) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n",packet.seqnum);
    packets_received++;

    /* send an ACK for the received packet */
    sendpkt.seqnum = 0;
        
  }
  else {
    /* packet is corrupted or out of order resend last ACK */
    if (TRACE > 0) 
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
    
    sendpkt.seqnum = 1;
  }

  /* create packet */
  sendpkt.acknum = packet.seqnum;
    
  /* we don't have any data to send.  fill payload with 0's */
  for ( i=0; i<20 ; i++ ) 
    sendpkt.payload[i] = '0';  

  /* computer checksum */
  sendpkt.checksum = ComputeChecksum(sendpkt); 

  //addTimer(packet.seqnum);

  /* send out packet */
  tolayer3 (B, sendpkt);

}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)  
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}