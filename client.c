#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <time.h>
#include <stdint.h>
#include "utils.h"

#pragma comment(lib, "ws2_32.lib") // Link with Winsock library

#define BUFFER_SIZE 1024 // 缓冲区大小
#define TIMEOUT 3000     // 超时时间
#define MAX_RETRIES 10 //最大连接重试次数

// 标志位
#define SYN 0x01
#define ACK 0x02
#define FIN 0x04
#define DATA 0x08
#define FILE_INFO 0x10

typedef struct
{
    uint32_t seq_num; // 序号
    uint32_t ack_num; // 确认号
    uint8_t flags; // 标志位
    uint16_t checksum; // 校验和
    int length; // 数据长度
    char payload[BUFFER_SIZE]; //数据载荷
} Packet;

void initializeWinsock();
SOCKET createUDPSocket();
uint32_t getFileSize(FILE *file);
struct sockaddr_in setServerAddr(int sin_family, char *serverIp, int serverPort);
void establishConnection(SOCKET socket, struct sockaddr_in *serverAddr, uint32_t *clientSeqNum);
void sendFileInfo(SOCKET socket, struct sockaddr_in *serverAddr, const char *fileName, uint32_t fileSize, uint32_t *clientSeqNum);
void sendData(SOCKET socket, struct sockaddr_in *serverAddr, FILE *file, uint32_t filesize, uint32_t *clientSeqNum);
void closeConnection(SOCKET socket, struct sockaddr_in *serverAddr, uint32_t *clientSeqNum);
uint16_t calculateChecksum(Packet *packet);

int main()
{
    SetConsoleOutputCP(CP_UTF8);
    // 初始化winsock
    initializeWinsock();
    // 创建一个UDP套接字
    SOCKET clientSocket = createUDPSocket();

    char serverIp[16];
    int serverPort = 9999;
    printf("请输入传输对象的ip地址:(默认连接9999端口)\n");
    scanf("%s", serverIp);
    // printf("请输入端口号\n");
    // scanf("%d", &serverPort);

    printf("三次握手建立连接中...\n");

    // 设置服务器地址
    struct sockaddr_in serverAddr = setServerAddr(AF_INET, serverIp, serverPort);

    // 为客户端套接字设置接收数据的超时时间
    int timeout = TIMEOUT;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    uint32_t seqnum = rand();
    // 建立连接
    establishConnection(clientSocket, &serverAddr, &seqnum);

    printf("连接已建立.\n");

    printf("请选择要发送的文件: (仅可以选择同文件夹下的文件)\n");
    char filename[MAX_PATH];
    if (!OpenFileDialog(filename, sizeof(filename)))
        printf("文件打开失败\n");
    // char filename[256] = "v1.mp4";
    
    FILE *file = fopen(filename, "rb"); // Open the file
    if (file == NULL)
    {
        printf("Failed to open file.\n");
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    uint32_t fileSize = getFileSize(file);

    // 首先发送文件信息
    sendFileInfo(clientSocket, &serverAddr, filename, fileSize, &seqnum);

    sendData(clientSocket, &serverAddr, file, fileSize, &seqnum);

    printf("文件传输完毕\n");

    fclose(file);

    printf("四次挥手关闭连接\n");
    closeConnection(clientSocket, &serverAddr, &seqnum);

    printf("连接已关闭\n");

    // 对与该套接字关联的所有资源进行释放
    closesocket(clientSocket);
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

struct sockaddr_in setServerAddr(int sin_family, char *serverIp, int serverPort)
{
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(serverIp);
    serverAddr.sin_port = htons(serverPort);
    return serverAddr;
}

uint32_t getFileSize(FILE *file)
{
    fseek(file, 0, SEEK_END); // 将文件位置指针移动到文件的末尾
    uint32_t fileSize = ftell(file); // 获取文件的大小
    fseek(file, 0, SEEK_SET); // 将文件位置指针移动到文件的开头
    return fileSize;
}

void establishConnection(SOCKET socket, struct sockaddr_in *serverAddr, uint32_t *clientSeqNum)
{
    Packet packet;
    uint32_t serverSeqNum = 0;
    int retries = 0;

    while (retries < MAX_RETRIES)
    {
        // Send SYN to server
        memset(&packet, 0, sizeof(Packet));
        packet.seq_num = *clientSeqNum;
        packet.flags = SYN;
        sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)serverAddr, sizeof(*serverAddr));
        printf("Sent SYN: seq_num = %u\n", packet.seq_num);

        // Receive SYN-ACK from server
        int serverAddrLen = sizeof(*serverAddr);
        int recvLen = recvfrom(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)serverAddr, &serverAddrLen);
        // ACK正确
        if (recvLen != SOCKET_ERROR && (packet.flags & ACK) && (packet.flags & SYN) && packet.ack_num == *clientSeqNum + 1)
        {
            printf("Received SYN-ACK: seq_num = %u, ack_num = %u\n", packet.seq_num, packet.ack_num);
            serverSeqNum = packet.seq_num;
            // Send ACK to server
            memset(&packet, 0, sizeof(Packet));
            *clientSeqNum = *clientSeqNum + 1;
            packet.seq_num = *clientSeqNum;
            packet.ack_num = serverSeqNum + 1;
            packet.flags = ACK;
            sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)serverAddr, sizeof(*serverAddr));
            printf("Sent ACK: seq_num = %u, ack_num = %u\n", packet.seq_num, packet.ack_num);
            break;
        } // 超时
        else if (recvLen == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT)
        {
            retries++;
            printf("超时,第%d次重试\n", retries);
        } // 其他错误
        else
        {
            printf("第%d次重试, error: %d\n", retries, WSAGetLastError());
        }
    }
    if (retries >= MAX_RETRIES)
    {
        printf("超时%d次,程序退出\n", MAX_RETRIES);
        closesocket(socket);
        WSACleanup();
        exit(1);
    }
}

