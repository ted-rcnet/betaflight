/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#ifdef UNIT_TEST
#include <stdio.h>
#endif

#include "platform.h"

#include "build_config.h"

#include "common/axis.h"
#include "common/maths.h"
#ifndef UNIT_TEST
#include "drivers/gpio.h"
#include "drivers/timer.h"
#include "drivers/pwm_output.h"
#include "drivers/pwm_mapping.h"
#endif

#include "rx/rx.h"
#include "io/gimbal.h"
#include "io/escservo.h"
#include "io/rc_controls.h"

#include "flight/mixer.h"
#include "flight/flight.h"

#include "config/runtime_config.h"
#include "config/config.h"

#define GIMBAL_SERVO_PITCH 0
#define GIMBAL_SERVO_ROLL 1

#define AUX_FORWARD_CHANNEL_TO_SERVO_COUNT 4

//#define MIXER_DEBUG

#include "drivers/system.h"
extern int16_t debug[4];

#ifndef UNIT_TEST
static uint8_t motorCount = 0;
static int useServo;
static uint8_t servoCount;
#endif

int16_t motor[MAX_SUPPORTED_MOTORS];
int16_t motor_disarmed[MAX_SUPPORTED_MOTORS];
int16_t servo[MAX_SUPPORTED_SERVOS];


static servoParam_t *servoConf;
static mixerConfig_t *mixerConfig;
static flight3DConfig_t *flight3DConfig;
static escAndServoConfig_t *escAndServoConfig;
static airplaneConfig_t *airplaneConfig;
static rxConfig_t *rxConfig;
static gimbalConfig_t *gimbalConfig;

#ifndef UNIT_TEST
static motorMixer_t currentMixer[MAX_SUPPORTED_MOTORS];
static mixerMode_e currentMixerMode;
#endif
static lowpass_t lowpassFilters[MAX_SUPPORTED_SERVOS];

static const motorMixer_t mixerQuadX[] = {
    { 1.0f, -1.0f,  1.0f, -1.0f },          // REAR_R
    { 1.0f, -1.0f, -1.0f,  1.0f },          // FRONT_R
    { 1.0f,  1.0f,  1.0f,  1.0f },          // REAR_L
    { 1.0f,  1.0f, -1.0f, -1.0f },          // FRONT_L
};
#ifndef USE_QUAD_MIXER_ONLY
static const motorMixer_t mixerTri[] = {
    { 1.0f,  0.0f,  1.333333f,  0.0f },     // REAR
    { 1.0f, -1.0f, -0.666667f,  0.0f },     // RIGHT
    { 1.0f,  1.0f, -0.666667f,  0.0f },     // LEFT
};

static const motorMixer_t mixerQuadP[] = {
    { 1.0f,  0.0f,  1.0f, -1.0f },          // REAR
    { 1.0f, -1.0f,  0.0f,  1.0f },          // RIGHT
    { 1.0f,  1.0f,  0.0f,  1.0f },          // LEFT
    { 1.0f,  0.0f, -1.0f, -1.0f },          // FRONT
};

static const motorMixer_t mixerBi[] = {
    { 1.0f,  1.0f,  0.0f,  0.0f },          // LEFT
    { 1.0f, -1.0f,  0.0f,  0.0f },          // RIGHT
};

static const motorMixer_t mixerY6[] = {
    { 1.0f,  0.0f,  1.333333f,  1.0f },     // REAR
    { 1.0f, -1.0f, -0.666667f, -1.0f },     // RIGHT
    { 1.0f,  1.0f, -0.666667f, -1.0f },     // LEFT
    { 1.0f,  0.0f,  1.333333f, -1.0f },     // UNDER_REAR
    { 1.0f, -1.0f, -0.666667f,  1.0f },     // UNDER_RIGHT
    { 1.0f,  1.0f, -0.666667f,  1.0f },     // UNDER_LEFT
};

