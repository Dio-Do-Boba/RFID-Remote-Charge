#include "bsp_timer.h"

#ifdef ENABLE_BSP_TIMER

static void GENERAL_TIM_NVIC_Config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure; 
    // �����ж���Ϊ0
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_0);		
	// �����ж���Դ
    NVIC_InitStructure.NVIC_IRQChannel = GENERAL_TIM_IRQ ;	
	// ���������ȼ�Ϊ 0
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;	 
	// ������ռ���ȼ�Ϊ3
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;	
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

static void GENERAL_TIM_Mode_Config(void)
{
	
	TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;		
	// ������ʱ��ʱ��,���ڲ�ʱ��CK_INT=72M
    GENERAL_TIM_APBxClock_FUN(GENERAL_TIM_CLK, ENABLE);	
	// �Զ���װ�ؼĴ�����ֵ���ۼ�TIM_Period+1��Ƶ�ʺ����һ�����»����ж�
    TIM_TimeBaseStructure.TIM_Period=GENERAL_TIM_Period;
	// ʱ��Ԥ��Ƶ��
    TIM_TimeBaseStructure.TIM_Prescaler= GENERAL_TIM_Prescaler;	
	// ʱ�ӷ�Ƶ���� ��û�õ����ù�
    TIM_TimeBaseStructure.TIM_ClockDivision=TIM_CKD_DIV1;		
	// ����������ģʽ������Ϊ���ϼ���
    TIM_TimeBaseStructure.TIM_CounterMode=TIM_CounterMode_Up; 		
	// �ظ���������ֵ��û�õ����ù�
	TIM_TimeBaseStructure.TIM_RepetitionCounter=0;	
	// ��ʼ����ʱ��
    TIM_TimeBaseInit(GENERAL_TIM, &TIM_TimeBaseStructure);
	// ����������жϱ�־λ
    TIM_ClearFlag(GENERAL_TIM, TIM_FLAG_Update);
	// �����������ж�
    TIM_ITConfig(GENERAL_TIM,TIM_IT_Update,ENABLE);
	// ʹ�ܼ�����
    TIM_Cmd(GENERAL_TIM, ENABLE);
}

void GENERAL_TIM_Init(void)
{
	GENERAL_TIM_NVIC_Config();
	GENERAL_TIM_Mode_Config();		
}

void GENERAL_TIM_IRQHandler (void)
{
	if (TIM_GetITStatus( GENERAL_TIM, TIM_IT_Update) != RESET) 
	{
		TIM_ClearITPendingBit(GENERAL_TIM , TIM_FLAG_Update);  
	}		 	
}

#else

void GENERAL_TIM_Init(void)
{
	/* Timer disabled: no-op */
}

void GENERAL_TIM_IRQHandler (void)
{
	/* Timer disabled: no-op */
}

#endif