/*
 * jmcki007_122A_Final_Project.c
 *
 * Created: 11/10/2018 4:03:21 PM
 * Author : mckin
 */ 

//define F_CPU before delay include
# define F_CPU 8000000UL
#include <avr/io.h>
#include <stdio.h>
#include <string.h>
#include "ds18b20.h"
#include "timer.h"
#include "io.h"
#include <util/delay.h>
#include "uart.h"
#include <stdlib.h>


#define tasksNum 2
const unsigned long tasksPeriodGCD = 500;

typedef struct task {
    int state; // Current state of the task
    unsigned long period; // Rate at which the task should tick
    unsigned long elapsedTime; // Time since task's previous tick
    int (*TickFct)(int); // Function to call for task's tick
} task;

task tasks[tasksNum];
enum wifi_state{POWERDOWN, BOOT, ON, CONNECT, CONNECTED, READY};
	
volatile char wifi_ready = 0;

void TimerISR() {
    unsigned char i;
    TimerFlag = 1;
    for (i = 0; i < tasksNum; ++i)
    {
        if ( tasks[i].elapsedTime >= tasks[i].period )
        {
            tasks[i].state = tasks[i].TickFct(tasks[i].state);
            tasks[i].elapsedTime = 0;
        }
        tasks[i].elapsedTime += tasksPeriodGCD;
    }
}

int temperature = 0x0000;
char temp_string[12];
volatile unsigned char pd = 0;

void LCD_write_S(char * str)
{
	LCD_ClearScreen();
	unsigned char c = 1;
	char *strpointer = str;
	while(*strpointer && c<33) {
		LCD_Cursor(c++);
		LCD_WriteData(*strpointer++);
	}
	LCD_Cursor(33);
}

void remove_whitespace(char* input, int len)
{
	char str[128];
	unsigned int j = 0;
	for (unsigned int i = 0; input[i] && i < len-1; i++)
	{
		if (input[i] != '\n' && input[i]!= '\r')
		{
			str[j] = input[i];
			j++;
		}
	}
	
	strcpy(input, str);
	input[j] = 0;
}

void get_response(char * str, int len)
{
	unsigned int i;
	for (i=0; (uart0_available() && i<len-1); i++)
	{
		str[i] = uart0_getc();
	}
	str[i++] = 0;
}

void display_response()
{
	if (uart0_available())
	{
		char str[128] = "";
		get_response(str, 64);
		remove_whitespace(str, 64);
		uart0_flush();
	
		LCD_ClearScreen();
		LCD_write_S(str);
	}
}

void display_input_line()
{
	char str[33] = "";
	if (uart0_available())
	{
		unsigned char c = 0;
		while (uart0_peek() == '\n' || uart0_peek() == '\r')
		{
			uart0_getc();
		}
		while (uart0_available() && uart0_peek() != '\r' && c < 33)
		{
			char tempstr[2];
			tempstr[0] = uart0_getc();
			tempstr[1] = 0;
			strcat(str, tempstr);
			c++;
		}
		
		LCD_ClearScreen();
		LCD_write_S(str);
	}
}

void get_input_line(char * str, char leng)
{
	if (uart0_available())
	{
		unsigned char c = 0;
		while (uart0_peek() == '\n' || uart0_peek() == '\r')
		{
			uart0_getc();
		}
		while (uart0_available() && uart0_peek() != '\r' && c < leng)
		{
			char tempstr[2];
			tempstr[0] = uart0_getc();
			tempstr[1] = 0;
			strcat(str, tempstr);
			c++;
		}
	}
}

void send_temp_uart()
{
	uart0_putc((char) (temperature >> 8));
	uart0_putc((char) (temperature));
}

int get_temperature(int state)
{
    if (ds18b20convert( &PORTA, &DDRA, &PINA, 0x01, NULL ))
	{
		PORTB = 0x80;
	}
	else
	{
		PORTB = 0x01;
	}
    
    _delay_ms(750);
    
    ds18b20read( &PORTA, &DDRA, &PINA, 0x01, NULL, &temperature );
	
	PORTC = temperature >> 4;
    if (wifi_ready)
	{
		char data[20] = "Temp: ";
		char cmd[64] = "AT+CIPSEND=";
		
		int temp_deg = temperature/16;
		int temp_frac = (temperature & 0x000F) * 625;
		
		sprintf(temp_string,"%i.%04i", temp_deg, temp_frac);
		
		strcat(data, temp_string);
		
		//data[5] = (char) temperature >> 8;
		//data[6] = (char) temperature;
		
		sprintf(cmd, "AT+CIPSEND=%d", strlen(data));
		strcat(cmd, "\r\n");
		uart0_flush();
		uart0_puts(cmd);
		_delay_ms(50);
		LCD_write_S(cmd);
		_delay_ms(1000);
		display_input_line();
		_delay_ms(1000);
		display_input_line();
		_delay_ms(1000);
		uart0_puts(data);
		display_input_line();
		_delay_ms(1000);
		for (unsigned char k = 0; k<7; k++)
		{
			uart0_putc(data[k]);
		}
		LCD_write_S(data);
		_delay_ms(1000);
		display_input_line();
		_delay_ms(1000);
	}
    return state;
}

