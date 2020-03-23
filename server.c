#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUF_LEN 1024
#define ACK 0x06
#define UPL 0x11
#define DNL 0x12
#define NAK 0x15

int server_fd, client_fd;
int msg_size;
char buf[BUF_LEN + 1];
char sig;

//ack메세지(데이터 6)을 받으면 1 리턴 그 외 -1
int AckReciver()
{
	if (recv(client_fd, &sig, 1, 0) < 0)
	{
		printf("Server: A client disconnected.\n");
		exit(0);
	}
	else
	{
		if (sig == ACK)
			return 1;
		else
			return -1;
	}
}

void help()
{
	msg_size = snprintf(buf, BUF_LEN, "----------------\nls - 파일 목록\nu - 파일 업로드\nd - 파일 다운로드\nq - 프로그램 종료\n----------------\n");
	send(client_fd, buf, msg_size, 0);
}

void ls()
{
	int name_size;
	DIR* dp;
	struct dirent* d;
	msg_size = 0;

	if ((dp = opendir(".")) == NULL)
	{
		perror("opendir()");
		exit(1);
	}

	while ((d = readdir(dp)) != NULL)
	{
		name_size = snprintf(&buf[msg_size], BUF_LEN - msg_size, "%s \n", d->d_name);
		msg_size += name_size;
	}
	send(client_fd, buf, msg_size, 0);
}

void upload()
{
	int fd, recvbyte, writebyte = 0;
	off_t fsize;
	mode_t fmode;

	//업로드받을 준비가 됐다고 알린다.
	sig = UPL;
	send(client_fd, &sig, 1, 0);

	//파일 명을 받음.
	msg_size = recv(client_fd, buf, BUF_LEN, 0);
	buf[msg_size] = '\0';

	if (buf[0] == NAK)	return;		//파일을 열지 못할시 업로드 중단

	//파일명을 받았다고 알림
	sig = ACK;
	send(client_fd, &sig, 1, 0);

	//파일권한을 받는다.
	recv(client_fd, &fmode, sizeof(fmode), 0);

	if ((fd = open(buf, O_CREAT | O_WRONLY | O_EXCL, fmode)) < 0)		//파일이 존재하면 에러
	{
		perror("open()");
		sig = NAK;
		send(client_fd, &sig, 1, 0);
		return;
	}

	//파일권한을 받았다고 알림
	sig = ACK;
	send(client_fd, &sig, 1, 0);

	//파일사이즈를 받음
	recv(client_fd, &fsize, sizeof(fsize), 0);

	//파일사이즈를 받았다고 알림.
	sig = ACK;
	send(client_fd, &sig, 1, 0);

	//파일 내용을 받음
	while (writebyte < fsize)
	{
		if (fsize - writebyte < BUF_LEN)	recvbyte = fsize - writebyte;
		else	recvbyte = BUF_LEN;
		recv(client_fd, buf, recvbyte, 0);
		write(fd, buf, recvbyte);
		writebyte += recvbyte;
	}

	close(fd);
}

