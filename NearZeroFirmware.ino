/*
* NearZero Firmware
* Author: Justine Haupt
* Project started on 9/9/18
* Version 1.1 (released 3/11/2020)
*
* After roscore is started, do $rosrun rosserial_python serial_node.py /dev/ttyUSB[X] on the machine that the NearZero board is attached to, which will let NearZero interract with ROS.
* After that, with no additional configuration, NearZero will respond to velocity commands issued over the cmd_vel topic.
*
*/

#include <avr/io.h>   //IO library for avr microcontrollers.
#include <EEPROM.h>   //We'll need to access the EEPROM for storing configuration data.
#include <PWM.h>    //For motor driver PWM commutation, include Sam Knight's PWM OUTPUT library, available at https://github.com/terryjmyers/PWM.git
#include <ros.h>    //Let there be ROS! The ROS arduino libraries are installed according to http://wiki.ros.org/rosserial_arduino/Tutorials/Arduino%20IDE%20Setup
#include <geometry_msgs/Twist.h>    //We're going to be subscribing to messages of type Twist, which contain 3-axis linear and angular motion commands.
#include <std_msgs/Float32.h>     //We're going to be publishing messages of type Float32, which holds a single float32 value. This is for reporting current draw.
#include <std_msgs/Int16MultiArray.h>
#include <sensor_msgs/JointState.h>
#include <EnableInterrupt.h>

//Defime some constants
#define pwm1_in A15 //A15 is pin 82 on the package. This is the input for pwm1.
#define pwm2_in 14 //14 is pin 64 on the package. This is the input for pwm2.
#define pi 3.1415926

//Define some more constants for memeory locations
#define addr_maxIset1l 0	
#define addr_maxIset1r 1
#define addr_maxIset2l 2
#define addr_maxIset2r 3
#define addr_gain1l 4
#define addr_gain1r 5
#define addr_gain2l 6
#define addr_gain2r 7
#define addr_torqueprofile1 8
#define addr_torqueprofile2 9
#define addr_currentmode1 10
#define addr_currentmode2 11
#define addr_commandmode1 12
#define addr_commandmode2 13
#define addr_sensortype1 14
#define addr_sensortype2 15
#define addr_minIset1l 16
#define addr_minIset1r 17
#define addr_minIset2l 18
#define addr_minIset2r 19
#define addr_tscoeff1 20
#define addr_tscoeff2 21
#define addr_tsphase1l 22
#define addr_tsphase1r 23
#define addr_tsphase2l 24
#define addr_tsphase2r 25
#define addr_accel1l 26
#define addr_accel1r 27
#define addr_accel2l 28
#define addr_accel2r 29
#define addr_dir1 30
#define addr_dir2 31
#define addr_c2vTcal_posvell 32
#define addr_c2vTcal_posvelr 33
#define addr_c2vTcal_negvell 34
#define addr_c2vTcal_negvelr 35
#define addr_c2TrigSequence 36
#define addr_commandtopic 37
#define addr_enctopic 39
#define addr_wheelbasescalel 41
#define addr_wheelbasescaler 42
#define addr_wheeldiamscalel 43
#define addr_wheeldiamscaler 44
#define addr_pwmoffset1l 45
#define addr_pwmoffset1r 46
#define addr_pwmoffset2l 47
#define addr_pwmoffset2r 48
#define addr_maxslewvel1l 49
#define addr_maxslewvel1r 50
#define addr_maxslewvel2l 51
#define addr_maxslewvel2r 52


