//本程序必须搭配头文件tm1638.h做成project包

//本程序时钟采用内部RC振荡器。     DCO：8MHz,供CPU时钟;  SMCLK：1MHz,供定时器时钟
#include <msp430g2553.h>
#include "tm1638.h" //与TM1638有关的变量及函数定义均在该H文件中

//////////////////////////////
//         常量定义         //
//////////////////////////////

// 0.1s软件定时器溢出值，5个20ms
#define V_T100ms 5
// 0.5s软件定时器溢出值，25个20ms
#define V_T500ms 25

// ADC10参考电压及量化单位
#define a_voltage 5.1836
#define a_current 42.36
#define b_voltage -0.0635
#define b_current -0.3427

#define n_sample 64
#define average_num 5

//	ADC10 变量定义
unsigned int sample[2] = {0}; //存放ADC采样结果（一次转换产生的两个结果）
unsigned int sample_v[n_sample] = {0};
unsigned int sample_i[n_sample] = {0};
unsigned int i_sample = 0;

double average_voltage, average_current;
double corrected_voltage, corrected_current;
int display;

//DAC6571操作  P1.4接SDA(pin4),P1.5接SCL(pin5)
#define SCL_L P1OUT &= ~BIT5
#define SCL_H P1OUT |= BIT5
#define SDA_L P1OUT &= ~BIT4
#define SDA_H P1OUT |= BIT4
#define SDa_currentN \
  P1OUT |= BIT4;     \
  P1DIR &= ~BIT4;    \
  P1REN |= BIT4
#define SDA_OUT  \
  P1DIR |= BIT4; \
  P1REN &= ~BIT4
#define DAC6571_voltage_max 499 //499x10mV
#define DAC6571_address 0x98    // 1001 10 A0 0  A0=0

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
unsigned int dac6571_voltage = 10; //#为什么是这个值
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
  TA0CCR0 = 20000;          // 1MHz时钟,计满20000次为 20ms
  TA0CCTL0 = CCIE;          // TA0CCR0 interrupt enabled
}

