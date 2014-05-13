/*
 * IR.h
 *
 * Created: 2014/03/30 22:35:21
 *  Author: wararyo
 */ 
#ifndef IR_H_
#define IR_H_

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
//#include "../../library/Serial.h"

#define cbi(addr,bit)     addr &= ~(1<<bit)
#define sbi(addr,bit)     addr |=  (1<<bit)
#define tbi(addr,bit)	  addr ^=  (1<<bit)
#define wait(ms) _delay_ms(ms)

#define TIMER_CONTROL_A TCCR0A
#define TIMER_CONTROL_B TCCR0B
#define TIMER_COMP OCR0A
#define TIMER_INTERRUPT TIMSK0
#define TIMER_INTERRUPT_BIT OCIE0A

#define PWM_CONTROL_A TCCR1A
#define PWM_CONTROL_B TCCR1B
#define PWM_CONTROL_C TCCR1C
#define PWM_COMP OCR1AL
#define PWM_TOP ICR1  

#define CAP_CONTROL_A TCCR1A
#define CAP_CONTROL_B TCCR1B
#define CAP_CONTROL_C TCCR1C
#define CAP_CONTROL_TOP OCR1A
#define CAP_PERIOD ICR1
#define CAP_DDR DDRB
#define CAP_PIN PB0
#define CAP_EDGESELECT ICES1
#define CAP_INTERRUPT TIMSK1
#define CAP_INTERRUPT_BIT ICIE1
#define CAP_OVF_INTERRUPT_BIT TOIE1

#define IR_LED_DDR DDRB
#define IR_LED_PORT PORTB
#define IR_LED_PIN PB1 //OC1A

#define IR_T 71// 562/8 560μs
#define IR_CAREER 26 //26μs
#define IR_DUTY 9 // 26/3 9μs

volatile char IR_receivecount = 0;
volatile int IR_receive_raw[72];
volatile int IR_received_customer = 0;
volatile char IR_received_data = 0;

volatile int IR_count = 0;
volatile char IR_data[96];//16+8+(4*32)+1

volatile char IR_isReceiving = 0;
volatile char IR_isSending = 0;



void IR_onSendStart();
void IR_onSendFinished();
void IR_onInitialize();
void IR_onReceived(int customer,char data);

ISR ( TIMER0_COMPA_vect ){
	//while(bit_is_clear(PWM_CONTROL_A,1));
	//sendStringLine("aho");
	//tbi(PORTD,PD7);
	PWM_COMP = IR_DUTY;
	if(IR_data[IR_count] & 0b00000001){
		sbi(PWM_CONTROL_A,7);
		sbi(PORTD,PD6);
	}
	else{
		cbi(PWM_CONTROL_A,7);
		cbi(PORTD,PD6);
	}
	TIMER_COMP = IR_T * (IR_data[IR_count] >> 1);
	//sbi(TIMER_INTERRUPT,TIMER_INTERRUPT_BIT);
	IR_count++;
	if(IR_data[IR_count] == 0) {
		//tbi(PORTD,PD7);
		cbi(PWM_CONTROL_A,7);
		cbi(TIMER_INTERRUPT,TIMER_INTERRUPT_BIT);
		IR_count = 0;
		cbi(IR_LED_PORT,IR_LED_PIN);
		IR_isSending = 0;
		IR_onSendFinished();
	}
}

ISR ( TIMER1_CAPT_vect ){
	if(IR_isSending) return;
	IR_isReceiving = 1;
	tbi(CAP_CONTROL_B,ICES1);
	//tbi(PORTD,PD7);
	//sbi(PORTD,PD7);
	unsigned int span = CAP_PERIOD;
	TCNT1 = 0;
	if(span != 0) IR_receive_raw[IR_receivecount++] = span;
	//sendStringLine(itoa(span,&ch,10));
}

//TIMER1比較一致(TOP:0x1FFF)を受信終了とみなす
//IR_receive_rawの中身　0番目がゴミで奇数番目が送信元LEDがHIGHの時の時間だと思われる
ISR ( TIMER1_COMPA_vect ) {
	if(IR_isReceiving){
		IR_isReceiving = 0;
		//tbi(PORTD,PD7);
		if(IR_receivecount > 64) IR_receivecount = 0;
		else return;
		if(1000 < IR_receive_raw[1] && IR_receive_raw[1] < 1210){//1,2番目はヘッダー
			if(490 < IR_receive_raw[2] && IR_receive_raw[2] < 610){//多分NECフォーマットだと判別できる
				char cursor = 3;
				for(unsigned char i=15;i <= 15;i--){
					if(50 < IR_receive_raw[cursor] && IR_receive_raw[cursor] < 80){
						cursor++;
						if(50 < IR_receive_raw[cursor] && IR_receive_raw[cursor] < 80){
							cbi(IR_received_customer,i);
						}
						else if(190 < IR_receive_raw[cursor] && IR_receive_raw[cursor] < 215){
							sbi(IR_received_customer,i);
						}
						cursor++;
					}
					else{
						return;
					}
				}
				for(unsigned char i=7;i <= 7;i--){
					if(50 < IR_receive_raw[cursor] && IR_receive_raw[cursor] < 80){
						cursor++;
						if(50 < IR_receive_raw[cursor] && IR_receive_raw[cursor] < 80){
							cbi(IR_received_data,i);
							//sendChar('0');
						}
						else if(190 < IR_receive_raw[cursor] && IR_receive_raw[cursor] < 215){
							sbi(IR_received_data,i);
							//sendChar('1');
						}
						cursor++;
					}
					else{
						return;
					}
				}
				char data_parity = 0;
				for(unsigned char i=7;i <= 7;i--){
					if(50 < IR_receive_raw[cursor] && IR_receive_raw[cursor] < 80){
						cursor++;
						if(50 < IR_receive_raw[cursor] && IR_receive_raw[cursor] < 80){
							cbi(data_parity,i);
							//sendChar('0');
						}
						else if(190 < IR_receive_raw[cursor] && IR_receive_raw[cursor] < 215){
							sbi(data_parity,i);
							//sendChar('1');
						}
						cursor++;
					}
					else{
						return;
					}
				}
				char ch;
				if(IR_received_data != (char)~data_parity) return;
				IR_onReceived(IR_received_customer,IR_received_data);
			}
		}
	}
}