float phaseindex1 = pi;     //Start at pi radians so that the starting value of sin(phaseangle_U1) is 0.
float phaseindex2 = pi;     //Start at pi radians so that the starting value of sin(phaseangle_U1) is 0.
float sensdelay1 = 0;	//Hall or encoder period since last phase trigger after compensation for offset of each phase
float sensdelay1_last = 0;	//For averaging
float sensdelay1_calcomp = 0;	//The sensdelay2 after it's been compensated by the c2vTcal values.
float sensdelay1_velcomp = 0;	//the sensdelay2_calcomp after it's been compensated for velocity.
float sensdelay2 = 0;	//Hall or encoder period since last phase trigger after compensation for offset of each phase
float sensdelay2_last = 0;	//For averaging
float sensdelay2_calcomp = 0;	//The sensdelay2 after it's been compensated by the c2vTcal values.
float sensdelay2_velcomp = 0;	//the sensdelay2_calcomp after it's been compensated for velocity.
unsigned long sensdelay1_1 = 0;
unsigned long sensdelay1_2 = 0;
unsigned long sensdelay1_3 = 0;
unsigned long sensdelay2_1 = 0;
unsigned long sensdelay2_2 = 0;
unsigned long sensdelay2_3 = 0;
float sensdelay1flt_1 = 0;
float sensdelay1flt_2 = 0;
float sensdelay1flt_3 = 0;
float sensdelay1_deviation = 0;
float sensdelay1_deviation_lowest;
float sensdelay2flt_1 = 0;
float sensdelay2flt_2 = 0;
float sensdelay2flt_3 = 0;
float sensdelay2_deviation = 0;
float sensdelay2_deviation_lowest;
unsigned long c1Trig1;
unsigned long c1Trig2;
unsigned long c1Trig3;
unsigned long c2Trig1;
unsigned long c2Trig2;
unsigned long c2Trig3;
float c1vTcal_posvel = 0;	//Calibration value for hall/encoder. Sets offset of trigger in phase index progression (fixes there being different numbers for time delay measurements forwards and reverse.
float c1vTcal_negvel = 0;	//Calibration value for hall/encoder. This is the vel*T_phase constant.
int c1TrigSequence = 0;	//Calibration value for hall.encoder. Sets order of phase triggering.
int c1TrigSequenceStore = 1;
float c2vTcal_posvel = 0;	//Calibration value for hall/encoder. Sets offset of trigger in phase index progression (fixes there being different numbers for time delay measurements forwards and reverse.
float c2vTcal_negvel = 0;	//Calibration value for hall/encoder. This is the vel*T_phase constant.
int c2TrigSequence = 0;	//Calibration value for hall.encoder. Sets order of phase triggering.
int c2TrigSequenceStore = 1;
float phaseangle_U1; 
float phaseangle_V1;
float phaseangle_W1;
float phaseangle_U2;
float phaseangle_V2;
float phaseangle_W2;
float v_x;    //Linear velocity, as commanded over the ROS cmd_vel topic.
float v_th;   //Angular velocity, as commanded over the ROS cmd_vel topic.
float vel1 = 0;    //sets the velocity (positive or negative) or position. This is what increments the phaseangle for each phase.
float vel2 = 0;
float vel1_holder = 0;
float vel2_holder = 0;
float actualvel1 = 0;		//Intermediate variable used to track motor velocity up to vel1 according to accel1	
float actualvel2 = 0;
float pos1 = 0;	//used for positioning mode
float pos2 = 0;
float pos1_holder = 0;
float pos2_holder = 0;
float pos1_last;
float pos2_last;
float slewvel1;
float slewvel2;
float I_A;   //Current in amps after conversion from ADUs.
float I_qui;  //Stores currrent in amps before energizing motors in SetPower() function.
float I_adu_av = 0;   //Averaged current in ADUs
float I_adu_sum = 0;   //Cumulative current in ADUs
int dutyU1;    //The current duty cycle of the gating signal for each phase of motor 1
int dutyV1;
int dutyW1;
int dutyU2;    //The current duty cycle of the gating signal for each phase of motor 2
int dutyV2;
int dutyW2;
int power1 = 0;   //This is the gating PWM modulation amplitude. If it's zero the duty cycle will not vary. If it's 127, it will vary all the way from 0 to 255. Effectively sets the voltage going to the motor.
int power2 = 0;
int power1_high;	//This is used to store the power value for the requested current setting
int power2_high;
int power1_low;	//This is used to store the power value for the requested static/rest current setting
int power2_low;
int power1_diag;	//This is used to store an intermediate power value (amount of A is hard coded in SetPower()) to use for the hall phase offset self calibration.
int power2_diag;
int pindex1_flag = 0;
int pindex2_flag = 0;
int enc1_state = 0;
int enc1_laststate = 0;
int enc2_state = 0;
int enc2_laststate = 0;
float enc1_abstick = 0;
float enc2_abstick = 0;
int ticksarray[] = {0, 0};
int c1Trig_laststate = 0;
int c1Trig_lastlaststate = 0;
int c2Trig_laststate = 0;
int c2Trig_lastlaststate = 0;
int j = 0;		//general purpose counter for the roll functions
int i = 1;    //A counter and divisor for calculating a running average for current-sensing the PubI() function.

