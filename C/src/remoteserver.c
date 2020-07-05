#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <wiringPi.h>
#include <softPwm.h>
#include <netdb.h>
#include <netinet/in.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/time.h>

#include <signal.h>
#include <stdarg.h>
#include <getopt.h>

#include "rpihw.h"
#include "pwm_common.h"
#include "lights.h"
#include "motordriver.h"
#include "remoteserver.h"
#include "audio.h"
#include "clk.h"
#include "gpio.h"
#include "dma.h"
#include "pwm.h"

#define BLOCK_SIZE (4*1024)
#define ARRAY_SIZE(stuff)       (sizeof(stuff) / sizeof(stuff[0]))

// defaults for cmdline options
#define SERVO_STEP              50
#define SERVO_1_CENTER          1450 // L/R - original: 1140
#define SERVO_2_CENTER          1350 // U/D - original: 630
#define SERVO_1_MIN             500
#define SERVO_1_MAX             2300
#define SERVO_2_MIN             400
#define SERVO_2_MAX             2000
#define DISTANCE_BEEP_DELAY     400000

int baseSpeed, addLeftSpeed, addRightSpeed;
static int speedVal_1 = 5000;
static int speedVal_2 = 5000;
static int speedVal_3 = 5000;
static int speedVal_4 = 5000;

static int angleA = SERVO_1_CENTER;
static int angleB = SERVO_2_CENTER;

struct motionstate carstate = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static unsigned char disWarning  = 0;
static unsigned char poweroffFlag = 0;
static unsigned char turnLeftFlag = 0;
static unsigned char turnBackFlag = 0;
static unsigned char servoBeep = 0;
unsigned char buf[IR_LIMITS];  // bytes buffer
unsigned int bits;  // 32768 bits capable
unsigned char done;
int sockfd, newsockfd, portno, clilen;
unsigned char client_Connected = 0;
unsigned char count;

unsigned long receive_colour_table[4] =
{
  0xFF0000,  // green
  0x00FF00,  // red
  0x0000FF,  // blue
  0xFFFF00,  //yellow
};

int mem_fd;
void *gpio_map;
// I/O access
volatile unsigned *gpio;
int clear_on_exit = 0;

const rpi_hw_t *my_rpi_hw;

/* 
 * Creates a server socket and listens for a command from the remote.
 */
