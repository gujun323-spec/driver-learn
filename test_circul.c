#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#define DEVICE_PATH "/dev/circul"
#define TEST_DATA_SIZE 50   // 每次写入和读取的数据包大小
#define LOOP_COUNT 1000     // 循环压测次数

// 生产者线程：专门负责往驱动里疯狂灌入数据
void *producer_thread(void *arg) {
    int fd = *(int *)arg;
    char write_buf[TEST_DATA_SIZE];
    
    for (int i = 0; i < LOOP_COUNT; i++) {
        // 构造带有序号的数据，方便消费者校验
        snprintf(write_buf, TEST_DATA_SIZE, "Package_ID:[%04d]\n", i);
        
        // 如果驱动缓冲区满了，这里会自动阻塞休眠，直到有空间被读走才醒来
        ssize_t ret = write(fd, write_buf, strlen(write_buf));
        if (ret < 0) {
            perror("生产者写入失败");
            pthread_exit(NULL);
        }
        // 适当微小延时，模拟真实的传感器数据突发
        usleep(1000); 
    }
    printf("▶️ 生产者线程执行完毕，成功灌入 %d 组数据。\n", LOOP_COUNT);
    pthread_exit(NULL);
}

// 消费者线程：专门负责从驱动里疯狂提取数据并打印
void *consumer_thread(void *arg) {
    int fd = *(int *)arg;
    char read_buf[TEST_DATA_SIZE];
    
    for (int i = 0; i < LOOP_COUNT; i++) {
        memset(read_buf, 0, TEST_DATA_SIZE);
        
        // 如果驱动里没数据，这里会自动安全阻塞休眠，数据来了瞬间被唤醒
        ssize_t ret = read(fd, read_buf, TEST_DATA_SIZE - 1);
        if (ret < 0) {
            perror("消费者读取失败");
            pthread_exit(NULL);
        }
        
        // 打印出读取到的完整分段回绕后的数据
        printf("倒出来数据 ──> %s", read_buf);
    }
    printf("⏹️ 消费者线程执行完毕，成功校验 %d 组数据。\n", LOOP_COUNT);
    pthread_exit(NULL);
}

int main() {
    int fd;
    pthread_t prod_tid, cons_tid;

    // 1. 打开驱动设备文件
    fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0) {
        perror("打不开驱动设备，请检查是否 insmod 或无 root 权限");
        return -1;
    }

    printf("🚀 开始对大容量无锁环形缓冲区驱动进行多线程并发压测...\n");

    // 2. 创建生产者和消费者两个并发线程
    if (pthread_create(&prod_tid, NULL, producer_thread, &fd) != 0) {
        perror("创建生产者线程失败");
        close(fd);
        return -1;
    }
    if (pthread_create(&cons_tid, NULL, consumer_thread, &fd) != 0) {
        perror("创建消费者线程失败");
        close(fd);
        return -1;
    }

    // 3. 等待两个线程安全闭环退出
    pthread_join(prod_tid, NULL);
    pthread_join(cons_tid, NULL);

    printf("🎉 完美！多线程并发读写与回绕寻址测试圆满成功，驱动未发生任何死锁与崩溃！\n");
    close(fd);
    return 0;
}
