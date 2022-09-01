#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;
bool package_sent = false; // to track whether package was sent to the server or not

/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
  int byte_read; // bytes read when the read() system call is called
  int start_index = 0; // the position at which read will start
  int total_byte_to_read; // total byte to be read

  total_byte_to_read = len;

  // if the socket is not connected, returns false
  if (cli_sd == -1){
    return false;
  }
  while (byte_read != total_byte_to_read){
    byte_read = read(fd, &buf[start_index], len); // byte read from read system call
    len = len - byte_read; // len is now decremented to make sure we read from the proper byte next time
    start_index = byte_read; // the position is incremented to the last byte that was read
  }
  return true;

}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
  int byte_written; // bytes written when the write() system call is called
  int start_index = 0; // the position at which write will start
  int total_byte_to_write; // total byte to be write

  total_byte_to_write = len;

  // if the socket is not connected, returns false
  if (cli_sd == -1){
    return false;
  }
  while (byte_written != total_byte_to_write){
    byte_written = write(fd, &buf[start_index], len);  // byte written from read system call
    len = len - byte_written; // len is now decremented to make sure we write from the proper byte next time
    start_index = byte_written; // the position is incremented to the last byte that was written
  }
  return true;
}

/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the return value of the server side calling the corresponding jbod_operation function.
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/

static bool recv_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block) {
  uint8_t temp_buf[HEADER_LEN]; // temporary buffer of packet size
  uint32_t t_op; // temporay op to hold the value of op
  uint16_t t_ret; // temporay return to hold the value returned
  uint16_t len;

  // if package was not sent, returns false
  if (package_sent == false){
    return false;
  }
  // reads the first "HEADER_LEN" bytes
  nread(sd,HEADER_LEN,temp_buf);
  
  // copies the returned values of op, ret and len to the variable created
  memcpy(&len,&temp_buf[0],2);
  memcpy(&t_op,&temp_buf[2],4);
  memcpy(&t_ret,&temp_buf[6],2);

  // converting the value of t_op, t_ret and len to host byte and setting it to the parameters provided 
  *op = ntohl(t_op); // uint32_t --> use ntohl()
  *ret  = ntohs(t_ret);
  len = ntohs(len);


  // if len = header_len + block size, read the block
  if (len == HEADER_LEN + 256){
    // if read was unsuccessful, returns false
    if (nread(sd, 256, block) != true){
      return false;
    }
  }

  // the package sent is set to false, cause we recieved the packet already
  package_sent = false;
  return true;

}



/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/

static bool send_packet(int sd, uint32_t op, uint8_t *block) {

  uint8_t temp_buf[HEADER_LEN]; // temporary buffer of packet size 
  int command; // the command of JBOD
  uint16_t len; 

  // deconstruct op to determine the command
  command = op >> 26;

  // len is the packet length
  len = HEADER_LEN;

  // if the command is JBOD_WRITE_BLOCK, we add the block size to the len variable
  if (command == 5){
    len = HEADER_LEN + 256;
  }

  // converting the len and op to network byte
  len = htons(len);
  op = htonl(op); // uint32_t --> use htonl()

  // insert the packet to the temp_buf
  memcpy(temp_buf, &len, 2);
  memcpy(temp_buf+2, &op, 4);


  // sends the packet to the server
  nwrite(sd, HEADER_LEN, temp_buf);

  // if command is JBOD_WRITE_BLOCK, we send the block too
  if (command == 5){
    // if sending the block was unsuccessful, return false
    if (nwrite(sd ,256, block) != true){
      return false;
    }
  }

  // the package_sent is set to true after sending the packet to the server
  package_sent = true;
  return true;

}


/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
  struct sockaddr_in addr;

  // creating the socket
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);
  if (cli_sd == -1){
    return false;
  }

  // setting up variables for connection
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_aton(ip, &addr.sin_addr) == 0){
    return false;
  }

  // connecting socket to the server
  if (connect(cli_sd, (const struct sockaddr *)&addr, sizeof(addr)) == -1){
    return false;
  }
  
  // connection successful
  return true;  
}




/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  // close the socket
  close(cli_sd);

  // set the socket to -1
  cli_sd = -1;
}



/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/


int jbod_client_operation(uint32_t op, uint8_t *block) {

  uint16_t return_val; // the value returned from the server (0 or -1)

  // we send the packet | if unsuccessful, returns false
  if (send_packet(cli_sd, op, block) == false){
    return -1;
  }
  // we receive the packet | if unsuccessful, returns false
  if (recv_packet(cli_sd, &op, &return_val, block) == false){
    return -1;
  }
  if (return_val == 0){
    return -1;
  }
  // returns the value returned from the server
  return return_val;
}

