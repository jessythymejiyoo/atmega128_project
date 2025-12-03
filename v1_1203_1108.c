/*
 * term_project_PMS.c
 *
 * Created: 2025-12-03 오전 10:00:08
 * Author : jiyum
 */ 

#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// =======================
// 설정
// =======================
#define THRESHOLD 20     // PM2.5 역치
#define BUZZER_PIN PB4   // 부저 핀

// =======================
// FND 테이블
// =======================
unsigned char number[10] = {
	0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F
};
unsigned char fnd_select[4] = {0x08, 0x04, 0x02, 0x01};

volatile unsigned char fnd_data[4] = {0,0,0,0};
volatile uint8_t fnd_index = 0;


// =======================
// UART1 (PMS7003)
// =======================
void UART1_init(void)
{
	UBRR1H = 0;
	UBRR1L = 103;   // 9600bps
	UCSR1B = (1<<RXEN1);
	UCSR1C = (1<<UCSZ11) | (1<<UCSZ10);
}

uint8_t UART1_receive(void)
{
	while(!(UCSR1A & (1<<RXC1)));
	return UDR1;
}


// =======================
// PMS7003 PM2.5 읽기
// =======================
int getPM25Int(void)
{
	uint8_t b;

	// --- HEADER ---
	b = UART1_receive();
	if (b != 0x42) return -1;

	b = UART1_receive();
	if (b != 0x4D) return -1;

	uint8_t buf[32];
	buf[0] = 0x42;
	buf[1] = 0x4D;

	for (int i = 2; i < 32; i++)
	buf[i] = UART1_receive();

	uint16_t pm25 = ((uint16_t)buf[12] << 8) | buf[13];
	return (int)pm25;
}


// =======================
// 부저 펄스 출력 함수
// =======================
void custom_delay_us(int us) {
	for(int i = 0; i < us; i++)
	_delay_us(1);
}

// 간단한 ‘삐–’ 경고음 (약 2kHz, 300ms)
void beep_warning(void)
{
	float hz = 2000.0;   // 경고음 주파수
	int ms = 300;        // 300ms
	int us = (int)(500000.0 / hz);
	int count = (int)(ms * 1000.0 / (2.0 * us));

	for(int i = 0; i < count; i++) {
		PORTB |= (1 << BUZZER_PIN);
		custom_delay_us(us);
		PORTB &= ~(1 << BUZZER_PIN);
		custom_delay_us(us);
	}
}


// =======================
// FND 스캔 인터럽트 (1ms)
// =======================
ISR(TIMER0_COMP_vect)
{
	uint8_t idx = fnd_index;

	PORTG = 0x00;

	if (idx == 0) PORTC = 0x00;
	else          PORTC = number[fnd_data[idx]];

	PORTG = fnd_select[idx];

	fnd_index = (fnd_index + 1) % 4;
}


// =======================
// 메인
// =======================
int main(void)
{
	DDRC = 0xFF;
	DDRG = 0x0F;
	DDRB |= (1 << BUZZER_PIN);   // 부저 출력

	UART1_init();

	// Timer0 → 1ms 인터럽트
	TCCR0 = (1<<WGM01) | (1<<CS02) | (1<<CS00);
	OCR0  = 249;
	TIMSK = (1<<OCIE0);

	sei();

	while(1)
	{
		int pm = getPM25Int();

		if (pm >= 0)
		{
			if (pm > 999) pm = 999;

			fnd_data[0] = 0;
			fnd_data[1] = (pm / 100) % 10;
			fnd_data[2] = (pm / 10)  % 10;
			fnd_data[3] = pm % 10;

			// ============================
			// 역치 → 경고음
			// ============================
			if (pm >= THRESHOLD)
			beep_warning();
		}

		_delay_ms(500);
	}
}

