#include "stm32f10x.h"
#include "led.h"
#include "usart.h"
#include "delay.h"
#include "oled.h"
#include "MFRC522.h"
#include "stdio.h"
#include "esp8266.h"
#include "onenet.h"
#include <string.h>
#include <stdint.h>


unsigned char buf[20];  // 卡片数据缓冲区

// 全局变量用于存储显示状态
unsigned char last_card_id[4] = {0, 0, 0, 0};  // 上次显示的卡号

// Mifare卡默认密钥（通常出厂默认值）
unsigned char default_key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// 余额存储块地址（扇区1，块1，地址=5）
#define BALANCE_BLOCK_ADDR  5
#define INITIAL_BALANCE     100  // 初始余额
#define DEDUCT_AMOUNT       10   // 每次扣费金额
#define INIT_FLAG           0xAA // 初始化标识（存储在第二个字节）

// 提供给 onenet.c 的告警标志变量定义（默认关闭）
uint8_t Alarm_flag = 0;

// 提供给 onenet.c 的卡片值变量定义
int c1c_value = 0;
int c2c_value = 0;
int c3c_value = 0;

// 固定的卡片ID映射（根据用户提供的ID直接映射）
// Card1: 40E9D961 -> [0x40, 0xE9, 0xD9, 0x61]
// Card2: 3DBFC901 -> [0x3D, 0xBF, 0xC9, 0x01]
// Card3: 618EC901 -> [0x61, 0x8E, 0xC9, 0x01]
static const unsigned char CARD1_ID[4] = {0x40, 0xE9, 0xD9, 0x61};  // 40E9D961
static const unsigned char CARD2_ID[4] = {0x3D, 0xBF, 0xC9, 0x01};  // 3DBFC901
static const unsigned char CARD3_ID[4] = {0x61, 0x8E, 0xC9, 0x01};  // 618EC901

#define MAX_CARDS_TRACKED   16
static unsigned char registered_cards[MAX_CARDS_TRACKED][4];
static unsigned char registered_card_count = 0;

static char publish_buf[128];
static const char devPubTopic[] = "$sys/TdTlyD3CtQ/Test1/thing/property/post";
const char *devSubTopic[] = {"$sys/TdTlyD3CtQ/Test1/thing/property/set"};
// 将十六进制数字转换为ASCII字符
char hex_to_char(u8 hex)
{
	if(hex < 10)
		return '0' + hex;
	else
		return 'A' + (hex - 10);
}

unsigned char *dataPtr = NULL;

// 显示searching和卡号ID（只在卡号改变时调用）
void OLED_ShowSearchingAndID(unsigned char *card_id, unsigned char balance)
{
	char display_str[20];
	unsigned char i;
	
	// 将4字节卡号转换为十六进制字符串显示
	display_str[0] = hex_to_char((card_id[0] >> 4) & 0x0F);
	display_str[1] = hex_to_char(card_id[0] & 0x0F);
	display_str[2] = hex_to_char((card_id[1] >> 4) & 0x0F);
	display_str[3] = hex_to_char(card_id[1] & 0x0F);
	display_str[4] = hex_to_char((card_id[2] >> 4) & 0x0F);
	display_str[5] = hex_to_char(card_id[2] & 0x0F);
	display_str[6] = hex_to_char((card_id[3] >> 4) & 0x0F);
	display_str[7] = hex_to_char(card_id[3] & 0x0F);
	display_str[8] = '\0';  // 字符串结束符
	
	// 将余额转换为字符串
	char balance_str[10];
	unsigned char temp_balance = balance;
	balance_str[0] = 'B';
	balance_str[1] = ':';
	if(temp_balance >= 100)
	{
		balance_str[2] = (temp_balance / 100) + '0';
		balance_str[3] = ((temp_balance / 10) % 10) + '0';
		balance_str[4] = (temp_balance % 10) + '0';
		balance_str[5] = '\0';
	}
	else if(temp_balance >= 10)
	{
		balance_str[2] = (temp_balance / 10) + '0';
		balance_str[3] = (temp_balance % 10) + '0';
		balance_str[4] = '\0';
	}
	else
	{
		balance_str[2] = temp_balance + '0';
		balance_str[3] = '\0';
	}
	
	// 清屏并同时显示searching、ID和余额
	OLED_Clear();
	OLED_ShowString(0, 0, "searching", 16, 1);  // 显示"searching"
	OLED_ShowString(0, 20, "ID:", 16, 1);  // 显示"ID:"
	OLED_ShowString(40, 20, display_str, 16, 1);  // 在"ID:"后面显示卡号
	OLED_ShowString(0, 40, balance_str, 16, 1);  // 显示余额
}