//PWM input related variables
volatile uint16_t risemark1; 
volatile int16_t pwm1_t = 0;
volatile uint16_t risemark2; 
volatile int16_t pwm2_t = 0;
int pwmstate1 = 0;
int pwmstate2 = 0;
int32_t freq = 25000; //PWM frequency (in Hz). 3-phase bridge is rated to 50kHz. Allegro current sensor bandwidth is 80kHz.
bool pwmflag = false;    //Used to detect if PWM mode has just been entered, in which case the two PWM inputs will be checked for activity one time only.
bool rosflag = true;	//This needs to be true by defualt. Its raison d'etre is entirely different from pwmflag. The rosflag is needed so that if the mode has been changed from usb-config to usb-ros without cycling power, the board will know to reset itself so that the ROS pub/sub stuff will initialize without getting screwed up by Serial.begin, which is needed for usb-config.
bool pwm1active = false;    //Indicates whether a PWM source is connected to channel 1.
bool pwm2active = false;    //Indicates whether a PWM source is connected to channel 2.
	
//User setting variables
float Iset1;    //Maximum or constant current draw in mA. Power is automatically ramped to this value at start time.
float maxIset1;
float maxIset2; 
float minIset1;		//minimum or reduced current used when restmode = 1.
float minIset2;
int gain1;		//Velocity multiplier.
int gain2;
int pwmoffset1;	//PWM center
int pwmoffset2;
int torqueprofile1;		//0 = OFF/sine, 1 = ON
int torqueprofile2;
int currentmode1;	//0 = FIXED, 1 = DYNAMIC
int currentmode2;
int commandmode1;	//0 = VELOCITY command, 1 = POSITION command, 2 = SERVO command
int commandmode2;
int sensortype1;	//0 = NONE, 1 = ENCODER, 2 = ENCODER/SERVO, 3 = HALL
int sensortype2; 	
int tscoeff1;		//Scaling coefficient for torque smoothing (ts) fourier series
int tscoeff2;
int dir1;		//0 = NORMAL, 1 = REVERSE
int dir2;
int commandtopic;		//0 = /joint_commands, 1 = /cmd_vel
int wheelbasescale;
int wheeldiamscale;
float tsphase1;		//Phase offset for torque smoothing (ts) fourier series
float tsphase2;
float accel1;	//Sets gain of position following velocity (sets servo aggressiveness)
float accel2;
float maxslewvel1;
float maxslewvel2;

//EEPROM address locations and related things
int lhlf;			//holder for the left half of an integer
int rhlf;			//holder for the right half of an integer

//Variables relating to serial terminal
const byte nchars = 31;	//Size of rxchars array
char rxchars[nchars];	//An array that holds up to "nchars" characters.	
char enctopic[] = "nzticks1";	//define this to be 1 character longer than shown here to accomodate double-digit postfixes. 
bool newdata = false;	//New data flag
bool exitflag = false;	//Used to kill a function part-way through the terminal menu system
int iholder;		//integer for temporary use
float fholder;		//float for temporary use

// Declare pin names (fixed values, so we use the byte type):
const byte U1_High = 6;  //Gating signal for U phase
const byte V1_High = 7;
const byte W1_High = 8;
const byte U1_En = 17;   //Enable pin for U phase
const byte V1_En = 16;
const byte W1_En = 9;
const byte U2_High = 46;  //Gating signal pin for U phase
const byte V2_High = 45;
const byte W2_High = 44;
const byte U2_En = 47;
const byte V2_En = 48;
const byte W2_En = 49;
const byte Isense = 0;  //Current sense input pin
const byte mode_ros = 28;   //When this pin is grounded, USB input is selected.
const byte mode_config = 27;  //When this pin is grounded, I2C input is selected.
const byte mode_i2c = 26;  //When this pin is grounded, isp input is selected.
const byte enc1_pin3 = 3;
const byte enc1_pin4 = 2;
const byte enc1_pin5 = 5;
const byte enc1_pin6 = 53;
const byte enc2_pin3 = 38;
const byte enc2_pin4 = 18;
const byte enc2_pin5 = 19;
const byte enc2_pin6 = 43;

void ISR_pwm1(){	//Interrupt Service Routine. This function is run if a change is detected on pwm1_in
	if (pwmstate1 == 0){	
		risemark1 = micros();	//Record the time that pwm1_in went high
		pwmstate1 = 1;
	}
	else {	//if flag was already 1...
		pwm1_t = micros() - risemark1;		//If it's already gone high, then the present change must be the time that pwm1_in is going low. The pulse duration is the difference.
		pwmstate1 = 0;
	}
}

void ISR_pwm2(){
	if (pwmstate2 == 0){	
		risemark2 = micros();
		pwmstate2 = 1;
	}
	else {
		pwm2_t = micros() - risemark2;
		pwmstate2 = 0;
	}
}

void velCallback(const geometry_msgs::Twist& vel){   //This is the heart of the cmdvel subscriber. It's a callback function that reads the data in the message of type geometry_msgs::Twist, where we call the data "vel".
	v_x = vel.linear.x;    //Pull out the forward/backward velocity command.
	v_th = vel.angular.z;   //Pull out the angular velocity (turn) command.
}

