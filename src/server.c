#include "chat.h"

#include <sys/shm.h>  //share memory
#include <unistd.h>	  //provides access to the POSIX operating system API  Unix-idiom
#include <signal.h>	  //ctrl c
#include <sys/wait.h> //wait();
#include <sys/sem.h>  //Semaphore operation flags:  semid_ds
#include <errno.h>

#define MAXMSG 500	//最大消息数
#define MAXUSER 500 //用户最大数量
#define RW 0		//读写进程对共享内存区的信号量 初始值：1
#define MUTEX 1		//计数器信号量                初始值：1
#define W 2			//为了写进程优先设置的信号量   初始值：1
#define COUNT 3		//读进程数量                  初始值：0
#define FILESEM 4	//文件访问信号量              初始值：1

union semun
{ //信号量处理必需的共用体
	int val;
	struct semid_ds *buf; // The semid_ds structure defines a semaphore set associated with a semaphore ID.  There is one semaphore set per semaphore ID.
	unsigned short *array;
};

typedef struct shared_space
{
	int length;				 //消息的数量，条数
	Message message[MAXMSG]; //消息，最多MAXMSG条
} Space;					 //共享区的全部内容

Space *space;		//共享区内存
int client_socket;	//客户端套接字
int server_socket;	//服务器套接字
char client_ip[20]; //客户端IP的字符串表示
int shmid;			//共享内存区(多人聊天)标识符(ID)
int semid;			//信号量标识符(ID)
int online_users[MAXUSER];	//在线用户  [c没有map用什么存呢？]

//初始化套接字，传入端口和地址，自动生成一个套接字并关联地址，监听。返回套接字。若失败，返回-1
int init_socket(int port, int addr);
//子进程对客户端的具体处理，使用多线程，用两个线程分别监控客户端的输入和输出到客户端
void do_server();
//接收客户端发来的字符串，存入内存区，并更新内存区序号，写互斥
void read_from();
//轮询内存区，若发现有新消息，则将新消息发送给客户端
void write_to();
//从客户端读取ID，返回此ID之后的未读消息
void get_unread();
//处理用户注册请求
int client_register(User user);
//处理用户登录请求
int client_login(User user);
//处理用户修改密码请求
int client_modify(User user);


//退出处理函数，捕获SIGINT信号。关闭信号量和共享内存区，保存历史聊天记录
void exit_func(int signal);
//子进程退出处理函数，捕获SIGCHIL信号，防止出现僵尸进程
void wait_child(int signal);
//设置信号量集中五个信号量的初值，失败返回-1
int init_sem(int rw, int mutex, int w, int count, int file);
//P操作，将信号量集合中对应类型的信号量的数量-1，失败返回-1
int P(int type);
//V操作，将信号量集合中对应类型的信号量的数量+1，失败返回-1
int V(int type);
//设置信号量type的值为value，失败返回-1
int sem_setval(int type, int value);

//读取历史聊天记录，储存到全局共享内存区中。若第一次开启服务器，无历史聊天记录文件，则不做处理
int load_msg_history();

