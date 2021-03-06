#include <util/delay.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/*=======================================================*/
/*:::::: defines ::::::::::::::::::::::::::::::::::::::::*/
/*=======================================================*/

#define BAUD 19600
#define MYUBRR (F_CPU/16/BAUD-1)

#define TCCRA_MASK	(1<<WGM11)|(1<<COM1A1)|(1<<COM1B1);	//NON Inverted PWM
#define	TCCRB_MASK	(1<<WGM13)|(1<<WGM12)|(1<<CS10);	//FAST PWM with NO

#define   	OCR_TOP_VALUE		39998				// top timer value
#define   	ONE_PERCENT_STEP  	OCR_TOP_VALUE/100 	// ~400

#define 	CWISE  		0xAA	// clockwise
#define 	CCWISE  	0xBB	// counterclockwise
#define 	OF  		'>'		// open serial flag
#define 	CF  		'<'		// close serial flag

#define 	MIN_SPEED  	100		// dc motor minimum speed
#define 	MAX_SPEED  	200		// dc motor miximum speed


/*=======================================================*/
/*::::: functions :::::::::::::::::::::::::::::::::::::::*/
/*=======================================================*/

void UART_init(void);
uint8_t UART_getChar(void);
uint8_t UART_getString(uint8_t* buf);
void UART_putChar(uint8_t c);
void UART_putString(uint8_t* buf);

void PWM_init(void);
void PWM_start(void);
void PWM_stop(void);
void setupTimer(void);

void setPacketRate(uint8_t _packet_per_sec);
void setDirection(uint8_t dir);
void setSpeed(uint8_t speed);
void set_speed_smoothly(uint8_t speed);

//---------------------------------------------------------
void UART_init(void){
  UBRR0H = (uint8_t)(MYUBRR>>8);
  UBRR0L = (uint8_t)MYUBRR;
  UCSR0C = (1<<UCSZ01) | (1<<UCSZ00); 				/* 8-bit data */
  UCSR0B = (1<<RXEN0) | (1<<TXEN0) | (1<<RXCIE0);   /* Enable RX and TX */

}
//---------------------------------------------------------
uint8_t UART_getChar(void){
	// Wait for incoming data, looping on status bit
	while ( !(UCSR0A & (1<<RXC0)) );

	// Return the data
	return UDR0;
}
//----------------------------------------------------------
uint8_t UART_getString(uint8_t* buf){
	uint8_t* b0 = buf; //beginning of buffer
	while(1){
		uint8_t c = UART_getChar();
		*buf = c;
		++buf;
		// reading a 0 terminates the string
		if( c==0 ) return buf-b0;
		// reading a \n  or a \r return results
    	// in forcedly terminating the string
		if( c=='\n' || c=='\r' ){
			*buf = 0;
			++buf;
			return buf-b0;
		}
	}
}
//---------------------------------------------------------
void UART_putChar(uint8_t c){
	// wait for transmission completed, looping on status bit
	while ( !(UCSR0A & (1<<UDRE0)) );

	// Start transmission
	UDR0 = c;
}
//---------------------------------------------------------
void UART_putString(uint8_t* buf){
	while(*buf){
		UART_putChar(*buf);
		++buf;
	}
}
//---------------------------------------------------------
/* Timer 3 ~ digital pin 5 ~ DDRE */
void PWM_init(void){
  	//Data direction register
	DDRE &= 0x00;   	//digital pin 5	OFF

	//Configure TIMER3
	TCCR3A = TCCRA_MASK;
	TCCR3B = TCCRB_MASK;

	//Top timers value
	ICR3 = 39999;
}

void PWM_start(void){
	DDRE |= 0xFF;	//digital pin 5 ON
}

void PWM_stop(void){
	DDRE &= 0x00;	//digital pin 5 OFF
}
//---------------------------------------------------------
void setupTimer(void){
	TCCR5A = 0x00;
	TCCR5B = (1 << WGM52) | (1 << CS50) | (1 << CS52); // set the prescaler to 1024
	TIMSK5 |= (1 << OCIE5A); // enable timer interrupt
	OCR5A = F_CPU/1024-1; // 1 per second
}
//---------------------------------------------------------
void setPacketRate(uint8_t _packet_per_sec){
	if(!_packet_per_sec){ /*do nothing*/ }
	else OCR5A = (uint16_t)((F_CPU/1024/_packet_per_sec)-1);
}
//---------------------------------------------------------
void setDirection(uint8_t dir){
	if( dir==CWISE ){
    	PORTH &= ~(1 << PH6);	// digital pin 9 low
		PORTH |= (1 << PH5);	// digital pin 8 high
	}
	else if( dir==CCWISE ){
		PORTH &= ~(1 << PH5);	// digital pin 8 low
		PORTH |= (1 << PH6);	// digital pin 9 high
	}
}
//---------------------------------------------------------
void setSpeed(uint8_t speed){
	OCR3A = (speed-MIN_SPEED)*ONE_PERCENT_STEP;
}

