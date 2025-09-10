#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <time.h>
#include <stdint.h>

#pragma comment(lib, "ws2_32.lib") // Link with Winsock library

#define SERVER_PORT 9999
#define BUFFER_SIZE 1024 // 缓冲区大小
#define TIMEOUT 300000 // 超时时间
#define MAX_RETRIES 10 // 最大连接重试次数

// 标志位
#define SYN 0x01
#define ACK 0x02
#define FIN 0x04
#define DATA 0x08
#define FILE_INFO 0x10

typedef struct
{
    uint32_t seq_num;          // 序号
    uint32_t ack_num;          // 确认号
    uint8_t flags;            // 标志位
    uint16_t checksum;         // 校验和
    int length;                // 数据长度
    char payload[BUFFER_SIZE]; // 数据载荷
} Packet;

// 服务端状态
typedef enum
{
    CLOSED,
    LISTEN,
    SYN_RCVD,
    ESTABLISHED
} State;

void initializeWinsock();
SOCKET createUDPSocket();
void bindSocket(SOCKET socket, int port);
void handleConnection(SOCKET socket);
uint16_t calculateChecksum(Packet *packet);

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    // 初始化Winsock
    initializeWinsock();
    // 创建UDP套接字
    SOCKET serverSocket = createUDPSocket();
    // 绑定端口
    bindSocket(serverSocket, SERVER_PORT);

    // 将serverSocket设置TIMEOUT秒无信息接收则关闭
    int timeout = TIMEOUT;
    setsockopt(serverSocket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    printf("服务端正在监听...\n");
    printf("%d秒后未收到信息自动关闭\n", TIMEOUT / 1000);

    // 处理连接
    handleConnection(serverSocket);

    // 对与该套接字关联的所有资源进行释放

    printf("服务端已关闭\n");
    closesocket(serverSocket);
    WSACleanup();

    return 0;
}

void initializeWinsock()
{
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        printf("WSAStartup failed: %d\n", result);
        exit(1);
    }
    srand((unsigned int)time(NULL));
}

SOCKET createUDPSocket()
{
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
    {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        WSACleanup();
        exit(1);
    }
    return sock;
}

void bindSocket(SOCKET socket, int port)
{
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(socket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(socket);
        WSACleanup();
        exit(1);
    }
}

