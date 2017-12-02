//������ʱ�Ӳ����ڲ�RC������     DCO��8MHz,��CPUʱ��;  SMCLK��1MHz,����ʱ��ʱ��
#include <msp430g2553.h>
#include <tm1638.h>  //��TM1638�йصı���������������ڸ�H�ļ���

//////////////////////////////
//         ��������         //
//////////////////////////////
#define V_T 50

// ADC10�ο���ѹ��������λ
#define a_v 2.0
#define a_i 1.0
#define b_v -0.04
#define b_i 0.01

#define n_sample 64
//////////////////////////////
//       ��������           //
//////////////////////////////

//	ADC10 ��������
unsigned int sample[2]={0};//���ADC���������һ��ת�����������������
unsigned int sample_v[n_sample] = {0};
unsigned int sample_i[n_sample] = {0};
unsigned int i_sample = 0;

double v_avg, i_avg;
double v, i;
int display;

// �����ʱ������
int clock = 0;
unsigned int clock_flag = 0;

// 8λ�������ʾ�����ֻ���ĸ����
// ע����������λ�������������Ϊ4��5��6��7��0��1��2��3
unsigned char digit[8]={' ',' ',' ',' ',' ',' ',' ',' '};

// 8λС���� 1��  0��
// ע����������λС����������������Ϊ4��5��6��7��0��1��2��3
unsigned char pnt=0x11;

// 8��LEDָʾ��״̬��ÿ����4����ɫ״̬��0��1�̣�2�죬3�ȣ���+�̣�
// ע������ָʾ�ƴ������������Ϊ7��6��5��4��3��2��1��0
//     ��ӦԪ��LED8��LED7��LED6��LED5��LED4��LED3��LED2��LED1
unsigned char led[]={0,0,0,0,0,0,0,0};

//////////////////////////////
//       ϵͳ��ʼ��         //
//////////////////////////////

//  I/O�˿ں����ų�ʼ��
void Init_Ports(void)
{
	P2SEL &= ~(BIT7+BIT6);       //P2.6��P2.7 ����Ϊͨ��I/O�˿�
	  //������Ĭ�������⾧�񣬹�����޸�
	P2DIR |= BIT7 + BIT6 + BIT5; //P2.5��P2.6��P2.7 ����Ϊ���
	  //����·������������������ʾ�ͼ��̹�����TM1638������ԭ�������DATASHEET

	P1SEL |= (BIT0+BIT1);
	P1DIR &=~ (BIT0+BIT1);
 }

//  ��ʱ��TIMER0��ʼ����ѭ����ʱ20ms
void Init_Timer0(void)
{
	TA0CTL = TASSEL_2 + MC_1 ;      // Source: SMCLK=1MHz, UP mode,
	TA0CCR0 = 20000;                // 1MHzʱ��,����20000��Ϊ 20ms
	TA0CCTL0 = CCIE;                // TA0CCR0 interrupt enabled
}

//	ADC10��ʼ��
void Init_ADC10()
{
  ADC10CTL1 = CONSEQ_1 + INCH_1;    // 2ͨ������ת��, ���ת��ͨ��ΪA1
  ADC10CTL0 = ADC10SHT_2 + ADC10ON + ADC10IE + MSC;
  //��������ʱ��Ϊ16 x ADC10CLKs��ADC�ں˿����ж�ʹ��   MSC���ת��ѡ��
  //�ο���ѹѡĬ��ֵVCC��VSS
  ADC10DTC1 = 0x02;
  ADC10AE0 |= BIT0+BIT1;// ʹ��ģ�������A0 A1
}

//  MCU������ʼ����ע���������������
void Init_Devices(void)
{
	WDTCTL = WDTPW + WDTHOLD;     // Stop watchdog timer��ͣ�ÿ��Ź�
	if (CALBC1_8MHZ ==0xFF || CALDCO_8MHZ == 0xFF)
	{
		while(1);            // If calibration constants erased, trap CPU!!
	}

    //����ʱ�ӣ��ڲ�RC������     DCO��8MHz,��CPUʱ��;  SMCLK��1MHz,����ʱ��ʱ��
	BCSCTL1 = CALBC1_8MHZ; 	 // Set range
	DCOCTL = CALDCO_8MHZ;    // Set DCO step + modulation
	BCSCTL3 |= LFXT1S_2;     // LFXT1 = VLO
	IFG1 &= ~OFIFG;          // Clear OSCFault flag
	BCSCTL2 |= DIVS_3;       //  SMCLK = DCO/8

    Init_Ports();           //���ú�������ʼ��I/O��
    Init_Timer0();          //���ú�������ʼ����ʱ��0
    Init_ADC10();			//��ʼ��ADC
    _BIS_SR(GIE);           //��ȫ���ж�

   //all peripherals are now initialized
}

//////////////////////////////
//      �жϷ������        //
//////////////////////////////

//20ms ���ж�
// Timer0_A0 interrupt service routine
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer0_A0 (void)
{
	// 0.5������ʱ������
	if(++clock >= V_T)
	{
		clock = 0;
		clock_flag = 1;
	}
}

////ADC10�жϷ������
#pragma vector=ADC10_VECTOR
__interrupt void ADC10ISR (void)
{
	sample_v[i_sample] = sample[0];
	sample_i[i_sample] = sample[1];
}

//////////////////////////////
//         ������           //
//////////////////////////////

int main(void)
{
	Init_Devices( );
	while (clock<3);   // ��ʱ60ms�ȴ�TM1638�ϵ����
	init_TM1638();	    //��ʼ��TM1638

	while(1)
	{
		//ADC10ת��
		ADC10CTL0 |= ENC + ADC10SC;
		while(ADC10CTL1 & BUSY);//�ȴ�ADC10ת�����
		ADC10SA = (unsigned int)sample;
		ADC10CTL0 &= ~ENC;

		++i_sample;

		//������n_sample��������
		if(i_sample == n_sample)
		{
			i_sample = 0;
			//����ƽ��ֵ
			unsigned int sum_v = 0;
			unsigned int sum_i = 0;
			int k;
			for (k=0; k<n_sample; ++k)
			{
				sum_v += sample_v[k];
				sum_i += sample_i[k];
			}

			v_avg = 3.55*sum_v/n_sample/1024;
			//����A1�˿��ϵ�ģ�������ѹ
			v = a_v*v_avg + b_v;
			display = (int)(1000*v);
			digit[0] = (display/1000)%10;
			digit[1] = (display/100)%10;
			digit[2] = (display/10)%10;
			digit[3] = (display/1)%10;

			i_avg = 3.55*sum_i/n_sample/1024;
			//��¼A0�˿��ϵ�ģ�������ѹ(����ת������A0�󱻲���������)
			i = a_i*i_avg + b_i;
			display = (int)(1000*i);
			digit[4] = (display/1000)%10;
			digit[5] = (display/100)%10;
			digit[6] = (display/10)%10;
			digit[7] = (display/1)%10;
		}

		//ÿ��0.5s,ˢ���������ʾ
		if(clock_flag)
		{
			TM1638_RefreshDIGIandLED(digit,pnt,led);
			clock_flag = 0;
		}
	}

}