void sendFileInfo(SOCKET socket, struct sockaddr_in *serverAddr, const char *fileName, uint32_t fileSize, uint32_t *clientSeqNum)
{
    Packet packet;
    *clientSeqNum = *clientSeqNum + 1;
    int serverAddrLen = sizeof(*serverAddr);
    int retries = 0;

    memset(&packet, 0, sizeof(Packet));
    packet.seq_num = *clientSeqNum;
    packet.flags = FILE_INFO;
    snprintf(packet.payload, BUFFER_SIZE, "%s|%u", fileName, fileSize);
    packet.length = strlen(packet.payload) + 1;
    packet.checksum = calculateChecksum(&packet);

    while (retries < MAX_RETRIES)
    {
        sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)serverAddr, serverAddrLen);
        printf("Sent file info: %s, size = %u\n", fileName, fileSize);
        int recvLen = recvfrom(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)serverAddr, &serverAddrLen);
        if (recvLen != SOCKET_ERROR && (packet.flags & ACK) && packet.ack_num == *clientSeqNum + 1)
        {// ACK正确
            printf("Received ACK for file info: ack_num=%u\n", packet.ack_num);
            break;
        } 
        else if (recvLen == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT)
        {// 超时
            retries++;
            printf("Resending file info... (%d/%d)\n", retries, MAX_RETRIES);
        } 
        else
        {// 其他错误
            printf("Failed to receive ACK for file info, error: %d\n", WSAGetLastError());
        }
    }
    if (retries >= MAX_RETRIES)
    {
        printf("Failed to send file info after %d retries. Exiting...\n", MAX_RETRIES);
        closesocket(socket);
        WSACleanup();
        exit(1);
    }
}

