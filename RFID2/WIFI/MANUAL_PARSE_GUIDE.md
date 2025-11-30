# MQTT PUBLISH 数据包手动解析指南

## 1. MQTT PUBLISH 数据包格式

MQTT PUBLISH 数据包的格式如下：

```
[固定头(1字节)] [剩余长度(1-4字节)] [Topic长度(2字节)] [Topic数据] [Payload数据]
```

### 详细结构：

1. **固定头 (Fixed Header)**
   - 1字节
   - 格式：`0x30` (PUBLISH消息类型，bit 7-4 = 3)
   - 示例：`30` = PUBLISH消息

2. **剩余长度 (Remaining Length)**
   - 1-4字节，使用变长编码
   - 每个字节的最高位(bit 7)表示是否还有后续字节
   - bit 7 = 0：这是最后一个字节
   - bit 7 = 1：还有后续字节
   - 实际长度 = 各字节的低7位组合
   - 示例：`5B` = 91字节（因为 0x5B = 91，且最高位为0）

3. **Topic长度 (Topic Length)**
   - 2字节，大端序（Big-Endian）
   - 示例：`00 28` = 40字节

4. **Topic数据**
   - 长度由Topic长度字段指定
   - 示例：`$sys/TdTlyD3CtQ/Test1/thing/property/set` (40字节)

5. **Payload数据**
   - 剩余的所有数据
   - 长度 = 剩余长度 - 2 - Topic长度
   - 示例：JSON字符串 `{"id":"23","version":"1.0","params":{"Card2":80}}`

## 2. 手动解析步骤

### 步骤1：检查固定头
```c
if(cmd[0] == 0x30)  // 确认是MQTT PUBLISH消息
{
    // 继续解析
}
```

### 步骤2：解析剩余长度（变长编码）
```c
unsigned char len_bytes = 1;
unsigned long remain_len = cmd[1] & 0x7F;  // 取低7位
unsigned char pos = 2;  // 当前位置

if(cmd[1] & 0x80)  // 如果最高位为1，还有后续字节
{
    remain_len += (cmd[2] & 0x7F) << 7;  // 第二字节左移7位
    len_bytes = 2;
    pos = 3;
    if(cmd[2] & 0x80)  // 如果还有第三字节
    {
        remain_len += (cmd[3] & 0x7F) << 14;
        len_bytes = 3;
        pos = 4;
        if(cmd[3] & 0x80)  // 如果还有第四字节
        {
            remain_len += (cmd[4] & 0x7F) << 21;
            len_bytes = 4;
            pos = 5;
        }
    }
}
```

### 步骤3：读取Topic长度
```c
unsigned char *data_ptr = cmd + pos;  // 跳过固定头和剩余长度
unsigned short topic_len = (data_ptr[0] << 8) | data_ptr[1];  // 大端序
```

### 步骤4：提取Topic数据
```c
unsigned char *topic = data_ptr + 2;  // Topic数据紧跟在长度后面
```

### 步骤5：提取Payload数据
```c
unsigned char *payload = topic + topic_len;  // Payload紧跟在Topic后面
unsigned short payload_len = remain_len - 2 - topic_len;  // 计算Payload长度
```

## 3. 完整示例代码