static const motorMixer_t mixerHex6P[] = {
    { 1.0f, -0.866025f,  0.5f,  1.0f },     // REAR_R
    { 1.0f, -0.866025f, -0.5f, -1.0f },     // FRONT_R
    { 1.0f,  0.866025f,  0.5f,  1.0f },     // REAR_L
    { 1.0f,  0.866025f, -0.5f, -1.0f },     // FRONT_L
    { 1.0f,  0.0f,      -1.0f,  1.0f },     // FRONT
    { 1.0f,  0.0f,       1.0f, -1.0f },     // REAR
};

static const motorMixer_t mixerY4[] = {
    { 1.0f,  0.0f,  1.0f, -1.0f },          // REAR_TOP CW
    { 1.0f, -1.0f, -1.0f,  0.0f },          // FRONT_R CCW
    { 1.0f,  0.0f,  1.0f,  1.0f },          // REAR_BOTTOM CCW
    { 1.0f,  1.0f, -1.0f,  0.0f },          // FRONT_L CW
};

static const motorMixer_t mixerHex6X[] = {
    { 1.0f, -0.5f,  0.866025f,  1.0f },     // REAR_R
    { 1.0f, -0.5f, -0.866025f,  1.0f },     // FRONT_R
    { 1.0f,  0.5f,  0.866025f, -1.0f },     // REAR_L
    { 1.0f,  0.5f, -0.866025f, -1.0f },     // FRONT_L
    { 1.0f, -1.0f,  0.0f,      -1.0f },     // RIGHT
    { 1.0f,  1.0f,  0.0f,       1.0f },     // LEFT
};

static const motorMixer_t mixerOctoX8[] = {
    { 1.0f, -1.0f,  1.0f, -1.0f },          // REAR_R
    { 1.0f, -1.0f, -1.0f,  1.0f },          // FRONT_R
    { 1.0f,  1.0f,  1.0f,  1.0f },          // REAR_L
    { 1.0f,  1.0f, -1.0f, -1.0f },          // FRONT_L
    { 1.0f, -1.0f,  1.0f,  1.0f },          // UNDER_REAR_R
    { 1.0f, -1.0f, -1.0f, -1.0f },          // UNDER_FRONT_R
    { 1.0f,  1.0f,  1.0f, -1.0f },          // UNDER_REAR_L
    { 1.0f,  1.0f, -1.0f,  1.0f },          // UNDER_FRONT_L
};

static const motorMixer_t mixerOctoFlatP[] = {
    { 1.0f,  0.707107f, -0.707107f,  1.0f },    // FRONT_L
    { 1.0f, -0.707107f, -0.707107f,  1.0f },    // FRONT_R
    { 1.0f, -0.707107f,  0.707107f,  1.0f },    // REAR_R
    { 1.0f,  0.707107f,  0.707107f,  1.0f },    // REAR_L
    { 1.0f,  0.0f, -1.0f, -1.0f },              // FRONT
    { 1.0f, -1.0f,  0.0f, -1.0f },              // RIGHT
    { 1.0f,  0.0f,  1.0f, -1.0f },              // REAR
    { 1.0f,  1.0f,  0.0f, -1.0f },              // LEFT
};

static const motorMixer_t mixerOctoFlatX[] = {
    { 1.0f,  1.0f, -0.5f,  1.0f },          // MIDFRONT_L
    { 1.0f, -0.5f, -1.0f,  1.0f },          // FRONT_R
    { 1.0f, -1.0f,  0.5f,  1.0f },          // MIDREAR_R
    { 1.0f,  0.5f,  1.0f,  1.0f },          // REAR_L
    { 1.0f,  0.5f, -1.0f, -1.0f },          // FRONT_L
    { 1.0f, -1.0f, -0.5f, -1.0f },          // MIDFRONT_R
    { 1.0f, -0.5f,  1.0f, -1.0f },          // REAR_R
    { 1.0f,  1.0f,  0.5f, -1.0f },          // MIDREAR_L
};

static const motorMixer_t mixerVtail4[] = {
    { 1.0f,  0.0f,  1.0f,  1.0f },          // REAR_R
    { 1.0f, -1.0f, -1.0f,  0.0f },          // FRONT_R
    { 1.0f,  0.0f,  1.0f, -1.0f },          // REAR_L
    { 1.0f,  1.0f, -1.0f, -0.0f },          // FRONT_L
};

