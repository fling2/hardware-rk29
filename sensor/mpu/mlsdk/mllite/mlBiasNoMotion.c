/*
 $License:
    Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.
 $
 */

/******************************************************************************
 *
 * $Id: mlBiasNoMotion.c 6218 2011-10-19 04:13:47Z mcaramello $
 *
 *****************************************************************************/

#include "mlBiasNoMotion.h"
#include "ml.h"
#include "mlinclude.h"
#include "mlos.h"
#include "mlFIFO.h"
#include "dmpKey.h"
#include "accel.h"
#include "mlMathFunc.h"
#include "mldl.h"
#include "mlstates.h"
#include "mlSetGyroBias.h"
#include "mlmath.h"

#include "log.h"
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-bias_no_mot"

#undef  MPL_LOG_NDEBUG
#define MPL_LOG_NDEBUG 1 /* Use 0 to turn on MPL_LOGV output */

/**
 *  @brief  inv_set_motion_callback is used to register a callback function that
 *          will trigger when a change of motion state is detected.
 *
 *  @pre    inv_dmp_open()
 *          @ifnot MPL_MF
 *              or inv_open_low_power_pedometer()
 *              or inv_eis_open_dmp()
 *          @endif
 *          and inv_dmp_start()
 *          must <b>NOT</b> have been called.
 *
 *  @param  func    A user defined callback function accepting a
 *                  lite_fusion->motion_state parameter, the new motion state.
 *                  May be one of INV_MOTION or INV_NO_MOTION.
 *  @return INV_SUCCESS if successful or Non-zero error code otherwise.
 */
inv_error_t inv_set_motion_callback(void (*func) (unsigned short motion_state))
{
    INVENSENSE_FUNC_START;

    if ((inv_get_state() != INV_STATE_DMP_OPENED) &&
        (inv_get_state() != INV_STATE_DMP_STARTED))
        return INV_ERROR_SM_IMPROPER_STATE;

    inv_params_obj.motion_cb_func = func;

    return INV_SUCCESS;
}


inv_error_t inv_update_bias(void)
{
    INVENSENSE_FUNC_START;
    inv_error_t result;
    unsigned char regs[12], is_enabled;
    short bias[GYRO_NUM_AXES];
    unsigned short motion_flag;

    result = inv_bias_nomot_is_enabled(&is_enabled);
    if (result) {
        LOG_RESULT_LOCATION(result);
        return result;
    }

    if (is_enabled && inv_get_gyro_present()) {

        regs[0] = DINAA0 + 3;
        result = inv_set_mpu_memory(KEY_FCFG_6, 1, regs);
        if (result) {
            LOG_RESULT_LOCATION(result);
            return result;
        }

        result = inv_get_mpu_memory(KEY_D_1_244, 12, regs);
        if (result) {
            LOG_RESULT_LOCATION(result);
            return result;
        }

        inv_convert_bias(regs, bias);

        result = inv_check_max_gyro_bias(bias);

        if (result) {
            regs[0] = DINAA0 + 15;
            result = inv_set_mpu_memory(KEY_FCFG_6, 1, regs);
            if (result) {
                LOG_RESULT_LOCATION(result);
                return result;
            }
            return INV_WARNING_GYRO_MAG;
        }

        result = inv_get_mpu_memory(KEY_D_1_98, 2, regs);
        if (result) {
            LOG_RESULT_LOCATION(result);
            return result;
        }

        motion_flag = (unsigned short)regs[0] * 256 + (unsigned short)regs[1];

        if (motion_flag != inv_obj.lite_fusion->motion_duration)
            return INV_WARNING_MOTION_RACE;

        regs[0] = DINAA0 + 15;
        result = inv_set_mpu_memory(KEY_FCFG_6, 1, regs);
        if (result) {
            LOG_RESULT_LOCATION(result);
            return result;
        }

        result = inv_set_gyro_bias_in_hw_unit(bias, INV_SGB_NO_MOTION);
        if (result) {
            LOG_RESULT_LOCATION(result);
            return result;
        }

        result =
            inv_serial_read(inv_get_serial_handle(), inv_get_mpu_slave_addr(),
                            MPUREG_TEMP_OUT_H, 2, regs);
        if (result) {
            LOG_RESULT_LOCATION(result);
            return result;
        }
        result = inv_set_mpu_memory(KEY_DMP_PREVPTAT, 2, regs);
        if (result) {
            LOG_RESULT_LOCATION(result);
            return result;
        }

        inv_obj.lite_fusion->got_no_motion_bias = true;
    }
    return INV_SUCCESS;
}

