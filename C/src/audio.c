#include "audio.h"

int word=0;

void sayHello() {
	system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2d2n8.wav");
}

void sayAngry() {
	system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2d2-1.wav");
}

void sayWow() {
	system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2-word2.wav");
}

void saySad() {
	system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2-word11.wav");
}

void sayRandom() {
	switch (word) {
		case 0:
			system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2-word2.wav");
			break;
		case 1:
			system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2-word3.wav");
			break;
		case 2:
			system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2-word4.wav");
			break;
		case 3:
			system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2-word5.wav");
			break;
		case 4:
			system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2-word7.wav");
			break;
		case 5:
			system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2-word8.wav");
			break;
		case 6:
			system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2-word9.wav");
			break;
		case 7:
			system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2-word11.wav");
			break;
		case 8:
			system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2d2_3.wav");
			break;
		case 9:
			system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2d2_4.wav");
			break;
		case 10:
			system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2d2-1.wav");
			break;
		case 11:
			system("aplay --device plughw:CARD=Device,DEV=0 /home/pi/sounds/r2d2n8.wav");
			break;
	}
	
	word=word+1;
	if (word > 11) {
		word=0;
	}
}
