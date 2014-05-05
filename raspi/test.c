/*
 * raspi.c
 *
 * Created: 2012/09/15 22:25:25
 *  Author: wararyo
 */ 


#include <avr/io.h>
#include <util/delay.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include <string.h>
#include "../../library/IR.h"
//#include "../../IR_LED/IR_LED/IR.h"
#include "../../library/Serial.h"

#define cbi(addr,bit)     addr &= ~(1<<bit)
#define sbi(addr,bit)     addr |=  (1<<bit)
#define tbi(addr,bit)	  addr ^=  (1<<bit)
#define wait(ms) _delay_ms(ms)

//#define F_CPU = 1000000UL;

volatile char sbuf[16];
volatile char sindex;
volatile char onreceivedline_flag = 0;
volatile char requirereceivedline_flag = 0;
volatile char *receivedstring;
volatile unsigned char sleepcount = 1;
volatile int cnt = 0;
volatile int adc = 0;

volatile char raspi_shutdown_flag = 0;
volatile char raspi_wake_flag = 0;
volatile char commandMode_flag = 0;
volatile char startCapture_flag = 0;
volatile char IRrecv_flag = 0;

volatile int IR_power_customer = 0;
volatile char IR_power_data = 0;

const char EEPROM_IR_power_button = 1;//~3
	
void onReceivedLine(char *string){
	receivedstring = string;
	if(requirereceivedline_flag) onreceivedline_flag = 1;
	else *receivedstring = "";
}

void onstartInput(){
	requirereceivedline_flag = 1;
}

void onReceivedChar(char ch){
	if (ch == '$') commandMode_flag = 1;
}


void EEPROM_write(unsigned int uiAddress, unsigned char ucData){
	while(bit_is_set(EECR,EEPE));
	EECR = (0<<EEPM1)|(0<<EEPM0);
	EEAR = uiAddress;
	EEDR = ucData;
	sbi(EECR,EEMPE);
	sbi(EECR,EEPE);
}

unsigned char EEPROM_read(unsigned int uiAddress){
	while(bit_is_set(EECR,EEPE));
	EEAR = uiAddress;
	sbi(EECR,EERE);
	return EEDR;
}

//timeout:0.1s
char waitInput(char timeout){
	if(timeout == 0) timeout = 0xFF;
	char i = 0;
	while(!onreceivedline_flag){
		wait(100);
		i++;
		if(i > timeout){
			 requirereceivedline_flag = 0;
			 return 0;
		}
	}
	onreceivedline_flag = 0;
	return 1;
}

char *ask(char *question,char timeout){
	char isTimeout = 0;
	sendStringLine(question);
	startInput();
	if(!waitInput(timeout)) isTimeout = 1;
	stopInput();
	requirereceivedline_flag = 0;
	return isTimeout ? "\0" : receivedstring;
}

char equal(char *one,char *two){
	unsigned char count = 0;
	while(1){
		//sendChar((*(one + count) == '\0') + 48);
		//sendChar((*(two + count) == '\0') + 48);
		if((*(one + count) == '\0') && (*(two + count) == '\0')) return 1;
		//sendReturn();
		//sendChar(*(one + count));sendChar(*(two + count));
		if(*(one + count) != *(two + count)) return 0;
		count++;
	}
}

void beep_init(){
	TCCR1B = 0b00011011;
	TCCR1A = 0b00000010;
}

void IR_onSendStart(){
	//sbi(PORTB,PB2);
	//cbi(PORTB,PB3);
	cbi(PRR,PRTIM0);
	IR_initialize(1);
}

void IR_onSendFinished(){
	sbi(PRR,PRTIM0);
	//sbi(PORTD,PD7);
	startCapture_flag = 1;
}

void IR_onInitialize(){
	
}

void IR_onReceived(int cousumer,int data){
	IRrecv_flag = 1;
}

void beep(unsigned int freq,unsigned int ms){//timer0、OC0A使用
	if(IR_isSending) return;
	beep_init();
	int period = 15625 / freq;
	ICR1 = period;
	OCR1B = period / 2;
	wait(10);
	sbi(TCCR1A,5);
	wait(10);
	for(int i=0;i<ms;i++) wait(1);
	cbi(TCCR1A,5);
	IR_initialize(0);
}

