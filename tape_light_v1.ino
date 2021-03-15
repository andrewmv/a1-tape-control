// 2021/02 Andrew Villeneuve <andrewmv@gmail.com>
// Monitor a Sony A1 Control serial interface for tape deck events
// and control lights for the corresponding Now Playing shelves

#include "limits.h"

// Arduino pin configuration
const int PIN_A1_READ = 2;
const int EXT_INT_A1 = 1;
const int PIN_CATHODE_A = 5;
const int PIN_CATHODE_B = 6;
// R, G, B, W
const int LED_PINS[] = {9, 10, 11, 3};

const int STATUS_LED = 13;

// LED Configuration - RGBW
const int LED_PAUSE[] = {255, 255, 255, 255};
const int LED_PLAY[] = {0, 0, 0, 0};

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
const int PW_INIT = 2200;
const int PW_ONE = 1000;
const int PW_ZERO = 400;

volatile int state;
volatile int bitcount;
volatile unsigned long pulse_start;
volatile byte data_byte;
volatile byte address;
volatile byte command;
volatile int tape_a_target;
volatile int tape_b_target;
volatile int tape_a_state;
volatile int tape_b_state;
volatile int command_queue;

// DEBUG
volatile unsigned long last_init_pulse;

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
	last_init_pulse = 0;
	pinMode(PIN_A1_READ, INPUT);
	for (int i = 0; i < 4; i++) {
		pinMode(LED_PINS[i], OUTPUT);
		digitalWrite(LED_PINS[i], LOW);
	}
	pinMode(STATUS_LED, OUTPUT);
	digitalWrite(STATUS_LED, LOW);
	// attachInterrupt(digitalPinToInterrupt(PIN_A1_READ), falling_edge, FALLING);
	// attachInterrupt(digitalPinToInterrupt(PIN_A1_READ), rising_edge, RISING);
	attachInterrupt(digitalPinToInterrupt(PIN_A1_READ), edge_change, CHANGE);
	Serial.println("Ready");
}

void loop() {
	delay(1000);
	// Serial.print("Machine state: ");
	// Serial.println(state, HEX);
 //    Serial.print("bits in queue: ");
 //    Serial.println(bitcount);
	while (command_queue > 0) {
		Serial.println("Processing new command");
		Serial.print("Last init width: ");
		Serial.print(last_init_pulse);
		Serial.println("us");
        // Serial.print("pulse_start: ");
        // Serial.println(pulse_start);
		parse_commands();
	}
	apply_states();
}

void apply_states() {
	// Set cathode states
	if (tape_a_state == TS_PLAYING) {
		digitalWrite(PIN_CATHODE_A, LOW);
	} else {
		digitalWrite(PIN_CATHODE_A, HIGH);
	}
	if (tape_b_state == TS_PLAYING) {
		digitalWrite(PIN_CATHODE_B, LOW);
	} else {
		digitalWrite(PIN_CATHODE_B, HIGH);
	}

	if ((tape_a_state == TS_PLAYING) || (tape_b_state == TS_PLAYING)) {
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
		Serial.println("Found command from known device address");
		switch (command) {
			case CMD_TAPE_A_PLAY:
				Serial.println("Tape A Playing");
				tape_a_state = TS_PLAYING;
				break;
			case CMD_TAPE_B_PLAY:
				Serial.println("Tape B Playing");
				tape_b_state = TS_PLAYING;
				break;
			case CMD_TAPE_A_STOP:
				Serial.println("Tape A Stopped");
				tape_a_state = TS_STOPPED;
				break;
			case CMD_TAPE_B_STOP:
				Serial.println("Tape B Stopped");
				tape_b_state = TS_STOPPED;
				break;
			default:
				Serial.print("Ignoring unknown command on bus: ");
                                Serial.println(command, HEX);
		}
	} else {
		Serial.print("Ignoring unknown address on bus: ");
                Serial.println(address, HEX);
	}
	command_queue--;
}

int handle_bit(unsigned long pulse_width) {
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

// The Arduino bootloader doesn't seem to allow R and F interrupts
// to both be bound to the same pin, because fuck you
void edge_change() {
	unsigned long ts = micros();
	if (digitalRead(PIN_A1_READ) == HIGH) {
		rising_edge(ts);
	} else {
		falling_edge(ts);
	}
}

void falling_edge(unsigned long ts) {
	pulse_start = ts;
	if (state == STATE_NO_FALL) {
		state = STATE_FIND_INIT;
	}
}

void rising_edge(unsigned long ts) {
	unsigned long pulse_end = ts;

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
		bitcount = 0;
		data_byte = 0;
		address = 0;
		command = 0;
		last_init_pulse = pulse_width;
	} else if (state == STATE_FIND_INIT) {
		// Ignore bits not prefixed by an INIT
		return;
	} else {
		data_byte = data_byte | (bit << (7 - bitcount));
		bitcount++;
		if (bitcount >= 8) {
			if (state == STATE_READ_ADDR) {
				address = data_byte;
				data_byte = 0;
				state = STATE_READ_CMD;
			} else {
				command = data_byte;
				command_queue++;
			}
			bitcount = 0;
			data_byte = 0;
		}
	}
}