static const motorMixer_t mixerAtail4[] = {
    { 1.0f, 0.0f, 1.0f, 1.0f },             // REAR_R
    { 1.0f, -1.0f, -1.0f, 0.0f },           // FRONT_R
    { 1.0f, 0.0f, 1.0f, -1.0f },            // REAR_L
    { 1.0f, 1.0f, -1.0f, -0.0f },           // FRONT_L
};

static const motorMixer_t mixerHex6H[] = {
    { 1.0f, -1.0f,  1.0f, -1.0f },     // REAR_R
    { 1.0f, -1.0f, -1.0f,  1.0f },     // FRONT_R
    { 1.0f,  1.0f,  1.0f,  1.0f },     // REAR_L
    { 1.0f,  1.0f, -1.0f, -1.0f },     // FRONT_L
    { 1.0f,  0.0f,  0.0f,  0.0f },     // RIGHT
    { 1.0f,  0.0f,  0.0f,  0.0f },     // LEFT
};

static const motorMixer_t mixerDualcopter[] = {
    { 1.0f,  0.0f,  0.0f, -1.0f },          // LEFT
    { 1.0f,  0.0f,  0.0f,  1.0f },          // RIGHT
};

// Keep this synced with MultiType struct in mw.h!
const mixer_t mixers[] = {
//    Mo Se Mixtable
    { 0, 0, NULL },                // entry 0
    { 3, 1, mixerTri },            // MIXER_TRI
    { 4, 0, mixerQuadP },          // MIXER_QUADP
    { 4, 0, mixerQuadX },          // MIXER_QUADX
    { 2, 1, mixerBi },             // MIXER_BI
    { 0, 1, NULL },                // * MIXER_GIMBAL
    { 6, 0, mixerY6 },             // MIXER_Y6
    { 6, 0, mixerHex6P },          // MIXER_HEX6
    { 1, 1, NULL },                // * MIXER_FLYING_WING
    { 4, 0, mixerY4 },             // MIXER_Y4
    { 6, 0, mixerHex6X },          // MIXER_HEX6X
    { 8, 0, mixerOctoX8 },         // MIXER_OCTOX8
    { 8, 0, mixerOctoFlatP },      // MIXER_OCTOFLATP
    { 8, 0, mixerOctoFlatX },      // MIXER_OCTOFLATX
    { 1, 1, NULL },                // * MIXER_AIRPLANE
    { 0, 1, NULL },                // * MIXER_HELI_120_CCPM
    { 0, 1, NULL },                // * MIXER_HELI_90_DEG
    { 4, 0, mixerVtail4 },         // MIXER_VTAIL4
    { 6, 0, mixerHex6H },          // MIXER_HEX6H
    { 0, 1, NULL },                // * MIXER_PPM_TO_SERVO
    { 2, 1, mixerDualcopter  },    // MIXER_DUALCOPTER
    { 1, 1, NULL },                // MIXER_SINGLECOPTER
    { 4, 0, mixerAtail4 },         // MIXER_ATAIL4
    { 0, 0, NULL },                // MIXER_CUSTOM
};
#endif

void mixerUseConfigs(servoParam_t *servoConfToUse, flight3DConfig_t *flight3DConfigToUse, escAndServoConfig_t *escAndServoConfigToUse, mixerConfig_t *mixerConfigToUse, airplaneConfig_t *airplaneConfigToUse, rxConfig_t *rxConfigToUse, gimbalConfig_t *gimbalConfigToUse)
{
    servoConf = servoConfToUse;
    flight3DConfig = flight3DConfigToUse;
    escAndServoConfig = escAndServoConfigToUse;
    mixerConfig = mixerConfigToUse;
    airplaneConfig = airplaneConfigToUse;
    rxConfig = rxConfigToUse;
    gimbalConfig = gimbalConfigToUse;
}

#ifndef UNIT_TEST
int16_t determineServoMiddleOrForwardFromChannel(int nr)
{
    uint8_t channelToForwardFrom = servoConf[nr].forwardFromChannel;

    if (channelToForwardFrom != CHANNEL_FORWARDING_DISABLED && channelToForwardFrom < rxRuntimeConfig.channelCount) {
        return rcData[channelToForwardFrom];
    }

    if (nr < MAX_SUPPORTED_SERVOS) {
        return servoConf[nr].middle;
    }

    return DEFAULT_SERVO_MIDDLE;
}