char isRaspiActive(){//割り込み処理内で呼び出されるとUSART_TX_vectとの多重割り込みになりマズい
	//ask("OK?",254);
	char *mes = ask("A?",10);
	sendStringLine(mes);
	return *mes != '\0';
}

void raspi_wake(){
	//sbi(PORTD,PD7);
	if(!isRaspiActive()) {
		sendStringLine("Wake");
		sbi(PORTB,PB4);
		wait(1);
		cbi(PORTB,PB4);
	}
	//else sendStringLine("Raspi is already active.");
	raspi_wake_flag = 0;
}

void raspi_shutdown(){
	//cbi(PORTD,PD7);
	if(isRaspiActive()) sendStringLine("Halt");
	//else sendStringLine("Raspi is already in halt.");
	//sbi(PORTD,PD7);
	raspi_shutdown_flag = 0;
}

void Mode_command(){
	char *message = ask("Command?",100);
	sendStringLine(message);
	if(equal(message,"ir")){//IR
		message = ask("Type?",100);
		if(equal(message,"nec")){
			sendStringLine("Data in NEC format");
			char customerc[32];
			strcpy(customerc,ask("Customer code?(Base:16)",100));
			char datac[32];
			strcpy(datac,ask("Data?(Base:16)",100));
			char ends[16];
			int customer = (int)strtol(customerc,&ends,16);
			char data = (char)strtol(datac,&ends,16);
			//sendStringLine(utoa(customer,ends,2));
			//sendString(utoa(data,ends,2));
			IR_send(customer,data);
		}
		if(equal(message,"0")){
			IR_send(0x21C7,0x94);
		}
	}
	if(equal(message,"power")){
		sendStringLine("Current Setting is");
		char ch;
		sendString("0x");
		sendStringLine(itoa(IR_power_customer,&ch,16));
		sendString("0x");
		sendStringLine(itoa(IR_power_data,&ch,16));
		
		EIMSK = 0b00000011;
		sendStringLine("Waiting for IR...");
		while(!IRrecv_flag);
		EIMSK = 0b00000000;
		sendStringLine("IR Received");
		sendString("0x");
		sendStringLine(itoa(IR_received_consumer,&ch,16));
		sendString("0x");
		sendStringLine(itoa(IR_received_data,&ch,16));
		if(equal(ask("Are you sure to apply? (y/N)",100),"y")){
			IR_power_customer = IR_received_consumer;
			IR_power_data = IR_received_data;
			EEPROM_write(EEPROM_IR_power_button,(char)IR_received_consumer);
			EEPROM_write(EEPROM_IR_power_button+1,(char)(IR_received_consumer >> 8));
			EEPROM_write(EEPROM_IR_power_button+2,IR_received_data);
			wait(1000);
			sendStringLine("Written");
		}
	}
	sendStringLine("Exit");
}

//約2.6秒ごと
ISR( TIMER2_OVF_vect ){
	//tbi(PORTD,PD7);
	if(sleepcount++ == 0){
			//tbi(PORTD,PD7);
			//cbi(PRR,PRADC);
			sbi(ADCSRA,ADSC);
			while(bit_is_set(ADCSRA,ADSC))wait(10);
			if(cnt == 0) cnt = ADC;
			adc = ADC;
				
			if((adc - cnt) > 170){
				raspi_wake_flag = 1;//多重割り込み回避のためメインルーチンでraspi_wake()を入れる
			}
			else if((adc - cnt) < -170){
				raspi_shutdown_flag = 1;
			}
				
			cnt = ADC;
			//char test;
			//sendStringLine(utoa(cnt,&test,10));
		
	}
}

ISR( INT0_vect ){
	//tbi(PORTD,PD7);
}

ISR( INT1_vect ){
	commandMode_flag = 1;
}

