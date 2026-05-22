#define F_CPU 16000000UL
#define BAUD 9600
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/setbaud.h>
#include <avr/sleep.h>
#include <stdint.h>
#include <string.h>

//WARNING -> the ADC fault recovery could had been better then trying more than 2 times. like adding system restart and halting the system if error still continues, but for such project. advaced fault reccovery is not needed.

// =================================================================== VARIABLES =================================================================
uint16_t raw_array[12] = {};
int refined_array[100] = {};
uint8_t ADC_timeout = 0;
uint8_t first_conversion = 1;

volatile uint8_t current_input = 0;
volatile uint8_t current_LED = 0;

typedef struct {
	int buffer[100];
	uint8_t head;
	uint8_t tail;
	uint8_t buffer_size;
} RA_buffer;

RA_buffer circular_buffer;

const int pixelPins[12][2] = {
	// Row 0: 4 pixels
	{0, 0b00000000}, {1, 0b00000010}, {2, 0b00000100}, {3, 0b00000000},  // Pixels 0,1,2,3
	// Row 1: 4 pixels
	{4,	0b00000001}, {5, 0b00000011}, {6, 0b00000101}, {7, 0b00000100},  // Pixels 4,5,6,7
	// Row 2: 4 pixels
	{8, 0b00000001}, {9, 0b00000010}, {10, 0b00000011}, {11, 0b00000100}   // Pixels 8,9,10,11
};
	
// ============================================================================ FLAGS =======================================================
typedef enum {
	CAMERA_IDLE,
	CAMERA_SAMPLING,
	CAMERA_PROCESSING,
	CAMERA_TRANSMITTING,
	CAMERA_ERROR,
	CAMERA_END
} camera_state_t;

volatile camera_state_t camera_state = CAMERA_IDLE;

// =========================================================================================== INITS =====================================================
void init_timer1() {
	TCCR1B |= (1 << WGM12);     // CTC mode (reset on OCR1A match)
	TCCR1B |= (1 << CS11) | (1 << CS10);  // Prescaler 64
	OCR1A = 12499;               // 0 to 12499 = 12500 ticks
	TIMSK1 |= (1 << OCIE1A);    // Enable Timer1 compare interrupt
}

void init_UART() {
	UCSR0B |= (1 << RXEN0) | (1 << TXEN0);     // enable Rx and Tx
	UCSR0C |= (1 << UCSZ01) | (1 << UCSZ00);   // 8-bit data, 1 stop bit
	UBRR0H = UBRRH_VALUE;
	UBRR0L = UBRRL_VALUE;
}

void init_ADC() {
	ADMUX |= (1 << REFS0);                    // AVcc reference
	ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);  // enable ADC, pre scaler 128
}

void init_ADCWatchdog() {
	TCCR0B |= (1 << CS01); // prescaler 8
	TCCR0A |= (1 << WGM01); // CTC mode
	TIMSK0 |= (1 << OCIE0A); // enabling interrupt (nested)
}

void set_pixel(uint8_t array_index) {
	ADMUX |= (1 << pixelPins[array_index][1]) | (1 << ADLAR); //sets ADC pin according to pin data
	if (array_index <= 8) { // sets LED pin according to pin data, if less then 9, continue on B IO register, but if the IO register limit of B is exceeded, it switches to Register D
		DDRB = (1 << pixelPins[array_index][0]);
	} else {
		uint8_t register_d_pin = array_index - 8;
		DDRD = (1 << register_d_pin);
	}
}

void UART_Fill_data(RA_buffer *circular) {
	for (uint8_t i = 0; i <=99; i++) {
		circular->buffer[i] = refined_array[i];
		circular->tail = (circular->tail + 1) % (circular->buffer_size);
	}
}

void UART_Send_data(RA_buffer *circular){
	camera_state = CAMERA_TRANSMITTING;
	
	// 1. Enable transmitter
	UCSR0B |= (1 << TXEN0);
	
	if (circular->head != circular->tail) {
		// 2. Clear TXC0 flag by writing a 1 to it before sending
		UCSR0A |= (1 << TXC0);
		
		// 3. Wait for the transmit buffer to be empty/ready for new data
		while (!(UCSR0A & (1 << UDRE0)));
		
		// 4. Send the byte
		UDR0 = circular->buffer[circular->head];
		
		// 5. Update circular buffer head
		circular->head = (circular->head + 1) % (circular->buffer_size);
		
		// 6. Optional: Wait here ONLY if you must block until the byte leaves the wire
		while (!(UCSR0A & (1 << TXC0)));
		
		} else {
		camera_state = CAMERA_ERROR;
	}
}


void UART_send_error() {
	
}

// FOR SUCH SYSTEM, WE CAN ADD SUPERSAMPLING BY AVERAGING THE MUTPLE ARRAYS AND NOT INDIVIDUAL READING, BUT FOR SUCH SMALL ARRAY SIZE, IT WILL BE OVER PROCESSING ONLY UTILIZING MEMORY AND SPEED, BUT FOR PHOTODIODE + AMPLIFIER CIRCUIT IN LARGER SIZE, SUPERSAMPLPING CAN BE IMPLEMENTED AS WAY TOLD