void jointCallback(const sensor_msgs::JointState& joint){  
    vel2_holder = joint.velocity[0];
	pos2_holder = joint.position[0];    
	vel2_holder = joint.velocity[1];
	pos2_holder = joint.position[1];
}

//NOTE: Below, I don't understand why the formats for ros::Publisher and ros::Subscriber need to be so different.
ros::NodeHandle nh;   //Create a Node Handler and call it nh.

//std_msgs::Float32 idraw_msg;    //Declare a Float32 message type (from the std_msgs class) and call it current_msg.
std_msgs::Int16MultiArray ticks_msg;
//ros::Publisher pub_1("idraw_1", &idraw_msg);   //Create a publisher and call the topic "idraw_1". The message published on this topic will be idraw_msg, defined above.
ros::Publisher pub_2(enctopic, &ticks_msg);	
ros::Subscriber <geometry_msgs::Twist> sub_1("cmd_vel", velCallback);   //  Create a subscriber for the topic "cmd_vel" with message type "Twist" in the geometry_msgs class. Messages are passed to a callback function, called here "velCallback".
ros::Subscriber <sensor_msgs::JointState> sub_2("joint_commands", jointCallback);

void setup(){

	InitTimersSafe(); //Initialize all timers except timer 0

	SetPinFrequencySafe(U1_High, freq);   //sets the frequency for the specified pin
	SetPinFrequencySafe(V1_High, freq);   //sets the frequency for the specified pin
	SetPinFrequencySafe(W1_High, freq);   //sets the frequency for the specified pin
	SetPinFrequencySafe(U2_High, freq);   //sets the frequency for the specified pin
	SetPinFrequencySafe(V2_High, freq);   //sets the frequency for the specified pin
	SetPinFrequencySafe(W2_High, freq);   //sets the frequency for the specified pin

	//Timer 4 outputs:
	pinMode(U1_High, OUTPUT); 
	pinMode(V1_High, OUTPUT);
	pinMode(W1_High, OUTPUT);

	//Timer 5 outputs:
	pinMode(U2_High, OUTPUT);
	pinMode(V2_High, OUTPUT);
	pinMode(W2_High, OUTPUT);

	//Other one time settings:
	pinMode(13, OUTPUT);    //indicator light
	pinMode(mode_ros, INPUT_PULLUP);
	pinMode(mode_i2c, INPUT_PULLUP);
	pinMode(mode_config, INPUT_PULLUP);
	pinMode(Isense, INPUT);
	pinMode(U1_En, OUTPUT);
	pinMode(V1_En, OUTPUT);
	pinMode(W1_En, OUTPUT);
	pinMode(U2_En, OUTPUT);
	pinMode(V2_En, OUTPUT);
	pinMode(W2_En, OUTPUT);
	pinMode(enc1_pin3, INPUT_PULLUP);
	pinMode(enc1_pin4, INPUT_PULLUP);
	pinMode(enc1_pin5, INPUT_PULLUP);
	pinMode(enc1_pin6, INPUT_PULLUP);
	pinMode(enc2_pin3, INPUT_PULLUP);
	pinMode(enc2_pin4, INPUT_PULLUP);
	pinMode(enc2_pin5, INPUT_PULLUP);
	pinMode(enc2_pin6, INPUT_PULLUP);
  	pinMode(pwm1_in, INPUT_PULLUP);  
  	pinMode(pwm2_in, INPUT_PULLUP);  
	enableInterrupt(pwm1_in, ISR_pwm1, CHANGE);
	enableInterrupt(pwm2_in, ISR_pwm2, CHANGE);

	digitalWrite(13, HIGH);   //Put the pin 13 LED high to indicate that the board is starting up.

	nh.initNode();    //Start the node handler (i.e. start a node), which allows this program to create publishers or subscribers. This also handles serial comms. There can be only a single ROS node per serial device (i.e. a single node per device like this, which is fine because a node can both publish and subscribe).
	nh.subscribe(sub_1);    //Start listening.
	nh.subscribe(sub_2);
	//nh.advertise(pub_1);
	nh.advertise(pub_2);

	//The purpose of the next lines is to syncyrhonize all the timers so that the PWM duty cycles overlap completely:
	GTCCR = (1<<TSM)|(1<<PSRASY)|(1<<PSRSYNC);  // Halt all timers using the GTCCR register. We're setting the bit called "TSM" to 1, the bit called "PSRASY" to 1, etc. The names of each bit in each register is described in the ATMega2560 datasheet.

	// The TCNTn registers store the actual timer values (the actual number that counts up). These are 16bit timers, so they count up to 2^16=65535. With the timers halted with the previous line,
	// we can set these to start at any value between 0 and 65536. This effectively sets the relative phase of the timers. These timer registers are 16 bit, but the data bus in the ATmega is only 8 bit,
	// so it gets around this by using a separate 8-bit high byte and 8-bit low byte for each timer. The low and high bytes get written to the 16bit register in the same clock cycle, and this is triggered by
	// accessing the low byte. Writing to the high byte alone won't do anything. Also importantly, the high byte is stored in the same temporary 8-bit register that all the timers use, so every time a
	// value is written to the high byte, the low byte must be written before the high byte for another timer is written, lest we overwrite the temporarily stored high byte that we initially accessed.
	// The safe thing is to always write to a register's high byte followed by its low byte. To read a register, read its low byte first, followed by its high byte.
	// Note: The OCRnA/B/C registers do NOT use the temporary register. The registers can be written in hex (with 0x prefix) or binary (with 0b prefix). No prefix indicates decimal.
	TCNT3H = 0b00000000;  // set timer3 high byte to 0. This could be entered in decimal format as simply "0", which has the binary value we actually entered.
	TCNT3L = 0b00000000;  // set timer3 low byte to 0. With both the high and low bytes set to zero, we're saying that the counter's 16-bit value is 0. In binary it's 0000000000000000. Example: If we want 2,000 (as in 2,000 out of 65535), in binary 2,000 = 0000011111010000. So the high byte is 00000111 and the low byte is 11010000. 1,000 = 0000001111101000. 4,000 = 0000111110100000. 500 = 0000000111110100.
	TCNT4H = 0b00000000;
	TCNT4L = 0b00000000;
	TCNT5H = 0b00000000;
	TCNT5L = 0b00000000;

	GTCCR = 0; // release all timers by setting all bits back to 0.

	if (digitalRead(mode_ros) == LOW){	//Initialize serial unless we're in ROS mode.
	}
	else{
		Serial.begin(9600);
	}
	DisplaySettings();
	SetPower();   //Set the 3-phase modulation amplitude to draw the amount of current dictated by the I_set_ variables.    
	digitalWrite(13, LOW);   //Put the pin 13 LED high to indicate that the board is starting up.
}