int servoDirection(int nr, int lr)
{
    // servo.rate is overloaded for servos that don't have a rate, but only need direction
    // bit set = negative, clear = positive
    // rate[2] = ???_direction
    // rate[1] = roll_direction
    // rate[0] = pitch_direction
    // servo.rate is also used as gimbal gain multiplier (yeah)
    if (servoConf[nr].rate & lr)
        return -1;
    else
        return 1;
}

static motorMixer_t *customMixers;


#ifndef USE_QUAD_MIXER_ONLY
void mixerInit(mixerMode_e mixerMode, motorMixer_t *initialCustomMixers)
{
    currentMixerMode = mixerMode;

    customMixers = initialCustomMixers;

    // enable servos for mixes that require them. note, this shifts motor counts.
    useServo = mixers[currentMixerMode].useServo;
    // if we want camstab/trig, that also enables servos, even if mixer doesn't
    if (feature(FEATURE_SERVO_TILT))
        useServo = 1;
}

void mixerUsePWMOutputConfiguration(pwmOutputConfiguration_t *pwmOutputConfiguration)
{
    int i;

    servoCount = pwmOutputConfiguration->servoCount;

    if (currentMixerMode == MIXER_CUSTOM) {
        // load custom mixer into currentMixer
        for (i = 0; i < MAX_SUPPORTED_MOTORS; i++) {
            // check if done
            if (customMixers[i].throttle == 0.0f)
                break;
            currentMixer[i] = customMixers[i];
            motorCount++;
        }
    } else {
        motorCount = mixers[currentMixerMode].motorCount;
        // copy motor-based mixers
        if (mixers[currentMixerMode].motor) {
            for (i = 0; i < motorCount; i++)
                currentMixer[i] = mixers[currentMixerMode].motor[i];
        }
    }

    // in 3D mode, mixer gain has to be halved
    if (feature(FEATURE_3D)) {
        if (motorCount > 1) {
            for (i = 0; i < motorCount; i++) {
                currentMixer[i].pitch *= 0.5f;
                currentMixer[i].roll *= 0.5f;
                currentMixer[i].yaw *= 0.5f;
            }
        }
    }

    // set flag that we're on something with wings
    if (currentMixerMode == MIXER_FLYING_WING ||
            currentMixerMode == MIXER_AIRPLANE)
        ENABLE_STATE(FIXED_WING);
    else
        DISABLE_STATE(FIXED_WING);

    mixerResetMotors();
}

void mixerLoadMix(int index, motorMixer_t *customMixers)
{
    int i;

    // we're 1-based
    index++;
    // clear existing
    for (i = 0; i < MAX_SUPPORTED_MOTORS; i++)
        customMixers[i].throttle = 0.0f;

    // do we have anything here to begin with?
    if (mixers[index].motor != NULL) {
        for (i = 0; i < mixers[index].motorCount; i++)
            customMixers[i] = mixers[index].motor[i];
    }
}

#else

void mixerInit(mixerMode_e mixerMode, motorMixer_t *initialCustomMixers)
{
    currentMixerMode = mixerMode;

    customMixers = initialCustomMixers;

    useServo = false;
}

void mixerUsePWMOutputConfiguration(pwmOutputConfiguration_t *pwmOutputConfiguration)
{
    UNUSED(pwmOutputConfiguration);
    motorCount = 4;
    servoCount = 0;

    uint8_t i;
    for (i = 0; i < motorCount; i++) {
        currentMixer[i] = mixerQuadX[i];
    }

    mixerResetMotors();
}
#endif

void mixerResetMotors(void)
{
    int i;
    // set disarmed motor values
    for (i = 0; i < MAX_SUPPORTED_MOTORS; i++)
        motor_disarmed[i] = feature(FEATURE_3D) ? flight3DConfig->neutral3d : escAndServoConfig->mincommand;
}

static void updateGimbalServos(void)
{
    pwmWriteServo(0, servo[0]);
    pwmWriteServo(1, servo[1]);
}