//************************主体功能实现部分*********************************************************/
int init_socket(int port, int addr)//*输入端口号和ip地址，初始化一个套接字并返回套接字fd
{
	struct sockaddr_in server_addr; //服务器地址结构
	int sockfd;
	if((sockfd=socket(AF_INET,SOCK_STREAM,0))==-1){
		
		printf("%s",strerror(errno));
		perror("socket error\n");
		return -1;
	}
	
	
	/*
	hton 网络字节序：4个字节的32 bit值以下面的次序传输：首先是0～7bit，其次8～15bit，然后16～23bit，最后是24~31bit。这种传输次序称作大端字节序。由于TCP/IP首部中所有的二进制整数在网络中传输时都要求以这种次序，因此它又称作网络字节序。字节序，顾名思义字节的顺序，就是大于一个字节类型的数据在内存中的存放顺序，一个字节的数据没有顺序的问题了。
所以：在将一个地址绑定到socket的时候，请先将主机字节序转换成为网络字节序，而不要假定主机字节序跟网络字节序一样使用的是Big-Endian。
	*/
	server_addr.sin_port = htons(port);
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(addr);
	//套接字关联地址
	if ((bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr))) != 0){
		printf("%s\n",strerror(errno));
		perror("bind error\n");
		return -1;
	}
	if ((listen(sockfd, 5)) != 0){ //设置最大监听数并监听
		printf("%s\n",strerror(errno));
		perror("listen error\n");
		return -1;
	}

	return sockfd;
}
//接受客户端发来的消息
void read_from()
{
	int readfd;
	char str[141];
	int msglength;
	Packet packet;
	Kind kind;
	Data data;

	while(1){
		if((readfd=read(client_socket,&packet,sizeof(Packet)))==-1){
			printf("Client %s reads error!\n",client_ip);
			return;
		}
		else
		{
			parse_packet(packet, &kind, &data);

			if (kind == enum_chat)
			{
				/*写内存区*/
				P(W);								//在无写进程时进入
				P(RW);								//互斥访问共享内存区
													//超过500条则覆盖前面的，写的位置永远是space->length%500
				msglength = space->length % MAXMSG; //更新内存区

				space->message[msglength].id = space->length;
				strcpy(space->message[msglength].str, data.message.str);

				space->length++;
				printf("Client\"%s\" has writed the message %d.\n", client_ip, msglength);

				V(RW); //释放共享内存区信号量
				V(W);  //可以让下个写进程进入
			}
			else if (kind == enum_logout)
			{
				printf("Client\"%s\" signs out!\n", client_ip);
				return;
			}
			else
			{
				printf("the type of the packet reveived is error!\n");
				return;
			}
		}
	}
}
//发送给客户端
void write_to()
{
	int msglength;
	msglength = space->length;
	int count;
	Packet packet;
	//*      刚把msglength定义为space->length 怎么会小于呢？	*/
	//*因为该功能是阻塞在这里的，函数刚运行时相等，在运行期间轮寻等待是否有新消息
	while (1)
	{
		if (msglength < space->length)
		{ //有新消息，互斥访问共享内存
			/*读内存区*/
			P(W);	  //无写进程等待进入时
			P(MUTEX); //对计数器加锁
			if ((count = semctl(semid, COUNT, GETVAL)) == 0)
				P(RW);								//如果count=0，即第一个读进程进入，则对共享内存加锁
			if (sem_setval(COUNT, count + 1) == -1) //如果不是第一个进入，则表示已经对共享内存加锁了
				printf("semaphore set value failed!\n");
			V(MUTEX); //对计数器访问完毕，释放计算器信号量
			V(W);	  //释放W信号量，写进程可以进了

			for (; msglength < space->length; msglength++)
			{ //读取新消息
				if (build_packet(&packet, enum_chat, space->message[msglength % MAXMSG]) == -1)
				{
					printf("fail to build the packet!\n");
					return;
				}
				write(client_socket, &packet, sizeof(Packet));
			}

			P(MUTEX);							  //对计数器加锁
			count = semctl(semid, COUNT, GETVAL);
			if (sem_setval(COUNT, count - 1) == -1) //读进程访问完毕，计数器减1
				printf("semaphore set value failed!\n");
			if (semctl(semid, COUNT, GETVAL) == 0) //如果是最后一个读进程，则要将共享内存区的锁解开，方便写进程进入
				V(RW);
			V(MUTEX); //计数器访问完毕，释放信号量
		}
		sleep(1); //每秒轮询一次
	}
}

void get_unread()//从客户端中读取该客户的消息界面所获得的最后一条消息的ID将该ID之后的消息发送给该客户端
{
	char lastid[10];
	int fromid;
	int temp;
	Packet packet;

	read(client_socket, lastid, 10); //客户端连接成功后发送一个ID，返回此ID以后的聊天记录
	for (fromid = atoi(lastid) + 1, temp = fromid; fromid < space->length; fromid++)
	{
		if (build_packet(&packet, enum_chat, space->message[fromid % MAXMSG]) == -1)
		{
			printf("fail to build the packet!\n");
			return;
		}
		write(client_socket, &packet, sizeof(packet));
	}
	if (temp < fromid)
		printf("Client\"%s\" has obtained the unread message between %d to %d.\n", client_ip, temp, fromid - 1);
}