/* using linear interpolation */
void set_speed_smoothly(uint8_t speed){
	cli();
	uint16_t delay = 1500;
	uint8_t num_step = 20;
	uint8_t delay_per_step = delay/num_step;
	uint16_t new_ocr = (speed-MIN_SPEED)*ONE_PERCENT_STEP;

	if( new_ocr>OCR3A ){
		uint16_t step = (new_ocr-OCR3A)/num_step;
		for(uint8_t i=1 ; i<=num_step ; i++){
			OCR3A += step;
			_delay_ms(delay_per_step);
		}
	}
	else if( new_ocr<OCR3A ){
		uint16_t step = (OCR3A-new_ocr)/num_step;
		for(uint8_t i=0 ; i<num_step ; i++){
			OCR3A -= step;
			_delay_ms(delay_per_step);
		}
	}
	setSpeed(speed);
	sei();
}
//---------------------------------------------------------

/*  global variables  */
volatile uint8_t timer_occurred = false;
volatile uint8_t msg_rcv = false;

/*=======================================================*/
/*::::: M A I N :::::::::::::::::::::::::::::::::::::::::*/
/*=======================================================*/
int main(void){

	cli();

	uint8_t buf[4];
	bool smooth = false;
	bool running = false;

	UART_init();
	_delay_ms(1500);

	while(true){	//infinite loop

		// handshake routine ----------------------------------
		uint8_t hshake[4] = { OF, OF+MIN_SPEED+1, OF+2, 10 };
		UART_putString(hshake);
		UART_getString(buf);
		if ( buf[0]==hshake[0] && buf[1]==hshake[1] ){
            uint8_t tmp[4] = { OF, OF+MIN_SPEED+1, OF+2, 10 };
			if( buf[2]==hshake[2] ){
				smooth = false;
				running = true;
			}
			else if( buf[2]=='l' ){
				smooth = true;
				running = true;
				tmp[2] = 'l';
			}
			_delay_ms(100);
			UART_putString(tmp);
		}
		else break;

		//-----------------------------------------------------
		// timer used as interrupt trigger
		setupTimer();

		// pwm to control motor speed
		PWM_init();

		// set starting parameters
		uint8_t _timestamp = 1;
		uint8_t _speed = MIN_SPEED;
		uint8_t _direction = CWISE;
		uint8_t _packet_rate = 1;
		//_delay_ms(1000);
		setSpeed(_speed);
		setDirection(_direction);
		setPacketRate(_packet_rate);
		PWM_start();

		// MAIN loop ------------------------------------------
		sei();
		while( running ){

			if( timer_occurred ){
				UART_putChar(_timestamp);
				UART_putChar(_speed);
				UART_putChar(_direction);
				UART_putChar(10);
				_timestamp = _timestamp<255?_timestamp+1:1;
				timer_occurred = false;
			}

			if( msg_rcv ){
				UART_getString(buf);
				if( buf[0] == CF ){
					cli();
					running = false;
				}
				else{
					if( _packet_rate != buf[0] ){
						_packet_rate = buf[0];
						setPacketRate(_packet_rate);
					}
					if( _speed != buf[1]){
						_speed = buf[1];
						setSpeed(_speed);
					}
					if( _direction != buf[2] ){
						_direction = buf[2];
						if(smooth) set_speed_smoothly(MIN_SPEED+5);
						setDirection(_direction);
						if(smooth) set_speed_smoothly(_speed);
					}
				}
				msg_rcv = false;
			}
		}

		PWM_stop();
		_timestamp = 1;
		_packet_rate = 1;
		_speed = MIN_SPEED;
		setPacketRate(_packet_rate);
		setSpeed(_speed);
		buf[0] = buf[1] = buf[2] = buf[3] = 0;
        _delay_ms(1000);
	}
	//-------------------------------------------------
}

/*=======================================================*/
/*::: INTERRUPT SERVICE ROUTINES ::::::::::::::::::::::::*/

ISR(TIMER5_COMPA_vect) {
	timer_occurred = true;
}

ISR(USART0_RX_vect) {
	msg_rcv = true;
}

/*:::::::::::::::::::::::::::::::::::::::::::::::::::::::*/
