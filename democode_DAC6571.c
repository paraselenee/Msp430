#include <msp430g2553.h>
#include "tm1638.h"

#define V_T100ms 5
// 0.5s杞欢瀹氭椂鍣ㄦ孩鍑哄�硷紝25涓�20ms
#define V_T500ms 25

//DAC6571鎿嶄綔  P1.4鎺DA(pin4),P1.5鎺CL(pin5)
#define SCL_L P1OUT &= ~BIT5
#define SCL_H P1OUT |= BIT5
#define SDA_L P1OUT &= ~BIT4
#define SDA_H P1OUT |= BIT4
#define SDA_IN    \
	P1OUT |= BIT4;  \
	P1DIR &= ~BIT4; \
	P1REN |= BIT4
#define SDA_OUT  \
	P1DIR |= BIT4; \
	P1REN &= ~BIT4
#define DAC6571_voltage_max 4990 //499x10mV
#define DAC6571_SET_MAX 1000
#define DAC6571_address 0x98 // 1001 10 A0 0  A0=0
#define CHANNEL_MAX 3
#define SAMPLE_NUM 20
#define MODE_MAX 2
/*
*
*       Timer
*
*/
unsigned char clock100ms = 0;
unsigned char clock500ms = 0;
unsigned char clock20ms = 0;
// timer overflow
unsigned char clock100ms_flag = 0;
unsigned char clock500ms_flag = 0;
unsigned char clock20ms_flag = 0;
/*
*
*       DAC6571
*
*/
unsigned int dac6571_code = 0x0800;
unsigned int voltage_set = 800;
unsigned char level = 8;
unsigned char dac6571_flag = 0;
unsigned char Rload = 3;
unsigned char Rload_new = 3;
const unsigned int DAC_CALIBRATION_TABLE[3][10] =
		{
				//	 0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 0.9 1.0
				{/*1*/ 10, /*2*/ 102, /*3*/ 195, /*4*/ 271, /*5*/ 362, /*6*/ 473, /*7*/ 592, /*8*/ 701, /*9*/ 816, /*10*/ 916}, //3 Ohm
				{/*1*/ 10, /*2*/ 105, /*3*/ 195, /*4*/ 274, /*5*/ 392, /*6*/ 516, /*7*/ 621, /*8*/ 722, /*9*/ 850, /*10*/ 949}, //5 Ohm
				{/*1*/ 10, /*2*/ 107, /*3*/ 195, 302, 392, 516, 621, 722, 850, 949},																						//10 Ohm
};
unsigned char Rload_Convert(unsigned char Rload)
{
	if (Rload < 4)
		return 0;
	else if (Rload < 8)
		return 1;
	else if (Rload < 12)
		return 2;
	else
		return 3;
}
void init_dac6571(void)
{ //DAC ports
	P1DIR |= BIT4 + BIT5;
	P1OUT |= BIT4 + BIT5;
}
void DAC_Convert(unsigned char byte_data)
{
	unsigned char i, shelter;
	shelter = 0x80;

	for (i = 1; i <= 8; i++)
	{
		if ((byte_data & shelter) == 0)
			SDA_L;
		else
			SDA_H;
		SCL_H;
		SCL_L;
		shelter >>= 1;
	}
	SDA_IN;
	SCL_H;
	SCL_L;
	SDA_OUT;
}
void DAC_Set_Continuous(void)
{
	unsigned char msbyte, lsbyte;
	SCL_H;
	SDA_H;
	SDA_L;
	SCL_L; // START condition
	DAC_Convert(DAC6571_address);
	msbyte = dac6571_code / 256;
	lsbyte = dac6571_code - msbyte * 256;
	DAC_Convert(msbyte);
	DAC_Convert(lsbyte);

	SDA_L;
	SCL_H;
	SDA_H; // STOP condition
}
void DAC_Set_Discrete(unsigned char r, unsigned char level)
{
	float temp;
	unsigned char msbyte, lsbyte;
	if (level >= 10 || r >= 3)
		return DAC_Set_Continuous();
	else
	{
		temp = DAC_CALIBRATION_TABLE[r][level] * 4096.0 / (DAC6571_voltage_max + 1);
		dac6571_code = temp;
		SCL_H;
		SDA_H;
		SDA_L;
		SCL_L; // START condition
		DAC_Convert(DAC6571_address);
		msbyte = dac6571_code / 256;
		lsbyte = dac6571_code - msbyte * 256;
		DAC_Convert(msbyte);
		DAC_Convert(lsbyte);

		SDA_L;
		SCL_H;
		SDA_H; // STOP condition
	}
}
/*
*
*       ADC10
*
*/
unsigned int current_a2_sum = 0;
unsigned int current_a1_sum = 0;
unsigned int voltage_sum = 0;
unsigned int current_a1 = 0;
unsigned int current_a2 = 0;
unsigned int voltage = 0;
unsigned char sample_num = 0;
unsigned int seq_num = CHANNEL_MAX - 1;
//unsigned int ADC_tmp[CHANNEL_MAX] ={0};
unsigned int ADC_sample_0 = 0;
unsigned int ADC_sample_1 = 0;
unsigned int ADC_sample_2 = 0;
unsigned int ADC_tmp_0 = 0;
unsigned int ADC_tmp_1 = 0;
unsigned int ADC_tmp_2 = 0;
// struct ADC{
// 	unsigned int value[CHANNEL_MAX];
// }adc10;
void init_ADC10(void)
{
	ADC10CTL1 |= CONSEQ_1;													 //sequence sampling using channel 1
	ADC10CTL0 |= SREF_1 + REFON + ADC10IE + REF2_5V; //enable the interrupt, using built-in 2.5 Vref
	ADC10CTL0 |= ADC10SHT_2 + MSC;
	ADC10CTL1 |= ADC10SSEL_3 + SHS_0;
	ADC10CTL1 |= INCH_2;						//largest number of used channel is 3
	ADC10CTL0 |= ADC10ON;						//enable ADC10
	ADC10AE0 |= BIT1 + BIT2 + BIT3; //open channel 1.1~1.3
}

