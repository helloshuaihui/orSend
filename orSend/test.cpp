#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <cstring>
#include <atomic>
#include <mutex>

// ���� Winsock ��
#pragma comment(lib, "ws2_32.lib")

// ����������
const uint16_t SERVER_PORT = 8888;       // �����˿�
const int MAX_BUFFER_SIZE = 1024;        // ���ջ�������С
const int MAX_CLIENT_COUNT = 10;         // ���ͻ���������select ֧�ֵ� fd_set ��С�����ƣ�

class SelectTcpServer {
public:
    SelectTcpServer() : m_listenSock(INVALID_SOCKET), m_isRunning(false) {}

    ~SelectTcpServer() {
        Stop();
    }

    // ����������
    bool Start() {
        // 1. ��ʼ�� Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup ʧ�ܣ������룺" << WSAGetLastError() << std::endl;
            return false;
        }

        // 2. ���������׽��֣�TCP��
        m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSock == INVALID_SOCKET) {
            std::cerr << "���������׽���ʧ�ܣ������룺" << WSAGetLastError() << std::endl;
            WSACleanup();
            return false;
        }

        // 3. ���õ�ַ���ã�����˿�ռ�ã�
        int opt = 1;
        if (setsockopt(m_listenSock, SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<const char*>(&opt), sizeof(opt)) == SOCKET_ERROR) {
            std::cerr << "setsockopt ʧ�ܣ������룺" << WSAGetLastError() << std::endl;
            Cleanup();
            return false;
        }

        // 4. �󶨶˿�
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;  // �������б��� IP
        serverAddr.sin_port = htons(SERVER_PORT); // ת��Ϊ�����ֽ���

        if (bind(m_listenSock, reinterpret_cast<SOCKADDR*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "�󶨶˿�ʧ�ܣ������룺" << WSAGetLastError() << std::endl;
            Cleanup();
            return false;
        }

        // 5. ��ʼ���������ȴ����� 5��
        if (listen(m_listenSock, 5) == SOCKET_ERROR) {
            std::cerr << "����ʧ�ܣ������룺" << WSAGetLastError() << std::endl;
            Cleanup();
            return false;
        }

        m_isRunning = true;
        std::cout << "�����������ɹ��������˿ڣ�" << SERVER_PORT << "���� Ctrl+C ֹͣ��" << std::endl;

        // 6. �����¼�ѭ��
        EventLoop();
        return true;
    }

    // ֹͣ������
    void Stop() {
        if (!m_isRunning) return;

        m_isRunning = false;
        std::cout << "\n����������ֹͣ..." << std::endl;

        // �رռ����׽���
        if (m_listenSock != INVALID_SOCKET) {
            closesocket(m_listenSock);
            m_listenSock = INVALID_SOCKET;
        }

        // �ر����пͻ����׽���
        std::lock_guard<std::mutex> lock(m_clientMutex);
        for (SOCKET clientSock : m_clientSocks) {
            if (clientSock != INVALID_SOCKET) {
                closesocket(clientSock);
            }
        }
        m_clientSocks.clear();

        // ���� Winsock
        WSACleanup();
        std::cout << "��������ֹͣ" << std::endl;
    }