void wifi_init()
{
	PORTD |= 0x08;
	LCD_ClearScreen();
	uart0_init(UART_BAUD_SELECT_DOUBLE_SPEED(19200, F_CPU));
	uart0_puts("ATE0\r\n");
	_delay_ms(100);
}

void wifi_connect(char* ssid, char* pswd)
{
	char cmd[64] = "AT+CWJAP=\"";
	
	strcat(cmd, ssid);
	strcat(cmd, "\",\"");
	strcat(cmd, pswd);
	strcat(cmd, "\"\r\n");
	
	LCD_write_S(cmd);
	uart0_puts(cmd);	
}

int task_wifi(int state)
{
	static char boot_timer = 7;
	char str[128] = "";
	
	switch (state) // transitions
	{
		case ON:
			if (pd)
			{
				LCD_write_S("POWERDOWN");
				_delay_ms(1000);
				state = POWERDOWN;
				PORTD &= 0xF7;
				break;
			}
			else
			{
				state = CONNECT;
				wifi_connect("TemperatureHub", "abd123456");
			}
			break;
			
		case BOOT:
			if (pd)
			{
				LCD_write_S("POWERDOWN");
				_delay_ms(1000);
				state = POWERDOWN;
				PORTD &= 0xF7;
				break;
			}
			if (!boot_timer) // if timer == 0
			{
				state = ON;
				LCD_write_S("Starting Wifi");
				wifi_init();
			}
			else
			{
				LCD_write_S("BOOT");
				boot_timer--;
			}
			break;
		
		case CONNECT:
			if (pd)
			{
				LCD_write_S("POWERDOWN");
				_delay_ms(1000);
				state = POWERDOWN;
				PORTD &= 0xF7;
				break;
			}
			get_input_line(str,64);
			remove_whitespace(str,64);
			if (strstr(str, "GOT IP"))
			{
				state = CONNECTED;
				LCD_write_S("CONNECTED");
			}
			else
			{
				LCD_write_S("STATE = CONNECT");
				_delay_ms(1000);
				LCD_write_S(str);
				_delay_ms(1000);
			}
			break;
		
		case CONNECTED:
			if (pd)
			{
				LCD_write_S("POWERDOWN");
				_delay_ms(1000);
				state = POWERDOWN;
				PORTD &= 0xF7;
				break;
			}
			LCD_write_S("STATE = Connected");
			_delay_ms(1000);
			uart0_puts("AT\r\n");
			_delay_ms(50);
			get_response(str,64);
			remove_whitespace(str,64);
			if (strstr(str, "OK"))
			{
				state = READY;
				_delay_ms(5000);
				LCD_write_S("STARTING TCP");
				uart0_flush();
				uart0_puts("AT+CIPSTART=\"TCP\",\"192.168.64.64\",80\r\n");
				_delay_ms(1000);
				display_input_line();
				_delay_ms(1000);
				display_input_line();
				_delay_ms(1000);
			}
			else
			{
				LCD_write_S("BLARG");
			}
			break;
			
		case POWERDOWN:
			if(!pd)
			{
				state = BOOT;
				PORTD |= 0x08;
			}
			break;
			
		case READY:
			if (pd)
			{
				LCD_write_S("POWERDOWN");
				_delay_ms(1000);
				state = POWERDOWN;
				PORTD &= 0xF7;
				break;
			}
			
			uart0_flush();
			uart0_puts("AT+CIPSTATUS\r\n");
			_delay_ms(50);
			get_response(str,64);
			if (strstr(str,"STATUS:4"))
			{
				state = CONNECTED;
			}
			break;
			
		default:
			state = BOOT;
			PORTD |= 0x08;
			break;
		
	}
	
	switch (state) // Actions
	{
		case ON:
			wifi_ready = 0;
		break;
		
		case CONNECT:
			break;
		
		case CONNECTED:
			wifi_ready = 0;
			break;
			
		case READY:
			wifi_ready = 1;
			display_input_line();
			_delay_ms(500);
			uart0_flush();
			uart0_puts("AT+CIPSTATUS\r\n");
			_delay_ms(50);
			display_input_line();
			_delay_ms(500);
			break;
			
		
		case BOOT:
			wifi_ready = 0;
			break;
		
		case POWERDOWN:
			wifi_ready = 0;
			break;
	}
	
	return state;
}

int main(void)
{
    DDRB = 0xFF;
    PORTB = 0x00;
    
    DDRD = 0xFE;
    PORTD = 0x01;
    
    DDRC = 0xFF;
    PORTC = 0x00;
	
    unsigned char i = 0;
    tasks[i].state = -1;
    tasks[i].period = 2000;
    tasks[i].elapsedTime = 0;
    tasks[i].TickFct = &get_temperature;
    i++;
	tasks[i].state = -1;
	tasks[i].period = 500;
	tasks[i].elapsedTime = 500;
	tasks[i].TickFct = &task_wifi;
   
	LCD_init();
	_delay_ms(10);
	
	LCD_write_S("LCD Init");
	
	ds18b20wsp(&PORTA, &DDRA, &PINA, 0x01, NULL, 0, 127, 0x7F);
	
	TimerSet(tasksPeriodGCD);
	TimerOn();
	
    while (1) 
    {
    }
}