// ADC10 interrupt
#pragma vector = ADC10_VECTOR
__interrupt void ADC10_ISR(void)
{
	switch (seq_num)
	{
	case 2:
		ADC_sample_2 += ADC10MEM;
		break;
	case 1:
		ADC_sample_1 += ADC10MEM;
		break;
	case 0:
		ADC_sample_0 += ADC10MEM;
		break;
	default:
		break;
	}
	if (--seq_num >= CHANNEL_MAX)
		seq_num = CHANNEL_MAX - 1;
}

//ADC sample
void ADC10_sample(void)
{
	unsigned char i;
	// clear previous data
	// for (i=0; i<CHANNEL_MAX; ++i)
	// {
	// 	ADC_tmp[i]=0;
	// }
	ADC_sample_2 = 0;
	ADC_sample_1 = 0;
	ADC_sample_0 = 0;
	for (i = 0; i < SAMPLE_NUM; i++)
	{
		ADC10CTL0 |= ENC + ADC10SC; //begin sampling
		__delay_cycles(300);				//ensure abundant sampling time
		while (ADC10CTL1 & BUSY)
			;
	}
	// for(i=0;i<CHANNEL_MAX;++i)
	// {
	// 	adc10.value[i] =(long)ADC_tmp[i]*2500/1023/SAMPLE_NUM;
	// }
	ADC_tmp_0 = (long)ADC_sample_0 * 2500 / 1023 / SAMPLE_NUM;
	ADC_tmp_1 = (long)ADC_sample_1 * 2500 / 1023 / SAMPLE_NUM;
	ADC_tmp_2 = (long)ADC_sample_2 * 2500 / 1023 / SAMPLE_NUM;
}

/*
*
*       UI
*
*/
// display mode
unsigned char mode = 0;
unsigned char display = 0;
unsigned char digit[8] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
unsigned char pnt = 0x02;
unsigned char led[] = {0, 0, 0, 0, 0, 0, 0, 0};
// key
unsigned char key_code = 0;
unsigned char key_cnt = 0;
//Display current 1 and current 2 on 7-seg
void Display_Stage3(void)
{
	unsigned int i;
	unsigned int tmp;
	switch (display)
	{
	case 0:
		pnt = 0x11;
		tmp = current_a1;
		for (i = 3; i < 4; i--)
		{
			digit[i] = tmp % 10;
			tmp /= 10;
		}
		tmp = current_a2;
		for (i = 0; i < 4; i++)
		{
			digit[7 - i] = tmp % 10;
			tmp /= 10;
		}
		break;
	case 1:
		pnt = 0x01;
		tmp = voltage;
		digit[1] = (tmp) / 100 % 10;
		digit[2] = (tmp) / 10 % 10;
		digit[3] = (tmp) % 10;
		digit[0] = (tmp) / 1000 % 10;
		digit[4] = ' ';
		digit[5] = ' ';
		digit[6] = ' ';
		digit[7] = ' ';
		break;
	default:
		break;
	}
}

