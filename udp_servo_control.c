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
#include <pthread.h>

// Set the port on which to listen for UDP packets.
#define UDP_PORT 2031
// Set which servo outputs are used
#define THROTTLE_SERVO 0
#define RUDDER_SERVO 1
// Set the microsecond pulse lengths corresponding to throttle and rudder
// positions
#define THROTTLE_MIN_PULSE_LENGTH_USEC 900
#define THROTTLE_MAX_PULSE_LENGTH_USEC 2100
#define RUDDER_MIN_PULSE_LENGTH_USEC 900
#define RUDDER_MAX_PULSE_LENGTH_USEC 2100
// Set the pulse rate
#define SERVO_PULSE_RATE_HZ 50
// Set the timeout - zero the demands after this many seconds without receiving
// a packet.
#define COMMS_TIMEOUT_SEC 5


// Additional calculated defines
#define THROTTLE_RANGE_USEC (THROTTLE_MAX_PULSE_LENGTH_USEC - THROTTLE_MIN_PULSE_LENGTH_USEC)
#define RUDDER_RANGE_USEC (RUDDER_MAX_PULSE_LENGTH_USEC - RUDDER_MIN_PULSE_LENGTH_USEC)
#define RUDDER_CENTRE_PULSE_LENGTH_USEC ((RUDDER_MAX_PULSE_LENGTH_USEC-RUDDER_MIN_PULSE_LENGTH_USEC) / 2.0 + RUDDER_MIN_PULSE_LENGTH_USEC)

// Globals and mutex for passing throttle and rudder info between threads
double throttle, rudder = 0.0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// interrupt handler to catch ctrl-c
static int running = 0;
static void __signal_handler(__attribute__ ((unused)) int dummy) {
    running = 0;
    return;
}

// Comms thread. Receiving UDP packets is handled here. This is forked off from
// the main code, which continues running to handle the servos.
void *commsThread() {
    // Create UDP socket, exit on failure
    int udpsocket;
    if ((udpsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        fprintf(stderr,"create socket failed\n");
        return NULL;
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
        return NULL;
    }
    struct timeval timeout;
    timeout.tv_sec = COMMS_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    if (setsockopt(udpsocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        fprintf(stderr,"configure socket failed 2\n");
        return NULL;
    }

    // Bind socket, exit on failure
    if (bind(udpsocket, (struct sockaddr*) &listener, sizeof(listener)) < 0) {
        fprintf(stderr,"bind socket failed\n");
        return NULL;
    }

    // Listen indefinitely until the program is stopped
    while (running) {
        // Read UDP packet, timeout after 5 seconds
        printf("Waiting for packet...\n");
        char buffer[20] = {0};
        struct sockaddr_in sender;
        unsigned int senderlen = sizeof(sender);
        bzero(&sender, sizeof(sender));
        int nbytes = recvfrom(udpsocket, buffer, sizeof(buffer), 0, (struct sockaddr*) &sender, &senderlen);

        double tmpThrottle = 0.0, tmpRudder = 0.0;
        if (nbytes > 0) {
            // Parse message
            char *eptr;
            buffer[nbytes] = '\0';
            char delim[] = ",";
            char *ptr = strtok(buffer, delim);
            tmpThrottle = strtod(ptr, &eptr);
            ptr = strtok(NULL, delim);
            tmpRudder = strtod(ptr, &eptr);

            printf("Received demand: Throttle %f Rudder %f\n", tmpThrottle, tmpRudder);
        } else {
            printf("No bytes received, zeroing outputs\n");
        }

        // Write to global variables to pass the new demands to the other thread
        pthread_mutex_lock(&mutex);
        throttle = tmpThrottle;
        rudder = tmpRudder;
        pthread_mutex_unlock(&mutex);
    }

    // Close the socket
    close(udpsocket);
    return NULL;
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
    while (rc_adc_batt()<6.0) {
        printf("Battery disconnected or insufficiently charged to drive servos, waiting until connected...\n");
        rc_usleep(5000000);
    }
    rc_adc_cleanup();

    // initialize PRU
    if(rc_servo_init()) return -1;

    // turn on power
    printf("Turning On 6V Servo Power Rail\n");
    rc_servo_power_rail_en(1);

    // Zero outputs at startup
    printf("Zero output\n");
    rc_servo_send_pulse_us(THROTTLE_SERVO, THROTTLE_MIN_PULSE_LENGTH_USEC);
    rc_servo_send_pulse_us(RUDDER_SERVO, RUDDER_CENTRE_PULSE_LENGTH_USEC);
    rc_usleep(2000000);

    // Spin off a new thread for the UDP socket listening
    pthread_t udp_socket_thread;
    pthread_create(&udp_socket_thread, NULL, commsThread, NULL);

    // Control servos indefinitely until the program is stopped
    while (running) {
        // Get global variables
        pthread_mutex_lock(&mutex);
        double tmpThrottle = throttle;
        double tmpRudder = rudder;
        pthread_mutex_unlock(&mutex);

        // Calculate outputs
        double throttle_usec = THROTTLE_MIN_PULSE_LENGTH_USEC;
        if (tmpThrottle >= 0 && tmpThrottle <= 100.0) {
            throttle_usec = (tmpThrottle / 100.0 * THROTTLE_RANGE_USEC) + THROTTLE_MIN_PULSE_LENGTH_USEC;
        } else {
            printf("Throttle demand out of range\n");
        }
        double rudder_usec = RUDDER_CENTRE_PULSE_LENGTH_USEC;
        if (tmpRudder >= -100.0 && tmpRudder <= 100.0) {
            rudder_usec = (tmpRudder / 200.0 * RUDDER_RANGE_USEC) + RUDDER_CENTRE_PULSE_LENGTH_USEC;
        } else {
            printf("Rudder demand out of range\n");
        }

        // Set outputs
        //printf("Sending servo pulse: Ch%d %f, Ch%d %f\n", THROTTLE_SERVO, throttle_usec, RUDDER_SERVO, rudder_usec);
        rc_servo_send_pulse_us(THROTTLE_SERVO, throttle_usec);
        rc_servo_send_pulse_us(RUDDER_SERVO, rudder_usec);

        rc_usleep(1000000/SERVO_PULSE_RATE_HZ);
    }

    // Wait for comms thread to finish
    pthread_join(udp_socket_thread, NULL);

    // Zero outputs
    rc_servo_send_pulse_us(THROTTLE_SERVO, THROTTLE_MIN_PULSE_LENGTH_USEC);
    rc_servo_send_pulse_us(RUDDER_SERVO, RUDDER_CENTRE_PULSE_LENGTH_USEC);

    // Turn off power rail & clean up
    rc_usleep(50000);
    rc_servo_power_rail_en(0);
    rc_servo_cleanup();
    return 0;
}