void scale_image(void) {
	// Create a 1D array of 12 values
	int input[12];
	for (int i = 0; i < 12; i++) {
		input[i] = raw_array[i];
	}
	
	// Interpolate from 12 to 100 values using your algorithm
	for (int out_idx = 0; out_idx < 100; out_idx++) {
		// Map output index (0-99) to input index (0-11)
		float pos = (out_idx / 99.0) * 11.0;
		int start_idx = (int)pos;
		int end_idx = start_idx + 1;
		
		// Boundary check
		if (end_idx >= 12) {
			refined_array[out_idx] = input[11];
			continue;
		}
		
		// Your formula: a + ((b - a) / steps) * position
		float t = pos - start_idx;
		int value = input[start_idx] + (int)((input[end_idx] - input[start_idx]) * t);
		
		refined_array[out_idx] = value;
	}
	
	camera_state = CAMERA_TRANSMITTING;
}

// ========================================================================= interupts =======================================================
ISR(TIMER1_COMPA_vect) {
	sleep_disable();
	// code to read and store data, since LDR stable reading time is 50 milli second, interrupt will never be missed
	camera_state = CAMERA_SAMPLING;
	
	if (first_conversion) {
		OCR0A = 400;  // 400 × 500ns = 200µs (plus margin)
		} else {
		OCR0A = 208;  // 208 × 500ns = 104µs (plus margin)
	}
	
	init_ADCWatchdog(); //initiating inside ISR to make sure watchdog timer start when ADC star
	TCNT0 = 0;

	ADCSRA |= (1 << ADSC);
	
	while ((ADCSRA & (1 << ADSC)) && !ADC_timeout);
	
	if (ADC_timeout) { // retry if error was in first conversion
		// give second chance to ADC if it gives error
		ADCSRA |= (1 << ADSC);
		while (ADCSRA & (1 << ADSC));
		
		uint16_t low = ADCL;
		uint16_t high = ADCH;
		uint16_t adc_value = (ADCH << 2) | (ADCL >> 6);
		
		// Extreme value check
		if (adc_value <= 5 || adc_value >= 1018 || adc_value < 10 || adc_value > 1013) {
			camera_state = CAMERA_ERROR;
			// Extreme value - use previous reading or set to default
			if (current_input > 0) {
				raw_array[current_input] = raw_array[current_input - 1];
				} else {
				raw_array[current_input] = 512;  // Mid-range default
			}
			} else {
			raw_array[current_input] = adc_value;
		}
	}
	
	if (current_input <= 11 && !ADC_timeout) { // this function if watchdog did not triggered error
		uint16_t low = ADCL;
		uint16_t high = ADCH;
		uint16_t adc_value = (ADCH << 2) | (ADCL >> 6);
		
		// Extreme value check
		if (adc_value <= 5 || adc_value >= 1018 || adc_value < 10 || adc_value > 1013) {
			camera_state = CAMERA_ERROR;
			// Extreme value detected
			// Use previous reading
			if (current_input > 0) {
				raw_array[current_input] = raw_array[current_input - 1];
				} else {
				raw_array[current_input] = 512;  // Default for first pixel
			}
			} else {
			raw_array[current_input] = adc_value;
		}
	}
	
	if (current_input == 11) { //change state to processing when raw input done
		camera_state = CAMERA_PROCESSING;
	}
	
	first_conversion = 0;
	current_input = current_input + 1;
	ADC_timeout = 0;
	current_LED = current_LED + 1;
}

ISR(TIMER0_COMPA_vect) {
	if (ADCSRA & (1 << ADSC)) {
		camera_state = CAMERA_ERROR;
		ADC_timeout = 1;
}
}
// ======================================================================= main loop =============================================================== 

int main(void) {
	
	sei();                       // enable global interrupts
	init_UART();
	init_ADC();
	init_timer1();  //timer init after ADC init to ensure atomicity
	UART_Send_data(&circular_buffer);
	
	while (1) {
		
		switch(camera_state) { // state machine using switch and case
			
			case CAMERA_IDLE:
				sleep_enable();
				break;
				
			case CAMERA_ERROR:
				UART_send_error(); //any custom message
				break;
				
			case CAMERA_SAMPLING:
				// wait or do other task like OLED or other.
				ADMUX |= (1 << pixelPins[current_LED][1]); // set corresponding ADC pin
				if (current_LED < 7) {
					DDRB = (1 << current_LED);
				} else {
					DDRD = (1 << (current_LED - 7));
				}
				break;
				
			case CAMERA_PROCESSING:
				cli(); // disable interrupts for atomic processing.
				scale_image();
				break;
				
			case CAMERA_TRANSMITTING:
				circular_buffer.head = 0;
				circular_buffer.tail = 0;
				circular_buffer.buffer_size = 100;
				
				UART_Fill_data(&circular_buffer);
				for (uint8_t i = 0; i <= 99; i++) {
					UART_Send_data(&circular_buffer);
				}
				camera_state = CAMERA_END;
				break;
				
			case CAMERA_END:
				raw_array[12] = {0}; //reset for next data
				refined_array[100] = {0};
				ADC_timeout = 0;
				first_conversion = 1;

				current_input = 0;
				current_LED = 0;
				
				circular_buffer.head = 0;
				circular_buffer.tail = 0;
				circular_buffer.buffer_size = 100;
				
				TCNT0 = 0; //reseting timer ticks
				TCNT1 = 0; //reseting timer ticks
				
				while (PIND & (1 << 7)); // wait for PD 7 to press to re click
				camera_state = CAMERA_IDLE;
				sei();
				break;
		}
	}
}
