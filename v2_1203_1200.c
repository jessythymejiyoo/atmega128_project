#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h> // _delay_ms() 함수가 포함된 헤더는 위에 위치해야 함

// =======================
// 설정
// =======================
#define THRESHOLD 75     // PM2.5 역치 (참고용. 실제 사용 PM 단위에 따라 변경 필요)
#define BUZZER_PIN PB4   // 부저 핀

// LED 핀 정의 (PORTA 사용)
#define LED1 PA0
#define LED2 PA1
#define LED3 PA2

// 스위치 핀 정의 (PORTE의 INT4/PE4 사용)
#define SWITCH_PIN PE4

// PM 모드 정의
#define MODE_PM1_0 0
#define MODE_PM2_5 1
#define MODE_PM10  2

// 전역 변수
volatile uint8_t pm_mode = MODE_PM2_5; // 초기 모드는 PM2.5

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
	UBRR1L = 103;     // 9600bps
	UCSR1B = (1<<RXEN1);
	UCSR1C = (1<<UCSZ11) | (1<<UCSZ10);
}

uint8_t UART1_receive(void)
{
	while(!(UCSR1A & (1<<RXC1)));
	return UDR1;
}

uint8_t pms_frame_buf[32]; // PMS7003 데이터 프레임 저장 버퍼

// =======================
// PMS7003 데이터 읽기 및 저장
// =======================
int getPMValue(void)
{
	uint8_t b;

	// --- HEADER ---
	b = UART1_receive();
	if (b != 0x42) return -1;

	b = UART1_receive();
	if (b != 0x4D) return -1;

	pms_frame_buf[0] = 0x42;
	pms_frame_buf[1] = 0x4D;

	for (int i = 2; i < 32; i++)
	pms_frame_buf[i] = UART1_receive();

	// PMS7003 매뉴얼에 따라 일반적인 PMx.x Standard 데이터를 사용
	switch(pm_mode) {
		case MODE_PM1_0:
		// PM1.0 (Standard)는 8/9번 째 바이트
		return ((uint16_t)pms_frame_buf[8] << 8) | pms_frame_buf[9];
		case MODE_PM2_5:
		// PM2.5 (Standard)는 10/11번 째 바이트
		return ((uint16_t)pms_frame_buf[10] << 8) | pms_frame_buf[11];
		case MODE_PM10:
		// PM10 (Standard)는 12/13번 째 바이트
		return ((uint16_t)pms_frame_buf[12] << 8) | pms_frame_buf[13];
		default:
		return -1;
	}
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
	float hz = 2000.0;     // 경고음 주파수
	int ms = 300;          // 300ms
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
// LED 모드 시각화 함수
// =======================
void update_led_mode(void) {
	// 모든 LED 끄기
	PORTA &= ~((1 << LED1) | (1 << LED2) | (1 << LED3));
	
	switch(pm_mode) {
		case MODE_PM1_0: // PM1.0: LED1만 켜기
		PORTA |= (1 << LED1);
		break;
		case MODE_PM2_5: // PM2.5: LED1, LED2 켜기
		PORTA |= (1 << LED1) | (1 << LED2);
		break;
		case MODE_PM10: // PM10: LED1, LED2, LED3 켜기
		PORTA |= (1 << LED1) | (1 << LED2) | (1 << LED3);
		break;
	}
}


// =======================
// FND 스캔 인터럽트 (1ms)
// =======================
ISR(TIMER0_COMP_vect)
{
	uint8_t idx = fnd_index;

	// 현재 FND 끄기
	PORTG = 0x00;

	// FND 세그먼트 데이터 출력
	// (첫 번째 FND는 유효하지 않은 0을 표시하지 않음 - 예를 들어 057이 아닌 57로 보이게)
	if (idx == 0 && fnd_data[idx] == 0 && fnd_data[1] == 0 && fnd_data[2] == 0)
	PORTC = 0x00;
	else
	PORTC = number[fnd_data[idx]];

	// 다음 FND 켜기
	PORTG = fnd_select[idx];

	fnd_index = (fnd_index + 1) % 4;
}

// =======================
// 외부 인터럽트 (스위치) 처리
// 스위치를 누르는 순간(하강 엣지) 동작하도록 수정
// =======================
ISR(INT4_vect)
{
	_delay_ms(20); // 짧은 디바운싱

	if (!(PINE & (1 << SWITCH_PIN))) {
		pm_mode = (pm_mode + 1) % 3;
		update_led_mode();

		// 모드 변경 즉시 FND 업데이트
		int pm = getPMValue();  // 변경된 모드 기준 PM 값 읽기
		if (pm >= 0) {
			if (pm > 999) pm = 999;
			fnd_data[0] = 0;
			fnd_data[1] = (pm / 100) % 10;
			fnd_data[2] = (pm / 10)  % 10;
			fnd_data[3] = pm % 10;
		}

		// 버튼 떼기 대기
		while (!(PINE & (1 << SWITCH_PIN))) {
			_delay_ms(1);
		}
	}
}



// =======================
// 메인
// =======================
int main(void)
{
	// 입출력 설정
	DDRC = 0xFF; // FND 세그먼트
	DDRG = 0x0F; // FND 선택
	DDRB |= (1 << BUZZER_PIN);     // 부저 출력

	// LED 출력 설정 및 초기 상태 설정
	DDRA |= (1 << LED1) | (1 << LED2) | (1 << LED3);
	update_led_mode(); // 초기 PM2.5 모드 LED 점등

	// 스위치 입력 및 풀업 설정 (PE4)
	DDRE &= ~(1 << SWITCH_PIN); // PE4 입력 설정
	PORTE |= (1 << SWITCH_PIN); // PE4 풀업 활성화

	// 외부 인터럽트 설정 (INT4)
	// 하강 엣지에서 인터럽트 발생 (스위치를 누르는 순간)
	// EICRB |= (1 << ISC41) | (0 << ISC40); 와 동일
	EICRB &= ~(1 << ISC40);
	EICRB |= (1 << ISC41);
	EIMSK |= (1 << INT4); // INT4 인터럽트 활성화

	UART1_init();

	// Timer0 → 1ms 인터럽트 설정 재정리
	// 16Mhz / 64분주 / 250(OCR0+1) = 1kHz (1ms)
	TCCR0 = (1<<WGM01) | (1<<CS01) | (1<<CS00); // CTC 모드, 64 분주
	OCR0 = 249;
	TIMSK |= (1<<OCIE0);

	sei(); // 전체 인터럽트 활성화

	while(1)
	{
		int pm = getPMValue(); // 선택된 모드의 PM 값 읽기

		if (pm >= 0)
		{
			if (pm > 999) pm = 999;

			// FND 출력 데이터 설정
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

		// 500ms마다 센서 값 업데이트 (이 지연은 센서 값 읽기 빈도를 조절합니다)
		_delay_ms(500);
	}
}