// 余额管理函数：处理卡片余额（初始化为100或扣费10）
// 返回值：0=成功，1=余额不足，2=验证失败，3=读写失败
unsigned char ProcessCardBalance(unsigned char *card_id, unsigned char *new_balance)
{
	unsigned char status;
	unsigned char read_data[16];
	unsigned char write_data[16];
	unsigned char current_balance;
	unsigned char i;
	
	// 1. 选择卡片
	status = MFRC522_SelectTag(card_id);
	if(status != MI_OK)
	{
		printf("Select card failed\r\n");
		return 3;
	}
	
	// 2. 验证密码（使用默认密钥）
	status = MFRC522_AuthState(PICC_AUTHENT1A, BALANCE_BLOCK_ADDR, default_key, card_id);
	if(status != MI_OK)
	{
		printf("Auth failed\r\n");
		return 2;
	}
	
	// 3. 读取余额块
	status = MFRC522_Read(BALANCE_BLOCK_ADDR, read_data);
	if(status != MI_OK)
	{
		printf("Read failed\r\n");
		return 3;
	}
	
	// 调试：打印读取到的数据
	printf("Read data: ");
	for(i = 0; i < 16; i++)
	{
		printf("%02X ", read_data[i]);
	}
	printf("\r\n");
	
	// 4. 检查是否是第一次使用
	// 判断条件：第二个字节不是初始化标识，或者余额块全为0xFF/0x00
	unsigned char is_first_use = 0;
	
	// 检查初始化标识（第二个字节）
	if(read_data[1] != INIT_FLAG)
	{
		// 没有初始化标识，检查是否全为0xFF或0x00
		unsigned char all_ff = 1;
		unsigned char all_00 = 1;
		
		for(i = 0; i < 16; i++)
		{
			if(read_data[i] != 0xFF)
			{
				all_ff = 0;
			}
			if(read_data[i] != 0x00)
			{
				all_00 = 0;
			}
		}
		
		// 如果全为0xFF或全为0x00，认为是第一次使用（未初始化）
		if(all_ff || all_00)
		{
			is_first_use = 1;
		}
		else
		{
			// 有数据但没有标识，可能是旧格式，也认为是第一次使用
			is_first_use = 1;
		}
	}
	
	if(is_first_use)
	{
		// 第一次使用，初始化余额为100
		printf("First use detected, init balance: %d\r\n", INITIAL_BALANCE);
		for(i = 0; i < 16; i++)
		{
			write_data[i] = 0x00;
		}
		write_data[0] = INITIAL_BALANCE;  // 在第一个字节存储余额
		write_data[1] = INIT_FLAG;         // 在第二个字节存储初始化标识
		current_balance = INITIAL_BALANCE;
	}
	else
	{
		// 读取当前余额（存储在第一个字节）
		current_balance = read_data[0];
		printf("Current balance: %d\r\n", current_balance);
		
		// 检查余额是否充足
		if(current_balance < DEDUCT_AMOUNT)
		{
			printf("Insufficient balance: %d\r\n", current_balance);
			*new_balance = current_balance;
			MFRC522_Halt();  // 让卡片休眠
			return 1;  // 余额不足
		}
		
		// 扣费：余额减去10
		current_balance -= DEDUCT_AMOUNT;
		
		// 准备写入数据
		for(i = 0; i < 16; i++)
		{
			write_data[i] = read_data[i];
		}
		write_data[0] = current_balance;  // 更新余额
		
		printf("Deduct %d, new balance: %d\r\n", DEDUCT_AMOUNT, current_balance);
	}
	
	// 5. 写入更新后的余额
	status = MFRC522_Write(BALANCE_BLOCK_ADDR, write_data);
	if(status != MI_OK)
	{
		printf("Write failed\r\n");
		MFRC522_Halt();
		return 3;
	}
	
	// 6. 验证写入是否成功（重新读取验证）
	delay_ms(10);  // 等待写入完成
	status = MFRC522_AuthState(PICC_AUTHENT1A, BALANCE_BLOCK_ADDR, default_key, card_id);
	if(status == MI_OK)
	{
		unsigned char verify_data[16];
		status = MFRC522_Read(BALANCE_BLOCK_ADDR, verify_data);
		if(status == MI_OK)
		{
			if(verify_data[0] == current_balance)
			{
				printf("Write verified successfully\r\n");
			}
			else
			{
				printf("Write verification failed: expected %d, got %d\r\n", current_balance, verify_data[0]);
			}
		}
	}
	
	// 7. 让卡片休眠
	MFRC522_Halt();
	
	*new_balance = current_balance;
	return 0;  // 成功
}