void softReset() {
	asm volatile ("  jmp 0");
}

void loop(){
	CheckMode();		
	if (commandmode1 == 0){	//if in velocity mode
		Roll_vel1();    
	}
	else  { //if in position or servo mode
		Roll_pos1();    
	}
	if (commandmode2 == 0){	//if in velocity mode
		Roll_vel2();    
	}
	else {	//if in position or servo mode
		Roll_pos2();    
	}
}

void Roll_vel1(){
	if (sensortype1 == 1 || sensortype1 == 2){		//If sensortype is set to encoder-external, report encoder ticks
		ReadEnc1();
	}
	else if (sensortype1 == 3){		//If sensortype is set to hall-external
		ReadHall1();
	}

	//CALCULATE ACCELERATION DYNAMICS
	if (accel1 == 10000) {	//If accel=10000, this means the accel variable shouldn't even be used
		actualvel1 = vel1;
	}
	else {	//If the accel variable is otherwise set, use it
		if ((vel1 - .0001) > actualvel1){		//the constant defines the deadzone	
			if (fabs(vel1) < .01){		//If the velocity is slow, don't use the acceleration parameter
				actualvel1 = vel1;
			}
			else {
				actualvel1 = actualvel1 + (accel1/1000000);
			}	
		}
		else if ((vel1 + .0001) < actualvel1){		//the constant defines the deadzone
			if (fabs(vel1) < .01){		//If the velocity is slow, don't use the acceleration parameter
				actualvel1 = vel1;
			}
			else {
				actualvel1 = actualvel1 - (accel1/1000000);
			}	
		}
	}
	//IF IN 'FIXED' CURRENT MODE (NOTE: THIS IS THE ONLY CURRENT MODE IN THIS FIRMWARE VERSION)
	if (currentmode1 == 0) {	
		if (actualvel1 == 0){		//Set power based on whether or not motor is moving.
			power1 = power1_low;		//This will be the same as power2_high when in the "simple" fixed current mode
		}
		else {
			power1 = power1_high;
		}
		//INCREMENT PHASE INDEX
		if (dir1 == 0) {	//If direction is NORMAL
			phaseindex1 = phaseindex1 + actualvel1;		
		}
		else if (dir1 == 1) {	//If direction is REVERSE
			phaseindex1 = phaseindex1 - actualvel1;
		}	
	}
	//CALCULATE TORQUE SMOOTHING (IF ON)
	if (torqueprofile1 == 1){	//If the anti-cogging profile is set to 1, make the phase index be the phase index plus (minus) a correction which is a function of the phase index. The fuction 
		phaseindex1 = phaseindex1 - (tscoeff1*.00025*sin(2*phaseindex1+(.469+tsphase1)) + tscoeff1*.00005*sin(4*phaseindex1+(.491+tsphase1)));  
	}

	//RESET PHASE INDEX AFTER 2PI
	if (phaseindex1 >= 2*pi) {   //Reset phaseindex_U1 once it completes 2*180o in phase.
		phaseindex1 = 0;
	}
	else if (phaseindex1 <= 0){
		phaseindex1 = 2*pi - phaseindex1;
	}

	//WRITE OUTPUTS
	dutyU1 = (255/2)+(power1*(sin(phaseindex1)));	   //The duty cycle varies with the sine function, which has output between -1 and 1. That is scaled by an amplitude variable, which effectively sets the motor power.This is all offset by half the maximum duty cycle so that the lowest instantaeous duty cycle is always positive.
	analogWrite(U1_High,dutyU1);	//Write to the PWM OUT pins.
	dutyV1 = (255/2)+(power1*(sin(phaseindex1+(2*pi/3))));
	analogWrite(V1_High,dutyV1);
	dutyW1 = (255/2)+(power1*(sin(phaseindex1+(4*pi/3))));
	analogWrite(W1_High,dutyW1);
}

