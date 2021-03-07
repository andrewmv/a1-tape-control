// 2021/02 Andrew Villeneuve <andrewmv@gmail.com>
// Monitor a Sony A1 Control serial interface for tape deck events
// and control lights for the corresponding Now Playing shelves

#include "limits.h"

// Arduino pin configuration
const int PIN_A1_READ = 2;
const int EXT_INT_A1 = 0;
const int PIN_CATHODE_A = 5;
const int PIN_CATHODE_B = 6;
// R, G, B, W
const int LED_PINS[] = {9, 10, 11, 3};

// LED Configuration - RGBW
const int LED_PLAY[] = {0, 0, 0, 0};
const int LED_PAUSE[] = {255, 255, 255, 255};

// Tape deck command configuration
const byte ADDR_TAPE = 0xac;
const byte CMD_TAPE_A_PLAY = 0x40;
const byte CMD_TAPE_B_PLAY = 0x00;
const byte CMD_TAPE_A_STOP = 0x41;
const byte CMD_TAPE_B_STOP = 0x01;
// Not using these, but they're available anyway
const byte CMD_TAPE_A_END = 0x4a;
const byte CMD_TAPE_B_END = 0x0a;
const byte CMD_TAPE_A_PAUSE = 0x42;
const byte CMD_TAPE_B_PAUSE = 0x02;

// State machine states
const int STATE_FIND_INIT = 0;	// Next thing we expect to see is an init/sync pulse
const int STATE_READ_ADDR = 1;	// Currently reading device address
const int STATE_READ_CMD = 2;	// currently reading device data
const int STATE_NO_FALL = 3; 	// Boot up state until we've found our first falling edge

// Tape states
const int TS_PLAYING = 0;
const int TS_STOPPED = 1;

// LED states
const int LED_FADE_STEP = 8;

// pulse width timings in microseconds
const int PW_INIT = 2300;
const int PW_ONE = 1100;
const int PW_ZERO = 500;

int state;
int bitcount;
unsigned long pulse_start;
byte data_byte;
byte address;
byte command;
int tape_a_target;
int tape_b_target;
int tape_a_state;
int tape_b_state;
int command_queue;

void setup() {
	Serial.begin(9600);
	state = STATE_NO_FALL;
	bitcount = 0;
	pulse_start = 0;
	data_byte = 0;
	address = 0;
	command = 0;
	tape_a_state = TS_STOPPED;
	tape_b_state = TS_STOPPED;
	tape_a_target = 0;
	tape_b_target = 0;
	command_queue = 0;
	pinMode(PIN_A1_READ, INPUT);
	for (int i = 0; i < 4; i++) {
		pinMode(LED_PINS[i], OUTPUT);
		digitalWrite(LED_PINS[i], LOW);
	}
	attachInterrupt(EXT_INT_A1, falling_edge, FALLING);
	attachInterrupt(EXT_INT_A1, rising_edge, RISING);
	Serial.println("Ready");
}

void loop() {
	delay(500);
	while (command_queue > 0) {
		Serial.println("Processing new command");
		parse_commands();
	}
	apply_states();
}

void apply_states() {
	// Set cathode states
	if (tape_a_state == TS_PLAYING) {
		digitalWrite(PIN_CATHODE_A, HIGH);
	} else {
		digitalWrite(PIN_CATHODE_A, LOW);
	}
	if (tape_b_state == TS_PLAYING) {
		digitalWrite(PIN_CATHODE_B, HIGH);
	} else {
		digitalWrite(PIN_CATHODE_B, LOW);
	}

	if (tape_a_state | tape_b_state == TS_PLAYING) {
		for (int i = 0; i < 4; i++) {
			analogWrite(LED_PINS[i], LED_PLAY[i]);
		}
	} else {
		for (int i = 0; i < 4; i++) {
			analogWrite(LED_PINS[i], LED_PAUSE[i]);
		}
	}
}

// For fading. Not implemented yet.
// int get_transition(state, target) {
// 	if (state < target) {
// 		state += LED_FADE_STEP;
// 	} 
// 	if (stage > target) {
// 		state -= LED_FADE_STEP;
// 	}
// }

void parse_commands() {
	if (address == ADDR_TAPE) {
		switch (command) {
			case CMD_TAPE_A_PLAY:
				tape_a_target = TS_PLAYING;
				break;
			case CMD_TAPE_B_PLAY:
				tape_b_target = TS_PLAYING;
				break;
			case CMD_TAPE_A_STOP:
				tape_a_target = TS_STOPPED;
				break;
			case CMD_TAPE_B_STOP:
				tape_b_target = TS_STOPPED;
				break;
		}
	}
	command_queue--;
}

int handle_bit(long pulse_width) {
	int rc;
	if (pulse_width > PW_INIT) {
		// Pulse bit in INIT/SYNC
		rc = -1;
	} else if (pulse_width > PW_ONE) {
		// Pulse bit is one
		rc = 1;
	} else {
		// Pulse bit is zero
		rc = 0;
	}
	return rc;
}

void falling_edge() {
	pulse_start = micros();
	if (state == STATE_NO_FALL) {
		state = STATE_FIND_INIT;
	}
}

void rising_edge() {
	unsigned long pulse_end = micros();

	// If we haven't seen our first falling edge, do nothing
	if (state == STATE_NO_FALL) {
		return;
	}

	// Calculate the pulse width in microseconds
	unsigned long pulse_width;
	if (pulse_end < pulse_start) {
		pulse_width = ULONG_MAX - pulse_start + pulse_end;
	} else {
		pulse_width = pulse_end - pulse_start;
	}

	// Determine bit value of pulse
	int bit = handle_bit(pulse_width);

	// State machine
	if (bit == -1) {
		// Reset state machine on INIT regardless of previous state
		state = STATE_READ_ADDR;
	} else if (state == STATE_FIND_INIT) {
		// Ignore bits not prefixed by an INIT
		return;
	} else {
		data_byte = data_byte | (bit << (7 - bitcount));
		bitcount++;
		if (bitcount >= 8) {
			bitcount = 0;
			if (state == STATE_READ_ADDR) {
				address = data_byte;
				state = STATE_READ_CMD;
			} else {
				command = data_byte;
				command_queue++;
			}
		}
	}
}