int main(void)
{
	DDRD = 0b11111111;//全部出力
	PORTD = 0b00000000;
	DDRC = 0b11111110;//PC0入力
	PORTC = 0b00000000;
	DDRB = 0b11111111;//全出力
	PORTB = 0b00000000;
	
	//Timer0,1は赤外線ライブラリにて使用
	//Timer2設定 TIMER2_OVF_vect 約2.6秒ごと
	TCCR2A = 0b00000000;//標準ポート動作　標準動作
	TCCR2B = 0b00000111;//CTC 1024分周
	TIMSK2 = 0b00000000;
	sbi(TIMSK2,TOIE2);//コンペアマッチA割り込み
	//OCR2A = 0xFF;
	
	//TCCR0A = 0b00000010;
	//TCCR0B = 0b00000101;
	//OCR0A = 5;
	//TIMSK0 = 0b00000010;
	
	ADCSRA = 0b10000100; //62.5kHz
	ADMUX = 0b00000000; //ADC0 AREF 右
	
	wait(10);
	IR_initialize(0);
	//beep_init();
	wait(10);
	sio_init(4800,8);
	
	//INT0を赤外線に接続、INT1をRaspiに接続
	EICRA = 0b00001110;
	EIMSK = 0b00000011;
	cbi(DDRD,PD2);//INT0
	cbi(DDRD,PD3);//INT1
	
	//EEPROM読出し
	IR_power_customer = EEPROM_read(EEPROM_IR_power_button) | ((EEPROM_read(EEPROM_IR_power_button+1) << 8));
	IR_received_data = EEPROM_read(EEPROM_IR_power_button+2);
	
	//sbi(PORTD,PD7);

	
	sei();
	
	//wait(1000);
	
	sendStringLine("AVR");

	/*while(1){
		sendChar(equal(ask("How do you do?",254),"IR") + 48);
		//sendString(ask("How do you do?"));
	}*/
	/*int d = getReceivedData();
	if(d == 'I') tbi(PORTD,PD7);
	wait(1000);
	tbi(PORTD,PD7);*/
	
	/*sbi(ADCSRA,ADSC);
	while(bit_is_set(ADCSRA,ADSC))wait(1000);
	cnt = ADC;
	adc = ADC;*/
	
	set_sleep_mode(SLEEP_MODE_PWR_SAVE);
	sbi(PRR,PRTWI);
	sbi(PRR,PRSPI);
	sbi(PRR,PRTIM0);
	//sbi(PRR,PRADC);
	
    while(1)
    {
		while(!is_transmitted());
		while(bit_is_clear(UCSR0A,UDRE0));
		while(IR_isReceiving);
		wait(1);
		
		if(raspi_wake_flag) raspi_wake();
		if(raspi_shutdown_flag) raspi_shutdown();
		if(startCapture_flag) IR_initialize(0);
		if(IRrecv_flag){
			IRrecv_flag = 0;
			char ch;
			sendStringLine("IR");
			sendString("0x");
			sendStringLine(itoa(IR_received_consumer,&ch,16));
			sendString("0x");
			sendStringLine(itoa(IR_received_data,&ch,16));
		}
		if(commandMode_flag){//コマンドモード
			cbi(TIMSK2,OCIE2A);//タイマー2割りこみ(明るさセンサー監視)なし
			EIMSK = 0;
			Mode_command();
			commandMode_flag = 0;
			sbi(TIMSK2,OCIE2A);
			EIMSK = 0b00000011;
		}
		//tbi(PORTD,PD7);
		//wait(1000);
		//sendStringLine("nanntoiukotodeshow");
		while(!is_transmitted());
		while(bit_is_clear(UCSR0A,UDRE0));
		while(IR_isReceiving);
		wait(1);
		
		
		sleep_mode();
    }
	/*while(1){
		sbi(ADCSRA,ADSC);
		while(bit_is_set(ADCSRA,ADSC));

		adc = ADC;
		
		char minus = 1;
		if(adc < cnt) minus = -1;
		
		sbi(PORTD,PD0);
		for(int i = 0; i < (((adc - cnt) * minus) / 10);i++) wait(10);
		cbi(PORTD,PD0);
		//for(int i = 0; i < ((adc - cnt) / 10);i++) wait(10);
		
		cnt = ADC;
	}*/
	return 0;
}