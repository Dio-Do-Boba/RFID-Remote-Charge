/**
	************************************************************
	************************************************************
	************************************************************
	*	文件名称： 	onenet.c
	*
	*	作者： 		张继瑞
	*
	*	日期： 		2017-05-08
	*
	*	版本号： 	V1.1
	*
	*	说明： 		与onenet平台进行数据交互的接口层
	*
	*	修改记录：	V1.0：协议封装和判断都在同一个文件，但不同协议接口层不同。
	*				V1.1：提供统一接口供应用层使用，根据不同协议文件来封装协议相关的数据。
	************************************************************
	************************************************************
	************************************************************
**/

//单片机头文件
#include "stm32f10x.h"

//网络设备
#include "esp8266.h"

//协议文件
#include "onenet.h"
#include "mqttkit.h"

//硬件驱动
#include "bsp_usart.h"
#include "bsp_delay.h"
#include "bsp_led.h"
#include "bsp_Alarm.h"

//C库
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"

/*product-ID*/
#define PROID		"000000000"

//Token
#define TOKEN	"version=2018-10-3100000000000000"

//device-ID
#define DEVID		"Test"

extern unsigned char esp8266_buf[512];
extern uint8_t Alarm_flag;
extern int c1c_value;
extern int c2c_value;
extern int c3c_value;
//==========================================================
//	函数名称：	OneNet_DevLink
//
//	函数功能：	连接onenet平台
//
//	入口参数：	无
//
//	返回参数：	1-成功	0-失败
//
//	说明：		连接onenet平台
//==========================================================
_Bool OneNet_DevLink(void)
{
	
	MQTT_PACKET_STRUCTURE mqttPacket = {NULL, 0, 0, 0};					//协议包

	unsigned char *dataPtr;
	_Bool status = 1;
	
	UsartPrintf(USART_DEBUG, "OneNet_DevLink\r\n"
							"PROID: %s,	TOKEN: %s, DEVID:%s\r\n"
                        , PROID, TOKEN, DEVID);
	
	if(MQTT_PacketConnect(PROID, TOKEN, DEVID, 256, 1, MQTT_QOS_LEVEL0, NULL, NULL, 0, &mqttPacket) == 0)
	{
		ESP8266_SendData(mqttPacket._data, mqttPacket._len);			//上传平台
		dataPtr = ESP8266_GetIPD(250);									//等待平台响应

		if(dataPtr != NULL)
		{	
			if(MQTT_UnPacketRecv(dataPtr) == MQTT_PKT_CONNACK)
			{
				switch(MQTT_UnPacketConnectAck(dataPtr))
				{
					case 0:UsartPrintf(USART_DEBUG, "Tips:	connected");status = 0;break;
					
					case 1:UsartPrintf(USART_DEBUG, "WARN:	连接失败：协议错误\r\n");break;
					case 2:UsartPrintf(USART_DEBUG, "WARN:	连接失败：非法clientid\r\n");break;
					case 3:UsartPrintf(USART_DEBUG, "WARN:	连接失败：服务器不可用\r\n");break;
					case 4:UsartPrintf(USART_DEBUG, "WARN:	连接失败：用户名密码错误\r\n");break;
					case 5:UsartPrintf(USART_DEBUG, "WARN:	连接失败：未授权(检查token是否)\r\n");break;
					
					default:UsartPrintf(USART_DEBUG, "ERR:	连接失败：未知错误\r\n");break;
				}
			}
		}
		
		MQTT_DeleteBuffer(&mqttPacket);								//删除
	}
	else
		UsartPrintf(USART_DEBUG, "WARN:	MQTT_PacketConnect Failed\r\n");
	
	return status;
	
}