//	ADC10初始化
void Init_ADC10()
{
  ADC10CTL1 = CONSEQ_1 + INCH_1; // 2通道单次转换, 最大转换通道为A1
  ADC10CTL0 = ADC10SHT_2 + ADC10ON + ADC10IE + MSC;
  //采样保持时间为16 x ADC10CLKs，ADC内核开，中断使能   MSC多次转换选择开
  //参考电压选默认值VCC和VSS
  ADC10DTC1 = 0x02;
  ADC10AE0 |= BIT0 + BIT1; // 使能模拟输入脚A0 A1
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
  DCOCTL = CALDCO_8MHZ;  // Set DCO step + modulation
  BCSCTL3 |= LFXT1S_2;   // LFXT1 = VLO
  IFG1 &= ~OFIFG;        // Clear OSCFault flag
  BCSCTL2 |= DIVS_3;     //  SMCLK = DCO/8

  Init_Ports();  //调用函数，初始化I/O口
  Init_Timer0(); //调用函数，初始化定时器0
  Init_ADC10();  //初始化ADC
  _BIS_SR(GIE);  //开全局中断
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
  SDa_currentN;
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
  // 0.1秒钟软定时器计数
  if (++clock100ms >= V_T100ms)
  {
    clock100ms_flag = 1; //当0.1秒到时，溢出标志置1
    clock100ms = 0;
  }
  // 0.5秒钟软定时器计数
  if (++clock500ms >= V_T500ms)
  {
    clock500ms_flag = 1; //当0.5秒到时，溢出标志置1
    clock500ms = 0;
  }

  // 刷新全部数码管和LED指示灯
  TM1638_RefreshDIGIandLED(digit, pnt, led);

  // 检查当前键盘输入，0代表无键操作，1-16表示有对应按键
  //   键号显示在两位数码管上
  key_code = TM1638_Readkeyboard();
  if (key_code != 0)
  {
    if (key_cnt < 4)
      key_cnt++;
    else if (key_cnt == 4)
    {
      if (key_code == 1)
      {
        if (dac6571_voltage < DAC6571_voltage_max)
        {
          dac6571_voltage++;
          dac6571_flag = 1;
        }
      }
      else if (key_code == 2)
      {
        if (dac6571_voltage > 0)
        {
          dac6571_voltage--;
          dac6571_flag = 1;
        }
      }
      else if (key_code == 3)
      {
        if (dac6571_voltage < DAC6571_voltage_max - 10)
        {
          dac6571_voltage += 10;
          dac6571_flag = 1;
        }
      }
      else if (key_code == 4)
      {
        if (dac6571_voltage > 10)
        {
          dac6571_voltage -= 10;
          dac6571_flag = 1;
        }
      }
      else if (key_code == 5)
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

  if (display_key == 0)
  {
    //digit[6]=key_code%10;
    //digit[5]=key_code/10;
    //digit[6]=0;
    //digit[5]=0;
  }
}

////ADC10中断服务程序
#pragma vector = ADC10_VECTOR
__interrupt void ADC10ISR(void)
{
  sample_v[i_sample] = sample[0]; //p1.1 J2
  sample_i[i_sample] = sample[1]; //p1.0 J1
}

//////////////////////////////
//         主程序           //
//////////////////////////////

int main(void)
{
  float temp;
  int i, k;
  unsigned int average_queue_voltage[average_num] = {0};
  unsigned int average_queue_current[average_num] = {0};
  int average_count = 0, index, non_zero = 0;
  int show_voltage = 0, all_voltage = 0;
  int show_current = 0, all_current = 0;
  Init_Devices();
  while (clock100ms < 3)
    ;            // 延时60ms等待TM1638上电完成
  init_TM1638(); //初始化TM1638
  dac6571_flag = 1;

  while (1)
  {
    //当采满n_sample个样本后
    if (display_key == 1)
    {
      //ADC10转换
      ADC10CTL0 |= ENC + ADC10SC;
      while (ADC10CTL1 & BUSY)
        ; //等待ADC10转换完成
      ADC10SA = (unsigned int)sample;
      ADC10CTL0 &= ~ENC;

      ++i_sample;

      if (i_sample == n_sample)
      {
        i_sample = 0;
        //计算平均值
        unsigned int sum_voltage = 0;
        unsigned int sum_current = 0;
        for (k = 0; k < n_sample; ++k)
        {
          sum_voltage += sample_v[k];
          sum_current += sample_i[k];
        }

        average_voltage = 3.55 * sum_voltage / n_sample / 1024; //计算A1端口上的模拟输入电压
        corrected_voltage = a_voltage * average_voltage + b_voltage;
        average_current = 3.55 * sum_current / n_sample / 1024; //记录A0端口上的模拟输入电压(按照转换规则A0后被采样并传输)
        corrected_current = a_current * average_current + b_current;
        //建队列
        if (average_count < average_num)
        {
          average_queue_voltage[average_count] = corrected_voltage;
          average_queue_current[average_count] = corrected_current;
          average_count++;
        }
        else
        {
          average_queue_voltage[0] = corrected_voltage;
          average_queue_current[0] = corrected_current;
          average_count = 1;
        }
        //求平均
        for (index = 0; index < average_num; index++)
        {
          if (average_queue_voltage[index] != 0)
          {
            non_zero++;
            all_voltage += average_queue_voltage[index];
            all_current += average_queue_current[index];
          }
          if (index == average_num)
          {
            show_voltage = all_voltage / non_zero;
            show_current = all_current / non_zero;
            non_zero = 0;
            all_voltage = 0;
            all_current = 0;
          }
        }

        display = (int)(1000 * show_voltage);
        digit[0] = (display / 1000) % 10;
        digit[1] = (display / 100) % 10;
        digit[2] = (display / 10) % 10;
        digit[3] = (display / 1) % 10;
        display = (int)(1000 * show_current);
        digit[4] = (display / 1000) % 10;
        digit[5] = (display / 100) % 10;
        digit[6] = (display / 10) % 10;
        digit[7] = (display / 1) % 10;
        pnt = 0x11;
      }
    }

    if (display_key == 0)
    {
      if (dac6571_flag == 1) // 检查DAC电压是否要变
      {
        dac6571_flag = 0;
        digit[0] = ' ';
        digit[1] = dac6571_voltage / 100 % 10; //计算个位数
        digit[2] = dac6571_voltage / 10 % 10;  //计算十分位数
        digit[3] = dac6571_voltage % 10;       //计算百分位数
        digit[4] = ' ';
        digit[5] = ' ';
        digit[6] = ' ';
        digit[7] = ' ';
        temp = dac6571_voltage * 4096.0 / (DAC6571_voltage_max + 1);
        dac6571_code = temp - 50;
        dac6571_fastmode_operation();
        pnt = 0x02;
      }
    }

    if (clock500ms_flag == 1) // 检查0.5秒定时是否到
    {
      clock500ms_flag = 0;
      // 8个指示灯以走马灯方式，每0.5秒向右（循环）移动一格
      temp = led[0];
      for (i = 0; i < 7; i++)
        led[i] = led[i + 1];
      led[7] = temp;
    }
  }
}