void writeServos(void)
{
    if (!useServo)
        return;

    switch (currentMixerMode) {
        case MIXER_BI:
            pwmWriteServo(0, servo[4]);
            pwmWriteServo(1, servo[5]);
            break;

        case MIXER_TRI:
            if (mixerConfig->tri_unarmed_servo) {
                // if unarmed flag set, we always move servo
                pwmWriteServo(0, servo[5]);
            } else {
                // otherwise, only move servo when copter is armed
                if (ARMING_FLAG(ARMED))
                    pwmWriteServo(0, servo[5]);
                else
                    pwmWriteServo(0, 0); // kill servo signal completely.
            }
            break;

        case MIXER_FLYING_WING:
            pwmWriteServo(0, servo[3]);
            pwmWriteServo(1, servo[4]);
            break;

        case MIXER_GIMBAL:
            updateGimbalServos();
            break;

        case MIXER_DUALCOPTER:
            pwmWriteServo(0, servo[4]);
            pwmWriteServo(1, servo[5]);
            break;

        case MIXER_AIRPLANE:
        case MIXER_SINGLECOPTER:
            pwmWriteServo(0, servo[3]);
            pwmWriteServo(1, servo[4]);
            pwmWriteServo(2, servo[5]);
            pwmWriteServo(3, servo[6]);
            break;

        default:
            // Two servos for SERVO_TILT, if enabled
            if (feature(FEATURE_SERVO_TILT)) {
                updateGimbalServos();
            }
            break;
    }
}

void writeMotors(void)
{
    uint8_t i;

    for (i = 0; i < motorCount; i++)
        pwmWriteMotor(i, motor[i]);


    if (feature(FEATURE_ONESHOT125)) {
        pwmCompleteOneshotMotorUpdate(motorCount);
    }
}

void writeAllMotors(int16_t mc)
{
    uint8_t i;

    // Sends commands to all motors
    for (i = 0; i < motorCount; i++)
        motor[i] = mc;
    writeMotors();
}

#ifndef USE_QUAD_MIXER_ONLY
static void airplaneMixer(void)
{
    int16_t flapperons[2] = { 0, 0 };
    int i;

    if (!ARMING_FLAG(ARMED))
        servo[7] = escAndServoConfig->mincommand; // Kill throttle when disarmed
    else
        servo[7] = constrain(rcCommand[THROTTLE], escAndServoConfig->minthrottle, escAndServoConfig->maxthrottle);
    motor[0] = servo[7];

    if (airplaneConfig->flaps_speed) {
        // configure SERVO3 middle point in GUI to using an AUX channel for FLAPS control
        // use servo min, servo max and servo rate for proper endpoints adjust
        static int16_t slow_LFlaps;
        int16_t lFlap = determineServoMiddleOrForwardFromChannel(2);

        lFlap = constrain(lFlap, servoConf[2].min, servoConf[2].max);
        lFlap = rxConfig->midrc - lFlap;
        if (slow_LFlaps < lFlap)
            slow_LFlaps += airplaneConfig->flaps_speed;
        else if (slow_LFlaps > lFlap)
            slow_LFlaps -= airplaneConfig->flaps_speed;

        servo[2] = ((int32_t)servoConf[2].rate * slow_LFlaps) / 100L;
        servo[2] += rxConfig->midrc;
    }

    if (FLIGHT_MODE(PASSTHRU_MODE)) {   // Direct passthru from RX
        servo[3] = rcCommand[ROLL] + flapperons[0];     // Wing 1
        servo[4] = rcCommand[ROLL] + flapperons[1];     // Wing 2
        servo[5] = rcCommand[YAW];                      // Rudder
        servo[6] = rcCommand[PITCH];                    // Elevator
    } else {
        // Assisted modes (gyro only or gyro+acc according to AUX configuration in Gui
        servo[3] = axisPID[ROLL] + flapperons[0];       // Wing 1
        servo[4] = axisPID[ROLL] + flapperons[1];       // Wing 2
        servo[5] = axisPID[YAW];                        // Rudder
        servo[6] = axisPID[PITCH];                      // Elevator
    }
    for (i = 3; i < 7; i++) {
        servo[i] = ((int32_t)servoConf[i].rate * servo[i]) / 100L; // servo rates
        servo[i] += determineServoMiddleOrForwardFromChannel(i);
    }
}
#endif

