#include "chat.h"

int lastmessage;//收到的最后一条消息id
int client_socket; //客户端套接字
char user_account[20];//用户名
int flag=0;// 0 登录页面 1 多人聊天室 2 私聊

void Log_Fun(int fd,pthread_t thIDr, pthread_t thIDw,int log_flag);//登录入口
void Multi_Chat(int fd,pthread_t thIDr,pthread_t thIDw);//多人聊天室
void Private_Chat(int fd,pthread_t thIDr, pthread_t thIDw);//私聊
void printlogin();//打印登录界面
void get_user_info(Kind kind, Data *data);//从屏幕读取用户输入的账号密码储存在data中
int init_client(int port, char *addr);////为客户端分配套接字并连接到服务器，服务器地址和端口由形参传入，连接成功返回套接字，失败返回-1
void read_from();//将从服务器读取enum_chat类型的数据包，只负责处理聊天消息。将字符串内容打印出来，将序号更新
void write_to();////从屏幕读取输入并将聊天消息打包发送给服务器，由服务器处理并存入共享内存区。
int compare_account(char *account, char *str);//比较消息str的发送者是否是account，是返回0，否则返回非0.account是用户名，str是包含用户名的一条消息






int init_client(int port, char *addr)
{
	int cli_socket;
	int try_time;
	struct sockaddr_in server_addr;

	cli_socket = socket(AF_INET, SOCK_STREAM, 0); //创建客户端套接字
	if (cli_socket == -1)
		return -1;

	server_addr.sin_addr.s_addr = inet_addr(addr);
	server_addr.sin_port = htons(port);
	server_addr.sin_family = AF_INET;

	try_time = 0; //如果不成功每隔一秒连接一次，最多10次
	while (try_time < 10 && connect(cli_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
		sleep(1);
	if (try_time >= 10)
		return -1;
	else
		return cli_socket;
}
int compare_account(char *account, char *str)
{
	char temp[20];
	char *ptr = temp;
	int i;
	i = 0;
	while ((*ptr++ = *str++) != ':')
		; //以':'为分隔符，将用户名取出来赋值到temp方便比较
	*(ptr - 1) = '\0';
	return strcmp(temp, account);
}
void read_from()
{
	Packet packet;
	Kind kind;
	Data data;

	while (1)
	{
		read(client_socket, &packet, sizeof(Packet)); //读取从服务器发来的聊天包
		parse_packet(packet, &kind, &data);			  //解析
		if (kind != enum_chat)
		{
			printf("the type of the packet received is error!\n");
			return;
		}
		lastmessage = data.message.id; //更新最新ID
		if (compare_account(user_account, data.message.str))
		{ //如果那条消息不是自己发的，则打印出来
			printf("\b\b%s\n", data.message.str);
			printf("I:");
			fflush(stdout);
		}
	}
}
void write_to()
{
	char str[250];
	Packet packet;
	Message message;

	while (1)
	{
		printf("I:");
		fgets(str, 250, stdin); //最多读取250个输入
		if (strlen(str) > 140)
		{ //超过140长度非法
			printf("Your message is larger than 140 characters.\n");
			printf("Please type the content again.\n");
		}
		else if (!strcmp(str, "quit\n") || !strcmp(str, "exit\n") || //输入这些表示要退出聊天
				 !strcmp(str, "QUIT\n") || !strcmp(str, "EXIT\n"))
		{
			if (build_packet(&packet, enum_logout, message) == -1)
			{ //打包类型为enum_logout的包
				printf("fail to build the packet!\n");
				return;
			}
			write(client_socket, &packet, sizeof(Packet)); //发登出包给服务器
			return;
		}
		else
		{									   //正常输入聊天内容，准备发送聊天内容给服务器
			str[strlen(str) - 1] = '\0';	   //去除换行符
			strcpy(message.str, user_account); //聊天消息内容格式："Peter:Hello everyone"
			strcat(message.str, ":");
			strcat(message.str, str);
			if (build_packet(&packet, enum_chat, message) == -1)
			{ //打包类型为enum_chat的包
				printf("fail to build the packet!\n");
				return;
			}
			write(client_socket, &packet, sizeof(Packet)); //发聊天包给服务器
		}
	}
}
void get_user_info(Kind kind, Data *data)
{
    char password_check[20];
    do
    {
        printf("请输入您的用户名:");
        scanf("%s", data->userinfo.account); //不能输入用空格等分隔符风格的账号和密码
        if (strlen(data->userinfo.account) > 19)
            printf("用户名长度应该小于19个字符！\n");
    } while (strlen(data->userinfo.account) > 19);
    do
    {
        do
        {
            printf("请输入您的密码:");
            scanf("%s", data->userinfo.password);
            if (strlen(data->userinfo.password) > 19)
                printf("密码的长度应该小于19个字符！\n");
        } while (strlen(data->userinfo.password) > 19);

        if (kind != enum_regist)                             //如果不是注册
            strcpy(password_check, data->userinfo.password); //检测密码是否正确
        else                                                 //注册
        {
            do
            {
                printf("请再次输入您的密码:");
                scanf("%s", password_check);
                if (strlen(password_check) > 19)
                    printf("密码的长度应该小于19个字符！\n");
                //如果两次输入密码不一致
                //放后面统一校验
            } while (strlen(password_check) > 19);
        }
        /*如何验证保存的密码？*/
        if (strcmp(data->userinfo.password, password_check))
            printf("The password you entered should be consistent.\n");
    } while (strcmp(data->userinfo.password, password_check)); //密码不对，重新输入
}
void log_display()
{
    printf("欢迎使用聊天室程序\n");
    printf("请输入您要进行的操作：\n");
    printf("1.注册\n");
    printf("2.登陆\n");
    printf("3.修改密码\n");
    printf("4.退出\n");
}
void Multi_Chat(int fd,pthread_t thIDr,pthread_t thIDw){
    fd = open(user_account, O_RDONLY); //读取上次的最后聊天记录ID
			if (fd != -1)
			{				 //将ID发给服务器，服务器会返回
				char id[10]; //从那以后所有的聊天记录
				read(fd, id, 10);
				lastmessage = atoi(id); //并置全局变量lassmessage,注意读出来的是字符型数组
				write(client_socket, id, 10);
				close(fd);
			}
			else
			{
				write(client_socket, "-1", 5); //如果是第一次登陆，没有那个文件，则发-1给服务器
			}

			printf("\n-----Welcome to the chatting room-----\n");
			pthread_create(&thIDr, NULL, (void *)read_from, NULL); //开两个线程监听读写
			pthread_create(&thIDw, NULL, (void *)write_to, NULL);
			pthread_join(thIDw, NULL); //如果写入一句"quit"或者"exit"则结束线程
			pthread_cancel(thIDr);
			fd = open(user_account, O_WRONLY | O_CREAT, 0660); //在主线程处理收尾工作，将新的lassmessage写入文件
			if (fd != -1)
			{
				char str[10];
				sprintf(str, "%d", lastmessage);
				write(fd, str, 10); //字符串的形式将ID数字存进文件
				close(fd);
			}
			else
			{
				printf("open error!\n");
			}

			close(client_socket);
			flag = 0;
}
void Single_Chat(int fd, pthread_t thIDr, pthread_t thIDw){
    
};
void Log_Fun(int fd,pthread_t thIDr, pthread_t thIDw,int log_flag)
{
    
    Packet packet;
    Kind kind;
    Data data;

    client_socket = init_client(MYPORT, "127.0.0.1"); //初始化客户端套接字并连接到服务器
    if (client_socket == -1)
    {
        printf("connect error!\n");
        return -1;
    }

    log_display();
    scanf("%d", &log_flag);
    while(log_flag<=0||log_flag>4){
        printf("没有该选项，请重新输入！\n");
        scanf("%d",&log_flag);
    }
    
    //1.Sign_Up();
    /*******************用户注册************************/
    if (log_flag == 1)
    {
        get_user_info(enum_regist, &data);
        if (build_packet(&packet, enum_regist, data) == -1)
        {
            printf("fail to build the packet!\n");
            return -1;
        }

        write(client_socket, &packet, sizeof(Packet)); //发送类型为enum_regist的包给服务器表示注册
        read(client_socket, &packet, sizeof(Packet));  //服务器处理后发送回应包，客户端接受并解析
        parse_packet(packet, &kind, &data);            //判断是否注册成功，account为空表示注册失败
        if (kind != enum_regist)
        {
            printf("the type of the packet received is error!\n");
            return -1;
        }

        if (strcmp(packet.data.userinfo.account, ""))
            printf("Regist succeed.\n");
        else
            printf("Regist failed.\n");
        sleep(1);

    }
    //2.Sign_In();
    /******************用户登录******************/
    else if (log_flag == 2)
    {
        get_user_info(enum_login, &data);
        if (build_packet(&packet, enum_login, data) == -1)
        {
            printf("fail to build the packet!\n");
            return -1;
        }

        write(client_socket, &packet, sizeof(Packet)); //发送类型为enum_login的包给服务器表示登录
        read(client_socket, &packet, sizeof(Packet));  //服务器处理后发送回应包，客户端接受并解析
        parse_packet(packet, &kind, &data);            //判断是否登录成功，account为空表示登录失败
        if (kind != enum_login)
        {
            printf("the type of the packet received is error!\n");
            return -1;
        }

        if (strcmp(packet.data.userinfo.account, ""))
        {
            printf("登录成功！\n");
            fgets(user_account, 20, stdin); //为了读取缓冲区中的'\n'？
            strcpy(user_account, packet.data.userinfo.account);
            printf("请选择您要进入的模式:\n");
            printf("1.公共聊天室模式\n");
            printf("2.私聊模式\n");
            scanf("%d",&flag);
            while(flag<0||flag>2){
                printf("没有该选项，请重新输入！\n");
                scanf("%d",&flag);
            }
            if(flag==1) {
                Multi_Chat(fd,thIDr,thIDw); //置为1切换到聊天室模式
            }
            else if(flag==2) //Single_Chat();//私聊模式
            //如何返回？
            {
                
            }
        }
        else
        {
            printf("登录失败！\n");
        }
        sleep(1);
    }
    //3. Modify_PSW();
    /*******************修改密码*****************/
    else if (log_flag == 3)
    {
        get_user_info(enum_modify, &data);
        if (build_packet(&packet, enum_modify, data) == -1)
        {
            printf("fail to build the packet!\n");
            return -1;
        }

        write(client_socket, &packet, sizeof(Packet)); //发送类型为enum_modify的包给服务器表示请求修改密码
        read(client_socket, &packet, sizeof(Packet));  //服务器处理后发送回应包，客户端接受并解析
        parse_packet(packet, &kind, &data);            //判断是否允许修改密码成功，account为空表示无法修改
        if (kind != enum_modify)
        {
            printf("the type of the packet received is error!\n");
            return -1;
        }

        if (strcmp(packet.data.userinfo.account, ""))
        { //允许修改密码，输入新密码(两次)
            char password_check[20];
            do
            {
                do
                {
                    printf("Please input your new password:");
                    scanf("%s", packet.data.userinfo.password);
                    if (strlen(packet.data.userinfo.password) > 19)
                        printf("The length of password should be shortter than 19 characters.\n");
                } while (strlen(packet.data.userinfo.password) > 19);
                do
                {
                    printf("Please input your nwe password again:");
                    scanf("%s", password_check);
                    if (strlen(password_check) > 19)
                        printf("The length of password should be shortter than 19 characters.\n");
                } while (strlen(password_check) > 19);
                if (strcmp(packet.data.userinfo.password, password_check))
                    printf("The password you entered should be consistent.\n");
            } while (strcmp(packet.data.userinfo.password, password_check));

            write(client_socket, &packet, sizeof(Packet)); //发送类型为enum_modify的包给服务器表示正式修改密码
            read(client_socket, &packet, sizeof(Packet));  //服务器处理后发送回应包，客户端接受并解析
            parse_packet(packet, &kind, &data);            //判断是否修改密码成功，account为空表示修改失败
            if (kind != enum_modify)
            {
                printf("the type of the packet received is error!\n");
                return -1;
            }

            if (strcmp(packet.data.userinfo.account, ""))
                printf("Modify succeed.\n");
            else
                printf("Modify failed.\n");
        }
        else
        {
            printf("Have no account or online.\n");
        }
        sleep(1);
    }
    else if (log_flag==4) exit(0);//退出程序
    if(log_flag!=2) close(client_socket);
}

int main(int argc, char *argv[])
{
    pthread_t thIDr,thIdw;
    int fd;
    char str[50];
    int log_flag = 0; //登陆选项


    if(argc!=1){
        printf("usage:%s\n\n",argv[0]);
        return -1;
    }

    while (1)
    {
        Log_Fun(fd, thIDr, thIdw, log_flag); 
    }
}