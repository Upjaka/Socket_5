#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <thread>
#include <iomanip>

#pragma comment(lib, "ws2_32.lib")

// Глобальная переменная для адреса назначения
const char* DEST_ADDRESS = "192.168.0.100";

// Функция для расчета контрольной суммы ICMP-пакета
unsigned short calculateChecksum(unsigned short* buffer, int size) {
    unsigned long checksum = 0;
    while (size > 1) {
        checksum += *buffer++;
        size -= sizeof(unsigned short);
    }
    if (size) {
        checksum += *(unsigned char*)buffer;
    }
    checksum = (checksum >> 16) + (checksum & 0xFFFF);
    checksum += (checksum >> 16);
    return (unsigned short)(~checksum);
}

int main() {
    // Инициализация Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock\n";
        return 1;
    }

    while (true) {
        // Создание сокета для отправки ICMP-запросов
        SOCKET icmpSocket = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (icmpSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create socket\n";
            WSACleanup();
            return 1;
        }

        // Задание параметров для пинга
        sockaddr_in destAddr;
        destAddr.sin_family = AF_INET;
        inet_pton(AF_INET, DEST_ADDRESS, &destAddr.sin_addr); // Используем глобальную переменную

        // Создание ICMP-запроса
        const int ICMP_PACKET_SIZE = 32; // Размер пакета может быть изменен
        char icmpPacket[ICMP_PACKET_SIZE];
        memset(icmpPacket, 0, sizeof(icmpPacket));

        icmpPacket[0] = 0x08; // Тип сообщения: Echo Request
        icmpPacket[1] = 0x00; // Код: 0
        // Заполнение идентификатора и последовательного номера
        unsigned short* pChecksum = (unsigned short*)(icmpPacket + 2);
        *pChecksum = 0; // Временно установим контрольную сумму в 0
        *((unsigned short*)(icmpPacket + 4)) = htons(GetCurrentProcessId()); // Идентификатор - ID текущего процесса
        *((unsigned short*)(icmpPacket + 6)) = htons(1); // Последовательный номер - 1

        // Вычисление и установка контрольной суммы
        *pChecksum = calculateChecksum((unsigned short*)icmpPacket, ICMP_PACKET_SIZE);

        // Отправка ICMP-запроса и отслеживание времени
        auto startTime = std::chrono::high_resolution_clock::now(); // Время отправки запроса
        int bytesSent = sendto(icmpSocket, icmpPacket, ICMP_PACKET_SIZE, 0, (sockaddr*)&destAddr, sizeof(destAddr));
        if (bytesSent == SOCKET_ERROR) {
            std::cerr << "Failed to send ICMP packet\n";
            closesocket(icmpSocket);
            WSACleanup();
            return 1;
        }

        // Ожидание ответа в течение 1 секунды
        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(icmpSocket, &readSet);

        int ready = select(0, &readSet, NULL, NULL, &timeout);
        if (ready == SOCKET_ERROR) {
            std::cerr << "Error in select function\n";
            closesocket(icmpSocket);
            WSACleanup();
            return 1;
        }
        else if (ready == 0) {
            std::cout << "Request timed out\n";
        }
        else {
            // Получение эхо-ответа
            char recvBuffer[1024]; // Буфер для приема данных
            sockaddr_in fromAddr;
            int fromAddrLen = sizeof(fromAddr);
            int bytesRead = recvfrom(icmpSocket, recvBuffer, sizeof(recvBuffer), 0, (sockaddr*)&fromAddr, &fromAddrLen);
            if (bytesRead > 0) {
                auto endTime = std::chrono::high_resolution_clock::now(); // Время получения ответа
                std::chrono::duration<double, std::milli> elapsedTime = endTime - startTime;
                std::cout << "Received " << bytesRead << " bytes from " << inet_ntoa(fromAddr.sin_addr) << ": ";
                std::cout << "Time: " << std::fixed << std::setprecision(1) << elapsedTime.count() << " ms, ";
                std::cout << "Checksum: " << ntohs(*((unsigned short*)(recvBuffer + 22))) << ", ";
                std::cout << "Sequence Number: " << ntohs(*((unsigned short*)(recvBuffer + 26))) << ", ";
                std::cout << "TTL: " << (int)recvBuffer[8] << "\n";
            }
            else {
                std::cerr << "Failed to receive ICMP packet\n";
            }
        }

        // Закрытие сокета перед следующей итерацией
        closesocket(icmpSocket);

        // Задержка между отправкой запросов
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // WSACleanup вызывается только при завершении программы, поэтому здесь не нужно его вызывать
    return 0;
}