int client_register(User user)
{
	
	int usernum;
	User userinfo[MAXUSER];
	Packet packet;

	//打开用户数据文件
	int fd;
	fd = open("userinfo.dat", O_RDWR | O_CREAT, 0660);
	if (fd == -1)
	{
		printf("file \"userinfo.dat\" opened failed!\n");
		return -1;
	}
	P(FILESEM);

	//
	int i;//读取用户数据文件标识符
	i = read(fd, &usernum, sizeof(int)); //文件开头是用户数量，接下来是若干个用户的帐号密码信息
	if (i == 0)
	{ //如果读取失败，则表示该文件第一次打开，没有信息
		usernum = 1;
		write(fd, &usernum, sizeof(int)); //写入1，表示用户数量数为1
		write(fd, &user, sizeof(User));	  //将用户结构体直接写入文件
		//将注册用户该事件打包，将包发送给客户端，表示注册成功
		if (build_packet(&packet, enum_regist, user) == -1)
		{
			printf("fail to build the packet!\n");
			return -1;
		}
		write(client_socket, &packet, sizeof(Packet));
		printf("Client\"%s\" regists succeed with the account \"%s\".\n", client_ip, user.account);
	}
	else
	{//如果用户数据文件中已有用户信息，则将已有用户信息读如fd并检该用户是否已注册
		read(fd, userinfo, MAXUSER * sizeof(User));
		for (i = 0; i < usernum; i++)
		{
			if (!strcmp(userinfo[i].account, user.account)) //在用户列表中找到该用户，说明已注册
			{
				strcpy(user.account, "");
				if (build_packet(&packet, enum_regist, user) == -1)//user.account为空为什么封包就会失败了？
				{
					printf("fail to build the packet!\n");
					return -1;
				}
				write(client_socket, &packet, sizeof(Packet)); //给客户端发送包回应，表示注册失败
				printf("Client\"%s\" regists failed with the repeting account.\n", client_ip);
				close(fd);
				V(FILESEM);
				return -1;
			}
		}

		//没有该用户信息，可以注册
		usernum++;								   //跳出循环，表示可以注册该用户
		strcpy(userinfo[i].account, user.account); //将帐号密码写入用户数组
		strcpy(userinfo[i].password, user.password);

		lseek(fd, 0, SEEK_SET);//SEEK_SET 参数offset 即为新的读写位置.
		write(fd, &usernum, sizeof(int)); //将用户数组和长度写入文件
		write(fd, userinfo, sizeof(User) * MAXUSER);

		if (build_packet(&packet, enum_regist, user) == -1)
		{
			printf("fail to build the packet!\n");
			return -1;
		}
		write(client_socket, &packet, sizeof(Packet));
		printf("Client\"%s\" regists succeed with the account \"%s\".\n", client_ip, user.account);
	}
	close(fd);
	V(FILESEM);
	return 0;
}

int client_modify(User user)
{
	int fd;
	int usernum;
	User userinfo[MAXUSER];
	int i;
	Packet packet;

	fd = open("userinfo.dat", O_RDWR | O_CREAT, 0660);
	if (fd == -1)
	{
		printf("file \"userinfo.dat\" opened failed!\n");
		return -1;
	}

	P(FILESEM);

	i = read(fd, &usernum, sizeof(int));
	if (i == 0)
	{ //文件第一次打开，没有用户注册，无法修改密码
		strcpy(user.account, "");
		if (build_packet(&packet, enum_modify, user) == -1)
		{
			printf("fail to build the packet!\n");
			return -1;
		}
		write(client_socket, &packet, sizeof(Packet)); //发送包给客户端，表示无法修改密码
		printf("Client\"%s\" modifies failed with no account.\n", client_ip);
	}
	else
	{
		read(fd, userinfo, MAXUSER * sizeof(User));
		for (i = 0; i < usernum; i++)
		{
			if (!strcmp(userinfo[i].account, user.account) && !strcmp(userinfo[i].password, user.password))
			{
				if (build_packet(&packet, enum_modify, user) == -1)
				{
					printf("fail to build the packet!\n");
					return -1;
				}
				write(client_socket, &packet, sizeof(Packet)); //发送包给客户端，表示允许修改密码  发送空包？
				read(client_socket, &packet, sizeof(Packet));  //等待客户端发送新的密码
				strcpy(userinfo[i].password, packet.data.userinfo.password);

				lseek(fd, 0, SEEK_SET);
				write(fd, &usernum, sizeof(int));
				write(fd, userinfo, sizeof(User) * MAXUSER);

				if (build_packet(&packet, enum_modify, user) == -1)
				{
					printf("fail to build the packet!\n");
					return -1;
				}
				write(client_socket, &packet, sizeof(Packet));//把修改完密码的包发回去
				printf("Client\"%s\" modifies succeed with the account \"%s\".\n", client_ip, user.account);
				close(fd);
				V(FILESEM);
				return 0;//修改成功在此返回
			}
		}
		//修改失败执行下面代码
		strcpy(user.account, ""); //找不到帐号和密码匹配的用户，修改密码失败
		if (build_packet(&packet, enum_modify, user) == -1)
		{
			printf("fail to build the packet!\n");
			return -1;
		}
		write(client_socket, &packet, sizeof(Packet));
		printf("Client\"%s\" modifies failed with no account.\n", client_ip);
	}

	close(fd);
	V(FILESEM);
	return -1;
}