//mode:0=receive 1=send
void IR_initialize(char mode){
	if(IR_isReceiving | IR_isSending) return;
	IR_onInitialize();
	TIMER_CONTROL_A = 0b00000010;//OC0A切断 CTC
	TIMER_CONTROL_B = 0b00000010;//1/8 8μs/1カウント
	TIMER_COMP = IR_T;
	
	sbi(IR_LED_DDR,IR_LED_PIN);//output
	cbi(IR_LED_PORT,IR_LED_PIN);
	
	if(mode){
		if(IR_isReceiving) return;
		sbi(PRR,PRTIM1);//念のため
		cbi(PRR,PRTIM1);
		sbi(PWM_CONTROL_C,FOC1A);
		CAP_INTERRUPT = 0;
		PWM_CONTROL_A = 0;
		PWM_CONTROL_B = 0;
		PWM_CONTROL_A = 0b00000010;
		PWM_CONTROL_B = 0b00011001;
		PWM_TOP = IR_CAREER;
		PWM_COMP = IR_CAREER;
		sbi(PWM_CONTROL_A,7);
		wait(100);
	}
	else{
		if(IR_isSending) return;
		CAP_CONTROL_A = 0b00000000;
		CAP_CONTROL_B = 0b10001010;//立下り割り込み 1/8
		CAP_CONTROL_TOP = 0x1FFF;
		CAP_INTERRUPT = 0b00100010;
		sbi(CAP_CONTROL_C,FOC1A);
		cbi(CAP_DDR,CAP_PIN);//input
	}
	
	//sei();
}

//IR_data一個の構造
//0ビット目でHIGHかLOWか
//残り7ビットでその状態の長さを表す
//ex) 0b00000100 LOWの状態を2カウント続ける　0b00000011 HIGHの状態を1カウント続ける

int add_data_raw(char mvalue,int *count,char length){
	char lengthnumber; char rest;//8ビットカウンタ　1MHz 1/8分周より　2048μsまでしか測れない
	if(length > 3){ lengthnumber = length / 3; rest = length % 3 ;}
	else if(length == 0) return *count;
	else {lengthnumber = 0; rest = length;}
		
	mvalue &= 0b00000001;//2以上の場合は1に
	for (char i=0;i < lengthnumber;i++){
		IR_data[*count] = (3 << 1) | mvalue;
		(*count)++;
	}
	IR_data[(*count)++] = (rest << 1) | mvalue;
	return *count;
}

int add_data(char mvalue,int *count){
	if(mvalue){
		add_data_raw(1,count,1);
		add_data_raw(0,count,3);
	}
	else{
		add_data_raw(1,count,1);
		add_data_raw(0,count,1);
	}
	return *count;
}

void IR_send(int customer,char data){

	int count = 0;
	count = 0;
	add_data_raw(1,&count,16);
	add_data_raw(0,&count,8);
	
	for(char i=0;i < 16;i++){
		add_data(((customer >> (15 - i)) & 0x0001),&count);//カスタマーコード　上位ビットからiビット目が1の時
	}
	for(char i=0;i < 8;i++){
		add_data(((data >> (7 - i)) & 0x01),&count);
		/*if((data >> (7 - i)) & 0x01){
			IR_data[count] = 1;count++;
			IR_data[count] = 0;count++;
		}
		else{
			IR_data[count] = 1;count++;
			for(char ii=0;ii < 3;ii++){IR_data[count] = 0;count++;}
		}*/
	}
	for(char i=0;i < 8;i++){
		add_data(((~data >> (7 - i)) & 0x01),&count);
		/*if((~data >> (7 - i)) & 0x01){
			IR_data[count] = 1;count++;
			IR_data[count] = 0;count++;
		}
		else{
			IR_data[count] = 1;count++;
			for(char ii=0;ii < 3;ii++){IR_data[count] = 0;count++;}
		}*/
	}
	//add_data(1,&count);
	add_data_raw(1,&count,1);
	add_data_raw(0,&count,1);
	
	//if(IR_data[0] == 0) sbi(PORTD,PD7);else cbi(PORTD,PD7);
	
	while(count < 96){
		IR_data[(count)++] = 0;
	}
	

	IR_onSendStart();
	IR_isSending = 1;
	//sbi(PORTD,PD6);
	sbi(TIMER_INTERRUPT,TIMER_INTERRUPT_BIT);
}


#endif /* IR_H_ */