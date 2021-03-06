//本程序必须搭配头文件tm1638.h做成project包

//本程序时钟采用内部RC振荡器。     DCO：8MHz,供CPU时钟;  SMCLK：1MHz,供定时器时钟
#include <msp430g2553.h>
#include "tm1638.h" //与TM1638有关的变量及函数定义均在该H文件中

//////////////////////////////
//         常量定义         //
//////////////////////////////

// 0.1s软件定时器溢出值，5个20ms
#define V_T100ms 5 //负载均衡控制节奏
// 0.5s软件定时器溢出值，25个20ms
#define V_T500ms 25 //LED走马灯

//标定参数
#define a_current_source_voltage 1
#define b_current_source_voltage 0
#define a_current_source_current 1
#define b_current_source_current 0
#define a_voltage_source_current 1
#define b_voltage_source_current 0
#define length 5
#define n_sample 8					//采样数
#define average_num 5				//滑动平均个数
#define average_code_num 10 //滑动平均个数
//DAC6571操作  P1.4接SDA(pin4),P1.5接SCL(pin5)
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
#define DAC6571_voltage_max 499 //499x10mV
#define DAC6571_address 0x98		// 1001 10 A0 0  A0=0

//////////////////////////////
//       变量定义           //
//////////////////////////////
// 软件定时器计数
unsigned char clock100ms = 0;
unsigned char clock500ms = 0;
// 软件定时器溢出标志
unsigned char clock100ms_flag = 0;
unsigned char clock500ms_flag = 0;
// 测试用计数器
unsigned int test_counter = 0;
// 8位数码管显示的数字或字母符号
// 注：板上数码位从左到右序号排列为4、5、6、7、0、1、2、3
unsigned char digit[8] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
// 8位小数点 1亮  0灭
// 注：板上数码位小数点从左到右序号排列为4、5、6、7、0、1、2、3
unsigned char pnt = 0x02;
// 8个LED指示灯状态，每个灯4种颜色状态，0灭，1绿，2红，3橙（红+绿）
// 注：板上指示灯从左到右序号排列为7、6、5、4、3、2、1、0
//     对应元件LED8、LED7、LED6、LED5、LED4、LED3、LED2、LED1
unsigned char led[] = {0, 0, 1, 1, 2, 2, 3, 3};
// 当前按键值
unsigned char key_code = 0;
unsigned char key_cnt = 0;
int display_key = 0; //control the display
// DAC6571
unsigned int dac6571_code = 0x0800;
// unsigned int dac6571_voltage = 10;
unsigned char dac6571_flag = 0;

//////////////////////////////
//       系统初始化         //
//////////////////////////////

//  I/O端口和引脚初始化
void Init_Ports(void)
{
	P2SEL &= ~(BIT7 + BIT6); //P2.6、P2.7 设置为通用I/O端口
			//因两者默认连接外晶振，故需此修改

	P2DIR |= BIT7 + BIT6 + BIT5; //P2.5、P2.6、P2.7 设置为输出
			//本电路板中三者用于连接显示和键盘管理器TM1638，工作原理详见其DATASHEET

	P1DIR |= BIT4 + BIT5;
	P1OUT |= BIT4 + BIT5;
}

//  定时器TIMER0初始化，循环定时20ms
void Init_Timer0(void)
{
	TA0CTL = TASSEL_2 + MC_1; // Source: SMCLK=1MHz, UP mode,
	TA0CCR0 = 20000;					// 1MHz时钟,计满20000次为 20ms
	TA0CCTL0 = CCIE;					// TA0CCR0 interrupt enabled
}

//	ADC10初始化
void Init_ADC10()
{
	ADC10CTL1 = CONSEQ_3 + INCH_3; // 4通道单次转换, 最大转换通道为A3
	ADC10CTL0 = ADC10SHT_2 + ADC10ON + ADC10IE + MSC;
	//采样保持时间为16 x ADC10CLKs，ADC内核开，中断使能，MSC多次转换选择开
	//参考电压选默认值VCC和VSS
	ADC10DTC1 = 0x20;											 //采样32次
	ADC10AE0 |= BIT0 + BIT1 + BIT2 + BIT3; // 使能模拟输入脚A0 A1 A2 A3
}