// 根据卡片ID获取固定的卡号（0=Card1, 1=Card2, 2=Card3, -1=未识别）
static int get_card_index_by_id(const unsigned char *card_id)
{
	// 调试：打印实际读取的卡片ID字节
	printf("Checking card ID: %02X%02X%02X%02X\r\n", 
	       card_id[0], card_id[1], card_id[2], card_id[3]);
	
	// 检查是否是Card1: 40E9D961
	if(memcmp(card_id, CARD1_ID, 4) == 0)
	{
		return 0;  // Card1
	}
	// 检查是否是Card2: 3DBFC901
	else if(memcmp(card_id, CARD2_ID, 4) == 0)
	{
		return 1;  // Card2
	}
	// 检查是否是Card3: 618EC901
	else if(memcmp(card_id, CARD3_ID, 4) == 0)
	{
		return 2;  // Card3
	}
	
	// 调试：打印期望的ID用于对比
	printf("Expected Card1: %02X%02X%02X%02X\r\n", 
	       CARD1_ID[0], CARD1_ID[1], CARD1_ID[2], CARD1_ID[3]);
	printf("Expected Card2: %02X%02X%02X%02X\r\n", 
	       CARD2_ID[0], CARD2_ID[1], CARD2_ID[2], CARD2_ID[3]);
	printf("Expected Card3: %02X%02X%02X%02X\r\n", 
	       CARD3_ID[0], CARD3_ID[1], CARD3_ID[2], CARD3_ID[3]);
	
	return -1;  // 未识别的卡片
}

// 兼容函数：register_card现在直接返回固定卡号
static int register_card(const unsigned char *card_id)
{
	int idx = get_card_index_by_id(card_id);
	if(idx >= 0)
	{
		printf("Card%d detected (ID: %02X%02X%02X%02X)\r\n", idx + 1, 
		       card_id[0], card_id[1], card_id[2], card_id[3]);
		return idx;
	}
	
	printf("Unknown card (ID: %02X%02X%02X%02X)\r\n", 
	       card_id[0], card_id[1], card_id[2], card_id[3]);
	return -1;
}

static void PublishCardBalance(const unsigned char *card_id, unsigned char balance)
{
	int card_index = register_card(card_id);
	if(card_index < 0)
		return;

	memset(publish_buf, 0, sizeof(publish_buf));
	snprintf(publish_buf, sizeof(publish_buf), "{\"id\":\"123\",\"params\":{\"Card%d\":{\"value\":%d}}}", card_index + 1, balance);

	printf("Publish payload: %s\r\n", publish_buf);
	OneNet_Publish(devPubTopic, publish_buf);
	
	// 不在这里主动检查订阅数据，让主循环统一处理
	// 这样可以避免读取不完整数据导致解析失败
}

