//===============================================
/// @file: cardetails.h
///
/// Constants to be applied to the RobotCar.ino
///
/// The car kit is supplied by LAFVIN
//===============================================

#ifndef H_CARDETAILS
#define H_CARDETAILS

#define CAR_MFG "LAFVIN"


//--- Pin Assignments ---------------------------
const uint8_t kPinMotorRightSpeed = 3;  // ENA gray (PWM)
const uint8_t kPinMotorRightFWD = 4;    // IN1 purple
const uint8_t kPinMotorRightREV = 5;    // IN2 blue
const uint8_t kPinMotorLeftFWD = 6;     // IN3 green
const uint8_t kPinMotorLeftREV = 7;     // IN4 yellow
const uint8_t kPinMotorLeftSpeed = 11;  // ENB orange (PWM)

const uint8_t kPinServo = 2;
const uint8_t kPinSonicEcho = 12;
const uint8_t kPinSonicTrig = 13;

const int k_nServoMaxSweep = 0;  //180 - 20;
const int k_nServoMinSweep = 0;


// range -30 to 30 (representing percentage)
//	turn towards right is negative, left is positive
const int kMotorAdjustment = -23;

// map 0..100 into a range 64+MOTOR_MAP_SPEED..255
#define MOTOR_MAP_SPEED 16 + 8 + 8

#define TURN_SPEED 10
#define TURN_TIME  700
#define REV_SPEED  5
#define REV_TIME   700

#define MOTOR_INx_ENx 1


#endif  // H_CARDETAILS
