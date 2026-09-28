// LSM6DSL used only as a low-power wake-on-motion source
#ifndef IMU_H
#define IMU_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*imu_motion_handler_t)(void);

// Gyro off, accel 1.6 Hz low-power, latched wake-up interrupt on INT pin.
// handler runs in interrupt context when motion is detected; the interrupt
// then stays disabled until imu_rearm() is called.
// threshold_mg = 0 puts the IMU into power-down and returns false.
bool imu_init(uint16_t threshold_mg, imu_motion_handler_t handler);

// Clear the latched interrupt and enable it again
void imu_rearm(void);

bool imu_present(void);

// --- game-mode streaming (accel + gyro at ~104 Hz) ---
// Reconfigure the sensor for continuous output. Returns false if the IMU is absent.
bool imu_stream_start(void);
// Read the 12 raw bytes (gyro XYZ then accel XYZ, int16 LE) into out[12].
bool imu_stream_read(uint8_t out[12]);
// Restore the low-power wake-on-motion configuration used by the tracker.
void imu_stream_stop(uint16_t threshold_mg);

#endif
