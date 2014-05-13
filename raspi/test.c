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
#include <avr/pgmspace.h>
#include "../../library/IR.h"
//#include "../../IR_LED/IR_LED/IR.h"
#include "../../library/Serial.h"

#define cbi(addr,bit)     addr &= ~(1<<bit)
#define sbi(addr,bit)     addr |=  (1<<bit)
#define tbi(addr,bit)	  addr ^=  (1<<bit)
#define wait(ms) _delay_ms(ms)

//#define F_CPU = 1000000UL;

/*const prog_char m_Command[] = "Command?";
const prog_char m_Customer_Code[] = "Customer code?(Base:16)";
const prog_char m_Current_Setting_is[] = "Current Setting is";*/

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
	cli();
	EECR = (0<<EEPM1)|(0<<EEPM0);
	EEAR = uiAddress;
	EEDR = ucData;
	sbi(EECR,EEMPE);
	sbi(EECR,EEPE);
	while(bit_is_set(EECR,EEPE));
	sei();
}

unsigned char EEPROM_read(unsigned int uiAddress){
	cli();
	EEAR = uiAddress;
	sbi(EECR,EERE);
	while(bit_is_set(EECR,EEPE));
	sei();
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

char *ask_P(const char *question,char timeout){
	char isTimeout = 0;
	sendStringLine_P(question);
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
	//sendStringLine("aho");
	startCapture_flag = 1;
}

void IR_onInitialize(){
	
}

void IR_onReceived(int customer,char data){
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
	cli();
	for(int i=0;i<ms;i++) wait(1);
	sei();
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
		beep(440,100);
		beep(880,100);
	}
	//else sendStringLine("Raspi is already active.");
	raspi_wake_flag = 0;
}

void raspi_shutdown(){
	//cbi(PORTD,PD7);
	if(isRaspiActive()) {
		sendStringLine("Halt");
		beep(440,100);
		beep(220,100);
	}
	//else sendStringLine("Raspi is already in halt.");
	//sbi(PORTD,PD7);
	raspi_shutdown_flag = 0;
}

void Mode_command(){
	//strcpy_P(s,PSTR("Command?"));
	char *message = ask_P(PSTR("Command?"),100);
	//char *message = ask("C?",100);
	sendStringLine(message);
	if(equal(message,"ir")){//IR
		message = ask("Type?",100);
		if(equal(message,"nec")){
			sendStringLine("NEC format");
			char customerc[32];
			strcpy(customerc,ask_P(PSTR("Customer code?(Base:16)"),100));
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
	else if(equal(message,"beep")){
		char freqc[8];
		strcpy(freqc,ask("Freq?",100));
		if(equal(freqc,"A")){
			beep(440,100);
			beep(880,100);
		}
		else if(equal(freqc,"B")){
			beep(440,100);
			beep(220,100);
		}
		else{
			char spanc[8];
			strcpy(spanc,ask("Dur?",100));
			char ch[8];
			//beep(strtol(freqc,&ch,10),strtol(spanc,&ch,10));
			beep(440,1000);
		}
	}
	else if(equal(message,"power")){
		sendStringLine("Current Setting is");
		char ch[5];
		sendStringLine(itoa(IR_received_customer,&ch,16));
		sendStringLine(itoa(IR_power_data,&ch,16));
	
		sendStringLine("Waiting for IR...");
		while(!IRrecv_flag);
		IRrecv_flag = 0;
		sendStringLine("Received");
		sendStringLine(itoa(IR_received_customer,&ch,16));
		sendStringLine(itoa(IR_received_data,&ch,16));
		if(equal(ask("Apply? (y/N)",100),"y")){
			IR_power_customer = IR_received_customer;
			IR_power_data = IR_received_data;
			EEPROM_write(EEPROM_IR_power_button,(char)IR_power_customer);
			EEPROM_write(EEPROM_IR_power_button+1,(char)(IR_power_customer >> 8));
			EEPROM_write(EEPROM_IR_power_button+2,IR_power_data);
			wait(100);
			sendStringLine("Written");
		}
	}
	else if(equal(message,"cds")){
		sendStringLine_P(PSTR("Sensetivity Alignment"));
		requirereceivedline_flag = 1;
		//onreceivedline_flag = 1;
		startInput();
		char ch[5];
		while(!onreceivedline_flag){
			wait(1000);
			sbi(ADCSRA,ADSC);
			while(bit_is_set(ADCSRA,ADSC)) wait(10);
			sendStringLine(utoa(ADC,&ch,10));
		}
		requirereceivedline_flag = 0;
		onreceivedline_flag = 0;
		stopInput();
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
			while(bit_is_set(ADCSRA,ADSC)) wait(10);
			if(cnt == 0) cnt = ADC;
			adc = ADC;
				
			if((adc - cnt) > 200){
				raspi_wake_flag = 1;//多重割り込み回避のためメインルーチンでraspi_wake()を入れる
			}
			else if((adc - cnt) < -200){
				raspi_shutdown_flag = 1;
			}
				
			cnt = ADC;
			char ch[5];
			//sendStringLine(utoa(cnt,&ch,10));
		
	}
}

EMPTY_INTERRUPT( INT0_vect );

ISR( INT1_vect ){
	commandMode_flag = 1;
}

ISR( BADISR_vect ){
	//tbi(PORTD,PD7);
	beep(440,2000);
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
	sbi(TIMSK2,TOIE2);//溢れ割り込み
	TCNT2 = 0;
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
	cbi(DDRD,PD2);//INT0
	cbi(DDRD,PD3);//INT1
	sbi(PORTD,PD3);//プルアップ
	EICRA = 0b00001101;
	EIMSK = 0b00000011;
	
	
	//EEPROM読出し
	IR_power_customer = EEPROM_read(EEPROM_IR_power_button) | ((EEPROM_read(EEPROM_IR_power_button+1) << 8));
	IR_power_data = EEPROM_read(EEPROM_IR_power_button+2);
	
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
		while(bit_is_set(UCSR0B,UDRIE0));
		//while(bit_is_clear(UCSR0A,UDRE0));
		while(IR_isReceiving);
		while(IR_isSending);
		//wait(100);
		
		if(raspi_wake_flag) raspi_wake();
		if(raspi_shutdown_flag) raspi_shutdown();
		if(startCapture_flag) IR_initialize(0);
		if(IRrecv_flag){
			IRrecv_flag = 0;
			if((IR_received_customer == IR_power_customer) && (IR_received_data == IR_power_data)){
				raspi_wake();
			}
			else{
				char ch[4];
				sendStringLine("IR");
				//sendString("0x");
				sendStringLine(itoa(IR_received_customer,&ch,16));
				//sendString("0x");
				sendStringLine(itoa(IR_received_data,&ch,16));
			}
		}
		if(commandMode_flag){//コマンドモード
			cbi(TIMSK2,TOIE2);//タイマー2割りこみ(明るさセンサー監視)なし
			sbi(PORTD,PD7);
			//EIMSK = 0;
			Mode_command();
			commandMode_flag = 0;
			sbi(TIMSK2,TOIE2);
			cbi(PORTD,PD7);
			//EIMSK = 0b00000011;
		}
		//tbi(PORTD,PD7);
		//wait(1000);
		//sendString("a");
		while(bit_is_set(UCSR0B,UDRIE0));//USART送信空き待機
		//while(bit_is_clear(UCSR0A,UDRE0));
		while(IR_isReceiving);
		while(IR_isSending);
		wait(1);
		
		//cbi(PORTD,PD7);
		sleep_mode();
		//sbi(PORTD,PD7);
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