inv_error_t MLAccelMotionDetection(struct inv_obj_t *inv_obj)
{
    long gain;
    long rate;
    inv_error_t result;
    long accel[3], temp;
    long accel_mag;
    unsigned long currentTime;
    int kk;

    if (!inv_accel_present()) {
        return INV_SUCCESS;
    }

    currentTime = inv_get_tick_count();

    /* we always run the accel low pass filter at the
       highest sample rate possible */
    result = inv_get_accel(accel);
    if (result != INV_ERROR_FEATURE_NOT_ENABLED) {
        if (result) {
            LOG_RESULT_LOCATION(result);
            return result;
        }
        rate = inv_get_fifo_rate() * 5 + 5;
        if (rate > 200)
            rate = 200;

        gain = inv_obj->lite_fusion->accel_lpf_gain * rate;

        accel_mag = 0;
        for (kk = 0; kk < ACCEL_NUM_AXES; ++kk) {
            inv_obj->lite_fusion->accel_lpf[kk] =
                inv_q30_mult(((1L << 30) - gain), inv_obj->lite_fusion->accel_lpf[kk]);
            inv_obj->lite_fusion->accel_lpf[kk] += inv_q30_mult(gain, accel[kk]);
            temp = accel[0] - inv_obj->lite_fusion->accel_lpf[0];
            if ( ABS(temp) > inv_obj->lite_fusion->no_motion_accel_sqrt_threshold ) {
                accel_mag = inv_obj->lite_fusion->no_motion_accel_threshold + 1;
                break;
            }
            accel_mag += temp * temp;
        }

        if (accel_mag > inv_obj->lite_fusion->no_motion_accel_threshold) {
            inv_obj->lite_fusion->no_motion_accel_time = currentTime;

            /* Check for change of state */
            if (!inv_get_gyro_present())
                inv_set_motion_state(INV_MOTION);

        } else if ((currentTime - inv_obj->lite_fusion->no_motion_accel_time) >
                   5 * inv_obj->lite_fusion->motion_duration) {
            /* We have no motion according to accel.
               Check fsor change of state */
            if (!inv_get_gyro_present())
                inv_set_motion_state(INV_NO_MOTION);
        }
    }
    return INV_SUCCESS;
}

/**
 * @internal
 * @brief   Manually update the motion/no motion status.  This is a
 *          convienence function for implementations that do not wish to use
 *          inv_update_data.
 *          This function can be called periodically to check for the
 *          'no motion' state and update the internal motion status and bias
 *          calculations.
 */