//==========================================================
//	函数名称：	OneNet_Subscribe
//
//	函数功能：	订阅
//
//	入口参数：	topics：订阅的topic
//				topic_cnt：topic数量
//
//	返回参数：	SEND_TYPE_OK-成功	SEND_TYPE_SUBSCRIBE-需要重发
//
//	说明：		
//==========================================================
void OneNet_Subscribe(const char *topics[], unsigned char topic_cnt)
{
	
	unsigned char i = 0;
	
	MQTT_PACKET_STRUCTURE mqttPacket = {NULL, 0, 0, 0};							//协议包
	
	for(; i < topic_cnt; i++)
		UsartPrintf(USART_DEBUG, "Subscribe Topic: %s\r\n", topics[i]);
	
	if(MQTT_PacketSubscribe(MQTT_SUBSCRIBE_ID, MQTT_QOS_LEVEL0, topics, topic_cnt, &mqttPacket) == 0)
	{
		ESP8266_SendData(mqttPacket._data, mqttPacket._len);					//向平台发送订阅请求
		
		MQTT_DeleteBuffer(&mqttPacket);											//删除
	}

}

//==========================================================
//	函数名称：	OneNet_Publish
//
//	函数功能：	发布消息
//
//	入口参数：	topic：发布主题
//				msg：消息内容
//
//	返回参数：	SEND_TYPE_OK-成功	SEND_TYPE_PUBLISH-需要重发
//
//	说明：		
//==========================================================
void OneNet_Publish(const char *topic, const char *msg)
{

	MQTT_PACKET_STRUCTURE mqttPacket = {NULL, 0, 0, 0};							//协议包
	
	UsartPrintf(USART_DEBUG, "Publish Topic: %s, Msg: %s\r\n", topic, msg);
	
	if(MQTT_PacketPublish(MQTT_PUBLISH_ID, topic, msg, strlen(msg),MQTT_QOS_LEVEL0, 0, 1, &mqttPacket) == 0)
	{
		ESP8266_SendData(mqttPacket._data, mqttPacket._len);					//向平台发送发布请求
		
		MQTT_DeleteBuffer(&mqttPacket);											//删除
	}

}

