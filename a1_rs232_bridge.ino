// 2022/03 Andrew Villeneuve <andrewmv@gmail.com>
// Decode and command a Sony A1 Control serial tape deck interface 
// Over serial

/* 
* Serial Prompt Commands:
* aXX - set working address to XX
* AXX - scan addresses 0x00 to 0xff using the command XX
* dXX - set command XX using current working address
* DXXYY - scan commands XX though YY using current working address 
*/

#include "limits.h"

// Arduino pin configuration
const int PIN_A1_DATA = 2;

// pulse width timings in microseconds
const int PW_INIT = 2400;
const int PW_ONE = 1200;
const int PW_ZERO = 600;
const int PW_GAP = 600;

const int CMD_SET_ADDR = 1;
const int CMD_SCAN_ADDR = 2;
const int CMD_SEND_DATA = 3;
const int CMD_SCAN_DATA = 4;

int command = 0;		// Current user command
char inputData[16];		// Input prompt data
int inputPos = 0;		// Input prompt index pos
byte workingAddr = 'a';	// Current working address
byte dataPacket[8];		// Data to send to A1
int dataSize = 0;		// Number of data packets to sent to A1

const byte MaxByteArraySize = 8;

void setup() {
	pinMode(PIN_A1_DATA, OUTPUT);
	digitalWrite(PIN_A1_DATA, HIGH);
	Serial.begin(9600);
	Serial.println("Ready");
}

void incAndRepeat() {
	if (dataPacket[0] == 255) {
		dataPacket[0] = 0;
	} else {
		dataPacket[0]++;
	}
	sendA1Packet(workingAddr, dataPacket, dataSize);
	prompt();
}

void badCommand() {
	Serial.println("\nUsage: [a|A|d|D][lowerbyte](upperbyte)");
	prompt();
}

void prompt() {
	command = 0;
	inputPos = 0;
	Serial.print(workingAddr, HEX);
	Serial.print("> ");
}

void sendA1Packet(byte addr, byte * payload, int payloadsize) {
	Serial.print("Sending Address 0x");
	Serial.print(addr, HEX);
	Serial.print(" Data 0x");
	for (int i = 0; i < payloadsize; i++) {
		Serial.print(payload[i], HEX);
	}
	Serial.println();

	// Send start - pulldown for 2400us
	pulseDataLine(PW_INIT);
	// Shift out address
	for (int i = 0; i < 8; i++) {
		if (addr & (128 >> i)) {
			// Send 1 bit - pulldown for 1200us
			pulseDataLine(PW_ONE);
		} else {
			// Send 0 bit - pulldown for 600us
			pulseDataLine(PW_ZERO);
		}	
	} 
	// Shift out data
	for (int i = 0; i < payloadsize; i++) {
		for (int bit = 0; bit < 8; bit++) {
			if (payload[i] & (128 >> bit)) {
				// Send 1 bit - pulldown for 1200us
				pulseDataLine(PW_ONE);
			} else {
				// Send 0 bit - pulldown for 600us
				pulseDataLine(PW_ZERO);
			}
		}
	}
}

// Pulldown the A1 data pin for this duration (in microseconds)
void pulseDataLine(int time) {
	digitalWrite(PIN_A1_DATA, LOW);
	delayMicroseconds(time);
	digitalWrite(PIN_A1_DATA, HIGH);
	delayMicroseconds(PW_GAP);
}

// String format helpers from:
// https://forum.arduino.cc/t/hex-string-to-byte-array/563827/4

byte nibble(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';

	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;

	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	Serial.print("\nCouldnt convert char to nibble: ");
	Serial.print(c, HEX);
	return 0;  // Not a valid hexadecimal character
}

// void hexCharacterStringToBytes(byte *byteArray, const char *hexString, int strLength) {
//   bool oddLength = strLength & 1;

//   byte currentByte = 0;
//   byte byteIndex = 0;

//   for (byte charIndex = 0; charIndex < strLength; charIndex++) {
//     bool oddCharIndex = charIndex & 1;

//     if (oddLength) {
//       // If the length is odd
//       if (oddCharIndex) {
//         // odd characters go in high nibble
//         currentByte = nibble(hexString[charIndex]) << 4;
//       } else {
//         // Even characters go into low nibble
//         currentByte |= nibble(hexString[charIndex]);
//         byteArray[byteIndex++] = currentByte;
//         currentByte = 0;
//       }
//     } else {
//       // If the length is even
//       if (!oddCharIndex) {
//         // Odd characters go into the high nibble
//         currentByte = nibble(hexString[charIndex]) << 4;
//       } else {
//         // Odd characters go into low nibble
//         currentByte |= nibble(hexString[charIndex]);
//         byteArray[byteIndex++] = currentByte;
//         currentByte = 0;
//       }
//     }
//   }
// }

// void dumpByteArray(const byte * byteArray, const byte arraySize) {
// 	for (int i = 0; i < arraySize; i++) {
// 		Serial.print("0x");
// 		if (byteArray[i] < 0x10)
// 			Serial.print("0");
// 		Serial.print(byteArray[i], HEX);
// 	}
// }

byte strToByte(char highNibble, char lowNibble) {
	return (nibble(highNibble) << 4) + nibble(lowNibble);
}

void loop() {
	while (Serial.available()) {
	    char b = (char)Serial.read();
	    if (b == '\n') {
	    	if (inputPos == 0) {
	    		incAndRepeat();
	    	} else if (inputPos > 2) {
		    	// Do command parsing
		    	if (inputData[0] == 'a') {
		    		command = CMD_SET_ADDR;
		    	} else if (inputData[0] == 'A') {
		    		command = CMD_SCAN_ADDR;
		    	} else if (inputData[0] == 'd') {
		    		command = CMD_SEND_DATA;
		    	} else if (inputData[0] == 'D') {
		    		command = CMD_SCAN_DATA;
		    	} else {
		    		Serial.println("\nUnknown command");
		    		badCommand();
		    	}
	    	} else {
	    		Serial.println("\nError: command too short");
	    		badCommand();
	    	}

	    	// Do argument parsing
	    	if (command == CMD_SET_ADDR) {
		    	workingAddr = strToByte(inputData[1], inputData[2]);
	    		Serial.print("\nWorking Address is 0x");
	    		Serial.println(workingAddr, HEX);
	    		prompt();
	    	} else if (command == CMD_SEND_DATA) {
	    		dataSize = ceil((inputPos - 1) / 2);
	    		for (int i = 0; i < dataSize; i++) {
	    			int offset = (2 * i) + 1;
			    	dataPacket[i] = strToByte(inputData[offset], inputData[offset+1]);
	    		}
		    	sendA1Packet(workingAddr, dataPacket, dataSize);
	    		prompt();
		    } else if (command == CMD_SCAN_ADDR) {
		    	Serial.println("\nNot implemented");
	    		prompt();
		    } else if (command == CMD_SCAN_DATA) {
		    	Serial.println("\nNot implemented");
	    		prompt();
		    } 
	    } else if (inputPos > 10) {
	    	Serial.println("\nError: command too long");
	    	badCommand();
	    	inputPos = 0;
	    } else {
	    	inputData[inputPos] = b;
	    	inputPos++;
	    }
	}
}