inv_error_t MLPollMotionStatus(struct inv_obj_t * inv_obj)
{
    INVENSENSE_FUNC_START;
    unsigned char regs[4] = {0};
    unsigned short motionFlag = 0;
    unsigned long currentTime;
    inv_error_t result;

    result = MLAccelMotionDetection(inv_obj);

    currentTime = inv_get_tick_count();

    // If it is not time to poll for a no motion event, return
    if (((inv_obj->sys->interrupt_sources & INV_INT_MOTION) == 0) &&
        ((currentTime - inv_obj->lite_fusion->poll_no_motion_time) <= 1000))
        return INV_SUCCESS;

    inv_obj->lite_fusion->poll_no_motion_time = currentTime;

    if (inv_get_gyro_present()) {
        static long repeatBiasUpdateTime = 0;

        result = inv_get_mpu_memory(KEY_D_1_98, 2, regs);
        if (result) {
            LOG_RESULT_LOCATION(result);
            return result;
        }

        motionFlag = (unsigned short)regs[0] * 256 + (unsigned short)regs[1];

        MPL_LOGV("motionFlag from RAM : 0x%04X\n", motionFlag);
        if (motionFlag == inv_obj->lite_fusion->motion_duration) {
            if (inv_obj->lite_fusion->motion_state == INV_MOTION) {
                result = inv_update_bias();
                /* Make sure we were able to read the gyro bias before
                   changing to a motion state. */
                if (result == INV_WARNING_MOTION_RACE) 
                    return INV_SUCCESS;

                if (result == INV_WARNING_GYRO_MAG) {
                    //Need to reset Motion Flag and time counter
                    regs[0] = 0;
                    regs[1] = 0;
                    regs[2] = 0;
                    regs[3] = 0;
                    result = inv_set_mpu_memory(KEY_D_1_98, 2, regs);
                    if (result) {
                        LOG_RESULT_LOCATION(result);
                        return result;
                    }
                    result = inv_set_mpu_memory(KEY_D_1_100, 4, regs);
                    if (result) {
                        LOG_RESULT_LOCATION(result);
                        return result;
                    }
                    return INV_SUCCESS;
                }

                repeatBiasUpdateTime = inv_get_tick_count();

                regs[0] = DINAD8 + 1;
                regs[1] = DINA0C;
                regs[2] = DINAD8 + 2;
                result = inv_set_mpu_memory(KEY_CFG_18, 3, regs);
                if (result) {
                    LOG_RESULT_LOCATION(result);
                    return result;
                }

                regs[0] = 0;
                regs[1] = 5;
                result = inv_set_mpu_memory(KEY_D_1_106, 2, regs);
                if (result) {
                    LOG_RESULT_LOCATION(result);
                    return result;
                }

                /* trigger no motion callback */
                inv_set_motion_state(INV_NO_MOTION);
            }
        }
        if (motionFlag == 5) {
            if (inv_obj->lite_fusion->motion_state == INV_NO_MOTION) {
                regs[0] = DINAD8 + 2;
                regs[1] = DINA0C;
                regs[2] = DINAD8 + 1;
                result = inv_set_mpu_memory(KEY_CFG_18, 3, regs);
                if (result) {
                    LOG_RESULT_LOCATION(result);
                    return result;
                }

                regs[0] =
                    (unsigned char)((inv_obj->lite_fusion->motion_duration >> 8) & 0xff);
                regs[1] = (unsigned char)(inv_obj->lite_fusion->motion_duration & 0xff);
                result = inv_set_mpu_memory(KEY_D_1_106, 2, regs);
                if (result) {
                    LOG_RESULT_LOCATION(result);
                    return result;
                }

                /* trigger no motion callback */
                inv_set_motion_state(INV_MOTION);
            }
        }
        if (inv_obj->lite_fusion->motion_state == INV_NO_MOTION) {
            if ((inv_get_tick_count() - repeatBiasUpdateTime) > 4000) {
                result = inv_update_bias();
                /* Make sure we were able to read the gyro bias before
                   changing to a motion state. */
                if ( result == INV_WARNING_MOTION_RACE )
                    return INV_SUCCESS;
                repeatBiasUpdateTime = inv_get_tick_count();
            }
        }
    }
    return INV_SUCCESS;
}

inv_error_t inv_enable_bias_no_motion(void)
{
    inv_error_t result;
    unsigned char is_registered;

    /* Check if already registered. */
    result = inv_check_fifo_callback(MLPollMotionStatus, &is_registered);
    if (result) {
        LOG_RESULT_LOCATION(result);
        return result;
    }

    if (is_registered) {
        return INV_SUCCESS;
    }

    result = inv_register_fifo_rate_process(MLPollMotionStatus,
        INV_PRIORITY_BIAS_NO_MOTION);
    if (result) {
        LOG_RESULT_LOCATION(result);
        return result;
    }


    MPL_LOGV("Bias no Motion enabled.\n");

    return INV_SUCCESS;
}

inv_error_t inv_disable_bias_no_motion(void)
{
    inv_error_t result;
    unsigned char is_registered;

    /* Check if already unregistered. */
    result = inv_check_fifo_callback(MLPollMotionStatus, &is_registered);
    if (result) {
        LOG_RESULT_LOCATION(result);
        return result;
    }

    if (!is_registered) {
        return INV_SUCCESS;
    }

    result = inv_unregister_fifo_rate_process(MLPollMotionStatus);
    if (result) {
        LOG_RESULT_LOCATION(result);
        return result;
    }


    MPL_LOGV("Bias no Motion disabled.\n");

    return INV_SUCCESS;
}

/**
 *  @brief      @e inv_bias_nomot_is_enabled checks if the regular Bias from
 *              No Motion algorithm is currently being used.
 *
 *  @param[out] is_enabled  True if Bias from No Motion is enabled.
 *
 *  @return     INV_SUCCESS if successful.
 */
inv_error_t inv_bias_nomot_is_enabled(unsigned char *is_enabled)
{
    unsigned char is_registered;
    inv_error_t result;

    result = inv_check_fifo_callback(MLPollMotionStatus, &is_registered);
    if (result) {
        LOG_RESULT_LOCATION(result);
        return result;
    }

    is_enabled[0] = is_registered;

    return INV_SUCCESS;
}