//==========================================================
//	函数名称：	OneNet_RevPro
//
//	函数功能：	平台下发数据检测
//
//	入口参数：	dataPtr：平台返回的数据
//
//	返回参数：	无
//
//	说明：		
//==========================================================
void OneNet_RevPro(unsigned char *cmd)
{
	
	MQTT_PACKET_STRUCTURE mqttPacket = {NULL, 0, 0, 0};								//协议包
	
	char *req_payload = NULL;
	char *cmdid_topic = NULL;
	
	unsigned short topic_len = 0;
	unsigned short req_len = 0;
	
	unsigned char type = 0;
	unsigned char qos = 0;
	static unsigned short pkt_id = 0;
	
	short result = 0;
	cJSON *json, *params_json, *c1c_json,*c2c_json,*c3c_json;
	
	type = MQTT_UnPacketRecv(cmd);
	switch(type)
	{
		case MQTT_PKT_CMD:															//命令下发
			
			result = MQTT_UnPacketCmd(cmd, &cmdid_topic, &req_payload, &req_len);	//解析topic和消息体
			if(result == 0)
			{
				UsartPrintf(USART_DEBUG, "cmdid: %s, req: %s, req_len: %d\r\n", cmdid_topic, req_payload, req_len);
				
				if(MQTT_PacketCmdResp(cmdid_topic, req_payload, &mqttPacket) == 0)	//发送响应包
				{
					UsartPrintf(USART_DEBUG, "Tips:	Send CmdResp\r\n");
					
					ESP8266_SendData(mqttPacket._data, mqttPacket._len);			//回复响应
					MQTT_DeleteBuffer(&mqttPacket);									//删除
				}
			}
		
		break;
			
		case MQTT_PKT_PUBLISH:														//接收到Publish消息
		
			result = MQTT_UnPacketPublish(cmd, &cmdid_topic, &topic_len, &req_payload, &req_len, &qos, &pkt_id);
			if(result == 0)
			{
				// 安全打印 topic
				if(cmdid_topic != NULL && topic_len > 0)
				{
					UsartPrintf(USART_DEBUG, "topic: %.*s, topic_len: %d\r\n", topic_len, cmdid_topic, topic_len);
				}
				else
				{
					UsartPrintf(USART_DEBUG, "WARN: topic is NULL or invalid\r\n");
				}
				
				// 安全打印 payload（限制长度避免打印过长或二进制数据）
				if(req_payload != NULL && req_len > 0)
				{
					unsigned short print_len = req_len < 200 ? req_len : 200;  // 限制打印长度
					UsartPrintf(USART_DEBUG, "payload_len: %d, payload (first %d bytes): ", req_len, print_len);
					for(unsigned short i = 0; i < print_len; i++)
					{
						if(req_payload[i] >= 32 && req_payload[i] < 127)  // 可打印字符
						{
							UsartPrintf(USART_DEBUG, "%c", req_payload[i]);
						}
						else
						{
							UsartPrintf(USART_DEBUG, "\\x%02X", (unsigned char)req_payload[i]);
						}
					}
					UsartPrintf(USART_DEBUG, "\r\n");
				}
				else
				{
					UsartPrintf(USART_DEBUG, "WARN: payload is NULL or empty (len=%d)\r\n", req_len);
				}
				
				// 手动解析JSON格式的payload（查找JSON字符串）
				if(req_payload != NULL && req_len > 0)
				{
					// 方法1：在payload中查找 '{' 字符，找到JSON开始位置
					unsigned short json_start = 0;
					unsigned short i;
					for(i = 0; i < req_len; i++)
					{
						if(req_payload[i] == '{')
						{
							json_start = i;
							break;
						}
					}
					
					// 如果找到JSON开始位置
					if(json_start < req_len)
					{
						// 提取JSON字符串
						unsigned short json_len = req_len - json_start;
						char *json_str = (char *)req_payload + json_start;
						
						// 创建临时缓冲区确保字符串null终止
						char temp_json[256];
						unsigned short copy_len = json_len < 255 ? json_len : 255;
						memcpy(temp_json, json_str, copy_len);
						temp_json[copy_len] = '\0';
						
						UsartPrintf(USART_DEBUG, "Found JSON at offset %d: %s\r\n", json_start, temp_json);
						
						// 方法2：使用简单的字符串搜索提取值（避免cJSON可能卡死）
						char *c1c_str = strstr(temp_json, "\"C1Charge\":");
						char *c2c_str = strstr(temp_json, "\"C2Charge\":");
						char *c3c_str = strstr(temp_json, "\"C3Charge\":");
						
						if(c1c_str != NULL)
						{
							char *num_start = c1c_str + 11;  // 跳过 "C1Charge":
							while(*num_start == ' ' || *num_start == '\t') num_start++;  // 跳过空格
							if(*num_start >= '0' && *num_start <= '9')
							{
								c1c_value = atoi(num_start);
								UsartPrintf(USART_DEBUG, "c1c_value = %d\r\n", c1c_value);
							}
						}
						
						if(c2c_str != NULL)
						{
							char *num_start = c2c_str + 11;  // 跳过 "C2Charge":
							while(*num_start == ' ' || *num_start == '\t') num_start++;  // 跳过空格
							if(*num_start >= '0' && *num_start <= '9')
							{
								c2c_value = atoi(num_start);
								UsartPrintf(USART_DEBUG, "c2c_value = %d\r\n", c2c_value);
							}
						}
						
						if(c3c_str != NULL)
						{
							char *num_start = c3c_str + 11;  // 跳过 "C3Charge":
							while(*num_start == ' ' || *num_start == '\t') num_start++;  // 跳过空格
							if(*num_start >= '0' && *num_start <= '9')
							{
								c3c_value = atoi(num_start);
								UsartPrintf(USART_DEBUG, "c3c_value = %d\r\n", c3c_value);
							}
						}
					}
				}
				
			}
			else
			{
				UsartPrintf(USART_DEBUG, "ERR: MQTT_UnPacketPublish failed, result=%d\r\n", result);
				// 解析失败时清空缓冲区，避免影响后续数据接收
				ESP8266_Clear();
			}
		break;
			
		case MQTT_PKT_PUBACK:														//发送Publish消息后平台回复的Ack
		
			if(MQTT_UnPacketPublishAck(cmd) == 0)
				UsartPrintf(USART_DEBUG, "Tips:	MQTT Publish Send OK\r\n");
			
		break;
			
		case MQTT_PKT_PUBREC:														//发送Publish消息后平台回复的Rec，设备需回复Rel消息
		
			if(MQTT_UnPacketPublishRec(cmd) == 0)
			{
				UsartPrintf(USART_DEBUG, "Tips:	Rev PublishRec\r\n");
				if(MQTT_PacketPublishRel(MQTT_PUBLISH_ID, &mqttPacket) == 0)
				{
					UsartPrintf(USART_DEBUG, "Tips:	Send PublishRel\r\n");
					ESP8266_SendData(mqttPacket._data, mqttPacket._len);
					MQTT_DeleteBuffer(&mqttPacket);
				}
			}
		
		break;
			
		case MQTT_PKT_PUBREL:														//收到Publish消息后设备回复Rec，平台回复的Rel，设备需再回复Comp
			
			if(MQTT_UnPacketPublishRel(cmd, pkt_id) == 0)
			{
				UsartPrintf(USART_DEBUG, "Tips:	Rev PublishRel\r\n");
				if(MQTT_PacketPublishComp(MQTT_PUBLISH_ID, &mqttPacket) == 0)
				{
					UsartPrintf(USART_DEBUG, "Tips:	Send PublishComp\r\n");
					ESP8266_SendData(mqttPacket._data, mqttPacket._len);
					MQTT_DeleteBuffer(&mqttPacket);
				}
			}
		
		break;
		
		case MQTT_PKT_PUBCOMP:													//发送Publish消息后平台发送Rec，设备回复Rel，平台再返回的Comp
		
			if(MQTT_UnPacketPublishComp(cmd) == 0)
			{
				UsartPrintf(USART_DEBUG, "Tips:	Rev PublishComp\r\n");
			}
		
		break;
			
		case MQTT_PKT_SUBACK:														//发送Subscribe消息后的Ack
		
			if(MQTT_UnPacketSubscribe(cmd) == 0)
				UsartPrintf(USART_DEBUG, "Tips:	MQTT Subscribe OK\r\n");
			else
				UsartPrintf(USART_DEBUG, "Tips:	MQTT Subscribe Err\r\n");
		
		break;
			
		case MQTT_PKT_UNSUBACK:													//发送UnSubscribe消息后的Ack
		
			if(MQTT_UnPacketUnSubscribe(cmd) == 0)
				UsartPrintf(USART_DEBUG, "Tips:	MQTT UnSubscribe OK\r\n");
			else
				UsartPrintf(USART_DEBUG, "Tips:	MQTT UnSubscribe Err\r\n");
		
		break;
		
		default:
			result = -1;
		break;
	}
	
	ESP8266_Clear();									//清空栈区
	
	if(result == -1)
		return;
	
	if(type == MQTT_PKT_CMD || type == MQTT_PKT_PUBLISH)
	{
		MQTT_FreeBuffer(cmdid_topic);
		MQTT_FreeBuffer(req_payload);
	}

}

