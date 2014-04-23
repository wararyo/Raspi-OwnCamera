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
	
void onReceivedLine(char *string){
	receivedstring = string;
	if(requirereceivedline_flag) onreceivedline_flag = 1;
}

void onstartInput(){
	requirereceivedline_flag = 1;
}

void onReceivedChar(char ch){
	if (ch == '$') commandMode_flag = 1;
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
	sbi(PORTB,PB2);
	cbi(PORTB,PB3);
	IR_initialize();
}

void IR_onSendFinished(){
	cbi(PORTB,PB2);
	sbi(PORTB,PB3);
	beep_init();
}

void beep(unsigned int freq,unsigned int ms){//timer0�AOC0A�g�p
	if(IR_isSending) return;
	int period = 15625 / freq;
	ICR1 = period;
	OCR1A = period / 2;
	wait(10);
	sbi(TCCR1A,7);
	wait(10);
	for(int i=0;i<ms;i++) wait(1);
	cbi(TCCR1A,7);
}

char isRaspiActive(){//���荞�ݏ������ŌĂяo������USART_TX_vect�Ƃ̑��d���荞�݂ɂȂ�}�Y��
	//ask("OK?",254);
	char *mes = ask("Active?",10);
	sendStringLine(mes);
	return *mes != '\0';
}

void raspi_wake(){
	if(!isRaspiActive()) sendStringLine("Raspi Wake!");
	else sendStringLine("Raspi is already active.");
	raspi_wake_flag = 0;
}

void raspi_shutdown(){
	if(isRaspiActive()) sendStringLine("Raspi Shutdown!");
	else sendStringLine("Raspi is already in halt.");
	//sbi(PORTD,PD7);
	raspi_shutdown_flag = 0;
}

void Mode_command(){
	char *message = ask("Command?",100);
	if(equal(message,"ir")){//IR
		message = ask("Type?",100);
		if(equal(message,"nec")){
			sendStringLine("Data in NEC format");
			char *customerc = ask("Customer code?",100);
			char *datac = ask("Data?",100);
			char *ends;
			int customer = (int)atoi(customerc);
			char data = (char)atoi(datac);
			//sendStringLine(utoa(customer,ends,2));
			//sendString(utoa(data,ends,2));
			IR_send(0x21C7,0x94);
		}
	}
	sendStringLine("Exit");
}

//��2.6�b����
ISR( TIMER2_COMPA_vect ){
	if(sleepcount++ == 0){
			sbi(ADCSRA,ADSC);
			while(bit_is_set(ADCSRA,ADSC))wait(10);
			if(cnt == 0) cnt = ADC;
			adc = ADC;
				
			if((adc - cnt) > 150){
				raspi_wake_flag = 1;//���d���荞�݉���̂��߃��C�����[�`����raspi_wake()������
			}
			else if((adc - cnt) < -150){
				raspi_shutdown_flag = 1;
			}
				
			cnt = ADC;
			//char test;
			//sendStringLine(utoa(cnt,&test,10));
	}
}

int main(void)
{
	DDRD = 0b11111111;//�S���o��
	PORTD = 0b00000000;
	DDRC = 0b11111110;//PC0����
	PORTC = 0b00000000;
	DDRB = 0b11111111;//�S�o��
	PORTB = 0b00000000;
	
	//Timer0,1�͐ԊO�����C�u�����ɂĎg�p
	//Timer2�ݒ� TIMER2_OVF_vect ��2.6�b����
	TCCR2A = 0b00000010;//�W���|�[�g����@CTC
	TCCR2B = 0b00000111;//CTC 1024����
	TIMSK2 = 0b00000000;
	sbi(TIMSK2,OCIE2A);//�R���y�A�}�b�`A���荞��
	OCR2A = 0xFF;
	
	//TCCR0A = 0b00000010;
	//TCCR0B = 0b00000101;
	//OCR0A = 5;
	//TIMSK0 = 0b00000010;
	
	ADCSRA = 0b10000100; //62.5kHz
	ADMUX = 0b00000000; //ADC0 AREF �E
	
	cbi(PORTB,PB2);//IRLED����(Base)
	sbi(PORTB,PB3);//�X�s�[�J�[����(Base)
	
	wait(10);
	//IR_initialize();
	beep_init();
	wait(10);
	sio_init(4800,8);
	
	sei();
	
	//wait(1000);
	
	sendStringLine("Hello AVR");

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
	
	//set_sleep_mode(SLEEP_MODE_PWR_SAVE);
	
    while(1)
    {
		if(raspi_wake_flag) raspi_wake();
		if(raspi_shutdown_flag) raspi_shutdown();
		if(commandMode_flag){//�R�}���h���[�h
			cbi(TIMSK2,OCIE2A);//�^�C�}�[2���肱��(���邳�Z���T�[�Ď�)�Ȃ�
			Mode_command();
			commandMode_flag = 0;
			sbi(TIMSK2,OCIE2A);
		}
		//tbi(PORTD,PD7);
		//wait(1000);
		//sleep_mode();
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