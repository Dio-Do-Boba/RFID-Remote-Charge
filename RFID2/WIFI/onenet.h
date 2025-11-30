#ifndef _ONENET_H_
#define _ONENET_H_





_Bool OneNET_RegisterDevice(void);

_Bool OneNet_DevLink(void);

void OneNet_Subscribe(const char *topics[], unsigned char topic_cnt);

void OneNet_RevPro(unsigned char *cmd);

void OneNet_Publish(const char *topic, const char *msg);

void OneNet_ParseTLV(unsigned char *data, unsigned short len);

void OneNet_ParseBinary(unsigned char *data, unsigned short len);


#endif