int main(int argc, char *argv[])
{
  usleep(10);
  char buffer[BUFFER_SIZE]; 
  struct sockaddr_in serv_addr, cli_addr;
  struct sigaction sa;
  int  n, pulsenum, count ;
  int on = 1;
  static unsigned long previous_time = 0;
  static unsigned long now_time = 0;
  static unsigned long time_stamp = 0;
  static int getColIndex = 0;
  static  unsigned char readNowTime = 1;
  signal(SIGINT, INThandler);
  /* Initialise GPIO */
  if (ControllerInit() < 0) return -1;
  setup_io();
  ultraInit();
  servoInit();
  trackModeInit();
  beepInit();
  irInit();
  myPWMInit();
  GRBInit();
  pthread_t t1, t2;
  //create two threads
  pthread_create(&t1, NULL, thread_distance_servo, NULL);
  pthread_create(&t2, NULL, thread_tracking_avoidance_ir, NULL);
  for (pulsenum = 0; pulsenum < 10; pulsenum++) {
    servoCtrl(servo_1, SERVO_1_CENTER); // orig: 1140
    servoCtrl(servo_2, SERVO_2_CENTER); // orig: 630
  }
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("ERROR opening socket");
    exit(1);
  }
  /* Initialize socket structure */
  bzero((char *) &serv_addr, sizeof(serv_addr));
  portno = 2001;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portno);

  /* Add support port reuse */
  if ((setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)
  {
    perror("setsockopt failed");
    exit(EXIT_FAILURE);
  }
  
  /* Now bind the host address using bind() call.*/
  if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    perror("ERROR on binding");
    exit(1);
  }
 
  /* Add support reconnection */
  sa.sa_handler = SIG_IGN;
  sigaction( SIGPIPE, &sa, 0 );
  /* Now start listening for the clients, here process will
    go in sleep mode and will wait for the incoming connection
  */
  BEEP_OPEN();
  while (1) {
    listen(sockfd, 5);
    clilen = sizeof(cli_addr);
    printf("Waiting for a connection...\r\n");
    client_Connected = 0;
    clearFlag();
    stop();
    /* Accept actual connection from the client */
    newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, (void *) &clilen);
    if (newsockfd < 0) {
      perror("ERROR on accept");
      //exit(1);
    }
    /* If connection is established then start communicating */
    printf("connected successfully\r\n");
    sayWow();

    sleep(0.5);
    GRB_work(3, getColor(), getBrightness());
    n = write(newsockfd, "{\"version\":2}", 13);
    client_Connected = 1;
    sleep(0.001);
    bzero(&buffer, BUFFER_SIZE);
    while ((n = read(newsockfd, &buffer, BUFFER_SIZE)) > 0)
    {
        int i=0;
        printf("Command Received: 0x");
        for (i=0; i<n; i++) {
            printf("%.2x", buffer[i]);
        }
        printf("\n");
        // --- SPEED ---
        if (buffer[0] == 's') { //0x73
            baseSpeed = buffer[1];
            addLeftSpeed = buffer[2];
            addRightSpeed = buffer[3];
            speedVal_1 = 10000 * ((buffer[1] + buffer[2]) / 255.0);
            speedVal_3 = 10000 * ((buffer[1] + buffer[2]) / 255.0);
            speedVal_2 = 10000 * ((buffer[1] + buffer[3]) / 255.0);
            speedVal_4 = 10000 * ((buffer[1] + buffer[3]) / 255.0);
        // --- LED COLOR ---
        } else if (buffer[0] == 'c') { //0x63
            setColor((buffer[1] << 16) | (buffer[2] << 8) | buffer[3]);
            setBrightness (buffer[4]);
            GRB_work(3, getColor(), getBrightness());
            if(readNowTime){
                readNowTime = 0;
                previous_time = get_pwm_timestamp();    
            }    
            now_time = get_pwm_timestamp();
            time_stamp = now_time - previous_time;
            if (time_stamp <2000000) {
                printf("set current direction\n");
                receive_colour_table[getColIndex] = getColor();
            } else {
                readNowTime = 1;
                printf("set next direction\n");
                getColIndex = (1+getColIndex)%4;
                receive_colour_table[getColIndex] = getColor();
                previous_time = get_pwm_timestamp();
            }  
        // --- VERSION QUERY ---
        } else if (buffer[0] == 'v') {
            printf("Reveive value %d\n", buffer[0]);
            write(newsockfd, "{\"version\":2}", 13);
        }
        else {
            for(count = 0; count <n; count ++){
                printf("receive data %d\r\n",buffer[count]);
            }
        // --- SCRATCH COMMAND ---
            if(buffer[0]==0xFF && buffer[1] == 0x55){
                for (count = 2; count < n; count ++) {
                    PhaseScratchCmd(buffer[count]);
                }
        // --- REMOTE CONTROL COMMANDS ---
            } else {
                for (count = 0; count < n; count ++) {
                      updateCarState(buffer[count]);
                      updateCarMotion();
                }
            }
        }
        bzero(&buffer, BUFFER_SIZE);
    }
    stop();
    clearFlag();
    if (n < 0) {
        printf("Connection exception!\n");
        //break;
    }
    saySad();
  }
  client_Connected = 0;
  close(sockfd);
  printf("ERROR\r\n");
  return 0;
}

void *thread_distance_servo(void *arg) {
  unsigned char cnt = 0;
  float dis = 0, temp_min = 0, temp_max = 0, temp_value = 0, temp = 0;
  while (1) {
    usleep(1);
    temp_min  = temp_max;
    temp_value = temp_max;
    for (cnt = 0; cnt < 3; cnt++ ) {
      temp = disMeasure();
      usleep(1);
      if (temp_max < temp) temp_max = temp;
      if (temp_min > temp) temp_min = temp;
      temp_value += temp;
    }
    dis = (temp_value - temp_max - temp_min);
    temp_value = 0; temp_min = 0; temp_max = 0; temp_value = 0; temp = 0;
    if (dis > 0 && dis <= 50) {
      disWarning = 1;
      if (carstate.forward) {
        if (!(carstate.autoAvoid)) {
          stop();
        }

      }
    } else disWarning = 0;

    if (!poweroffFlag) {
        //beepWarning();
    }
    if(servoBeep){
        servoBeep = 0;
        //digitalWrite(BEEP, HIGH);
        //delay(100);
        //digitalWrite(BEEP, LOW);
        sayAngry();
    }
  }
}

void *thread_tracking_avoidance_ir(void *arg) {
  int IRVal = 0;
  while (1) {
    usleep(1);
    if (carstate.trackenable) {
      printf("Come in track mode \n");
      trackModeWork();
    }
    if (carstate.autoAvoid) {
      avoidance();
    }
    if (done == 1) {
      done = 0;
      IRVal = buf[2]; IRVal = IRVal << 8;
      IRVal = IRVal | buf[3];
      IR_updateCarState(IRVal);
      IR_updateCarMotion();
      IRVal = 0;
    }
    mySoftPwmWrite1(speedVal_1);
    mySoftPwmWrite2(speedVal_2);
    mySoftPwmWrite3(speedVal_3);
    mySoftPwmWrite4(speedVal_4);

    // This updates the LEDs when the car is disconnected
    //if (!poweroffFlag)  getLedSta(disWarning, client_Connected);
  }

}

