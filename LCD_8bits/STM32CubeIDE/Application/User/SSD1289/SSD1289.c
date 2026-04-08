/*
 * SSD1289.c
 *
 *  Created on: Sep 24, 2024
 *      Author: aaas1
 */

#include "SSD1289.h"


#define _assert_reg(x,y) if(!dv->put_reg(x,y)) return false

static bool _init_power_control(SSD1289_drive_t *dv) {
	//DCT = fosc/4, BT = Vci*4, DC - fosc/4, AP = Large to Maximum
	_assert_reg(SSD1289_POWER_CONTROL_REG, 0xAEAC);
	//VCR = 5.8V
	_assert_reg(SSD1289_POWER_CONTROL2_REG, 0x0007);
	//VRH = Vref*2.725
	_assert_reg(SSD1289_POWER_CONTROL3_REG, 0x000F);
	//VDV = VLCD63*1.08
	_assert_reg(SSD1289_POWER_CONTROL4_REG, 0x2900);
	//VCM = VLCD63*0.86
	_assert_reg(SSD1289_POWER_CONTROL5_REG, 0x00B3);
	return true;
}

bool SSD1289_init(SSD1289_handle *lcd, SSD1289_drive_t driver, uint32_t width,
		uint32_t heigh) {
	lcd->width = width;
	lcd->heigh = heigh;
	lcd->driver = driver;
	SSD1289_drive_t *dv = &lcd->driver;
	if (!dv->reset())
		return false;
	if (!dv->init_sig())
		return false;
	dv->delayMs(20);

	// set R00h at 0001h (OSCEN=1)
	_assert_reg(SSD1289_OSC_REG, 0x0001);
	if (!_init_power_control(dv))
		return false;

	//MUX = 319, RL = 0, REV = 1, CAD = 0(cs on common)
	//GBR = 1(BGR), TB = 1(G0-G319)
	_assert_reg(SSD1289_DIVER_OUTPUT_REG, 0x693F);
	_assert_reg(SSD1289_LDC_DRIVE_AC_REG, 0x0600);

	// set R10h at 0000h (Exit sleep mode)
	_assert_reg(SSD1289_SLEEP_MODE_REG, 0x0000);
	dv->delayMs(10);

	_assert_reg(SSD1289_ENTRY_MODE_REG, 0x60B0);

	_assert_reg(SSD1289_COMPARE_REGISTER1_REG, 0x0000);
	_assert_reg(SSD1289_COMPARE_REGISTER2_REG, 0x0000);
	_assert_reg(SSD1289_HORIZONTAL_PORCH_REG, 0xEF1C);
	_assert_reg(SSD1289_VERTICAL_PORCH_REG, 0x0003);
	_assert_reg(SSD1289_DISPLAY_CONTROL_REG, 0x0233);
	_assert_reg(SSD1289_FRAME_CYCLE_CONTROL_REG, 0x5312);
	_assert_reg(SSD1289_GATE_SCAN_START_REG, 0x0000);

	_assert_reg(SSD1289_VERTICAL_SCROLL_CONTROL1_REG, 0x0000);
	_assert_reg(SSD1289_VERTICAL_SCROLL_CONTROL2_REG, 0x0000);
	_assert_reg(SSD1289_FIRST_WINDOW_START_REG, 0x0000);
	_assert_reg(SSD1289_FIRST_WINDOW_END_REG, 0x013F);

	_assert_reg(SSD1289_HORIZONTAL_RAM_ADDRESS_POSITION_REG, 0xEF00);
	_assert_reg(SSD1289_VERTICAL_RAM_ADDRESS_START_POSITION_REG, 0x0000);
	_assert_reg(SSD1289_VERTICAL_RAM_ADDRESS_END_POSITION_REG, 0x013F);
	_assert_reg(SSD1289_SECOND_WINDOW_START_REG, 0x0000);
	_assert_reg(SSD1289_SECOND_WINDOW_END_REG, 0x0000);

	//gamma control
	_assert_reg(SSD1289_GAMMA_CONTROL1_REG, 0x0707);
	_assert_reg(SSD1289_GAMMA_CONTROL2_REG, 0x0204);
	_assert_reg(SSD1289_GAMMA_CONTROL3_REG, 0x0204);
	_assert_reg(SSD1289_GAMMA_CONTROL4_REG, 0x0502);
	_assert_reg(SSD1289_GAMMA_CONTROL5_REG, 0x0507);
	_assert_reg(SSD1289_GAMMA_CONTROL6_REG, 0x0204);
	_assert_reg(SSD1289_GAMMA_CONTROL7_REG, 0x0204);
	_assert_reg(SSD1289_GAMMA_CONTROL8_REG, 0x0502);
	_assert_reg(SSD1289_GAMMA_CONTROL9_REG, 0x0302);
	_assert_reg(SSD1289_GAMMA_CONTROL10_REG, 0x0302);

	_assert_reg(SSD1289_RAM_WRITE_DATA_MASK1_REG, 0x0000);
	_assert_reg(SSD1289_RAM_WRITE_DATA_MASK2_REG, 0x0000);
	_assert_reg(SSD1289_FRAME_FREQUENCY_REG, 0x8000);
	_assert_reg(SSD1289_SET_GDDRAM_Y_ADDRESS_COUNTER_REG, 0);
	_assert_reg(SSD1289_SET_GDDRAM_X_ADDRESS_COUNTER_REG, 0);

	_assert_reg(SSD1289_ENTRY_MODE_REG, 0x60b0);
	return true;
}