//==========================================================
//	函数名称：	OneNet_ParseTLV
//
//	函数功能：	解析TLV格式数据
//
//	入口参数：	data：TLV数据指针
//				len：数据长度
//
//	返回参数：	无
//
//	说明：		解析TLV格式数据，提取Card1、Card2、Card3的值
//==========================================================
void OneNet_ParseTLV(unsigned char *data, unsigned short len)
{
	unsigned short pos = 0;
	unsigned char tag;
	unsigned char length;
	int value;
	
	LED_ON();  // 指示开始解析TLV数据
	
	UsartPrintf(USART_DEBUG, "Parsing TLV data, len=%d\r\n", len);
	
	// 打印前16个字节用于调试
	UsartPrintf(USART_DEBUG, "First 16 bytes: ");
	for(unsigned short i = 0; i < 16 && i < len; i++)
	{
		UsartPrintf(USART_DEBUG, "%02X ", data[i]);
	}
	UsartPrintf(USART_DEBUG, "\r\n");
	
	while(pos < len)
	{
		// 检查是否有足够的字节读取Tag和Length
		if(pos + 2 > len)
		{
			UsartPrintf(USART_DEBUG, "WARN: TLV data incomplete at pos %d\r\n", pos);
			break;
		}
		
		// 读取Tag
		tag = data[pos++];
		
		// 读取Length
		length = data[pos++];
		
		// 跳过无效的TLV条目（tag=0, length=0）
		if(tag == 0 && length == 0)
		{
			UsartPrintf(USART_DEBUG, "Skipping invalid TLV entry (tag=0, length=0) at pos %d\r\n", pos - 2);
			continue;
		}
		
		// 检查length是否合理（不能太大）
		if(length > 64)
		{
			UsartPrintf(USART_DEBUG, "WARN: TLV length too large (%d), skipping at pos %d\r\n", length, pos - 2);
			break;
		}
		
		// 检查是否有足够的字节读取Value
		if(pos + length > len)
		{
			UsartPrintf(USART_DEBUG, "WARN: TLV value incomplete, tag=%d, length=%d, pos=%d, remaining=%d\r\n", 
			            tag, length, pos, len - pos);
			break;
		}
		
		// 解析Value（假设是整数，根据长度解析）
		value = 0;
		if(length == 1)
		{
			value = (int)data[pos];
		}
		else if(length == 2)
		{
			value = (int)((data[pos] << 8) | data[pos + 1]);
		}
		else if(length == 4)
		{
			value = (int)((data[pos] << 24) | (data[pos + 1] << 16) | 
			              (data[pos + 2] << 8) | data[pos + 3]);
		}
		else if(length > 0 && length <= 4)
		{
			// 对于其他长度，按小端序解析
			unsigned char i;
			for(i = 0; i < length; i++)
			{
				value |= ((int)data[pos + i]) << (8 * i);
			}
		}
		
		// 根据Tag设置对应的Card值
		switch(tag)
		{
			case 1:  // Card1
				c1c_value = value;
				UsartPrintf(USART_DEBUG, "TLV: c1c_value = %d\r\n", c1c_value);
				break;
				
			case 2:  // Card2
				c2c_value = value;
				UsartPrintf(USART_DEBUG, "TLV: c2c_value = %d\r\n", c2c_value);
				break;
				
			case 3:  // Card3
				c3c_value = value;
				UsartPrintf(USART_DEBUG, "TLV: c3c_value = %d\r\n", c3c_value);
				break;
				
			default:
				UsartPrintf(USART_DEBUG, "TLV: Unknown tag=%d, length=%d, value=%d\r\n", tag, length, value);
				break;
		}
		
		// 移动到下一个TLV条目
		pos += length;
	}
	
	LED_OFF();  // 指示TLV解析完成
	
	UsartPrintf(USART_DEBUG, "TLV parsing completed\r\n");
}