int client_login(User user)
{
	User userinfo[MAXUSER];
	Packet packet;
	int usernum;
	
	//
	int fd;
	fd = open("userinfo.dat", O_RDWR | O_CREAT, 0660);
	if (fd == -1)
	{
		printf("file \"userinfo.dat\" opened failed!\n");
		return -1;
	}

	P(FILESEM);
	int i;
	i = read(fd, &usernum, sizeof(int));
	if (i == 0)
	{ //文件第一次打开，没有用户注册，无法登录
		strcpy(user.account, "");
		if (build_packet(&packet, enum_login, user) == -1)
		{
			printf("fail to build the packet!\n");
			return -1;
		}
		write(client_socket, &packet, sizeof(Packet)); //发送包给客户端，表示登录失败
		printf("Client\"%s\" logins failed with no account.\n", client_ip);
	}
	else
	{//比对用户信息找到要更改的用户
		read(fd, userinfo, MAXUSER * sizeof(User));
		for (i = 0; i < usernum; i++)
		{
			if (!strcmp(userinfo[i].account, user.account) && !strcmp(userinfo[i].password, user.password))//如果用户密码相等，也就是匹配上了
			{
				if (build_packet(&packet, enum_login, user) == -1)
				{
					printf("fail to build the packet!\n");
					return -1;
				}
				write(client_socket, &packet, sizeof(Packet)); //发送包给客户端，表示登录成功
				//*online_users[getpid()%500]=client_socket;
				printf("Client\"%s\" logins succeed with the account \"%s\".\n", client_ip, user.account);
				close(fd);
				V(FILESEM);
				return 0;
			}
		}
		strcpy(user.account, ""); //找不到帐号和密码匹配的用户，登录失败
		if (build_packet(&packet, enum_login, user) == -1)
		{
			printf("fail to build the packet!\n");
			return -1;
		}
		write(client_socket, &packet, sizeof(Packet));
		printf("Client\"%s\" logins failed with no account.\n", client_ip);
	}
	close(fd);
	V(FILESEM);
	return -1;
}
//**************多线程怎么搞
void do_server()
{
	pthread_t thIDr, thIDw;
	Packet packet;
	Kind kind;
	Data data;

	signal(SIGINT, SIG_DFL); //设置子进程Ctrl+C为系统默认处理

	read(client_socket, &packet, sizeof(Packet));
	parse_packet(packet, &kind, &data);
	switch (kind)
	{
	case enum_regist: //处理注册
		client_register(data.userinfo);
		return;
	case enum_modify: //处理修改密码
		client_modify(data.userinfo);
		return;
	case enum_login: //处理登录
		if (client_login(data.userinfo) == -1)
			return;
		break;
	default:
		printf("the type of the packet reveived is error!\n");
		return;
	}

	get_unread(); //获取未读的聊天记录

	pthread_create(&thIDr, NULL, (void *)read_from, NULL);
	pthread_create(&thIDw, NULL, (void *)write_to, NULL);
	pthread_join(thIDr, NULL);
}


//*********************************处理部分********************************************
void exitfunc(int signal)//关闭共享内存区，关闭信号量，保存记录，关闭套接字
{
	int fd;
	if (shmctl(shmid, IPC_RMID, 0) == -1) //关闭共享内存区
		printf("shared memory closed error!\n");
	if (semctl(semid, 0, IPC_RMID, 0) == -1) //关闭信号量
		printf("semaphore closed error!\n");
	fd = open("histmsg.dat", O_WRONLY | O_CREAT, 0660); //打开并保存历史聊天记录
	if (fd == -1)
	{
		printf("file \"histmsg.dat\" opened failed!\n");
	}
	else
	{
		int write_byte;
		write_byte = write(fd, space, sizeof(Space)); //将聊天记录存入文件
		if (write_byte != sizeof(Space))
		{
			printf("the length written is incorrect!\n");
		}
		else
		{
			printf("\nHistory message has stored successfully!\n");
			printf("Server exit!\n");
		}
	}
	if (close(server_socket) == -1) //关闭服务器套接字
		printf("server_socket closed error!\n");
	_exit(0);
}

