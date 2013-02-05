/* A tftp client implementation.
   Author: Erik Nordström <erikn@it.uu.se>
*/
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>

#include "tftp.h"

extern int h_errno;

#define TFTP_TYPE_GET 0
#define TFTP_TYPE_PUT 1

/* Should cover most needs */
#define MSGBUF_SIZE (TFTP_DATA_HDR_LEN + BLOCK_SIZE)


/*
 * NOTE:
 * In tftp.h you will find definitions for headers and constants. Make
 * sure these are used throughout your code.
 */


/* A connection handle */
struct tftp_conn
{
    int type; /* Are we putting or getting? */
    FILE *fp; /* The file we are reading or writing */
    int sock; /* Socket to communicate with server */
    int blocknr; /* The current block number */
    char *fname; /* The file name of the file we are putting or getting */
    char *mode; /* TFTP mode */
    struct sockaddr_in peer_addr; /* Remote peer address */
    socklen_t addrlen; /* The remote address length */
    char msgbuf[MSGBUF_SIZE]; /* Buffer for messages being sent or received */
};

/* Close the connection handle, i.e., delete our local state. */
void tftp_close(struct tftp_conn *tc)
{
    if (!tc)
        return;

    fclose(tc->fp);
    close(tc->sock);
    free(tc);
}