void mixTable(void)
{
    int16_t maxMotor;
    uint32_t i;

    // paranoia: give all servos a default command; prevents drift on unused servos with lowpass enabled
    for (i = 0; i < MAX_SUPPORTED_SERVOS; i++) {
        servo[i] = DEFAULT_SERVO_MIDDLE;
    }

    if (motorCount > 3) {
        // prevent "yaw jump" during yaw correction
        axisPID[YAW] = constrain(axisPID[YAW], -100 - abs(rcCommand[YAW]), +100 + abs(rcCommand[YAW]));
    }

    // motors for non-servo mixes
    if (motorCount > 1)
        for (i = 0; i < motorCount; i++)
            motor[i] =
                rcCommand[THROTTLE] * currentMixer[i].throttle +
                axisPID[PITCH] * currentMixer[i].pitch +
                axisPID[ROLL] * currentMixer[i].roll +
                -mixerConfig->yaw_direction * axisPID[YAW] * currentMixer[i].yaw;

#ifndef USE_QUAD_MIXER_ONLY
    // airplane / servo mixes
    switch (currentMixerMode) {
        case MIXER_BI:
            servo[4] = (servoDirection(4, 2) * axisPID[YAW]) + (servoDirection(4, 1) * axisPID[PITCH]) + determineServoMiddleOrForwardFromChannel(4);     // LEFT
            servo[5] = (servoDirection(5, 2) * axisPID[YAW]) + (servoDirection(5, 1) * axisPID[PITCH]) + determineServoMiddleOrForwardFromChannel(5);     // RIGHT
            break;

        case MIXER_TRI:
            servo[5] = (servoDirection(5, 1) * axisPID[YAW]) + determineServoMiddleOrForwardFromChannel(5); // REAR
            break;

        case MIXER_GIMBAL:
            servo[GIMBAL_SERVO_PITCH] = (((int32_t)servoConf[GIMBAL_SERVO_PITCH].rate * inclination.values.pitchDeciDegrees) / 50) + determineServoMiddleOrForwardFromChannel(GIMBAL_SERVO_PITCH);
            servo[GIMBAL_SERVO_ROLL] = (((int32_t)servoConf[GIMBAL_SERVO_ROLL].rate * inclination.values.rollDeciDegrees) / 50) + determineServoMiddleOrForwardFromChannel(GIMBAL_SERVO_ROLL);
            break;

        case MIXER_AIRPLANE:
            airplaneMixer();
            break;

        case MIXER_FLYING_WING:
            if (!ARMING_FLAG(ARMED))
                servo[7] = escAndServoConfig->mincommand;
            else
                servo[7] = constrain(rcCommand[THROTTLE], escAndServoConfig->minthrottle, escAndServoConfig->maxthrottle);
            motor[0] = servo[7];
            if (FLIGHT_MODE(PASSTHRU_MODE)) {
                // do not use sensors for correction, simple 2 channel mixing
                servo[3] = (servoDirection(3, 1) * rcCommand[PITCH]) + (servoDirection(3, 2) * rcCommand[ROLL]);
                servo[4] = (servoDirection(4, 1) * rcCommand[PITCH]) + (servoDirection(4, 2) * rcCommand[ROLL]);
            } else {
                // use sensors to correct (gyro only or gyro + acc)
                servo[3] = (servoDirection(3, 1) * axisPID[PITCH]) + (servoDirection(3, 2) * axisPID[ROLL]);
                servo[4] = (servoDirection(4, 1) * axisPID[PITCH]) + (servoDirection(4, 2) * axisPID[ROLL]);
            }
            servo[3] += determineServoMiddleOrForwardFromChannel(3);
            servo[4] += determineServoMiddleOrForwardFromChannel(4);
            break;

        case MIXER_DUALCOPTER:
            for (i = 4; i < 6; i++) {
                servo[i] = axisPID[5 - i] * servoDirection(i, 1); // mix and setup direction
                servo[i] += determineServoMiddleOrForwardFromChannel(i);
            }
            break;

        case MIXER_SINGLECOPTER:
            for (i = 3; i < 7; i++) {
                servo[i] = (axisPID[YAW] * servoDirection(i, 2)) + (axisPID[(6 - i) >> 1] * servoDirection(i, 1)); // mix and setup direction
                servo[i] += determineServoMiddleOrForwardFromChannel(i);
            }
            motor[0] = rcCommand[THROTTLE];
            break;

        default:
            break;
    }
#endif

    // do camstab
    if (feature(FEATURE_SERVO_TILT)) {
        // center at fixed position, or vary either pitch or roll by RC channel
        servo[0] = determineServoMiddleOrForwardFromChannel(0);
        servo[1] = determineServoMiddleOrForwardFromChannel(1);

        if (IS_RC_MODE_ACTIVE(BOXCAMSTAB)) {
            if (gimbalConfig->gimbal_flags & GIMBAL_MIXTILT) {
                servo[0] -= (-(int32_t)servoConf[0].rate) * inclination.values.pitchDeciDegrees / 50 - (int32_t)servoConf[1].rate * inclination.values.rollDeciDegrees / 50;
                servo[1] += (-(int32_t)servoConf[0].rate) * inclination.values.pitchDeciDegrees / 50 + (int32_t)servoConf[1].rate * inclination.values.rollDeciDegrees / 50;
            } else {
                servo[0] += (int32_t)servoConf[0].rate * inclination.values.pitchDeciDegrees / 50;
                servo[1] += (int32_t)servoConf[1].rate * inclination.values.rollDeciDegrees  / 50;
            }
        }
    }

    // constrain servos
    for (i = 0; i < MAX_SUPPORTED_SERVOS; i++)
        servo[i] = constrain(servo[i], servoConf[i].min, servoConf[i].max); // limit the values

    // forward AUX1-4 to servo outputs (not constrained)
    if (gimbalConfig->gimbal_flags & GIMBAL_FORWARDAUX) {
        // offset servos based off number already used in mixer types
        // airplane and servo_tilt together can't be used
        int8_t firstServo = servoCount - AUX_FORWARD_CHANNEL_TO_SERVO_COUNT;

        // start forwarding from this channel
        uint8_t channelOffset = AUX1;

        int8_t servoOffset;
        for (servoOffset = 0; servoOffset < AUX_FORWARD_CHANNEL_TO_SERVO_COUNT; servoOffset++) {
            if (firstServo + servoOffset < 0) {
                continue; // there are not enough servos to forward all the AUX channels.
            }
            pwmWriteServo(firstServo + servoOffset, rcData[channelOffset++]);
        }
    }

    maxMotor = motor[0];
    for (i = 1; i < motorCount; i++)
        if (motor[i] > maxMotor)
            maxMotor = motor[i];
    for (i = 0; i < motorCount; i++) {
        if (maxMotor > escAndServoConfig->maxthrottle)     // this is a way to still have good gyro corrections if at least one motor reaches its max.
            motor[i] -= maxMotor - escAndServoConfig->maxthrottle;
        if (feature(FEATURE_3D)) {
            if ((rcData[THROTTLE]) > rxConfig->midrc) {
                motor[i] = constrain(motor[i], flight3DConfig->deadband3d_high, escAndServoConfig->maxthrottle);
            } else {
                motor[i] = constrain(motor[i], escAndServoConfig->mincommand, flight3DConfig->deadband3d_low);
            }
        } else {
            motor[i] = constrain(motor[i], escAndServoConfig->minthrottle, escAndServoConfig->maxthrottle);
            if ((rcData[THROTTLE]) < rxConfig->mincheck) {
                if (!feature(FEATURE_MOTOR_STOP))
                    motor[i] = escAndServoConfig->minthrottle;
                else
                    motor[i] = escAndServoConfig->mincommand;
            }
        }
        if (!ARMING_FLAG(ARMED)) {
            motor[i] = motor_disarmed[i];
        }
    }
}