//  MCU器件初始化，注：会调用上述函数
void Init_Devices(void)
{
	WDTCTL = WDTPW + WDTHOLD; // Stop watchdog timer，停用看门狗
	if (CALBC1_8MHZ == 0xFF || CALDCO_8MHZ == 0xFF)
	{
		while (1)
			; // If calibration constants erased, trap CPU!!
	}

	//设置时钟，内部RC振荡器。     DCO：8MHz,供CPU时钟;  SMCLK：1MHz,供定时器时钟
	BCSCTL1 = CALBC1_8MHZ; // Set range
	DCOCTL = CALDCO_8MHZ;	// Set DCO step + modulation
	BCSCTL3 |= LFXT1S_2;	 // LFXT1 = VLO
	IFG1 &= ~OFIFG;				 // Clear OSCFault flag
	BCSCTL2 |= DIVS_3;		 //  SMCLK = DCO/8

	Init_Ports();	//调用函数，初始化I/O口
	Init_Timer0(); //调用函数，初始化定时器0
	Init_ADC10();	//初始化ADC
	_BIS_SR(GIE);	//开全局中断
								 //all peripherals are now initialized
}

void dac6571_byte_transmission(unsigned char byte_data)
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

void dac6571_fastmode_operation(void)
{
	unsigned char msbyte, lsbyte;

	SCL_H;
	SDA_H;
	SDA_L;
	SCL_L; // START condition
	dac6571_byte_transmission(DAC6571_address);
	msbyte = dac6571_code / 256;
	lsbyte = dac6571_code - msbyte * 256;
	dac6571_byte_transmission(msbyte);
	dac6571_byte_transmission(lsbyte);

	SDA_L;
	SCL_H;
	SDA_H; // STOP condition
}

//////////////////////////////
//      中断服务程序        //
//////////////////////////////

// Timer0_A0 interrupt service routine
#pragma vector = TIMER0_A0_VECTOR
__interrupt void Timer0_A0(void)
{
	if (++clock100ms >= V_T100ms) // 0.1秒钟软定时器计数
	{
		clock100ms_flag = 1; //当0.1秒到时，溢出标志置1
		clock100ms = 0;
	}
	if (++clock500ms >= V_T500ms) // 0.5秒钟软定时器计数
	{
		clock500ms_flag = 1; //当0.5秒到时，溢出标志置1
		clock500ms = 0;
	}
	TM1638_RefreshDIGIandLED(digit, pnt, led); // 刷新全部数码管和LED指示灯

	//检查当前键盘输入，0代表无键操作，1 - 16表示有对应按键号显示在两位数码管上
	key_code = TM1638_Readkeyboard();
	if (key_code != 0)
	{
		if (key_cnt < 4) //按键消抖
			key_cnt++;
		else if (key_cnt == 4)
		{
			if (key_code == 5)
			{
				display_key = 0;
			}
			else if (key_code == 6)
			{
				display_key = 1;
			}
			key_cnt = 5;
		}
	}
	else
		key_cnt = 0;
}