void Roll_vel2(){
	if (sensortype2 == 1 || sensortype2 == 2){		//If sensortype is set to encoder-external, report encoder ticks
		ReadEnc2();
	}
	else if (sensortype2 == 3){		//If sensortype is set to hall-external
		ReadHall2();
	}
	//CALCULATE ACCELERATION DYNAMICS
	if (accel2 == 10000) {	//If accel=10000, this means the accel variable shouldn't even be used
		actualvel2 = vel2;
	}
	else {	//If the accel variable is otherwise set, use it
		if ((vel2 - .0001) > actualvel2){		//the constant defines the deadzone	
			if (fabs(vel2) < .01){		//If the velocity is slow, don't use the acceleration parameter
				actualvel2 = vel2;
			}
			else {
				actualvel2 = actualvel2 + (accel2/1000000);
			}	
		}
		else if ((vel2 + .0001) < actualvel2){		//the constant defines the deadzone
			if (fabs(vel2) < .01){		//If the velocity is slow, don't use the acceleration parameter
				actualvel2 = vel2;
			}
			else {
				actualvel2 = actualvel2 - (accel2/1000000);
			}	
		}
	}
	//IF IN 'FIXED' CURRENT MODE
	if (currentmode2 == 0) {	
		if (actualvel2 == 0){		//Set power based on whether or not motor is moving.
			power2 = power2_low;		//This will be the same as power2_high when in the "simple" fixed current mode
		}
		else {
			power2 = power2_high;
		}
		//INCREMENT PHASE INDEX
		if (dir2 == 0) {	//If direction is NORMAL
			phaseindex2 = phaseindex2 + actualvel2;		
		}
		else if (dir2 == 1) {	//If direction is REVERSE
			phaseindex2 = phaseindex2 - actualvel2;
		}	
	}
	//CALCULATE TORQUE SMOOTHING (IF ON)
	if (torqueprofile2 == 1){	//If the anti-cogging profile is set to 1, make the phase index be the phase index plus (minus) a correction which is a function of the phase index. The fuction 
		phaseindex2 = phaseindex2 - (tscoeff2*.00025*sin(2*phaseindex2+(.469+tsphase1)) + tscoeff2*.00005*sin(4*phaseindex2+(.491+tsphase2)));  
	}

	//RESET PHASE INDEX AFTER 2PI
	if (phaseindex2 >= 2*pi) {   //Reset phaseindex_U1 once it completes 2*180o in phase.
		phaseindex2 = 0;
	}
	else if (phaseindex2 <= 0){
		phaseindex2 = 2*pi - phaseindex2;
	}

	//WRITE OUTPUTS
	dutyU2 = (255/2)+(power2*(sin(phaseindex2)));	   //The duty cycle varies with the sine function, which has output between -1 and 1. That is scaled by an amplitude variable, which effectively sets the motor power.This is all offset by half the maximum duty cycle so that the lowest instantaeous duty cycle is always positive.
	analogWrite(U2_High,dutyU2);	//Write to the PWM pins.
	dutyV2 = (255/2)+(power2*(sin(phaseindex2+(2*pi/3))));
	analogWrite(V2_High,dutyV2);
	dutyW2 = (255/2)+(power2*(sin(phaseindex2+(4*pi/3))));
	analogWrite(W2_High,dutyW2);
}