//파일 사이즈 전송, 파일 읽어들임, 읽어들인 바이트 만큼 전송, 읽어들인 바이트를 누적해서 파일사이즈만큼 보냈을 때 종료.
void download()
{
	int fd, readbyte, sendbyte = 0;
	off_t fsize;
	struct stat fdstat;

	msg_size = snprintf(buf, BUF_LEN, "다운로드 받을 파일을 확장자까지 입력해주세요.\n");
	send(client_fd, buf, msg_size, 0);
	
	if ((msg_size = recv(client_fd, buf, BUF_LEN, 0)) < 0)
	{
		printf("Server: A client disconnected.\n");
		exit(0);
	}
	else
		buf[msg_size - 1] = '\0';		//개행문자대신에 널문자대입
	
	//파일 오픈
	if ((fd = open(buf, O_RDONLY)) < 0)
	{
		perror("open()");
		msg_size = snprintf(buf, BUF_LEN, "해당되는 파일이 존재하지 않습니다.\n");
		send(client_fd, buf, msg_size, 0);
		return;
	}

	if (fstat(fd, &fdstat) < 0)
	{
		perror("fstat()");
		msg_size = snprintf(buf, BUF_LEN, "해당되는 파일이 존재하지 않습니다.\n");
		send(client_fd, buf, msg_size, 0);
		return;
	}

	//다운로드 전 파일을 보낸다고 알려준다
	sig = DNL;
	send(client_fd, &sig, 1, 0);

	//ACK를 받으면 파일 사이즈 전송
	if (AckReciver())
	{
		fsize = fdstat.st_size;
		send(client_fd, &fsize, sizeof(fsize), 0);
	}

	//파일 사이즈 전송 후 ACK를 다시 받으면 파일 권한 전송
	if (AckReciver()) // 파일 사이즈 잘 받았다면 true
		send(client_fd, &fdstat.st_mode, sizeof(fdstat.st_mode), 0);

	//파일 내용 전송
	if (AckReciver()) // 파일 권한 잘 받았다면 true
	{
		while (sendbyte < fsize)
		{
			readbyte = read(fd, buf, BUF_LEN);
			send(client_fd, buf, readbyte, 0);
			sendbyte += readbyte;
		}
	}

	close(fd);
}

int main(int argc, char** argv)
{
	struct sockaddr_in server_addr, client_addr;
	int client_addr_len, pid;

	if (argc != 2)
	{
		printf("usage: %s [port]\n", argv[0]); // ./server
		exit(0);
	}

	// 소켓 생성
	if ((server_fd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("socket()");
		exit(1);
	}

	bzero((char*)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(atoi(argv[1]));

	//소켓 바인드
	if (bind(server_fd, (struct sockaddr*) & server_addr, sizeof(server_addr)) < 0) // -1(fail)
	{
		perror("bind()");
		exit(1);
	}

	//리슨 (클라이언트 대기)
	if (listen(server_fd, 0) < 0)
	{
		perror("perror()");
		exit(1);
	}
	printf("Server: waiting connection request.\n");

	//SIGCHLD를 무시한다. 좀비프로세스 방지
	signal(SIGCHLD, SIG_IGN);

	//무한루프 서버종료는 외부시그널로 종료시킨다.
	while (1)
	{
		//클라이언트와 연결
		client_addr_len = sizeof(client_addr);
		client_fd = accept(server_fd, (struct sockaddr*) & client_addr, &client_addr_len);
		// printf("client_fd : %d \n", client_fd);

		if (client_fd < 0)
		{
			perror("accpet()");
			exit(1);
		}

		//프로세스 생성
		pid = fork();

		if (pid < 0)
		{
			perror("fork()");
			exit(1);
		}

		//부모프로세스(서버) 클라이언트 소켓을 닫고 다시 다른 클라이언트와의 연결을 받는다.
		else if (pid > 0) // 자식 pid를 받는다.
			close(client_fd);

		
		else // 자식프로세스 pid = 0을 리턴 받는다.
		{
			close(server_fd);
			printf("Server: A client connected.\n");

			//서버 공지 출력
			msg_size = snprintf(buf, BUF_LEN, "파일 전송 서버에 연결되었습니다!\n명령어가 궁금하시다면 h를 입력해주세요\n");
			send(client_fd, buf, msg_size, 0);
			//q or Q를 입력받기 전까지 연결 유지
			while (1)
			{
				if ((msg_size = recv(client_fd, buf, BUF_LEN, 0)) < 0)
				{
					printf("Server: A client disconnected.\n");
					exit(0);
				}
				else
					buf[msg_size] = '\0';

				//q입력시 종료
				if (!strcmp(buf, "q\n"))
				{
					printf("Server: A client disconnected.\n");
					return 0;
				}
					

				//h 입력시 도움말
				else if (!strcmp(buf, "h\n"))	help();

				//ls 입력시 파일 목록 출력
				else if (!strcmp(buf, "ls\n"))	ls();

				//u 입력시 파일 업로드
				else if (!strcmp(buf, "u\n"))	upload();

				//d 입력시 파일 다운로드
				else if (!strcmp(buf, "d\n"))	download();
			}
		}
	}

	close(server_fd);

	return 0;
}