void Display_Stage2(void)
{
	unsigned int i;
	unsigned int tmp;
	switch (display)
	{
	case 0:
		pnt = 0x02;
		digit[1] = (voltage_set) / 1000 % 10;
		digit[2] = (voltage_set) / 100 % 10;
		digit[3] = (voltage_set) / 10 % 10;
		digit[0] = ' ';
		digit[4] = ' ';
		digit[5] = ' ';
		digit[6] = ' ';
		digit[7] = ' ';
		break;
	case 1:
		pnt = 0x11;
		tmp = current_a2;
		for (i = 3; i < 4; i--)
		{
			digit[i] = tmp % 10;
			tmp /= 10;
		}
		tmp = voltage;
		for (i = 0; i < 4; i++)
		{
			digit[7 - i] = tmp % 10;
			tmp /= 10;
		}
		break;
	default:
		break;
	}
}

//UI display
void Update_UI(void)
{
	switch (mode)
	{
	case 1:
		Display_Stage3();
		break;
	case 0:
		Display_Stage2();
		break;
	default:
		break;
	}
}

/*
*
*       initialization
*
*/
const unsigned char timer_period = 20;
//I/O ports initialization
void Init_Ports(void)
{
	P2SEL &= ~(BIT7 + BIT6);
	P2DIR |= BIT7 + BIT6 + BIT5;

	init_dac6571();
	//1.4~SDA(J6), 1.5~SCL(J7)

	//ADC ports
	P1DIR &= ~(BIT1 + BIT2 + BIT3);
	//1.1~voltage_sum, 1.2~current1, 1.3~current2
}

//  initialize timer 0
void Init_Timer0(void)
{
	TA0CTL = TASSEL_2 + MC_1;			 // Source: SMCLK=1MHz, UP mode,
	TA0CCR0 = timer_period * 1000; // 1MHz闁哄啫鐖奸幐锟�,閻犱讲鍓濆锟�20000婵炲棌锟借尪绀� 20ms
	TA0CCTL0 = CCIE;							 // TA0CCR0 interrupt enabled
}

//  initialize MCU
void Init_Devices(void)
{
	WDTCTL = WDTPW + WDTHOLD; // Stop watchdog timer
	if (CALBC1_8MHZ == 0xFF || CALDCO_8MHZ == 0xFF)
	{
		while (1)
			; // If calibration constants erased, trap CPU!!
	}

	BCSCTL1 = CALBC1_8MHZ; // Set range
	DCOCTL = CALDCO_8MHZ;	// Set DCO step + modulation
	BCSCTL3 |= LFXT1S_2;	 // LFXT1 = VLO
	IFG1 &= ~OFIFG;				 // Clear OSCFault flag
	BCSCTL2 |= DIVS_3;		 //  SMCLK = DCO/8

	Init_Ports();
	Init_Timer0();
	init_dac6571();
	init_ADC10();
	_BIS_SR(GIE);
	__delay_cycles(3000);
	//all peripherals are now initialized
}
/*
*
*       Interrupt
*
*/
// Timer0_A0 interrupt service routine
#pragma vector = TIMER0_A0_VECTOR
__interrupt void Timer0_A0(void)
{
	clock20ms_flag = 1;
	if (++clock100ms >= V_T100ms)
	{
		clock100ms_flag = 1;
		clock100ms = 0;
	}
	if (++clock500ms >= V_T500ms)
	{
		clock500ms_flag = 1;
		clock500ms = 0;
	}
	TM1638_RefreshDIGIandLED(digit, pnt, led);
	key_code = TM1638_Readkeyboard();
	if (key_code != 0)
	{
		if (key_cnt < 4)
			key_cnt++;
		else if (key_cnt == 4)
		{
			if (key_code == 1)
			{
				mode = (mode + 1) % MODE_MAX;
				voltage_set = 800;
				dac6571_flag = 1;
			}
			else if (key_code == 2)
			{
				display = (display + 1) % 2;
				led[0] = !led[0];
			}
			else if (key_code == 3)
			{
				if (voltage_set < DAC6571_SET_MAX - 100)
				{
					voltage_set += 100;
					dac6571_flag = 1;
				}
			}
			else if (key_code == 4)
			{
				if (voltage_set > 100)
				{
					voltage_set -= 100;
					dac6571_flag = 1;
				}
			}
			key_cnt = 5;
		}
	}
	else
		key_cnt = 0;
}

