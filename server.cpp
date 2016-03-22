#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include"mysql.h"
#include"convert.h"
using namespace std;
using namespace google::protobuf::io;

class Server
{
	const static int MAXEPOLLSIZE = 10000;
	const static int BUFSIZE = 1024;
	const static int MAXCLIENT = 1024;
protected:
    int listenSocketfd;
    vector<int> clientSocketfd;
	vector<bool> isClient;

    string ip;
    int port;
    int waitqueLength;
    struct sockaddr_in serverAddress;

	int epollfd;
	struct epoll_event events[MAXEPOLLSIZE];	//事件集合
	int currentSizeOfEpoll;

	virtual int acceptConnectRequest()
	{
		struct sockaddr_in clientAddress;
		socklen_t len = sizeof(struct sockaddr_in);
		int newfd = accept(listenSocketfd,  (struct sockaddr *) &clientAddress, &len);
		if(newfd < 0)	
		{	
			perror("accept");
			return -1;
		}
		else
		{	//接受连接请求成功，发送“Hello”给客户端
			printf("Client %d From %s:%d is up\n", newfd, inet_ntoa(clientAddress.sin_addr), ntohs(clientAddress.sin_port));
			write(newfd, "Hello", 6);
			//将套接字句柄放入epoll集合
			setnonblocking(newfd);
			struct epoll_event ev;
			ev.events = EPOLLIN | EPOLLET;
			ev.data.fd = newfd;
			if (epoll_ctl(epollfd, EPOLL_CTL_ADD, newfd, &ev) < 0) 
			{
				fprintf(stderr, "epoll set insertion error: fd=%d\n", newfd);
				return -1;
			}
			else
			{
				clientSocketfd.push_back(newfd);
				isClient[newfd] = true;
			}
			currentSizeOfEpoll++;
			return 0;
		}
	}
	
	//设置句柄为非阻塞方式
	int setnonblocking(int fd) 
	{
		if (fcntl(fd, F_SETFL, 
				fcntl(fd, F_GETFD, 0)|O_NONBLOCK) == -1) 
		{
			return -1;
		}
		return 0;
	}

	//处理接收来自客户端的信息
	virtual int receive(int sockfd)
	{
		char buf[BUFSIZE+1];
		int len = -1;
		memset(&buf, 0, sizeof(buf));
	
		len = recv(sockfd, buf, BUFSIZE, 0);
		if(len > 0)		//若成功接收信息则将其打印
		{
			deal();
		}
		else if(len < 0)//若接收信息失败，打印错误信息并关闭连接
		{
			printf("ERROR%d(%s) from receive\n", errno, strerror(errno));
			close(sockfd);
			printf("client %d disconnected\n", sockfd);
			return -1;
		}
		return len;
	}

	int receiveMessage(int i)
	{
		struct epoll_event ev;
		int ret = receive(events[i].data.fd);
		if (ret < 1 && errno != 11) 
		{	//若接受消息失败，认为连接失效，删去epoll集合中本套接字句柄
			epoll_ctl(epollfd, EPOLL_CTL_DEL, events[i].data.fd,&ev);
			printf("remove Client %d", events[i].data.fd);
			for(int c = 0; c < clientSocketfd.size(); ++ c)
				if(clientSocketfd[c] == events[i].data.fd)
					clientSocketfd.erase(clientSocketfd.begin()+c);
			isClient[events[i].data.fd] = false;			
			currentSizeOfEpoll--;
			return -1;
		}
		return 0;
	}

	int setEpoll()
	{
		//设置epoll
		struct epoll_event ev;
		struct rlimit rt;
		//设置每个进程允许打开的最大文件数
		rt.rlim_max = rt.rlim_cur = MAXEPOLLSIZE;
		if (setrlimit(RLIMIT_NOFILE, &rt) == -1) 
		{
			perror("setrlimit");
			return -1;
		}
		//创建epoll句柄
		epollfd = epoll_create(MAXEPOLLSIZE);
		return 0;
	}

    int beginListen()
    {
        listenSocketfd = socket(AF_INET, SOCK_STREAM, 0);
        if(listenSocketfd < 0)
        {
            perror("socket");
            return -1;
        }
		setnonblocking(listenSocketfd);
		memset(&serverAddress, 0, sizeof(serverAddress));
		serverAddress.sin_family = PF_INET;
		serverAddress.sin_port = htons(port);
		serverAddress.sin_addr.s_addr = INADDR_ANY;	
		if(bind(listenSocketfd, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr)) == -1)
		{
			perror("bind");
			return -1;
		}
		if (listen(listenSocketfd, waitqueLength) == -1) {
			perror("listen");
			return -1;
		}
		//将监听socket放入epoll集合
		struct epoll_event ev;
		ev.events = EPOLLIN | EPOLLET;
		ev.data.fd = listenSocketfd;
		if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenSocketfd, &ev) < 0) 
		{
			fprintf(stderr, "epoll set insertion error: fd=%d\n", listenSocketfd);
			return -1;
		}
		currentSizeOfEpoll ++;
		return 0;
    }

public: 
	Server(int _waitqueLength = 10, string _ip = "", int _port = 9099)
    {
        this->waitqueLength = _waitqueLength;
        this->ip = _ip;
        this->port = _port;
		this->currentSizeOfEpoll = 0;
		isClient = vector<bool>(MAXCLIENT, false);
    }
	
	int inti()
	{
		setEpoll();
		beginListen();
	}	

	int process()
	{
		while(true)
		{
			//等待事件发生
			currentSizeOfEpoll = epoll_wait(epollfd, events, currentSizeOfEpoll, -1);
			if(currentSizeOfEpoll == -1)
			{
				perror("epoll_wait");
				break;
			}
			//处理所有事件
			for(int i = 0; i < currentSizeOfEpoll; ++ i)
			{
				//若为监听事件，则接收连接请求
				if(events[i].data.fd == listenSocketfd)
				{
					acceptConnectRequest();
				}
				else if(isClient[events[i].data.fd])//若为客户端发来消息，则调用receive函数接收消息
				{	
					receiveMessage(i);
				}
			}
		}
	}
};