int updateCarMotion(void) {
  char direction[16];
  strcpy(direction, "stop");

  if (carstate.forward && !carstate.back) {
    if ((!carstate.left && !carstate.right) ||
        (carstate.left && carstate.right)) {
      strcpy(direction, "forward");
      go_forward();
    } else if (carstate.left && !carstate.right) {
      strcpy(direction, "forward left");
      go_forward_left();
    } else if (carstate.right && !carstate.left) {
      strcpy(direction, "forward right");
      go_forward_right();
    } else {
      strcpy(direction, "stop");
      stop();
    }
  } 
  else if (carstate.back && !carstate.forward) {
    if ((!carstate.left && !carstate.right) ||
        (carstate.left && carstate.right)) {
      strcpy(direction, "back");
      go_back();

    } else if (carstate.left && !carstate.right) {
      strcpy(direction, "back left");
      go_back_left();
    } else if (carstate.right && !carstate.left) {
      strcpy(direction, "back right");
      go_back_right();
    } else {
      strcpy(direction, "stop");
      stop();
    }
  } else if (carstate.left && !carstate.right) {
    strcpy(direction, "left");
    go_left();
  } else if (carstate.right && !carstate.left) {
    strcpy(direction, "right");
    go_right();
  } else if( carstate.stop){
      carstate.stop = 0;
      stop();
  }

  if (carstate.servoLeft) {
    carstate.servoLeft = 0;
    strcpy(direction, "servo_left");
    if (angleA < SERVO_1_MAX)
        angleA = angleA + SERVO_STEP;
    else{
        angleA = SERVO_1_MAX;
        servoBeep = 1;
        //BEEP_OPEN();
    }
    servoCtrl(servo_1,  angleA);
    printf("angleA = %d\n", angleA);
  } 
  if (carstate.servoRight) {
    carstate.servoRight = 0;
    strcpy(direction, "servo_right");
    if (angleA > SERVO_1_MIN)
        angleA = angleA - SERVO_STEP;
    else{  
        angleA = SERVO_1_MIN;
        servoBeep =1;    
    }
    servoCtrl(servo_1,  angleA);
    printf("angleA = %d\n", angleA);
  } 
  if (carstate.servoUp) {
    carstate.servoUp = 0;
    strcpy(direction, "servo_UP");
    if (angleB > SERVO_2_MIN)
        angleB = angleB - SERVO_STEP;
    else{
        angleB = SERVO_2_MIN;
        servoBeep =1;     
    }
    servoCtrl(servo_2,  angleB);
     
    printf("angleB = %d\n", angleB);
  } else if (carstate.servoDown) {
    carstate.servoDown = 0;
    strcpy(direction, "servo_down");
    if (angleB < SERVO_2_MAX)
        angleB = angleB + SERVO_STEP;
    else{
        angleB = SERVO_2_MAX;
        servoBeep =1;
    }
    servoCtrl(servo_2,  angleB);
    printf("angleB = %d\n", angleB);
  } 
  printf("Motion: %s\n", direction);
  return 0;
}

void  clearFlag(void) {
  carstate.left = 0;
  carstate.forward = 0;
  carstate.right = 0;
  carstate. stop = 0;
  carstate. back = 0;
  carstate.servoLeft = 0;
  carstate. servoRight = 0;
  carstate. servoUp = 0;
  carstate. servoDown = 0;
  carstate. speedUp = 0;
  carstate. speedDown = 0;
  carstate. autoAvoid = 0;

}

int IR_updateCarMotion(void) {
  static int angleA = 1140;
  static int angleB = 630;
  char direction[16];
  if (carstate.forward) {
    strcpy(direction, "forward");
    go_forward();
    printf("Motion: %s \n", direction);
  }
  if (carstate.back) {
    strcpy(direction, "back");
    go_back();
    printf("Motion: %s \n", direction);
  }
  if (carstate.left) {
    strcpy(direction, "left");
    go_left();
    printf("Motion: %s \n", direction);
  }

  if (carstate.right) {
    strcpy(direction, "right");
    go_right();
    printf("Motion: %s \n", direction);
  }
  if (carstate.servoLeft) {
    carstate.servoLeft = 0;
    strcpy(direction, "servo_left");
    if (angleA < 2300)
      angleA = angleA + SERVO_STEP;
    else
      angleA = 2300;
    servoCtrl(servo_1,  angleA);
    printf("angleA = %d\n", angleA);
  }
  if (carstate.servoRight) {
    carstate.servoRight = 0;
    strcpy(direction, "servo_right");
    if (angleA > 300)
      angleA = angleA - SERVO_STEP;
    else
      angleA = 300;
    servoCtrl(servo_1,  angleA);
    printf("angleA = %d\n", angleA);
  }
  if (carstate.servoUp) {
    carstate.servoUp = 0;
    strcpy(direction, "servo_UP");
    if (angleB > 300)
      angleB = angleB - SERVO_STEP;
    else
      angleB = 300;
    servoCtrl(servo_2,  angleB);
    printf("angleB = %d\n", angleB);
  }
  if (carstate.servoDown) {
    carstate.servoDown = 0;
    strcpy(direction, "servo_down");
    if (angleB < 1160)
      angleB = angleB + SERVO_STEP;
    else
      angleB = 1160;
    servoCtrl(servo_2,  angleB);

    printf("angleB = %d\n", angleB);
  }
  return 0;
}