```c
// 手动解析 MQTT PUBLISH 数据包
unsigned char *manual_topic = NULL;
unsigned char *manual_payload = NULL;
unsigned short manual_topic_len = 0;
unsigned short manual_payload_len = 0;

if(cmd[0] == 0x30)  // MQTT PUBLISH
{
    // 1. 读取剩余长度（MQTT 变长编码）
    unsigned char len_bytes = 1;
    unsigned long remain_len = cmd[1] & 0x7F;
    unsigned char pos = 2;
    
    if(cmd[1] & 0x80)
    {
        remain_len += (cmd[2] & 0x7F) << 7;
        len_bytes = 2;
        pos = 3;
        if(cmd[2] & 0x80)
        {
            remain_len += (cmd[3] & 0x7F) << 14;
            len_bytes = 3;
            pos = 4;
            if(cmd[3] & 0x80)
            {
                remain_len += (cmd[4] & 0x7F) << 21;
                len_bytes = 4;
                pos = 5;
            }
        }
    }
    
    // 2. Topic 长度（2字节，大端序）
    unsigned char *data_ptr = cmd + pos;
    manual_topic_len = (data_ptr[0] << 8) | data_ptr[1];
    
    // 3. Topic 数据
    manual_topic = data_ptr + 2;
    
    // 4. Payload 数据
    manual_payload = manual_topic + manual_topic_len;
    manual_payload_len = remain_len - 2 - manual_topic_len;
    
    // 5. 复制到安全缓冲区（避免数据被覆盖）
    static char manual_topic_buf[128];
    static char manual_payload_buf[256];
    
    if(manual_topic_len < sizeof(manual_topic_buf))
    {
        memcpy(manual_topic_buf, manual_topic, manual_topic_len);
        manual_topic_buf[manual_topic_len] = '\0';
    }
    
    if(manual_payload_len < sizeof(manual_payload_buf))
    {
        memcpy(manual_payload_buf, manual_payload, manual_payload_len);
        manual_payload_buf[manual_payload_len] = '\0';
    }
}
```

## 4. 从Payload中提取JSON

如果Payload是JSON格式，可以：

### 方法1：直接解析（如果payload就是纯JSON）
```c
json = cJSON_Parse(manual_payload_buf);
```

### 方法2：查找JSON字符串（如果payload前面有其他数据）
```c
// 查找 '{' 字符，找到JSON开始位置
unsigned short json_start = 0;
for(unsigned short i = 0; i < manual_payload_len; i++)
{
    if(manual_payload_buf[i] == '{')
    {
        json_start = i;
        break;
    }
}

if(json_start < manual_payload_len)
{
    char *json_str = manual_payload_buf + json_start;
    json = cJSON_Parse(json_str);
}
```

### 方法3：简单字符串搜索（如果cJSON解析失败）
```c
// 直接搜索 "Card1":, "Card2":, "Card3": 后面的数字
char *card2_str = strstr(manual_payload_buf, "\"Card2\":");
if(card2_str != NULL)
{
    char *num_start = card2_str + 8;  // 跳过 "Card2":
    while(*num_start == ' ' || *num_start == '\t') num_start++;  // 跳过空格
    if(*num_start >= '0' && *num_start <= '9')
    {
        Card2_value = atoi(num_start);
    }
}
```

## 5. 实际数据示例

假设接收到的原始数据（十六进制）：
```
30 5B 00 28 24 73 79 73 2F 54 64 54 6C 79 44 33 43 74 51 2F 54 65 73 74 31 2F 74 68 69 6E 67 2F 70 72 6F 70 65 72 74 79 2F 73 65 74 7B 22 69 64 22 3A 22 32 33 22 2C 22 76 65 72 73 69 6F 6E 22 3A 22 31 2E 30 22 2C 22 70 61 72 61 6D 73 22 3A 7B 22 43 61 72 64 32 22 3A 38 30 7D 7D
```

解析过程：
1. `30` = PUBLISH消息
2. `5B` = 91字节（剩余长度）
3. `00 28` = 40字节（Topic长度）
4. Topic: `24 73 79 73...` = `$sys/TdTlyD3CtQ/Test1/thing/property/set`
5. Payload: `7B 22 69 64...` = `{"id":"23","version":"1.0","params":{"Card2":80}}`

## 6. 注意事项

1. **数据安全**：手动解析得到的指针指向原始缓冲区，数据可能被覆盖，需要复制到安全缓冲区
2. **字符串终止**：确保复制的数据以 `\0` 结尾，否则字符串函数会出错
3. **边界检查**：始终检查数组边界，避免越界访问
4. **字节序**：Topic长度是大端序，需要注意字节序转换
5. **变长编码**：剩余长度使用变长编码，需要正确处理

## 7. 为什么需要手动解析？

在某些情况下，`MQTT_UnPacketPublish` 函数可能：
- 返回错误的指针（指向无效内存）
- 内存分配失败
- 解析逻辑有bug

手动解析可以：
- 绕过这些问题
- 直接从原始数据提取所需信息
- 更可靠和可控