void Roll_pos1(){
	float encstep1 = .03;	//The bigger this is, the smaller the motor's movement range will be
	if (sensortype1 == 1){		//If sensortype is set to encoder, report encoder ticks and keep track of relative position in case we're in SERVO mode
		ReadEnc1();
		if (enc1_state == 0 && enc1_laststate == 3){
			enc1_abstick = enc1_abstick + encstep1;		//Increment the absolute tracking counter
			enc1_laststate = enc1_state;
		}
		else if (enc1_state == 3 && enc1_laststate == 0){
			enc1_abstick = enc1_abstick - encstep1;		//decrement the absolute tracking counter
			enc1_laststate = enc1_state;
		}
		else {
			enc1_abstick = enc1_abstick + (encstep1*(enc1_state - enc1_laststate));		//Increment or decrement the absolute tracking counter depending on how big of a change was detected in the encoder state
			enc1_laststate = enc1_state;
		}		
	}
	else if (sensortype1 == 2){		//If sensortype is set to hall
		ReadHall1();
	}
	//Check for REVERSE directionality
	if (dir1 == 1) {  //If directionality is REVERSE
		pos1 = (-1)*pos1;
	}

	//TRACK THE MOTOR AGAINST THE COMMANDED POSITION
	if (commandmode1 == 1) {		//If in POSITION command mode, track phase index to the commanded position
		slewvel1 = (accel1/100000)*(fabs(phaseindex1 - pos1));	//make this a signmoid func. instead of capping below
		if (slewvel1 > maxslewvel1) {	//cap the max slewrate	
			slewvel1 = maxslewvel1;
		}
		else if (slewvel1 < -maxslewvel1){
			slewvel1 = -maxslewvel1;
		}
		if ((phaseindex1 - .2) > pos1){		//the constant defines the deadzone
			phaseindex1 = phaseindex1 - slewvel1; 
			power1 = power1_high;
			j = 0;
		}
		else if ((phaseindex1 + .2) < pos1){
			phaseindex1 = phaseindex1 + slewvel1;
			power1 = power1_high;
			j = 0;
		}
		else {//they're equal, so don't move the phaseindex, but DO set the power to the low setting
			j++;
			if (j == 100){
				power1 = power1_low;
			}
		}
	}
	else if (commandmode1 == 2){ //If in SERVO command mode, track the encoder tick counter (enc1_abstick) to the commanded position
		slewvel1 = (accel1/100000)*(fabs(enc1_abstick - pos1));  //make this a signmoid func. instead of capping below
		if (slewvel1 > maxslewvel1) {	//cap the max slewrate	
			slewvel1 = maxslewvel1;
		}
		else if (slewvel1 < -maxslewvel1){
			slewvel1 = -maxslewvel1;
		}
		if ((enc1_abstick - .1) > pos1){		//the constant defines the deadzone
			phaseindex1 = phaseindex1 - slewvel1; 
			power1 = power1_high;
			j = 0;
		}
		else if ((enc1_abstick + .1) < pos1){
			phaseindex1 = phaseindex1 + slewvel1;
			power1 = power1_high;
			j = 0;
		}
		else {//they're equal, so don't move the phaseindex, but DO set the power to the low setting
			j++;
			if (j == 100){
				power1 = power1_low;
			}
		}	
	}

	//UPDATE MOTOR POSITION
	pos1_last = pos1;

	//CALCULATE TORQUE SMOOTHING (IF ON)
	if (torqueprofile1 == 1){	//If the anti-coffing profile is set to 1, make the phase index be the phase index plus (minus) a correction which is a function of the phase index. The fuction 
		phaseindex1 = phaseindex1 - (tscoeff1*.00025*sin(2*phaseindex1+(.469+tsphase1)) + tscoeff1*.00005*sin(4*phaseindex1+(.491+tsphase1)));  
	}

	//WRITE OUTPUTS
	dutyU1 = (255/2)+(power1*(sin(phaseindex1)));	   //The duty cycle varies with the sine function, which has output between -1 and 1. That is scaled by an amplitude variable, which effectively sets the motor power.This is all offset by half the maximum duty cycle so that the lowest instantaeous duty cycle is always positive.
	analogWrite(U1_High,dutyU1);	//Write to the PWM pins.
	dutyV1 = (255/2)+(power1*(sin(phaseindex1+(2*pi/3))));
	analogWrite(V1_High,dutyV1);
	dutyW1 = (255/2)+(power1*(sin(phaseindex1+(4*pi/3))));
	analogWrite(W1_High,dutyW1);
}