/*
*
*       Main
*
*/
int main(void)
{
	//unsigned char i=0;
	//float temp;
	Init_Devices();
	while (clock100ms < 3)
		;
	init_TM1638();
	dac6571_flag = 1;

	while (1)
	{
		if (clock20ms_flag)
		{
			clock20ms_flag = 0;
			ADC10_sample();
			++sample_num;
			current_a1_sum += ADC_tmp_1;
			current_a2_sum += ADC_tmp_2;
			voltage_sum += ADC_tmp_0;
		}

		if (clock100ms_flag == 1)
		{
			clock100ms_flag = 0;

			Rload_new = voltage / current_a2;
			if (Rload_new != Rload)
			{
				Rload = Rload_new;
				dac6571_flag = 1; //Reset the dac if R has changed
			}
			Update_UI();
		}

		if (clock500ms_flag == 1)
		{
			clock500ms_flag = 0;
			// compute the average value of DAC results; Generally, sample_num = 500ms/20ms = 25;
			current_a2 = current_a2_sum / sample_num + 8;
			current_a1 = current_a1_sum / sample_num + 8;
			voltage = 2 * voltage_sum / sample_num + 10;
			sample_num = 0;
			current_a1_sum = 0;
			current_a2_sum = 0;
			voltage_sum = 0;
			if (mode == 1)
			{
				if (current_a2 > current_a1 + 50 && current_a2 + 50 < current_a1)
				{
					voltage_set = (current_a2 + current_a1) / 2;
					dac6571_flag = 1;
				}
				else if (current_a2 > current_a1 + 500 && voltage_set > 250)
				{
					voltage_set -= 250;
					dac6571_flag = 1;
				}
				else if (current_a2 + 500 < current_a1 && voltage_set + 250 < DAC6571_SET_MAX)
				{
					voltage_set += 250;
					dac6571_flag = 1;
				}
				else if (current_a2 > current_a1 + 300 && voltage_set > 150)
				{
					voltage_set -= 150;
					dac6571_flag = 1;
				}
				else if (current_a2 + 300 < current_a1 && voltage_set + 150 < DAC6571_SET_MAX)
				{
					voltage_set += 150;
					dac6571_flag = 1;
				}
				else if (current_a2 > current_a1 + 100 && voltage_set > 50)
				{
					voltage_set -= 50;
					dac6571_flag = 1;
				}
				else if (current_a2 + 100 < current_a1 && voltage_set + 50 < DAC6571_SET_MAX)
				{
					voltage_set += 50;
					dac6571_flag = 1;
				}
				else if (current_a2 > current_a1 + 50 && voltage_set > 25)
				{
					voltage_set -= 25;
					dac6571_flag = 1;
				}
				else if (current_a2 + 50 < current_a1 && voltage_set + 25 < DAC6571_SET_MAX)
				{
					voltage_set += 25;
					dac6571_flag = 1;
				}
				else if (current_a2 > current_a1 + 5 && voltage_set > 5)
				{
					voltage_set -= 1;
					dac6571_flag = 1;
				}
				else if (current_a2 + 5 < current_a1 && voltage_set + 5 < DAC6571_SET_MAX)
				{
					voltage_set += 1;
					dac6571_flag = 1;
				}
			}
		}

		if (dac6571_flag == 1)
		{
			dac6571_flag = 0;
			level = (voltage_set - 1) / 100;
			dac6571_code = (long)voltage_set * 4096 / (DAC6571_voltage_max + 1);
			dac6571_code -= 62;
			switch (mode)
			{
			case 0:
				DAC_Set_Continuous();
			case 1:
				DAC_Set_Continuous();
				break;
			default:
				break;
			}
		}
	}
}