void waitchild(int signal)
{
	wait(NULL);
}

//初始化信号量
int init_sem(int rw, int mutex, int w, int count, int file)
{
	union semun arg;
	int flag;
	arg.array = (unsigned short *)malloc(sizeof(unsigned short) * 5);
	arg.array[RW] = rw;					  //初值为1
	arg.array[MUTEX] = mutex;			  //初值为1
	arg.array[W] = w;					  //初值为1
	arg.array[COUNT] = count;			  //初值为0
	arg.array[FILESEM] = file;			  //初值为1
	flag = semctl(semid, 0, SETALL, arg); //给5个信号量赋初值
	free(arg.array);
	return flag;
}
int P(int type)
{
	struct sembuf buf;

	buf.sem_num = type;
	buf.sem_op = -1;
	buf.sem_flg = SEM_UNDO;

	return semop(semid, &buf, 1);
}
int V(int type)
{
	struct sembuf buf;

	buf.sem_num = type;
	buf.sem_op = 1;
	buf.sem_flg = SEM_UNDO;

	return semop(semid, &buf, 1);
}
int sem_setval(int type, int value)
{
	union semun arg;
	arg.val = value;
	return semctl(semid, type, SETVAL, arg);
}

//读入历史记录
int load_msg_history()
{
	int fd;
	int read_byte;
	fd = open("histmsg.dat", O_RDONLY);
	if (fd == -1)
		return -2; //文件不存在
	read_byte = read(fd, space, sizeof(Space));
	close(fd);
	if (read_byte == sizeof(Space))
		return 0; //读取成功
	else
		return -1; //文件存在但读取过程出错
}

/*******************************************************************/
int main()
{
	struct in_addr client_addr;
	int len;
	char *addr;
	signal(SIGINT, exitfunc);	//设置函数捕获并处理Ctrl+C按下时的信号
	signal(SIGCHLD, waitchild); //子进程退出后wait它防止出现僵尸进程
	server_socket = init_socket(MYPORT, INADDR_ANY);

	if((shmid = shmget(IPC_PRIVATE, sizeof(Space), IPC_CREAT | 0660))==-1){//创建一个共享内存区
		printf("shared memeoy created failed.\n");
		return -1;
	}
	
	if((space = (Space *)shmat(shmid, NULL, 0))==-1){//将space绑定到共享内存区
		printf("shared memeoy matched failed.\n");
		return -1;
	}
	
	if((semid = semget(IPC_PRIVATE, 5, IPC_CREAT | 0660))==-1){//创建一个有5个信号量的信号量集
		printf("semaphore created failed!\n");
		return -1;
	}
	
	if (init_sem(1, 1, 1, 0, 1) == -1){ //将5个信号量初始化值为1 1 1 0 1
		printf("semaphore initilize failed!\n");
		return -1;
	}

	//// len = load_msg_history(); //读取历史聊天记录
	//// if (len == 0)
	//// {
	//// 	printf("File \"histmsg.dat\" opened succeed!\n");
	//// 	printf("Server has loaded the data from the file!\n");
	///// }
	//// else if (len == -1)
	//// {
	//// 	printf("File \"histmsg.dat\" opened failed!\n");
	//// 	return -1;
	//// }
	//// else
	//// {
	//// 	printf("File \"histmsg.dat\" is not exist.\n");
	//// }

	printf("Wating for connecting......\n");
	while (1)
	{
		client_socket = accept(server_socket, NULL, NULL); //接收连接请求
		if (client_socket != -1)
		{
			len = sizeof(client_addr);
			getpeername(client_socket, (struct sockaddr *)&client_addr, &len);
			strcpy(client_ip, inet_ntoa(client_addr));

			printf("Connect succeed!\n");
			printf("Client ip:%s\n", client_ip);

			if (fork() == 0)
			{ //子进程进行具体处理
				do_server();
			}
			else
			{ //父进程关闭客户端套接字，继续监听
				close(client_socket);
				strcpy(client_ip, "");
			}
		}
		else{
			printf("%s",strerror(errno));
			printf("connect failed!\n");
			exit(1);
		}
	}
}