void handleConnection(SOCKET socket)
{
    // 设定初始状态
    State state = LISTEN;

    Packet packet;
    struct sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    uint32_t serverSeqNum = 0;
    uint32_t CurrentRecvSeqNum = 0; //当前已确认接收的数据包序号

    char fileName[BUFFER_SIZE];
    uint32_t fileSize = 0;
    uint32_t TotalRecvFileSize = 0; // 当前已经接受的文件大小总和
    int retries = 0;
    FILE *file;

    while (WSAGetLastError() != WSAETIMEDOUT)
    {
        memset(&packet, 0, sizeof(Packet));

        // 函数返回接收到的数据长度
        int recvLen = recvfrom(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)&clientAddr, &clientAddrLen);
        if (recvLen > 0)
        {
            switch (state)
            {
            case LISTEN:
                if (packet.flags & SYN)
                {   //接收SYN
                    printf("Received SYN: seq_num = %u\n", packet.seq_num);
                    CurrentRecvSeqNum = packet.seq_num;
                    serverSeqNum = rand();
                    memset(&packet, 0, sizeof(Packet));
                    packet.seq_num = serverSeqNum;
                    packet.ack_num = CurrentRecvSeqNum + 1;
                    packet.flags = SYN | ACK;
                    sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)&clientAddr, clientAddrLen);
                    printf("Sent SYN-ACK: seq_num = %u, ack_num = %u\n", packet.seq_num, packet.ack_num);
                    state = SYN_RCVD;
                }
                break;
            case SYN_RCVD:
                if ((packet.flags & ACK) && packet.ack_num == serverSeqNum + 1)
                {   // 接受ACK
                    CurrentRecvSeqNum = CurrentRecvSeqNum + 1;
                    printf("Received ACK: seq_num = %u, ack_num = %u\n", packet.seq_num, packet.ack_num);
                    state = ESTABLISHED;
                    printf("连接已建立\n");
                }
                break;
            case ESTABLISHED:
                // 是否为文件信息
                if (packet.flags & FILE_INFO)
                {
                    // 解析文件名和文件大小
                    sscanf(packet.payload, "%[^|]|%u", fileName, &fileSize); //"%[^|]"：匹配直到|字符为止的所有字符，并将其存储到fileName变量中
                    printf("Received file info seq_num : %u \n", packet.seq_num);
                    printf("file info: %s, size = %u\n", fileName, fileSize);
                    CurrentRecvSeqNum = CurrentRecvSeqNum + 1;
                    // 打开文件
                    file = fopen(fileName, "wb");
                    if (file == NULL)
                    {
                        printf("文件创建失败.\n");
                        closesocket(socket);
                        WSACleanup();
                        return;
                    }
                    // 发送ACK
                    memset(&packet, 0, sizeof(Packet));
                    packet.ack_num = CurrentRecvSeqNum + 1;
                    packet.flags = ACK;
                    packet.checksum = calculateChecksum(&packet);
                    sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)&clientAddr, clientAddrLen);
                    printf("Sent ACK for file info: ack_num = %u\n", packet.ack_num);
                }
                // 是否为数据帧
                else if (packet.flags & DATA)
                {
                    // 是否是重复数据帧
                    if (packet.seq_num != CurrentRecvSeqNum + 1)
                    {
                        printf("Received duplicate data: seq_num = %u\n", packet.seq_num);
                        // Retransmit ACK
                        memset(&packet, 0, sizeof(Packet));
                        packet.ack_num = CurrentRecvSeqNum + 1;
                        packet.flags = ACK;
                        packet.checksum = calculateChecksum(&packet);
                        // 重发ACK
                        sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)&clientAddr, clientAddrLen);
                        continue;
                    }
                    // 检验校验和
                    uint16_t receivedChecksum = packet.checksum;
                    uint16_t calculatedChecksum = calculateChecksum(&packet);
                    if (receivedChecksum != calculatedChecksum)
                    {  // 丢弃包,检验错误
                        printf("Checksum error: received = %u, calculated = %u\n", receivedChecksum, calculatedChecksum);
                        continue;
                    }
                    // 非重复帧或错误帧
                    // printf("Received DATA: seq_num = %u\n", packet.seq_num);
                    fwrite(packet.payload, 1, packet.length, file); // Write data to file
                    CurrentRecvSeqNum = CurrentRecvSeqNum + 1;
                    TotalRecvFileSize += packet.length;
                    memset(&packet, 0, sizeof(Packet));
                    packet.ack_num = CurrentRecvSeqNum + 1;
                    packet.flags = ACK;
                    packet.checksum = calculateChecksum(&packet);
                    sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)&clientAddr, clientAddrLen);
                    // printf("Sent ACK: ack_num = %u\n", packet.ack_num);

                    printf("进度：%.2f%%\n", TotalRecvFileSize / (float)fileSize * 100);
                }
                // 是否为结束帧 ->四次挥手
                else if (packet.flags & FIN)
                {
                    printf("Received FIN from client: seq_num = %u\n", packet.seq_num);
                    CurrentRecvSeqNum = packet.seq_num;
                    serverSeqNum = rand();
                    // Send ACK for FIN
                    memset(&packet, 0, sizeof(Packet));
                    packet.seq_num = serverSeqNum;
                    packet.ack_num = CurrentRecvSeqNum + 1;
                    packet.flags = ACK;
                    packet.checksum = calculateChecksum(&packet);
                    sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)&clientAddr, clientAddrLen);
                    printf("Sent ACK for client FIN: seq_num = %u ,ack_num = %u\n",packet.seq_num, packet.ack_num);

                    while (retries < MAX_RETRIES)
                    {
                        // Send FIN to client
                        memset(&packet, 0, sizeof(Packet));
                        serverSeqNum = rand();
                        packet.seq_num = serverSeqNum;
                        packet.ack_num = CurrentRecvSeqNum + 1;
                        packet.flags = FIN | ACK;
                        packet.checksum = calculateChecksum(&packet);
                        sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)&clientAddr, clientAddrLen);
                        printf("Sent ACK: seq_num = %u, ack_num = %u\n", packet.seq_num, packet.ack_num);

                        // Wait for ACK from client
                        recvLen = recvfrom(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)&clientAddr, &clientAddrLen);
                        if (recvLen != SOCKET_ERROR && (packet.flags & ACK) && packet.ack_num == serverSeqNum + 1)
                        {
                            printf("Received ACK for server FIN : seq_num = %u, ack_num = %u\n", packet.seq_num, packet.ack_num);
                            state = CLOSED;
                            fclose(file);
                            return;
                            // break;
                        }
                        else if (recvLen == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT)
                        {
                            retries++;
                            printf("Resending FIN... (%d/%d)\n", retries, MAX_RETRIES);
                        }
                        else
                        {
                            printf("Failed to receive ACK for FIN, error: %d\n", WSAGetLastError());
                        }
                    }
                    if (retries >= MAX_RETRIES)
                    {
                        printf("Failed to close. Exiting...\n", MAX_RETRIES);
                        closesocket(socket);
                        WSACleanup();
                    }
                }
                break;
            default:
                break;
            }
        }
        else
        {
            break;
        }
    }
    fclose(file);
    printf("连接超时...文件传输结束.\n");
}

uint16_t calculateChecksum(Packet *packet)
{
    packet->checksum = 0;
    uint16_t *data = (uint16_t *)packet;
    uint32_t sum = 0;
    for (int i = 0; i < sizeof(Packet) / 2; i++)
    {
        sum += data[i];
    }
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}
