#define F_CPU 16000000UL
#define BAUD 9600
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/setbaud.h>
#include <avr/sleep.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// =================================================================== VARIABLES =================================================================
uint16_t raw_array[12] = {0};
int refined_array[100] = {0};
volatile uint8_t ADC_timeout = 0;
volatile uint8_t first_conversion = 1;

volatile uint8_t current_input = 0;
volatile uint8_t current_LED = 0;
volatile uint8_t watchdog_configured = 0;

typedef struct {
    int buffer[100];
    uint8_t head;
    uint8_t tail;
    uint8_t buffer_size;
} RA_buffer;

RA_buffer circular_buffer;

const int pixelPins[12][2] = {
    {0, 0}, {1, 2}, {2, 4}, {3, 0},
    {4, 1}, {5, 3}, {6, 5}, {7, 4},
    {8, 1}, {9, 2}, {10, 3}, {11, 4}
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

// ============================================================ UART Functions ================================================================
void UART_SendChar(char c) {
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

void UART_SendString(const char *str) {
    while (*str) {
        UART_SendChar(*str++);
    }
}

void UART_send_error(void) {
    UART_SendString("ERR:CAMERA_FAULT\n");
}

void UART_SendInt(int val) {
    char buf[8];
    itoa(val, buf, 10);
    UART_SendString(buf);
}

// =========================================================================================== INITS =====================================================
void init_timer1(void) {
    TCCR1B |= (1 << WGM12);                 // CTC mode
    TCCR1B |= (1 << CS11) | (1 << CS10);    // Prescaler 64
    OCR1A = 12499;                          // 50ms at 16MHz with prescaler 64
    TIMSK1 |= (1 << OCIE1A);                // Enable Timer1 compare interrupt
}

void init_UART(void) {
    UCSR0B |= (1 << RXEN0) | (1 << TXEN0);   // enable Rx and Tx
    UCSR0C |= (1 << UCSZ01) | (1 << UCSZ00); // 8-bit data, 1 stop bit
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;
}

void init_ADC(void) {
    ADMUX |= (1 << REFS0);                   // AVcc reference
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);  // enable ADC, prescaler 128
}

void init_ADCWatchdog(void) {
    if (!watchdog_configured) {
        TCCR0B |= (1 << CS01);               // prescaler 8
        TCCR0A |= (1 << WGM01);              // CTC mode
        TIMSK0 |= (1 << OCIE0A);             // enable interrupt
        watchdog_configured = 1;
    }
}

void select_pixel(uint8_t pixel_index) {
    // Set ADC channel (using direct channel numbers, not masks)
    uint8_t channel = pixelPins[pixel_index][1];
    ADMUX = (ADMUX & 0xF0) | (channel & 0x0F);
    
    // Charlieplexing LED control
    uint8_t led_pin = pixelPins[pixel_index][0];
    if (led_pin < 7) {
        DDRB = (1 << led_pin);
        PORTB = (1 << led_pin);
    } else {
        DDRD = (1 << (led_pin - 7));
        PORTD = (1 << (led_pin - 7));
    }
}

void UART_Fill_data(RA_buffer *circular) {
    for (uint8_t i = 0; i < 100; i++) {
        circular->buffer[i] = refined_array[i];
        circular->tail = (circular->tail + 1) % (circular->buffer_size);
    }
}

void UART_Send_data(RA_buffer *circular) {
    if (circular->head != circular->tail) {
        while (!(UCSR0A & (1 << UDRE0)));
        UDR0 = circular->buffer[circular->head];
        circular->head = (circular->head + 1) % (circular->buffer_size);
        while (!(UCSR0A & (1 << TXC0)));
    } else {
        camera_state = CAMERA_ERROR;
    }
}

// ============================================================ Image Scaling ================================================================
void scale_image(void) {
    int input[12];
    for (int i = 0; i < 12; i++) {
        input[i] = raw_array[i];
    }
    
    for (int out_idx = 0; out_idx < 100; out_idx++) {
        float pos = (out_idx / 99.0) * 11.0;
        int start_idx = (int)pos;
        int end_idx = start_idx + 1;
        
        if (end_idx >= 12) {
            refined_array[out_idx] = input[11];
            continue;
        }
        
        float t = pos - start_idx;
        int value = input[start_idx] + (int)((input[end_idx] - input[start_idx]) * t);
        refined_array[out_idx] = value;
    }
}

// ========================================================================= INTERRUPTS =======================================================
ISR(TIMER1_COMPA_vect) {
    sleep_disable();
    
    if (camera_state == CAMERA_IDLE) {
        camera_state = CAMERA_SAMPLING;
        current_input = 0;
        current_LED = 0;
        first_conversion = 1;
        select_pixel(0);
    }
    
    if (camera_state == CAMERA_SAMPLING) {
        if (first_conversion) {
            OCR0A = 400;      // 200µs for first conversion
        } else {
            OCR0A = 208;      // 104µs for normal conversions
        }
        TCNT0 = 0;
        ADCSRA |= (1 << ADSC);
    }
}

ISR(ADC_vect) {
    if (camera_state != CAMERA_SAMPLING) return;
    
    uint16_t adc_value = ADC;   // Read right-aligned value
    
    // Extreme value check
    if (adc_value <= 5 || adc_value >= 1018) {
        // Use previous value if available
        if (current_input > 0) {
            adc_value = raw_array[current_input - 1];
        } else {
            adc_value = 512;
        }
    }
    
    raw_array[current_input] = adc_value;
    current_input++;
    current_LED++;
    
    if (current_input >= 12) {
        current_input = 0;
        camera_state = CAMERA_PROCESSING;
    } else {
        select_pixel(current_input);
        // Next ADC will be triggered by Timer1
    }
}

ISR(TIMER0_COMPA_vect) {
    if (ADCSRA & (1 << ADSC)) {
        ADC_timeout = 1;
        ADCSRA &= ~(1 << ADSC);   // Abort hanging conversion
    }
}

// ======================================================================= MAIN LOOP =============================================================== 
int main(void) {
    // Initialize peripherals
    init_UART();
    init_ADC();
    init_timer1();
    init_ADCWatchdog();
    
    // Initialize circular buffer
    circular_buffer.head = 0;
    circular_buffer.tail = 0;
    circular_buffer.buffer_size = 100;
    memset((int*)circular_buffer.buffer, 0, sizeof(circular_buffer.buffer));
    
    // Clear global variables
    current_input = 0;
    current_LED = 0;
    ADC_timeout = 0;
    first_conversion = 1;
    camera_state = CAMERA_IDLE;
    
    sei();  // Enable global interrupts
    
    UART_SendString("Camera Ready\n");
    
    while (1) {
        switch(camera_state) {
            
            case CAMERA_IDLE:
                sleep_enable();
                break;
                
            case CAMERA_SAMPLING:
                // Let interrupts do the work
                sleep_enable();
                break;
                
            case CAMERA_PROCESSING:
                cli();
                scale_image();
                sei();
                camera_state = CAMERA_TRANSMITTING;
                break;
                
            case CAMERA_TRANSMITTING:
                circular_buffer.head = 0;
                circular_buffer.tail = 0;
                UART_Fill_data(&circular_buffer);
                for (uint8_t i = 0; i < 100; i++) {
                    UART_Send_data(&circular_buffer);
                }
                UART_SendString("\n");
                camera_state = CAMERA_END;
                break;
                
            case CAMERA_END:
                ADC_timeout = 0;
                first_conversion = 1;
                current_input = 0;
                current_LED = 0;
                circular_buffer.head = 0;
                circular_buffer.tail = 0;
                TCNT0 = 0;
                TCNT1 = 0;
                
                // Wait for button press on PD7 to restart
                while (PIND & (1 << 7));
                camera_state = CAMERA_IDLE;
                break;
                
            case CAMERA_ERROR:
                UART_send_error();
                camera_state = CAMERA_IDLE;
                break;
        }
    }
    
    return 0;
}