int IR_updateCarState(int command) {

  if ( command == IR_up || command == IR_up_v2) {
    if (!disWarning)
      carstate.forward = 1;
    else
      carstate.forward = 0;
  }
  if ( command == IR_down || command == IR_down_v2) {
    carstate.back = 1;
  }
  if ( command == IR_Left || command == IR_Left_v2) {
    carstate.left = 1;
  }
  if ( command == IR_right || command == IR_right_v2) {
    carstate.right = 1;
  }
  if ( command == IR_stop || command == IR_stop_v2) {
    carstate.forward = 0;
    carstate.back = 0;
    carstate.right = 0;
    carstate.left = 0;
    stop();
    printf("Motion: stop \n" );
  }
  if ( command == IR_speed_up) {
    carstate.speedUp = 1;
  }
  if ( command == IR_speed_down) {
    carstate.speedDown = 1;
  }
  if ( command == IR_servo_up) {
    carstate.servoUp = 1;
  }
  if ( command == IR_servo_down) {
    carstate.servoDown = 1;
  }
  if ( command == IR_servo_left) {
    carstate.servoLeft = 1;
  }
  if ( command == IR_servo_right) {
    carstate.servoRight = 1;
  }
  if ( command == IR_track) {
    carstate.trackenable = 1;
  }
  if ( command == IR_track_stop) {
    carstate.trackenable = 0;
    stop();
    printf("Motion: stop \n" );
  }
  return 0;
}

int PhaseScratchCmd(char command){
    static int angleA = 1140;
    static int angleB = 630;

    switch (command) {
    case 1: // go forward
      go_forward();
      GRB_work(3, receive_colour_table[2], getBrightness());
      break;
    case 2: //go backward
      if (!disWarning) {
        go_back();
        GRB_work(3, receive_colour_table[0], getBrightness());
      }
    else
        stop();
      carstate.trackenable = 0;
      carstate.autoAvoid = 0;
      break;
    case 3: //go left
      go_left();
      GRB_work(3, receive_colour_table[3], getBrightness());
      carstate.trackenable = 0;
      carstate.autoAvoid = 0;
      break;
    case 4: //go right
      go_right();
      GRB_work(3, receive_colour_table[1], getBrightness());
      carstate.trackenable = 0;
      carstate.autoAvoid = 0;
      break;
    case 5: //stop
      stop();
      carstate.trackenable = 0;
      carstate.autoAvoid = 0;
      break;
    case 7: /* servo left */
       if (angleA < 2300)
          angleA = angleA + SERVO_STEP;
       else
          angleA = 2300;
       servoCtrl(servo_1,  angleA);
     break;
    case 8: /*servo right */
      if (angleA > 300)
      angleA = angleA - SERVO_STEP;
     else
      angleA = 300;
     servoCtrl(servo_1,  angleA);
     break;
    case 9: /* servo up */
      if (angleB > 300)
          angleB = angleB - SERVO_STEP;
      else
          angleB = 300;
      servoCtrl(servo_2,  angleB);
    break;
    case 10: /* servo down */
     if (angleB < 1160)
          angleB = angleB + SERVO_STEP;
     else
      angleB = 1160;
     servoCtrl(servo_2,  angleB);
    break;
    case 11: /* enable track */
      carstate.trackenable = 1;
      break;
    case 12: /* disable track */
      carstate.trackenable = 0;
      break;
    case 13: 
      carstate.speedUp = 1;
      break;
    case 14: 
      carstate.speedDown = 1;
      break;
    case 15: /* honk start */
      digitalWrite(BEEP, HIGH);
      break;
    case 16: /* honk end */
      digitalWrite(BEEP, LOW);
      break;
    case 17: /*automatic avoidance*/
      carstate.autoAvoid = 1;
      break;
    case 18: /*stop automatic avoidance*/
      carstate.autoAvoid = 0;
      break;
    case 19: /*turn off the robot car*/
      poweroffFlag = 1;
      exit_UCTRONICS_Robot_Car();
      printf("power off\n");
      system("sudo poweroff");
      break;
  }
  return 0;
}