private:
    // �¼�ѭ���������߼���
    void EventLoop() {
        while (m_isRunning) {
            // ��ʼ���׽��ּ���
            fd_set readSet;
            FD_ZERO(&readSet);

            // �������׽��ּ�������ϣ���������ӣ�
            FD_SET(m_listenSock, &readSet);

            // �����пͻ����׽��ּ�������ϣ�������ݵ��
            std::lock_guard<std::mutex> lock(m_clientMutex);
            for (SOCKET clientSock : m_clientSocks) {
                FD_SET(clientSock, &readSet);
            }

            // ���ó�ʱ��NULL ��ʾ���޵ȴ���
            timeval timeout{};
            timeout.tv_sec = 1;  // 1�볬ʱ���������������������˳���
            timeout.tv_usec = 0;

            // ȷ������׽��־����select ��һ��������Ҫ��
            SOCKET maxSock = m_listenSock;
            for (SOCKET clientSock : m_clientSocks) {
                if (clientSock > maxSock) {
                    maxSock = clientSock;
                }
            }

            // ���� select �ȴ��¼���Windows �µ�һ�������� +1��
            int ret = select(static_cast<int>(maxSock) + 1, &readSet, nullptr, nullptr, &timeout);
            if (ret == SOCKET_ERROR) {
                std::cerr << "select ʧ�ܣ������룺" << WSAGetLastError() << std::endl;
                continue;
            }
            else if (ret == 0) {
                // ��ʱ������ѭ�������ڼ�� m_isRunning ״̬��
                continue;
            }

            // ��������׽����¼��������ӣ�
            if (FD_ISSET(m_listenSock, &readSet)) {
                HandleNewConnection();
                ret--;  // ����ʣ���¼�����
                if (ret == 0) continue;  // �������¼�������ѭ��
            }

            // ����ͻ����׽����¼������ݵ����Ͽ���
            HandleClientEvents(readSet);
        }
    }

    // ����������
    void HandleNewConnection() {
        sockaddr_in clientAddr{};
        int clientAddrLen = sizeof(clientAddr);

        // ����������
        SOCKET clientSock = accept(m_listenSock,
            reinterpret_cast<SOCKADDR*>(&clientAddr),
            &clientAddrLen);
        if (clientSock == INVALID_SOCKET) {
            std::cerr << "��������ʧ�ܣ������룺" << WSAGetLastError() << std::endl;
            return;
        }

        // ����Ƿ񳬹����ͻ�������
        std::lock_guard<std::mutex> lock(m_clientMutex);
        if (m_clientSocks.size() >= MAX_CLIENT_COUNT) {
            std::cerr << "�ͻ��������Ѵ����ޣ��ܾ�����" << std::endl;
            closesocket(clientSock);
            return;
        }

        // ��ȡ�ͻ��� IP �Ͷ˿�
        char clientIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
        uint16_t clientPort = ntohs(clientAddr.sin_port);

        // ��ӵ��ͻ����б�
        m_clientSocks.push_back(clientSock);
        std::cout << "�¿ͻ������ӣ�" << clientIp << ":" << clientPort
            << "����ǰ��������" << m_clientSocks.size() << "��" << std::endl;
    }

    // ����ͻ����¼������ݵ����Ͽ���
    void HandleClientEvents(const fd_set& readSet) {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        // ʹ�õ���������������ɾ���Ͽ�������
        for (auto it = m_clientSocks.begin(); it != m_clientSocks.end();) {
            SOCKET clientSock = *it;

            // ��鵱ǰ�ͻ����Ƿ����¼�
            if (FD_ISSET(clientSock, &readSet)) {
                char buffer[MAX_BUFFER_SIZE];
                int recvLen = recv(clientSock, buffer, MAX_BUFFER_SIZE - 1, 0);

                if (recvLen > 0) {
                    // �������ݳɹ�
                    buffer[recvLen] = '\0';
                    std::cout << "�յ��ͻ������ݣ�sock=" << clientSock << "����" << buffer << std::endl;

                    // ʾ�����ظ��ͻ���
                    std::string response = "���������յ���" + std::string(buffer);
                    send(clientSock, response.c_str(), response.length(), 0);
                }
                else {
                    // ���ӶϿ������
                    if (recvLen == 0) {
                        std::cout << "�ͻ��˶Ͽ����ӣ�sock=" << clientSock << "��" << std::endl;
                    }
                    else {
                        std::cerr << "��������ʧ�ܣ�sock=" << clientSock << "���������룺" << WSAGetLastError() << std::endl;
                    }

                    // �ر��׽��ֲ����б����Ƴ�
                    closesocket(clientSock);
                    
                    it = m_clientSocks.erase(it);  // ����������
                    std::cout << "��ǰ��������" << m_clientSocks.size() << std::endl;
                    continue;  // ������������
                }
            }
            ++it;  // ����������
        }
    }

    // ������Դ
    void Cleanup() {
        if (m_listenSock != INVALID_SOCKET) {
            closesocket(m_listenSock);
            m_listenSock = INVALID_SOCKET;
        }
        WSACleanup();
    }

private:
    SOCKET m_listenSock;                     // �����׽���
    std::atomic<bool> m_isRunning;           // ����������״̬���̰߳�ȫ��
    std::vector<SOCKET> m_clientSocks;       // �ͻ����׽����б�
    std::mutex m_clientMutex;                // �ͻ����б��������̰߳�ȫ��
};

int a() {
    SelectTcpServer server;
    if (!server.Start()) {
        std::cerr << "����������ʧ��" << std::endl;
        return 1;
    }
    return 0;
}