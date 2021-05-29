#ifndef chat_h
#define chat_h    

#include <stdio.h>
#include <sys/socket.h>//与套接字相关的函数声明和结构体定义
#include <netinet/in.h>//sockaddr_in
#include <arpa/inet.h>//inet_ntop 等
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>// C POSIX library file control options
#include <sys/types.h>
#include <sys/stat.h>//define the structure of the data returned by the functions fstat(), lstat(), and stat().
#include <stdarg.h>//allows functions to accept an indefinite number of arguments
/*2*/
#include <sys/ipc.h>//interprocess communication (IPC): messages, semaphores and shared memory
#include <sys/msg.h>//massage queue
#include <error.h>//定义错误码


#define MYPORT 4567			//约定端口
#define MAXLEN 140			//最大消息长度

typedef struct my_message{//包含标识符id和消息正文
	int id;					      //消息的标识符
	char str[MAXLEN+1];		//消息正文
}Message;					      //一条消息的结构体

typedef struct my_user{//包含账号和密码
	char account[20];		  //账号
	char password[20];		//密码
}User;						      //用户登录信息

typedef union my_data{//为什么用Union  包含用户信息和消息
	User userinfo;			  //用户信息
	Message message;		  //消息
}Data;						      //数据包共用体

typedef enum my_kind{//记录要执行的功能
	enum_regist, //注册
	enum_login,  //登录
	enum_logout, //登出
	enum_chat,   //发送消息
	enum_modify  //修改用户密码			     		 		        
}Kind;						      //用枚举变量表示包类型

typedef struct my_packet{//包含执行的功能（包的类型）和数据包
	Kind kind;		        //包类型
	Data data;		        //数据包
}Packet;//通信协议
				

//将数据和类型打包，封装到packet中。使用变长参数，第三个参数可以任意类型
int build_packet(Packet *packet,Kind kind,...);
//解析数据包，把类型和数据取出来
int parse_packet(Packet packet,Kind *kind,Data *data);

#endif