/*
 * Updates the struct MotionState of the car.
 */
 int updateCarState(char command) {
  switch (command) {
    case 0: /* left */
      carstate.left = 1;
      GRB_work(3, receive_colour_table[2], getBrightness());
      break;
    case 1: /* up */
      if (!disWarning) {
        carstate.forward = 1;
        GRB_work(3, receive_colour_table[0], getBrightness());
      } else {
        carstate.forward = 0;
      }
      carstate.trackenable = 0;
      carstate.autoAvoid = 0;
      break;
    case 2: /* right */
      carstate.right = 1;
      GRB_work(3, receive_colour_table[3], getBrightness());
      carstate.trackenable = 0;
      carstate.autoAvoid = 0;
      break;
    case 3: /* down */
      carstate.back = 1;
      GRB_work(3, receive_colour_table[1], getBrightness());
      carstate.trackenable = 0;
      carstate.autoAvoid = 0;
      break;
    case 4: /* stop left */
      carstate.left = 0;
      carstate.trackenable = 0;
      carstate.autoAvoid = 0;
      break;
    case 5: /* stop*/
      carstate.forward = 0;
      carstate.back = 0;
      carstate.left = 0;
      carstate.right = 0;
      carstate.trackenable = 0;
      carstate.autoAvoid = 0;
      carstate. stop = 1;
      break;
    case 6: /* stop right */
      carstate.right = 0;
      carstate.trackenable = 0;
      carstate.autoAvoid = 0;
      break;
    case 7: /* servo left */
      carstate.servoLeft = 1;
      break;
    case 8: /*servo right */
      carstate.servoRight = 1;
      break;
    case 9: /* servo up */
      carstate.servoUp = 1;
      break;
    case 10: /* servo down */
      carstate.servoDown = 1;
      break;
    case 11: /* enable track */
      carstate.trackenable = 1;
      break;
    case 12: /* disable track */
      carstate.trackenable = 0;
      break;
    case 13: /* speed up */
      carstate.speedUp = 1;
      break;
    case 14: /* speed down */
      carstate.speedDown = 1;
      break;
    case 15: /* honk start */
      //  carstate.beepenable = 1;
      // digitalWrite(BEEP, HIGH);
      sayRandom();
      break;
    case 16: /* honk end */
      //  carstate.beepenable = 0;
      digitalWrite(BEEP, LOW);
      break;
    case 17: /*automatic avoidance*/
      carstate.autoAvoid = 1;
      break;
    case 18: /*stop automatic avoidance*/
      carstate.autoAvoid = 0;
      break;
    case 19: /*turn off the robot car*/
      poweroffFlag = 1;
      exit_UCTRONICS_Robot_Car();
      printf("power off\n");
      system("sudo poweroff");
      break;
    case 20: /* center camera */
      printf("center camera\n");
      centerServos();
      break;
  }
  return 0;
}

void centerServos(void)
{
    int pulsenum;

    angleA = SERVO_1_CENTER;
    angleB = SERVO_2_CENTER;
    for (pulsenum = 0; pulsenum < 10; pulsenum++) {
    servoCtrl(servo_1, SERVO_1_CENTER); // orig: 1140
    servoCtrl(servo_2, SERVO_2_CENTER); // orig: 630
    }
}

void ultraInit(void)
{
  pinMode(Echo, INPUT);
  pinMode(Trig, OUTPUT);
}