bool SSD1289_set_cursor(SSD1289_handle *lcd, uint16_t x, uint16_t y) {
	SSD1289_drive_t *dv = &lcd->driver;
	if (x > lcd->width)
		x = lcd->width;
	if (y > lcd->heigh)
		y = lcd->heigh;
	_assert_reg(SSD1289_SET_GDDRAM_X_ADDRESS_COUNTER_REG, x);
	_assert_reg(SSD1289_SET_GDDRAM_Y_ADDRESS_COUNTER_REG, y);
	return true;
}

bool SSD1289_pixel(SSD1289_handle *lcd, uint16_t x, uint16_t y, uint16_t color) {
	SSD1289_drive_t *dv = &lcd->driver;
	if (!SSD1289_set_cursor(lcd, x, y))
		return false;
	_assert_reg(SSD1289_RAM_DATA_WRITE_REG, color);
	return true;
}

bool SSD1289_set_window(SSD1289_handle *lcd, uint16_t x_pos, uint16_t y_pos,
		uint16_t width, uint16_t heigh) {
	SSD1289_drive_t *dv = &lcd->driver;
	uint32_t x_size = x_pos + width - 1;
	uint32_t y_size = y_pos + heigh - 1;
	_assert_reg(SSD1289_VERTICAL_RAM_ADDRESS_START_POSITION_REG, y_pos);
	_assert_reg(SSD1289_VERTICAL_RAM_ADDRESS_END_POSITION_REG, y_size);
	_assert_reg(SSD1289_HORIZONTAL_RAM_ADDRESS_POSITION_REG,
			((x_size << 8) + (x_pos & 0xff)));
	if(!SSD1289_set_cursor(lcd, x_pos, y_pos))
			return false;
	return true;
}

bool SSD1289_transmit_to_ram(SSD1289_handle *lcd, uint16_t* data, uint32_t data_len) {
	if(!lcd->driver.reg_wirte_block(SSD1289_RAM_DATA_WRITE_REG, data, data_len))
			return false;
	return true;
}

bool SSD1289_clear(SSD1289_handle *lcd, uint16_t color) {
	if(!SSD1289_set_window(lcd, 0, 0, lcd->width, lcd->heigh))
		return false;
	if(!lcd->driver.put_cmd(SSD1289_RAM_DATA_WRITE_REG))
		return false;
	for(int i = 0; i < lcd->width* lcd->heigh; i++)
		if(!lcd->driver.put_data(color))
			return false;
	return true;
}
