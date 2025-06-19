#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <thread>
#include <chrono>

// 定义网络接口信息结构体
struct NetworkInterface {
    std::string name;       // 接口名称
    std::string ip;         // IPv4地址
    std::string mac;        // MAC地址
    bool has_internet;      // 是否可访问互联网
};

/**
 * @brief 获取所有网络接口信息（IPv4地址 + MAC地址）
 * @return 包含所有网络接口信息的vector
 */
std::vector<NetworkInterface> GetNetworkInfo() {
    std::vector<NetworkInterface> interfaces;
    struct ifaddrs *ifaddr, *ifa;

    // 获取网络接口列表
    if (getifaddrs(&ifaddr) == -1) {
        std::cerr << "Error: getifaddrs() failed (" << strerror(errno) << ")\n";
        return interfaces;
    }

    // 遍历所有接口
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        // 跳过无地址的接口和回环接口
        if (!ifa->ifa_addr || ifa->ifa_flags & IFF_LOOPBACK) continue;

        NetworkInterface iface;
        iface.name = ifa->ifa_name;

        // 获取IPv4地址
        if (ifa->ifa_addr->sa_family == AF_INET) {
            char ipBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET,
                      &reinterpret_cast<sockaddr_in*>(ifa->ifa_addr)->sin_addr,
                      ipBuffer, INET_ADDRSTRLEN);
            iface.ip = ipBuffer;
        }

        // 获取MAC地址（使用ioctl更可靠）
        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd >= 0) {
            struct ifreq ifr;
            strncpy(ifr.ifr_name, iface.name.c_str(), IFNAMSIZ-1);

            if (ioctl(sockfd, SIOCGIFHWADDR, &ifr) == 0) {
                unsigned char *mac =
                        reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
                char macBuffer[18];
                snprintf(macBuffer, sizeof(macBuffer),
                         "%02x:%02x:%02x:%02x:%02x:%02x",
                         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                iface.mac = macBuffer;
            }
            close(sockfd);
        }

        // 过滤无效数据
        if (!iface.ip.empty() && !iface.mac.empty()) {
            interfaces.push_back(iface);
        }
    }

    freeifaddrs(ifaddr);
    return interfaces;
}

/**
 * @brief 检测互联网连接状态
 * @param timeout 超时时间（秒）
 * @return true表示可访问互联网，false表示不可访问
 */
bool CheckInternetConnection(int timeout = 2) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Error: socket creation failed (" << strerror(errno) << ")\n";
        return false;
    }

    struct sockaddr_in servaddr{};
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(53);  // DNS端口

    // 尝试连接Google DNS（8.8.8.8）
    if (inet_pton(AF_INET, "8.8.8.8", &servaddr.sin_addr) <= 0) {
        std::cerr << "Error: invalid address\n";
        close(sockfd);
        return false;
    }

    // 设置非阻塞模式
    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

    // 发起连接
    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        if (errno != EINPROGRESS) {
            close(sockfd);
            return false;
        }
    }

    // 等待连接完成（带超时）
    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);

    struct timeval tv{timeout, 0};
    int ret = select(sockfd + 1, nullptr, &fdset, nullptr, &tv);

    // 获取连接结果
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);

    close(sockfd);
    return ret > 0 && error == 0;
}

int main() {
    // 获取网络接口信息
    auto interfaces = GetNetworkInfo();

    // 检测互联网连接（异步进行避免阻塞）
    bool hasInternet = false;
    auto checkThread = std::thread([&] {
        hasInternet = CheckInternetConnection();
    });
    checkThread.detach();  // 分离线程以便主线程继续执行

    // 打印网络接口信息
    std::cout << "Network Interfaces:\n";
    for (const auto& iface : interfaces) {
        std::cout << "Interface: " << iface.name << "\n"
                  << "  IPv4:    " << iface.ip << "\n"
                  << "  MAC:     " << iface.mac << "\n"
                  << "  --------\n";
    }

    // 等待连接检测结果（最多2秒）
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "\nInternet Access: "
              << (hasInternet ? "Available" : "Unavailable") << "\n";

    return 0;
}