#include "unity.h"
#include "bmp280.h"
#include <stddef.h>
#include <stdint.h>

/* Worked example from the Bosch BMP280 datasheet (BST-BMP280-DS001 §3.11.3,
   "computation formulae" appendix): this exact calibration set with
   raw_t=519888 / raw_p=415148 must yield 25.08 °C and 100653.27 Pa. */
static const bmp280_calib_t EXAMPLE_CALIB = {
    .dig_T1 = 27504,
    .dig_T2 = 26435,
    .dig_T3 = -1000,
    .dig_P1 = 36477,
    .dig_P2 = -10685,
    .dig_P3 = 3024,
    .dig_P4 = 2855,
    .dig_P5 = 140,
    .dig_P6 = -7,
    .dig_P7 = 15500,
    .dig_P8 = -14600,
    .dig_P9 = 6000,
};

TEST_CASE("bmp280: datasheet example temperature", "[bmp280]")
{
    int32_t t_fine = 0;
    int32_t t_centi = bmp280_compensate_t(519888, &EXAMPLE_CALIB, &t_fine);
    TEST_ASSERT_EQUAL_INT32(2508, t_centi); /* 25.08 °C */
}

TEST_CASE("bmp280: datasheet example pressure", "[bmp280]")
{
    int32_t t_fine = 0;
    (void)bmp280_compensate_t(519888, &EXAMPLE_CALIB, &t_fine);
    uint32_t p_q248 = bmp280_compensate_p(415148, &EXAMPLE_CALIB, t_fine);
    float pa = (float)p_q248 / 256.0f;
    /* Datasheet: 100653.27 Pa (1006.5327 hPa); integer math tolerance ±0.1 Pa */
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100653.27f, pa);
}

TEST_CASE("bmp280: pressure guards division by zero", "[bmp280]")
{
    bmp280_calib_t c = EXAMPLE_CALIB;
    c.dig_P1 = 0; /* forces var1 == 0 inside compensation */
    int32_t t_fine = 0;
    (void)bmp280_compensate_t(519888, &c, &t_fine);
    TEST_ASSERT_EQUAL_UINT32(0, bmp280_compensate_p(415148, &c, t_fine));
}

/* ── BME280 humidity compensation (feature 007) ──
   BST-BME280-DS002 gives no single numeric worked example for humidity, so we
   cross-check the 32-bit fixed-point port against the datasheet's own
   double-precision reference formula (research D6). T/P coefficients reuse the
   BMP280 datasheet example above so t_fine is a known constant. */

static const bmp280_calib_t H_CALIB = {
    .dig_T1 = 27504, .dig_T2 = 26435, .dig_T3 = -1000,
    .dig_P1 = 36477, .dig_P2 = -10685, .dig_P3 = 3024, .dig_P4 = 2855,
    .dig_P5 = 140,   .dig_P6 = -7,     .dig_P7 = 15500,
    .dig_P8 = -14600, .dig_P9 = 6000,
    /* representative real-part humidity coefficients */
    .dig_H1 = 75, .dig_H2 = 362, .dig_H3 = 0,
    .dig_H4 = 317, .dig_H5 = 0, .dig_H6 = 30,
};

#define H_T_FINE 128422 /* bmp280_compensate_t(519888, H_CALIB, &t_fine) */

/* Datasheet BST-BME280-DS002 §4.2.3, "compensation formula in double precision". */
static double compensate_h_double(int32_t adc_H, const bmp280_calib_t *c, int32_t t_fine)
{
    double var_H = (double)t_fine - 76800.0;
    var_H = ((double)adc_H - ((double)c->dig_H4 * 64.0 + (double)c->dig_H5 / 16384.0 * var_H)) *
            ((double)c->dig_H2 / 65536.0 *
             (1.0 + (double)c->dig_H6 / 67108864.0 * var_H *
                        (1.0 + (double)c->dig_H3 / 67108864.0 * var_H)));
    var_H = var_H * (1.0 - (double)c->dig_H1 * var_H / 524288.0);
    if (var_H > 100.0) var_H = 100.0;
    if (var_H < 0.0)   var_H = 0.0;
    return var_H;
}

TEST_CASE("bme280: t_fine constant for the humidity calib set", "[bmp280]")
{
    int32_t t_fine = 0;
    (void)bmp280_compensate_t(519888, &H_CALIB, &t_fine);
    TEST_ASSERT_EQUAL_INT32(H_T_FINE, t_fine);
}

TEST_CASE("bme280: fixed-point humidity tracks the double formula", "[bmp280]")
{
    const int32_t raw[] = {22000, 25000, 28000, 31000, 34000};
    for (size_t i = 0; i < sizeof(raw) / sizeof(raw[0]); i++) {
        double got = (double)bme280_compensate_h(raw[i], &H_CALIB, H_T_FINE) / 1024.0;
        double ref = compensate_h_double(raw[i], &H_CALIB, H_T_FINE);
        TEST_ASSERT_FLOAT_WITHIN(1.0f, (float)ref, (float)got);
    }
}

TEST_CASE("bme280: humidity clamps to [0, 100] %RH", "[bmp280]")
{
    /* far-dry raw code → clamped to 0 */
    TEST_ASSERT_EQUAL_UINT32(0, bme280_compensate_h(0, &H_CALIB, H_T_FINE));
    /* far-wet raw code → clamped to exactly 100 %RH (102400 in Q22.10) */
    TEST_ASSERT_EQUAL_UINT32(102400, bme280_compensate_h(100000, &H_CALIB, H_T_FINE));
    /* nothing in between ever exceeds 100 %RH */
    TEST_ASSERT_TRUE(bme280_compensate_h(45000, &H_CALIB, H_T_FINE) <= 102400);
}