//==========================================================
//	函数名称：	OneNet_ParseBinary
//
//	函数功能：	解析OneNet二进制格式数据
//
//	入口参数：	data：二进制数据指针
//				len：数据长度
//
//	返回参数：	无
//
//	说明：		尝试从OneNet二进制格式中提取C1Charge、C2Charge、C3Charge的值
//				格式可能是：属性ID + 数据类型 + 数据长度 + 数据值
//==========================================================
void OneNet_ParseBinary(unsigned char *data, unsigned short len)
{
	unsigned short pos = 0;
	
	UsartPrintf(USART_DEBUG, "Parsing OneNet binary data, len=%d\r\n", len);
	
	// 打印完整的十六进制数据用于分析
	UsartPrintf(USART_DEBUG, "Full hex data: ");
	for(unsigned short i = 0; i < len && i < 64; i++)
	{
		UsartPrintf(USART_DEBUG, "%02X ", data[i]);
		if((i + 1) % 16 == 0)
			UsartPrintf(USART_DEBUG, "\r\n");
	}
	if(len > 64)
		UsartPrintf(USART_DEBUG, "... (truncated)");
	UsartPrintf(USART_DEBUG, "\r\n");
	
	// 尝试查找可能的属性标识符
	// OneNet可能使用特定的标识符来标识属性
	// 常见的可能是：C1Charge, C2Charge, C3Charge 的某种编码
	
	// 方法1：尝试查找整数模式（可能是属性值）
	// 查找连续的4字节整数（可能是32位整数）
	for(pos = 0; pos <= len - 4; pos++)
	{
		// 尝试读取一个32位整数（大端序）
		int value_be = (int)((data[pos] << 24) | (data[pos + 1] << 16) | 
		                     (data[pos + 2] << 8) | data[pos + 3]);
		// 尝试读取一个32位整数（小端序）
		int value_le = (int)((data[pos + 3] << 24) | (data[pos + 2] << 16) | 
		                     (data[pos + 1] << 8) | data[pos]);
		
		// 如果值在合理范围内（0-1000），可能是余额值
		if(value_be >= 0 && value_be <= 1000)
		{
			UsartPrintf(USART_DEBUG, "Found possible value (BE) at pos %d: %d\r\n", pos, value_be);
		}
		if(value_le >= 0 && value_le <= 1000 && value_le != value_be)
		{
			UsartPrintf(USART_DEBUG, "Found possible value (LE) at pos %d: %d\r\n", pos, value_le);
		}
	}
	
	// 方法2：尝试查找特定的字节模式
	// 查找可能的属性标识符（例如：0x01, 0x02, 0x03 可能对应 C1, C2, C3）
	for(pos = 0; pos < len - 1; pos++)
	{
		if(data[pos] == 0x01 || data[pos] == 0x02 || data[pos] == 0x03)
		{
			unsigned char attr_id = data[pos];
			unsigned char data_type = data[pos + 1];
			unsigned char data_len = 0;
			int value = 0;
			
			// 尝试读取后续的数据
			if(pos + 2 < len)
			{
				data_len = data[pos + 1];
				if(pos + 2 + data_len <= len && data_len > 0 && data_len <= 4)
				{
					// 读取值
					value = 0;
					for(unsigned char i = 0; i < data_len; i++)
					{
						value |= ((int)data[pos + 2 + i]) << (8 * i);
					}
					
					UsartPrintf(USART_DEBUG, "Found attr_id=%d, type=%d, len=%d, value=%d at pos %d\r\n", 
					            attr_id, data_type, data_len, value, pos);
					
					// 根据属性ID设置对应的值
					if(attr_id == 0x01 && value >= 0 && value <= 1000)
					{
						c1c_value = value;
						UsartPrintf(USART_DEBUG, "Binary: c1c_value = %d\r\n", c1c_value);
					}
					else if(attr_id == 0x02 && value >= 0 && value <= 1000)
					{
						c2c_value = value;
						UsartPrintf(USART_DEBUG, "Binary: c2c_value = %d\r\n", c2c_value);
					}
					else if(attr_id == 0x03 && value >= 0 && value <= 1000)
					{
						c3c_value = value;
						UsartPrintf(USART_DEBUG, "Binary: c3c_value = %d\r\n", c3c_value);
					}
				}
			}
		}
	}
	
	UsartPrintf(USART_DEBUG, "Binary parsing completed\r\n");
}