// 充值函数：给指定卡片增加余额
// 参数：card_id - 卡片ID，add_amount - 增加的金额
// 返回值：0=成功，1=卡片不存在，2=验证失败，3=读写失败
unsigned char AddCardBalance(unsigned char *card_id, unsigned char add_amount)
{
	unsigned char status;
	unsigned char read_data[16];
	unsigned char write_data[16];
	unsigned char current_balance;
	unsigned char i;
	unsigned char new_balance;
	
	// 1. 选择卡片
	status = MFRC522_SelectTag(card_id);
	if(status != MI_OK)
	{
		printf("AddBalance: Select card failed\r\n");
		return 3;
	}
	
	// 2. 验证密码（使用默认密钥）
	status = MFRC522_AuthState(PICC_AUTHENT1A, BALANCE_BLOCK_ADDR, default_key, card_id);
	if(status != MI_OK)
	{
		printf("AddBalance: Auth failed\r\n");
		MFRC522_Halt();
		return 2;
	}
	
	// 3. 读取余额块
	status = MFRC522_Read(BALANCE_BLOCK_ADDR, read_data);
	if(status != MI_OK)
	{
		printf("AddBalance: Read failed\r\n");
		MFRC522_Halt();
		return 3;
	}
	
	// 4. 检查是否已初始化
	if(read_data[1] != INIT_FLAG)
	{
		// 未初始化，先初始化为100
		printf("AddBalance: Card not initialized, init first\r\n");
		for(i = 0; i < 16; i++)
		{
			write_data[i] = 0x00;
		}
		write_data[0] = INITIAL_BALANCE;
		write_data[1] = INIT_FLAG;
		current_balance = INITIAL_BALANCE;
	}
	else
	{
		// 已初始化，读取当前余额
		current_balance = read_data[0];
	}
	
	// 5. 增加余额（防止溢出）
	if((unsigned int)current_balance + (unsigned int)add_amount > 255)
	{
		new_balance = 255;  // 最大255
		printf("AddBalance: Balance overflow, set to max 255\r\n");
	}
	else
	{
		new_balance = current_balance + add_amount;
	}
	
	printf("AddBalance: Current=%d, Add=%d, New=%d\r\n", current_balance, add_amount, new_balance);
	
	// 6. 准备写入数据
	for(i = 0; i < 16; i++)
	{
		write_data[i] = read_data[i];
	}
	write_data[0] = new_balance;  // 更新余额
	
	// 7. 写入更新后的余额
	status = MFRC522_Write(BALANCE_BLOCK_ADDR, write_data);
	if(status != MI_OK)
	{
		printf("AddBalance: Write failed\r\n");
		MFRC522_Halt();
		return 3;
	}
	
	// 8. 验证写入
	delay_ms(10);
	status = MFRC522_AuthState(PICC_AUTHENT1A, BALANCE_BLOCK_ADDR, default_key, card_id);
	if(status == MI_OK)
	{
		unsigned char verify_data[16];
		status = MFRC522_Read(BALANCE_BLOCK_ADDR, verify_data);
		if(status == MI_OK && verify_data[0] == new_balance)
		{
			printf("AddBalance: Write verified successfully\r\n");
		}
	}
	
	// 9. 发布新余额到平台（不在此处休眠，避免紧接着的扣费流程重新选卡失败）
	PublishCardBalance(card_id, new_balance);
	
	return 0;  // 成功
}

// 处理充值：检查当前卡片对应的c值，如果大于0则充值
// 参数：card_id - 当前卡片ID，card_index - 卡片索引（0=Card1, 1=Card2, 2=Card3）
// 返回值：1=已充值（已发布），0=未充值
static unsigned char ProcessChargeForCard(unsigned char *card_id, int card_index)
{
	if(card_index < 0 || card_index >= 3)
		return 0;
	
	// 根据卡片索引检查对应的c值
	if(card_index == 0 && c1c_value > 0)
	{
		printf("Charging Card1 with %d\r\n", c1c_value);
		if(AddCardBalance(card_id, (unsigned char)c1c_value) == 0)
		{
			c1c_value = 0;  // 清零，避免重复充值
			return 1;  // 已充值并发布
		}
	}
	else if(card_index == 1 && c2c_value > 0)
	{
		printf("Charging Card2 with %d\r\n", c2c_value);
		if(AddCardBalance(card_id, (unsigned char)c2c_value) == 0)
		{
			c2c_value = 0;  // 清零，避免重复充值
			return 1;  // 已充值并发布
		}
	}
	else if(card_index == 2 && c3c_value > 0)
	{
		printf("Charging Card3 with %d\r\n", c3c_value);
		if(AddCardBalance(card_id, (unsigned char)c3c_value) == 0)
		{
			c3c_value = 0;  // 清零，避免重复充值
			return 1;  // 已充值并发布
		}
	}
	
	return 0;  // 未充值
}