bool isMixerUsingServos(void)
{
    return useServo;
}

#endif

void generate_lowpass_coeffs2(int16_t freq, lowpass_t *filter)
{
    float fixedScaler;
    int i;

    // generates coefficients for a 2nd-order butterworth low-pass filter
    float freqf = (float)freq*0.001f;
    float omega = tanf(M_PI*freqf/2.0f);
    float scaling = 1.0f / (omega*omega + 1.4142136f*omega + 1.0f);


#ifdef UNIT_TEST
    printf("lowpass cutoff: %f, omega: %f\n", freqf, omega);
#endif
   
    filter->bf[0] = scaling * omega*omega;
    filter->bf[1] = 2.0f * filter->bf[0];
    filter->bf[2] = filter->bf[0];

    filter->af[0] = 1.0f;
    filter->af[1] = scaling * (2.0f * omega*omega - 2.0f);
    filter->af[2] = scaling * (omega*omega - 1.4142136f * omega + 1.0f);

    // Scale for fixed-point
    filter->input_bias = 1500; // Typical servo range is 1500 +/- 500
    filter->input_shift = 16;
    filter->coeff_shift = 24; 
    fixedScaler = (float)(1ULL << filter->coeff_shift);
    for (i = 0; i < LOWPASS_NUM_COEF; i++) {
        filter->a[i] = LPF_ROUND(filter->af[i] * fixedScaler);
        filter->b[i] = LPF_ROUND(filter->bf[i] * fixedScaler);
#ifdef UNIT_TEST
        printf("(%d) bf: %f af: %f b: %ld a: %ld\n", i, 
                filter->bf[i], filter->af[i], filter->b[i], filter->a[i]);
#endif
    }

    filter->freq = freq;
}

