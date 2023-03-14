// Beaglebone Blue UDP Throttle/Heading Servo Control
// by Ian Renton, March 2023
// https://github.com/ianrenton/beaglebone-blue-udp-servo-control
// Based on the Servo example from the Beaglebone Robot Control Library.
//
// Receives UDP packets containing throttle and rudder demands, and sets
// servo outputs accordingly.
//
// Packets are expected to have ASCII contents of the form X,Y where X is
// a number between 0 and 100 to set the throttle percentage, and Y is a
// number between -100 and 100 to set the rudder percentage (negative to
// port).
//
// On startup and if no packets are received for a certain amount of time,
// the controls will be zeroed.
//
// If using this for yourself, you may need to customise the #define values
// near the top of the file to reflect the UDP port and servo control outputs
// you want to use.
//
// May need to be run as root for proper hardware control.

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <rc/time.h>
#include <rc/adc.h>
#include <rc/servo.h>

// Set the port on which to listen for UDP packets.
#define UDP_PORT 2031
// Set which servo outputs are used
#define THROTTLE_SERVO 0
#define RUDDER_SERVO 1
// Set the timeout - zero the demands after this many seconds without receiving
// a packet.
#define COMMS_TIMEOUT_SEC 5


// interrupt handler to catch ctrl-c
static int running = 0;
static void __signal_handler(__attribute__ ((unused)) int dummy) {
    running = 0;
    return;
}

// Function to zero outputs
void zero_output() {
    printf("zero output\n");
    // todo zero output
    rc_usleep(2000000);
}

int main()  {
    // Set up interrupt handler
    signal(SIGINT, __signal_handler);
    running = 1;

    // Read ADC to make sure battery is connected
    if (rc_adc_init()) {
        fprintf(stderr,"ERROR: failed to run rc_adc_init()\n");
        return -1;
    }
    if (rc_adc_batt()<6.0) {
        fprintf(stderr,"ERROR: battery disconnected or insufficiently charged to drive servos\n");
// todo re-enable       return -1;
    }
    rc_adc_cleanup();

    // initialize PRU
    if(rc_servo_init()) return -1;

    // turn on power
    printf("Turning On 6V Servo Power Rail\n");
// todo re-enable    rc_servo_power_rail_en(1);

    // Zero outputs at startup
    zero_output();

    // Create UDP socket, exit on failure
    int udpsocket;
    if ((udpsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        fprintf(stderr,"create socket failed\n");
        return -1;
    }

    // Configure socket
    struct sockaddr_in listener;
    bzero(&listener, sizeof(listener));
    listener.sin_family      = AF_INET;
    listener.sin_port        = htons(UDP_PORT);
    listener.sin_addr.s_addr = htonl(INADDR_ANY);
    int reuse = 1;
    if (setsockopt(udpsocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        fprintf(stderr,"configure socket failed 1\n");
        return -1;
    }
    struct timeval timeout;
    timeout.tv_sec = COMMS_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    if (setsockopt(udpsocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        fprintf(stderr,"configure socket failed 2\n");
        return -1;
    }

    // Bind socket, exit on failure
    if (bind(udpsocket, (struct sockaddr*) &listener, sizeof(listener)) < 0) {
        fprintf(stderr,"bind socket failed\n");
        return -1;
    }

    // Main run loop
    while (running) {
        // Read UDP packet, timeout after 5 seconds
        printf("Waiting for packet...\n");
        char buffer[20] = {0};
        struct sockaddr_in sender;
        unsigned int senderlen = sizeof(sender);
        bzero(&sender, sizeof(sender));
        int nbytes = recvfrom(udpsocket, buffer, sizeof(buffer), 0, (struct sockaddr*) &sender, &senderlen);

        // If timed out, zero outputs
        if (nbytes < 0) {
            printf("No bytes received\n");
            zero_output();
            continue;
        }

        // Parse message
        double throttle, rudder;
        char *eptr;
        buffer[nbytes] = '\0';
        char delim[] = ",";
        char *ptr = strtok(buffer, delim);
        throttle = strtod(ptr, &eptr);
        ptr = strtok(NULL, delim);
        rudder = strtod(ptr, &eptr);

        printf("Throttle: %f\n", throttle);
        printf("Rudder: %f\n", rudder);
        
        // Set outputs
        // todo
    }

    // Turn off power rail, clean up & close sockets
    rc_usleep(50000);
    rc_servo_power_rail_en(0);
    rc_servo_cleanup();
    close(udpsocket);
    return 0;
}