int main(void)
{ 
	unsigned char status;		// RFID操作状态
	unsigned int temp,i;
	unsigned char card_changed = 0;  // 卡号是否改变标志
	
  SystemInit();  // 系统初始化，时钟为72MHz	
	delay_init(72);
	LED_Init();
	LED_On();
	USART1_Config();
	OLED_Init();  // 初始化OLED
	OLED_Clear(); // 清屏
	MFRC522_Init();
	printf ( "MFRC522 Test\r\n" );
	
	// 初始化ESP8266（会在内部初始化USART2）
	ESP8266_Init();
	
	// 连接云平台（OneNet）
	OLED_Clear();
	OLED_ShowString(0, 0, "connecting", 16, 1);
	while(OneNet_DevLink())
	{
		delay_ms(500);
	}
	OLED_Clear();
	OLED_ShowString(0, 0, "connected", 16, 1);
	delay_ms(1000);
	
	delay_ms(1000);
	
	// 初始显示"searching"（在循环外显示）
	OLED_Clear();
	OLED_ShowString(0, 0, "searching", 16, 1);
	OneNet_Subscribe(devSubTopic,1);
  while (1)
  {
		// 处理云平台下发数据（非阻塞检查，超时50ms）
		dataPtr = ESP8266_GetIPD(15);  // 10 * 5ms = 50ms，兼顾RFID响应速度和数据接收可靠性
		if(dataPtr != NULL)
			OneNet_RevPro(dataPtr);
		
		status = MFRC522_Request(PICC_REQALL, buf);  // 寻卡
			if (status != MI_OK)
			{    
					// 寻卡失败，不做任何显示更新，保持当前显示
					MFRC522_Reset();
					MFRC522_AntennaOff(); 
					MFRC522_AntennaOn(); 
					continue;
			}

			printf("card type:");
			for(i=0;i<2;i++)
			{
					temp=buf[i];
					printf("%X",temp);

			}
		
			status = MFRC522_Anticoll(buf);
			if (status != MI_OK)
			{    
						continue;    
			}
			
			
			printf("card id");	
			for(i=0;i<4;i++)
			{
					temp=buf[i];
					printf("%X",temp);

			}
			
			printf("\r\n");
			
			card_changed = 0;
			for(i=0; i<4; i++)
			{
				if(buf[i] != last_card_id[i])
				{
					card_changed = 1;
					break;
				}
			}
			
			// 只有，仅限，唯一条件：卡号改变时才更新显示和处理余额
			if(card_changed)
			{
				// 更新储存的卡号
				for(i=0; i<4; i++)
				{
					last_card_id[i] = buf[i];
				}
				
				// 注册卡片并获取索引
				int card_index = register_card(buf);
				
				// 先检查是否需要充值（在扣费之前）
				unsigned char charged = 0;  // 标记是否已充值并发布
				if(card_index >= 0 && card_index < 3)
				{
					charged = ProcessChargeForCard(buf, card_index);
				}
				
				// 处理卡片余额（初始化或扣费）
				unsigned char new_balance = 0;
				unsigned char balance_status = ProcessCardBalance(buf, &new_balance);
				
				if(balance_status == 0)
				{
					// 成功，显示卡号和余额
					OLED_ShowSearchingAndID(buf, new_balance);
					printf("Balance processed successfully: %d\r\n", new_balance);
					// 如果充值时已经发布过，这里就不重复发布了
					if(!charged)
					{
						PublishCardBalance(buf, new_balance);
					}
				}
				else if(balance_status == 1)
				{
					// 余额不足，显示卡号和余额
					OLED_ShowSearchingAndID(buf, new_balance);
					printf("Insufficient balance!\r\n");
					// 如果充值时已经发布过，这里就不重复发布了
					if(!charged)
					{
						PublishCardBalance(buf, new_balance);
					}
				}
				else
				{
					// 验证或读写失败，只显示卡号，余额显示为0
					// 失败时不发布余额，避免发送错误数据
					OLED_ShowSearchingAndID(buf, 0);
					printf("Balance process failed!\r\n");
				}
			}

  }
}