void Roll_pos2(){
	float encstep2 = .03;	//The bigger this is, the smaller the motor's movement range will be
	if (sensortype2 == 1){		//If sensortype is set to encoder, report encoder ticks and keep track of relative position in case we're in SERVO mode
		ReadEnc2();
		if (enc2_state == 0 && enc2_laststate == 3){
			enc2_abstick = enc2_abstick + encstep2;		//Increment the absolute tracking counter
			enc2_laststate = enc2_state;
		}
		else if (enc2_state == 3 && enc2_laststate == 0){
			enc2_abstick = enc2_abstick - encstep2;		//decrement the absolute tracking counter
			enc2_laststate = enc2_state;
		}
		else {
			enc2_abstick = enc2_abstick + (encstep2*(enc2_state - enc2_laststate));		//Increment or decrement the absolute tracking counter depending on how big of a change was detected in the encoder state
			enc2_laststate = enc2_state;
		}		
	}
	else if (sensortype2 == 2){		//If sensortype is set to hall
		ReadHall2();
	}
	//Check for REVERSE directionality
	if (dir2 == 1) {  //If directionality is REVERSE
		pos2 = (-1)*pos2;
	}

	//TRACK THE MOTOR AGAINST THE COMMANDED POSITION
	if (commandmode2 == 1) {		//If in POSITION command mode, track phase index to the commanded position
		slewvel2 = (accel2/100000)*(fabs(phaseindex2 - pos2));	////make this a signmoid func. instead of capping below
		if (slewvel2 > maxslewvel2) {	//cap the max slewrate	
			slewvel2 = maxslewvel2;
		}
		else if (slewvel2 < -maxslewvel2){
			slewvel2 = -maxslewvel2;
		}
		if ((phaseindex2 - .2) > pos2){		//the constant defines the deadzone
			phaseindex2 = phaseindex2 - slewvel2; 
			power2 = power2_high;
			j = 0;
		}
		else if ((phaseindex2 + .2) < pos2){
			phaseindex2 = phaseindex2 + slewvel2;
			power2 = power2_high;
			j = 0;
		}
		else {	//they're equal, so don't move the phaseindex, but DO set the power to the low setting
			j++;
			if (j == 100){
				power2 = power2_low;
			}
		}
	}
	else if (commandmode2 == 2){ //If in SERVO command mode, track the encoder tick counter (enc2_abstick) to the commanded position
		slewvel2 = (accel2/100000)*(fabs(enc2_abstick - pos2));  //make this a signmoid func. instead of capping below
		if (slewvel2 > maxslewvel2) {	//cap the max slewrate	
			slewvel2 = maxslewvel2;
		}	
	else if (slewvel2 < -maxslewvel2){
			slewvel2 = -maxslewvel2;
		}
		if ((enc2_abstick - .1) > pos2){		//the constant defines the deadzone
			phaseindex2 = phaseindex2 - slewvel2; 
			power2 = power2_high;
			j = 0;
		}
		else if ((enc2_abstick + .1) < pos2){
			phaseindex2 = phaseindex2 + slewvel2;
			power2 = power2_high;
			j = 0;
		}
		else {//they're equal, so don't move the phaseindex, but DO set the power to the low setting
			j++;
			if (j == 100){
				power2 = power2_low;
			}
		}	
	}

	//UPDATE MOTOR POSITION
	pos2_last = pos2;

	//CALCULATE TORQUE SMOOTHING (IF ON)
	if (torqueprofile2 == 1){	//If the anti-coffing profile is set to 1, make the phase index be the phase index plus (minus) a correction which is a function of the phase index. The fuction 
		phaseindex2 = phaseindex2 - (tscoeff2*.00025*sin(2*phaseindex2+(.469+tsphase2)) + tscoeff2*.00005*sin(4*phaseindex2+(.491+tsphase2)));  
	}

	//WRITE OUTPUTS
	dutyU2 = (255/2)+(power2*(sin(phaseindex2)));	   //The duty cycle varies with the sine function, which has output between -1 and 1. That is scaled by an amplitude variable, which effectively sets the motor power.This is all offset by half the maximum duty cycle so that the lowest instantaeous duty cycle is always positive.
	analogWrite(U2_High,dutyU2);	//Write to the PWM pins.
	dutyV2 = (255/2)+(power2*(sin(phaseindex2+(2*pi/3))));
	analogWrite(V2_High,dutyV2);
	dutyW2 = (255/2)+(power2*(sin(phaseindex2+(4*pi/3))));
	analogWrite(W2_High,dutyW2);
}