/* Connect to a remote TFTP server. */
struct tftp_conn *tftp_connect(int type, char *fname, char *mode,
                               const char *hostname)
{
    struct addrinfo hints;
    struct addrinfo * res = NULL;
    struct tftp_conn *tc;

    if (!fname || !mode || !hostname)
        return NULL;

    tc = malloc(sizeof(struct tftp_conn));

    if (!tc)
        return NULL;

    /* Create a socket.
     * Check return value. */

    /* ... */

    if ((tc->sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
        {
            fprintf(stderr, "Could not create socket!\n");
            return NULL;
        }


    if (type == TFTP_TYPE_PUT)
        tc->fp = fopen(fname, "rb");
    else if (type == TFTP_TYPE_GET)
        tc->fp = fopen(fname, "wb");
    else
        {
            fprintf(stderr, "Invalid TFTP mode, must be put or get\n");
            return NULL;
        }

    if (tc->fp == NULL)
        {
            fprintf(stderr, "File I/O error!\n");
            close(tc->sock);
            free(tc);
            return NULL;
        }

    memset(&hints,0,sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    char port_str[5];
    sprintf(port_str, "%d", TFTP_PORT);

    if (getaddrinfo(hostname, port_str, &hints, &res))
        {
            fprintf(stderr, "Couldn't get host address info!\n");
            close(tc->sock);
            fclose(tc->fp);
            free(tc);  
            return NULL;
        }

    /* get address from host name.
     * If error, gracefully clean up.*/

    /* ... */

    /* Assign address to the connection handle.
     * You can assume that the first address in the hostent
     * struct is the correct one */

    memcpy(&tc->peer_addr, res->ai_addr, res->ai_addrlen);

    tc->addrlen = sizeof(struct sockaddr_in);

    tc->type = type;
    tc->mode = mode;
    tc->fname = fname;
    tc->blocknr = 0;

    memset(tc->msgbuf, 0, MSGBUF_SIZE);
    
    
    printf("Connection opened. \n");

    return tc;
}

/*
  Send a read request to the server.
  1. Format message.
  2. Send the request using the connection handle.
  3. Return the number of bytes sent, or negative on error.
 */
int tftp_send_rrq(struct tftp_conn *tc)
{
    /* struct tftp_rrq *rrq; */

    int reqlen = TFTP_RRQ_LEN(tc->fname, tc->mode);

    struct tftp_rrq *rrq;
    
    if((rrq = malloc(reqlen)) == NULL)
        return -1;

    rrq->opcode = htons(OPCODE_RRQ);

    strcpy (&rrq->req[0], tc->fname);
    strcpy (&rrq->req[strlen(tc->fname) + 1], tc->mode);

    // Save the message in the msgbuffer
    memcpy(tc->msgbuf, rrq, reqlen);

    size_t size = sendto(tc->sock, rrq, reqlen, 0, (struct sockaddr *) &tc->peer_addr, tc->addrlen);
    
    free(rrq);
    
    return size;
}
/*

  Send a write request to the server.
  1. Format message.
  2. Send the request using the connection handle.
  3. Return the number of bytes sent, or negative on error.
 */
int tftp_send_wrq(struct tftp_conn *tc)
{

    int reqlen = TFTP_WRQ_LEN(tc->fname, tc->mode);

    struct tftp_wrq *wrq;
    if((wrq = malloc(reqlen)) == NULL)
        return -1;

    wrq->opcode = htons(OPCODE_WRQ);

    strcpy (&wrq->req[0], tc->fname);
    strcpy (&wrq->req[strlen(tc->fname) + 1], tc->mode);

    // Save the message in the msgbuffer
    memcpy(tc->msgbuf, wrq, reqlen);

    size_t size = sendto(tc->sock, wrq, reqlen, 0, (struct sockaddr *) &tc->peer_addr, tc->addrlen);
    
    printf("SENT WRQ\n");
    
    free(wrq);
    
    return size;
}


/*
  Acknowledge reception of a block.
  1. Format message.
  2. Send the acknowledgement using the connection handle.
  3. Return the number of bytes sent, or negative on error.
 */
int tftp_send_ack(struct tftp_conn *tc)
{
    struct tftp_ack *ack;
    if((ack = malloc(TFTP_ACK_HDR_LEN)) == NULL)
        return -1;
    
    ack->opcode = htons(OPCODE_ACK);
    ack->blocknr = htons(tc->blocknr);
    
    memcpy(tc->msgbuf, ack, TFTP_ACK_HDR_LEN);    

    size_t size = sendto(tc->sock, ack, TFTP_ACK_HDR_LEN, 0, (struct sockaddr *) &tc->peer_addr, tc->addrlen);
    
    free(ack);
    
    return size;
}

/*
  Send a data block to the other side.
  1. Format message.
  2. Add data block to message according to length argument.
  3. Send the data block message using the connection handle.
  4. Return the number of bytes sent, or negative on error.

  TIP: You need to be able to resend data in case of a timeout. When
  resending, the old message should be sent again and therefore no
  new message should be created. This can, for example, be handled by
  passing a negative length indicating that the creation of a new
  message should be skipped.
 */
int tftp_send_data(struct tftp_conn *tc, int length)
{
	//TODO: Det här borde snyggas upp, ska length vara med eller utan headern?
    struct tftp_data *tdata;
	int length_real = abs(length);
	int	dataplen = TFTP_DATA_HDR_LEN + BLOCK_SIZE;
	
	if((tdata = malloc(dataplen)) == NULL)
		return -1;
	
    if (length < 0) {

		dataplen = length_real;
		
		/* Resend old data block */
		printf("Resending .. \n");
		memcpy(tdata, tc->msgbuf, dataplen);
	
	} else {
		/* Create new data block */
		printf("Not resending.. \n");
		tc->blocknr++;
		
		tdata->opcode = htons(OPCODE_DATA);
		tdata->blocknr = htons(tc->blocknr);
		
		length_real = read(fileno(tc->fp), tdata->data, length_real);
		
		printf("Updated length: %d \n", length_real);
		
		/* Recalculate the package length in case we only was able to
		 * read less than 'length_real' bytes from the file */
		dataplen = TFTP_DATA_HDR_LEN + length_real;
		memcpy(tc->msgbuf, tdata, dataplen);
	}
    
    size_t size = sendto(tc->sock, tdata, dataplen, 0, (struct sockaddr *) &tc->peer_addr, tc->addrlen);
    
    printf("Sent %d bytes of data \n",size);
    
    free(tdata);
    
    return size;
}

int tftp_send_error(struct tftp_conn *tc, int errcode) {

    struct tftp_err *err;
    
    char * errmsg = tftp_err_to_str(errcode);
    
    size_t errlen = TFTP_ERR_HDR_LEN + strlen(errmsg) + 1;
    
    if((err = malloc(errlen)) == NULL)
        return -1;
        
    err->opcode = htons(OPCODE_ERR);
    err->errcode = htons(errcode);
    strcpy(&err->errmsg[0],errmsg);
        
    memcpy(tc->msgbuf, err, errlen);
    
    size_t size = sendto(tc->sock, err, errlen, 0, (struct sockaddr *) &tc->peer_addr, tc->addrlen);  

    free(err);
    
    return size;

}

/*
  Transfer a file to or from the server.

 */
int tftp_transfer(struct tftp_conn *tc)
{
    int retval = 0;
    int len;
    int reclen;
    int totlen = 0;
    int terminate = 0;
    
    struct timeval timeout;


        /* Sanity check */
    if (!tc)
        return -1;
        
        
    fd_set sfd;
    char recbuf[MSGBUF_SIZE];

    FD_ZERO(&sfd);
    FD_SET(tc->sock, &sfd);



    len = BLOCK_SIZE + TFTP_DATA_HDR_LEN;
    reclen = 0;

    /* After the connection request we should start receiving data
     * immediately */

    /* Set a timeout for resending data. */

    timeout.tv_sec = TFTP_TIMEOUT;
    timeout.tv_usec = 0;

    /* Check if we are putting a file or getting a file and send
     * the corresponding request. */

    if (tc->type == TFTP_TYPE_GET)
        {
            /* Send read request */
            if(tftp_send_rrq(tc) < 0)
                fprintf(stderr,"FAIL TO SEND RRQ\n");

        }
    else if (tc->type == TFTP_TYPE_PUT)
        {
            /* Send write request */
             if(tftp_send_wrq(tc) < 0)
                fprintf(stderr,"FAIL TO SEND WRQ\n");

        }
    else
        {
            return -1;
        }

    
    /*
      Put or get the file, block by block, in a loop.
     */
    do
        {
            /* 1. Wait for something from the server (using
             * 'select'). If a timeout occurs, resend last block
             * or ack depending on whether we are in put or get
             * mode. */

            /* ... */    
            printf("Waiting for response... \n");
            
                FD_ZERO(&sfd);
                FD_SET(tc->sock, &sfd);

            switch (select(tc->sock + 1, &sfd, NULL, NULL, &timeout))
                {
                case (-1):
                    fprintf(stderr, "\nselect()\n");
                    break;
                case (0): 
					/* Timeout, reinit the counter and do a resend.
					 * Nested switch-case statemens are awesome! */
					
					printf("**** TIMEOUT *****\n");
					timeout.tv_sec = TFTP_TIMEOUT;
					timeout.tv_usec = 0;
                    
                    switch (ntohs(((u_int16_t*) tc->msgbuf)[0]))
                        {
                        case OPCODE_RRQ:
                            tftp_send_rrq(tc);
                            continue;
                        case OPCODE_WRQ:
                            tftp_send_wrq(tc);
                            continue;
                        case OPCODE_DATA:
                            tftp_send_data(tc, -len);
                            continue;
                        case OPCODE_ACK:
                            tftp_send_ack(tc);
                            continue;
                        case OPCODE_ERR:
                            //TODO: Vilka error-medelanden ska skickas om, om några?
                            continue;
                        default:
                            fprintf(stderr, "\nThis shouldn't happend\n");
                            goto out;
                        }
                    break;
                default:
                    //TODO: Använda recvfrom() istället och kolla efter felaktig source port.
                    
                    /* Save the recieved bytes in 'rec_len' so we
                     * can check if we should terminate the transfer */
                     
                    printf("GOT SOMETHING!!!!\n");
                    reclen = read(tc->sock, recbuf, MSGBUF_SIZE);
                    printf("%d\n", ntohs(((u_int16_t*) recbuf)[0]));
                    break;
                }
               
            /* 2. Check the message type and take the necessary
             * action. */
            switch (ntohs(((u_int16_t*) recbuf)[0]))
                {
                case OPCODE_DATA:
                    /* Received data block, send ack */
                    //TODO: Skriv datan till en fil
                  
                    if (tc->type == TFTP_TYPE_PUT) {
						fprintf(stderr, "\nExpected ack, got data\n");
						goto out;
					}
					
					if (ntohs(((u_int16_t*) tc->msgbuf)[1]) != (tc->blocknr + 1))
						{
							fprintf(stderr, "\nGot unexpected data block§ nr\n");
							goto out;
						}
					
					tc->blocknr++;
					
					/* If we are getting and recieved a data package with
                     * a block of < 512, we want to terminate the loop
                     * after getting sending an ack */
					if (reclen < (TFTP_DATA_HDR_LEN + BLOCK_SIZE))
						terminate = 1;
					
					tftp_send_ack(tc); //TODO: Kolla returvärdet					
                    
                    break;
                case OPCODE_ACK:
                    printf("Received ACK, send next block\n");
                        
                    if (tc->type == TFTP_TYPE_GET) {
						fprintf(stderr, "\nExpected data, got ack\n");
						goto out;
					}
                    
                    /* If we are putting and sent a data package with
                     * a block of < 512 bytes last time, we want to
                     * terminate the loop after getting the final ack */
					if (len < (TFTP_DATA_HDR_LEN + BLOCK_SIZE)) {
						terminate = 1;
						printf("We're done sending, let's terminate\n");
						  }       
					else {
						/* Save the numer of sent bytes in 'len' in case
						* it's < 512 and the package has to be resent. */
						len = tftp_send_data(tc, BLOCK_SIZE); 
						printf("We sent a packet of length %d\n",len);
						}
                                
                    break;
                case OPCODE_ERR:
                    /* Handle error... */
                    break;
                default:
                    fprintf(stderr, "\nUnknown message type\n");
                    goto out;

                }

        	totlen += (len + reclen);
			
        }
    while (!terminate);
    
    
    if (tc->type == TFTP_TYPE_GET && terminate)
		{
			/* The loop terminated succesfully but the last ack might
			 * have been lost. Wait for a possible duplicate data
			 * package and in that case send the ack one more time */
            timeout.tv_sec = TFTP_TIMEOUT;
			timeout.tv_usec = 0;   
			 
			if (select(1, &sfd, NULL, NULL, &timeout) > 0)
				{
					read(tc->sock, tc->msgbuf, MSGBUF_SIZE);
						if (ntohs(((u_int16_t *) tc->msgbuf)[0]) == OPCODE_DATA
								&& ntohs(((u_int16_t*) tc->msgbuf)[1]) == tc->blocknr)
							{
									tftp_send_ack(tc);
							}
				}			
		}
			
	

    printf("\nTotal data bytes sent/received: %d.\n", totlen);
out:
    fclose(tc->fp);
    return retval;
}

int main (int argc, char **argv)
{

    char *fname = NULL;
    char *hostname = NULL;
    char *progname = argv[0];
    int retval = -1;
    int type = -1;
    struct tftp_conn *tc;

    /* Check whether the user wants to put or get a file. */
    while (argc > 0)
        {

            if (strcmp("-g", argv[0]) == 0)
                {
                    fname = argv[1];
                    hostname = argv[2];

                    type = TFTP_TYPE_GET;
                    break;
                }
            else if (strcmp("-p", argv[0]) == 0)
                {
                    fname = argv[1];
                    hostname = argv[2];

                    type = TFTP_TYPE_PUT;
                    break;

                }
            argc--;
            argv++;
        }

    /* Print usage message */
    if (!fname || !hostname)
        {
            fprintf(stderr, "Usage: %s [-g|-p] FILE HOST\n",
                    progname);
            return -1;
        }

    /* Connect to the remote server */
    tc = tftp_connect(type, fname, MODE_NETASCII, hostname);

    if (!tc)
        {
            fprintf(stderr, "Failed to connect!\n");
            return -1;
        }

    /* Transfer the file to or from the server */
    retval = tftp_transfer(tc);

    if (retval < 0)
        {
            fprintf(stderr, "File transfer failed!\n");
        }

    /* We are done. Cleanup our state. */
    tftp_close(tc);

    return retval;
}