static int32_t lowpass_fixed(lowpass_t *filter, int32_t in, int16_t freq)
{
    int16_t coefIdx;
    int64_t out;
    int32_t in_s;
  
    // Check to see if cutoff frequency changed
    if (freq != filter->freq) {
        filter->init = false;
    }

    // Initialize if needed
    if (!filter->init) {
        generate_lowpass_coeffs2(freq, filter);
        for (coefIdx = 0; coefIdx < LOWPASS_NUM_COEF; coefIdx++) {
            filter->x[coefIdx] = (in - filter->input_bias) << filter->input_shift;
            filter->y[coefIdx] = (in - filter->input_bias) << filter->input_shift;
        }
        filter->init = true;
    }

    // Unbias input and scale
    in_s = (in - filter->input_bias) << filter->input_shift;

    // Delays
    for (coefIdx = LOWPASS_NUM_COEF-1; coefIdx > 0; coefIdx--) {
        filter->x[coefIdx] = filter->x[coefIdx-1];
        filter->y[coefIdx] = filter->y[coefIdx-1];
    }
    filter->x[0] = in_s;

    // Accumulate result
    out = filter->x[0] * filter->b[0];
    for (coefIdx = 1; coefIdx < LOWPASS_NUM_COEF; coefIdx++) {
        out -= filter->y[coefIdx] * filter->a[coefIdx];
        out += filter->x[coefIdx] * filter->b[coefIdx];
    }

    // Scale output by coefficient shift
    out >>= filter->coeff_shift;
    filter->y[0] = (int32_t)out;

    // Scale output by input shift and round
    out = (out + (1 << (filter->input_shift-1))) >> filter->input_shift;
    
    // Reapply bias
    out += filter->input_bias;

    return (int32_t)out;
}

void filterServos(void)
{
    int16_t servoIdx;

#if defined(MIXER_DEBUG)
    uint32_t startTime = micros();
#endif

    if (mixerConfig->servo_lowpass_enable) {
        for (servoIdx = 0; servoIdx < MAX_SUPPORTED_SERVOS; servoIdx++) {
            servo[servoIdx] = (int16_t)lowpass_fixed(&lowpassFilters[servoIdx], servo[servoIdx], mixerConfig->servo_lowpass_freq);

            // Sanity check
            servo[servoIdx] = constrain(servo[servoIdx], servoConf[servoIdx].min, servoConf[servoIdx].max);
        }
    }
#if defined(MIXER_DEBUG)
    debug[0] = (int16_t)(micros() - startTime);
#endif
}