float disMeasure(void)
{
  struct timeval tv1;
  struct timeval tv2;
  long start, stop;
  float dis;
  long waitCount = 0;

  digitalWrite(Trig, LOW);
  delayMicroseconds(2);

  digitalWrite(Trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(Trig, LOW);
  waitCount = 0;
  while (!(digitalRead(Echo) == 1)) {
    if (++waitCount >= 5000)
      break;
    else sleep(0.001);
  }
  gettimeofday(&tv1, NULL);
  waitCount = 0;
  while (!(digitalRead(Echo) == 0)) {
    if (++waitCount >= 5000)
      break;
    else sleep(0.001);
  }
  gettimeofday(&tv2, NULL);
  /*
    int gettimeofday(struct timeval *tv, struct timezone *tz);
    The functions gettimeofday() and settimeofday() can get and set the time as well as a timezone.
    The use of the timezone structure is obsolete; the tz argument should normally be specified as NULL.
  */
  start = tv1.tv_sec * 1000000 + tv1.tv_usec;
  stop  = tv2.tv_sec * 1000000 + tv2.tv_usec;

  dis = (float)(stop - start) / 1000000 * 34000 / 2;

  return dis;
}

void trackModeInit() {
  INP_GPIO(leftSensor);
  GPIO_PULL = 1 << leftSensor;
  INP_GPIO(middleSensor);
  GPIO_PULL = 1 << middleSensor;
  INP_GPIO(rightSensor);
  GPIO_PULL = 1 << rightSensor;
}

void beepInit() {
  pinMode(BEEP, OUTPUT);
  digitalWrite(BEEP, LOW);
}

void beepWarning() {
  static unsigned long previous_time = 0;
  static unsigned long now_time = 0;
  static unsigned long time_stamp = 0;
  static unsigned char flag = 0;
  static unsigned char canStopBeep = 0;
  if (disWarning) {
    canStopBeep = 1;
    if (!flag) {
      flag = 1;
      previous_time = get_pwm_timestamp();
    }
    now_time = get_pwm_timestamp();
    time_stamp = now_time - previous_time;
    if (time_stamp > 0 && time_stamp <= 50000 ) { //1/2T
//      digitalWrite(BEEP, HIGH);
//      GRB_work(3, 0, 0);
    }
    if (time_stamp > 50000 && time_stamp <= 2 * 50000 ) { //1T
//      digitalWrite(BEEP, LOW);
//      GRB_work(3, getColor(), getBrightness());
    }
    if (time_stamp > 2 * 50000) {
      flag = 0;
    }
  } else if (canStopBeep) {
    canStopBeep = 0;
//    digitalWrite(BEEP, LOW);
//    GRB_work(3, getColor(), getBrightness());
  }
}

void trackModeWork() {
  int num1 = 0, num2 = 0, num3 = 0;
  while (1) {
    if (!(carstate.trackenable))break;

    mySoftPwmWrite1(speedVal_1);
    mySoftPwmWrite2(speedVal_2);
    mySoftPwmWrite3(speedVal_3);
    mySoftPwmWrite4(speedVal_4);

    num1 = GET_GPIO(leftSensor);
    num2 = GET_GPIO(middleSensor);
    num3 = GET_GPIO(rightSensor);
    if ((num2 == 0) && (num1 == 0) && (num3 == 0)) {
      stop(); continue;
    } else if ( (num1 == 0) && num3) { //go to left
      GRB_work(3, receive_colour_table[0], 100 ) ;
      go_forward_left();
      while (1) {
        GRB_work(3, receive_colour_table[0], 100 ) ;
        num2 = GET_GPIO(middleSensor);
        mySoftPwmWrite1(speedVal_1);
        mySoftPwmWrite2(speedVal_2);
        mySoftPwmWrite3(speedVal_3);
        mySoftPwmWrite4(speedVal_4);
        if (num2) {
          if (!(carstate.trackenable))break;
          if (disWarning) {
            stop();
          }
          else {
            GRB_work(3, receive_colour_table[1], 100 ) ;
            go_forward_left();
          }
          continue;
        } else
          break;
      }
    } else if ((num3 == 0) && num1) { // go to right
      go_forward_right();
      while (1) {
        num2 = GET_GPIO(middleSensor);
        mySoftPwmWrite1(speedVal_1);
        mySoftPwmWrite2(speedVal_2);
        mySoftPwmWrite3(speedVal_3);
        mySoftPwmWrite4(speedVal_4);
        if (num2) {
          if (!(carstate.trackenable))break;
          if (disWarning) {
            stop();
          }
          else {
              GRB_work(3, receive_colour_table[2], 100 ) ;
            go_forward_right();
          }
          continue;
        } else
          break;
      }
    } else if (disWarning) {
      stop();
    }
    else {
      go_forward();
    }
  }
}

unsigned char countLow(void)
{
  unsigned char i = 0;
  while ( digitalRead(IRIN) == 0 ); // wait
  while ( digitalRead(IRIN) == 1 ) {
    ++i;
    delayMicroseconds(26);
    if (i == 0) return 0; // timeout
  }
  return i;
}

void getIR() {
  unsigned char i;
  unsigned short j;   // capable 32768 bits = 4096 bytes
  unsigned char k;
  bits = 0;
  i = countLow();
  for (j = 0; j < IR_LIMITS; j++) { // buffer bytes LIMITS
    for (i = 0; i < 8; i++) { // 8 bits
      k = countLow();
      if (k == 0) {
        buf[j] >>= (8 - i);
        done = 1; //printf("  \n");
        return;
      }
      buf[j] >>= 1;
      buf[j] += ((k > 30) ? 0x80 : 0);
      ++bits;
    }
  }
  done = 1;
}

void turn() {
  static unsigned long previous_time = 0;
  static unsigned long now_time = 0;
  static unsigned long time_stamp = 0;
  static unsigned char flag = 0;
  if (!flag) {
    flag = 1;
    previous_time = get_pwm_timestamp();
  }
  now_time = get_pwm_timestamp();
  time_stamp = now_time - previous_time;
  if (time_stamp > 0 && time_stamp <= turnTime ) { //1/2T
    go_back();
    turnBackFlag = 1;
  }
  if (time_stamp > turnTime && time_stamp <= 2 * turnTime ) { //1T
    go_left();
    turnLeftFlag = 1;
  }
  if (time_stamp > 2 * turnTime) {
    stop();
    flag = 0;
    turnLeftFlag = 0;
    turnBackFlag = 0;
  }
}

void avoidance(void)
{
  while (1) {
    if (!(carstate.autoAvoid)) {
      stop();
      break;
    }
    mySoftPwmWrite1(speedVal_1);
    mySoftPwmWrite2(speedVal_2);
    mySoftPwmWrite3(speedVal_3);
    mySoftPwmWrite4(speedVal_4);
    if (disWarning || turnLeftFlag||turnBackFlag) {
      turn();
    } else {
      printf("Go forward\n");
      go_forward();
    }
  }
}

void BEEP_OPEN (void) {
//  digitalWrite(BEEP, HIGH);
//  delay(500);
//  digitalWrite(BEEP, LOW);
    sayHello();
}

// Set up a memory regions to access GPIO
void setup_io()
{
 my_rpi_hw = rpi_hw_detect();

  /* open /dev/mem */
  if ((mem_fd = open("/dev/mem", O_RDWR | O_SYNC) ) < 0) {
    printf("can't open /dev/mem \n");
    exit(-1);
  }
  /* mmap GPIO */
  gpio_map = mmap(
               NULL,             //Any adddress in our space will do
               BLOCK_SIZE,       //Map length
               PROT_READ | PROT_WRITE, // Enable reading & writting to mapped memory
               MAP_SHARED,       //Shared with other processes
               mem_fd,           //File to map
               my_rpi_hw->periph_base + 0x200000//GPIO_BASE         //Offset to GPIO peripheral
             );

  close(mem_fd); //No need to keep mem_fd open after mmap

  if (gpio_map == MAP_FAILED) {
    printf("mmap error %d\n", (int)gpio_map);//errno also set!
    exit(-1);
  }
  // Always use volatile pointer!
  gpio = (volatile unsigned *)gpio_map;
} // setup_io



void servoInit(void)
{
  INP_GPIO(servo_1); // must use INP_GPIO before we can use OUT_GPIO
  OUT_GPIO(servo_1);
  INP_GPIO(servo_2); // must use INP_GPIO before we can use OUT_GPIO
  OUT_GPIO(servo_2);
}

void servoCtrl(int servoNum, int dutyCycle)
{
    int count =0;
    for(count = 0; count < 3; count ++){
        GPIO_SET = 1 << servoNum;
        delayMicroseconds(dutyCycle);
        GPIO_CLR = 1 << servoNum;
        delayMicroseconds(25000 - dutyCycle);
    }
}

void irInit() {
  pinMode(IRIN, INPUT);
  pullUpDnControl(IRIN, PUD_UP);
  // to detect falling edge
  wiringPiISR(IRIN, INT_EDGE_FALLING, (void*)getIR);
  done = 0;
}

void myPWMInit() {
  // Switch GPIO 7..11 to output mode
  INP_GPIO(MOTOR_1_PWM); // must use INP_GPIO before we can use OUT_GPIO
  OUT_GPIO(MOTOR_1_PWM);
  INP_GPIO(MOTOR_2_PWM); // must use INP_GPIO before we can use OUT_GPIO
  OUT_GPIO(MOTOR_2_PWM);
  INP_GPIO(MOTOR_3_PWM); // must use INP_GPIO before we can use OUT_GPIO
  OUT_GPIO(MOTOR_3_PWM);
  INP_GPIO(MOTOR_4_PWM); // must use INP_GPIO before we can use OUT_GPIO
  OUT_GPIO(MOTOR_4_PWM);
}

void mySoftPwmWrite1(int value)
{
  static int previous_time = 0;
  static int now_time = 0;
  static int time_stamp = 0;
  static  unsigned char flag1 = 0;

  if (!flag1) {
    flag1 = 1;
    previous_time = get_pwm_timestamp();
  }
  now_time = get_pwm_timestamp();
  time_stamp = now_time - previous_time;
  if (time_stamp > 0 && time_stamp <= halfPWMPeriod ) { //1/2T
    if (time_stamp <= value ) {
      GPIO_SET = 1 << MOTOR_1_PWM;
    }
    else {
      GPIO_CLR = 1 << MOTOR_1_PWM;
    }
  }
  if (time_stamp > halfPWMPeriod && time_stamp <= 2 * halfPWMPeriod ) { //1T
    if (time_stamp <= value ) {
      GPIO_SET = 1 << MOTOR_1_PWM;
    }
    else {
      GPIO_CLR = 1 << MOTOR_1_PWM;
    }
  }
  if (time_stamp > 2 * halfPWMPeriod) {
    flag1 = 0;
  }
}

void mySoftPwmWrite2(int value)
{
  static int previous_time = 0;
  static int now_time = 0;
  static int time_stamp = 0;
  static  unsigned char flag2 = 0;
  if (!flag2) {
    flag2 = 1;
    previous_time = get_pwm_timestamp();
  }
  now_time = get_pwm_timestamp();
  time_stamp = now_time - previous_time;
  if (time_stamp > 0 && time_stamp <= halfPWMPeriod ) { //1/2T
    if (time_stamp <= value ) {
      GPIO_SET = 1 << MOTOR_2_PWM;
    }
    else {
      GPIO_CLR = 1 << MOTOR_2_PWM;
    }
  }
  if (time_stamp > halfPWMPeriod && time_stamp <= 2 * halfPWMPeriod ) { //1T
    if (time_stamp <= value ) {
      GPIO_SET = 1 << MOTOR_2_PWM;
    }
    else {
      GPIO_CLR = 1 << MOTOR_2_PWM;
    }
  }
  if (time_stamp > 2 * halfPWMPeriod) {
    flag2 = 0;
  }
}
void mySoftPwmWrite3(int value)
{
  static int previous_time = 0;
  static int now_time = 0;
  static int time_stamp = 0;
  static  unsigned char flag3 = 0;
  if (!flag3) {
    flag3 = 1;
    previous_time = get_pwm_timestamp();
  }
  now_time = get_pwm_timestamp();
  time_stamp = now_time - previous_time;
  if (time_stamp > 0 && time_stamp <= halfPWMPeriod ) { //1/2T
    if (time_stamp <= value ) {
      GPIO_SET = 1 << MOTOR_3_PWM;
    }
    else {
      GPIO_CLR = 1 << MOTOR_3_PWM;
    }
  }
  if (time_stamp > halfPWMPeriod && time_stamp <= 2 * halfPWMPeriod ) { //1T
    if (time_stamp <= value ) {
      GPIO_SET = 1 << MOTOR_3_PWM;
    }
    else {
      GPIO_CLR = 1 << MOTOR_3_PWM;
    }
  }
  if (time_stamp > 2 * halfPWMPeriod) {
    flag3 = 0;
  }
}

void mySoftPwmWrite4(int value)
{
  static int previous_time = 0;
  static int now_time = 0;
  static int time_stamp = 0;
  static  unsigned char flag4 = 0;

  if (!flag4) {
    flag4 = 1;
    previous_time = get_pwm_timestamp();
  }
  now_time = get_pwm_timestamp();
  time_stamp = now_time - previous_time;
  if (time_stamp > 0 && time_stamp <= halfPWMPeriod ) { //1/2T
    if (time_stamp <= value ) {
      GPIO_SET = 1 << MOTOR_4_PWM;
    }
    else {
      GPIO_CLR = 1 << MOTOR_4_PWM;
    }
  }
  if (time_stamp > halfPWMPeriod && time_stamp <= 2 * halfPWMPeriod ) { //1T
    if (time_stamp <= value ) {
      GPIO_SET = 1 << MOTOR_4_PWM;
    }
    else {
      GPIO_CLR = 1 << MOTOR_4_PWM;
    }
  }
  if (time_stamp > 2 * halfPWMPeriod) {
    flag4 = 0;
  }
}

void exit_UCTRONICS_Robot_Car(void)
{
  int pulsenum;
  client_Connected = 0;
  close(sockfd);
  for (pulsenum = 0; pulsenum < 10; pulsenum++) {
    servoCtrl(servo_1, SERVO_1_CENTER);
    servoCtrl(servo_2, SERVO_2_CENTER);
  }
  digitalWrite(BEEP, LOW);
  GRB_work(3, 0, 0 );
}

void  INThandler(int sig)
{
  char  c;
  signal(sig, SIG_IGN);
  printf("You hit Ctrl-C\n"
         "Do you really want to quit? [y/n] ");
  c = getchar();
  if (c == 'y' || c == 'Y') {
    exit_UCTRONICS_Robot_Car();
    printf("Bye.\n");
    exit(0);
  }
  else {
    signal(SIGINT, INThandler);
  }
  getchar(); // Get new line character
}