void sendData(SOCKET socket, struct sockaddr_in *serverAddr, FILE *file, uint32_t filesize, uint32_t *clientSeqNum)
{
    Packet packet;
    uint32_t seqNum = *clientSeqNum + 1;
    int serverAddrLen = sizeof(*serverAddr);
    char buffer[BUFFER_SIZE];
    int bytesRead;
    int retries = 0;
    uint32_t TotalSendFileSize = 0; //已经发送文件的总大小

    while ((bytesRead = fread(buffer, 1, BUFFER_SIZE, file)) > 0)
    {
        memset(&packet, 0, sizeof(Packet));
        packet.seq_num = seqNum;
        packet.flags = DATA;
        memcpy(packet.payload, buffer, bytesRead);
        packet.length = bytesRead;
        packet.checksum = calculateChecksum(&packet);

        sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)serverAddr, serverAddrLen);
        // printf("Sent DATA: seq_num = %u\n", packet.seq_num);

        while (1)
        {
            int recvLen = recvfrom(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)serverAddr, &serverAddrLen);

            if (recvLen == SOCKET_ERROR)
            {
                if (WSAGetLastError() == WSAETIMEDOUT)
                { //超时
                    retries++;
                    if (retries > MAX_RETRIES)
                    {
                        printf("连接超时...退出...\n");
                        return;
                    }
                    sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)serverAddr, serverAddrLen);
                    printf("(%d/%d) Resent DATA: seq_num = %u\n", retries, MAX_RETRIES, packet.seq_num);
                }
                else
                {
                    printf("Failed to receive ACK for file info, error: %d\n", WSAGetLastError());
                    return;
                }
            }
            else
            { // 正确接收报文
                if (packet.flags & ACK)
                { 
                    if (packet.ack_num != seqNum + 1)
                    {
                        // 丢弃错误ACK，继续等待
                        printf("ERROR Received ACk :ack_num = %u", packet.ack_num);
                    }
                    else
                    {
                        // 接收正确ACK，继续发送
                        // printf("SUCCESS Received ACK: ack_num = %u\n", packet.ack_num);
                        seqNum++;
                        retries = 0;
                        TotalSendFileSize += bytesRead;
                        printf("进度：%.2f%%\n", TotalSendFileSize / (float)filesize * 100);
                        break;
                    }
                }
            }
        }
    }
}

void closeConnection(SOCKET socket, struct sockaddr_in *serverAddr, uint32_t *clientSeqNum)
{
    Packet packet;
    int serverAddrLen = sizeof(*serverAddr);
    int retries = 0;

    while (retries < MAX_RETRIES)
    {
        // 发送FIN
        memset(&packet, 0, sizeof(Packet));
        packet.seq_num = *clientSeqNum + 1;
        *clientSeqNum = *clientSeqNum + 1;
        packet.flags = FIN;
        sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)serverAddr, serverAddrLen);
        printf("Sent FIN: seq_num = %u\n", packet.seq_num);

        // 接收ACK
        int recvLen = recvfrom(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)serverAddr, &serverAddrLen);
        if (recvLen != SOCKET_ERROR && (packet.flags & ACK) && packet.ack_num == *clientSeqNum + 1)
        {
            printf("Received ACK for FIN :seq_num = %u ,ack_num = %u\n", packet.seq_num, packet.ack_num);
            // *clientSeqNum += 2;
            break;
        }
        else if (recvLen == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT)
        { // timeout
            retries++;
            printf("(%d/%d)Resending FIN...\n", retries, MAX_RETRIES);
        }
        else
        {
            printf("Failed to receive ACK for FIN, error: %d\n", WSAGetLastError());
        }
    }
    // 超时多次
    if (retries >= MAX_RETRIES)
    {
        printf("Failed to send FIN after %d retries. Exiting...\n", MAX_RETRIES);
        closesocket(socket);
        WSACleanup();
        exit(1);
    }

    retries = 0;
    while (retries < MAX_RETRIES)
    {
        // 接收FIN
        int recvLen = recvfrom(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)serverAddr, &serverAddrLen);
        if (recvLen != SOCKET_ERROR && (packet.flags & FIN) && (packet.flags & ACK))
        {
            printf("Received FIN-ACK from server: seq_num = %u,ack_num = %u\n", packet.seq_num, packet.ack_num);
            // 发送ACK
            uint32_t temp = packet.seq_num;
            packet.seq_num = packet.ack_num;
            packet.ack_num = temp + 1;
            packet.flags = ACK;
            sendto(socket, (char *)&packet, sizeof(Packet), 0, (struct sockaddr *)serverAddr, serverAddrLen);
            printf("Sent ACK for server FIN: seq_num = %u, ack_num = %u\n", packet.seq_num, packet.ack_num);
            break;
        }
        else if (recvLen == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT)
        {   //超时
            retries++;
            printf("Waiting for server FIN... (%d/%d)\n", retries, MAX_RETRIES);
        }
        else
        {
            printf("Failed to receive FIN from server, error: %d\n", WSAGetLastError());
        }
    }
    // 超时多次
    if (retries >= MAX_RETRIES)
    {
        printf("Failed to receive FIN from server after %d retries. Exiting...\n", MAX_RETRIES);
        closesocket(socket);
        WSACleanup();
        exit(1);
    }
}

uint16_t calculateChecksum(Packet *packet)
{
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