//////////////////////////////
//         主程序           //
//////////////////////////////
int main(void)
{
	int temp = 0;
	int j, k, average_count = 0, average_code_count = 0; //循环计数
	unsigned int code = 0x0800;													 //用于滑动平均
	unsigned int sum_current_source_voltage = 0;				 //电流源的采样电压
	unsigned int sum_current_source_current = 0;				 //电流源的采样电流
	unsigned int sum_voltage_source_current = 0;				 //电压源的采样电流
	double average_current_source_voltage, average_voltage_source_current, average_current_source_current;
	double all_current_source_voltage = 0, all_current_source_current = 0, all_voltage_source_current = 0, all_code = 0;
	double current_temp1, current_temp2, balance;
	double corrected_current_source_voltage, corrected_current_source_current, corrected_voltage_source_current;
	int display_c_v, display_c_c, display_v_c; //待显示数字
	unsigned int sample[32] = {0};						 //存放ADC采样结果（一次转换产生的四个结果）
	double average_current_source_queue_voltage[average_num] = {0};
	double average_current_source_queue_current[average_num] = {0};
	double average_voltage_source_queue_current[average_num] = {0};
	double average_code[average_code_num] = {0};
	int steps[length] = {250, 125, 25, 5, 1};
	int difference[length] = {120, 30, 10, 3, 1};
	Init_Devices();
	while (clock100ms < 3)
		;						 // 延时60ms等待TM1638上电完成
	init_TM1638(); //初始化TM1638

	while (1)
	{
		///////ADC10转换///////
		ADC10CTL0 &= ~ENC; //关
		while (ADC10CTL1 & BUSY)
			;															//等待ADC10转换完成
		ADC10CTL0 |= ENC + ADC10SC;			//开，采样，转换
		ADC10SA = (unsigned int)sample; //地址负值
		ADC10CTL0 &= ~ENC;							//关

		for (k = 0; k < n_sample; ++k) //采样8次
		{
			sum_current_source_voltage += sample[k * 4];		 //p1.3 J2
			sum_current_source_current += sample[k * 4 + 1]; //p1.2 J1
			sum_voltage_source_current += sample[k * 4 + 2]; //p1.1 J1
		}
		average_current_source_voltage = 3.55 * sum_current_source_voltage / n_sample / 1024;
		average_current_source_current = 3.55 * sum_current_source_current / n_sample / 1024;
		average_voltage_source_current = 3.55 * sum_voltage_source_current / n_sample / 1024;

		///////滑动平均///////
		if (average_count < average_num) //建循环队列
		{
			average_current_source_queue_voltage[average_count] = average_current_source_voltage;
			average_current_source_queue_current[average_count] = average_current_source_current;
			average_voltage_source_queue_current[average_count] = average_voltage_source_current;
		}
		else
		{
			average_count = 0;
			average_current_source_queue_voltage[0] = average_current_source_voltage;
			average_current_source_queue_current[0] = average_current_source_current;
			average_voltage_source_queue_current[0] = average_voltage_source_current;
		}
		all_current_source_voltage += average_current_source_queue_voltage[average_count]; //求和
		all_current_source_current += average_current_source_queue_current[average_count];
		all_voltage_source_current += average_voltage_source_queue_current[average_count];
		all_current_source_voltage -= average_current_source_queue_voltage[(average_count + 1) % average_num];
		all_current_source_current -= average_current_source_queue_current[(average_count + 1) % average_num];
		all_voltage_source_current -= average_voltage_source_queue_current[(average_count + 1) % average_num];
		average_count++;

		///////电路实际输出///////
		if (clock100ms_flag == 1) // 检查0.1秒定时是否到
		{
			clock100ms_flag = 0;
			current_temp1 = all_current_source_current * 100 / average_num; //电流值 数量级70mA
			current_temp2 = all_voltage_source_current * 100 / average_num;
			balance = current_temp1 - current_temp2;
			if (current_temp2 < 1) //稳压源过流保护
			{
				temp = 0;
			}
			else
			{
				for (j = 0; j < length; j++) //变步长均衡
				{
					if (abs(balance) > difference[j])
					{
						if (balance > 0)
							temp += steps[j];
						else
							temp -= steps[j];
						break;
					}
				}
			}
			//是不是 /1000 ???
			code = (current_temp1 + current_temp2) / 2 / 1000 * 4096.0 / (DAC6571_voltage_max + 1) + temp - 50;
			if (average_code_count < average_code_num) //建循环队列
			{
				average_code[average_code_count] = code;
			}
			else
			{
				average_code_count = 0;
				average_code[0] = code;
			}
			all_code += average_code[average_code_count]; //求和
			all_code -= average_code[(average_code_count + 1) % average_code_num];
			dac6571_code = all_code / average_code_num;
			average_code_count++;
			dac6571_fastmode_operation();
			//sum_voltage_source_current 控制 dac6571_code 控制输出电流使 sum_current_source_current == sum_voltage_source_current
		}

		///////数码管显示///////
		corrected_current_source_voltage = a_current_source_voltage * all_current_source_voltage / average_num + b_current_source_voltage; //标定
		corrected_voltage_source_current = a_voltage_source_current * all_voltage_source_current / average_num + b_voltage_source_current;
		corrected_current_source_current = a_current_source_current * all_current_source_current / average_num + b_current_source_current;

		display_c_v = (int)(1000 * corrected_current_source_voltage); //待显示数值
		display_v_c = (int)(1000 * corrected_voltage_source_current);
		display_c_c = (int)(1000 * corrected_current_source_current);

		if (display_key == 0) //key_code == 5
		{
			digit[0] = ' ';
			digit[1] = ' ';
			digit[2] = ' ';
			digit[3] = ' ';
			digit[4] = (display_c_v / 1000) % 10;
			digit[5] = (display_c_v / 100) % 10;
			digit[6] = (display_c_v / 10) % 10;
			digit[7] = (display_c_v / 1) % 10;
			pnt = 0x10;
		}
		if (display_key == 1) //key_code == 6
		{
			digit[0] = (display_v_c / 1000) % 10;
			digit[1] = (display_v_c / 100) % 10;
			digit[2] = (display_v_c / 10) % 10;
			digit[3] = (display_v_c / 1) % 10;
			digit[4] = (display_c_c / 1000) % 10;
			digit[5] = (display_c_c / 100) % 10;
			digit[6] = (display_c_c / 10) % 10;
			digit[7] = (display_c_c / 1) % 10;
			pnt = 0x11;
		}
	}
}
