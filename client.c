#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <dirent.h>

#define BUF_LEN 1024
#define ACK 0x06
#define UPL 0x11
#define DNL 0x12
#define NAK 0x15

int fd, msg_size;			//소켓, 메세지크기
char buf[BUF_LEN + 1];			//read로 읽어온 정보를 담을 버퍼
char sig;

//ack메세지(데이터 6)을 받으면 1 리턴 그 외 0
int AckReciver()
{
	if (recv(fd, &sig, 1, 0) < 0)
	{
		printf("Server: A client disconnected.\n");
		exit(0);
	}
	else
	{
		if (sig == ACK)
			return 1;
		else
			return 0;
	}
}

void upload()
{
	int _fd, readbyte, sendbyte = 0;
	off_t fsize;
	struct stat fdstat;
	DIR* dp;
	struct dirent* d;

	//파일 목록 출력
	if ((dp = opendir(".")) == NULL)
	{
		perror("opendir()");
		exit(1);
	}
	while ((d = readdir(dp)) != NULL)
	{
		printf("%s \n", d->d_name);
	}

	printf("\n전송할 파일을 확장자까지 입력해주세요.\n");
	
	if (fgets(buf, BUF_LEN, stdin) == NULL) // 키보드 입력
	{
		perror("fgets()");
		exit(1);
	}
	msg_size = strlen(buf);
	msg_size--;
	buf[msg_size] = '\0';		//엔터키까지 버퍼에 들어가므로 엔터자리에 NULL값 대입

	//파일을 열음
	if ((_fd = open(buf, O_RDONLY)) < 0)
	{
		perror("open()");
		sig = NAK;
		send(fd, &sig, 1, 0);
		return;
	}

	if (fstat(_fd, &fdstat) < 0) // 파일을 못 열 경우
	{
		perror("fstat()");
		sig = NAK;
		send(fd, &sig, 1, 0);
		return;
	}

	//파일명 전송
	send(fd, buf, msg_size, 0);

	//파일 권한 전송
	if (AckReciver())
		send(fd, &fdstat.st_mode, sizeof(fdstat.st_mode), 0);

	//파일 사이즈 전송
	if (AckReciver())
	{
		fsize = fdstat.st_size;
		send(fd, &fsize, sizeof(fsize), 0);
	}
	//서버에서 파일생성이 불가(해당파일이 이미 존재)할 경우 업로드 중단
	else
	{
		fprintf(stderr, "해당파일은 서버에 업로드할 수 없습니다.\n");
		close(_fd);
		return;
	}

	//파일 내용 전송
	if (AckReciver())
	{
		while (sendbyte < fsize)
		{
			readbyte = read(_fd, buf, BUF_LEN);
			send(fd, buf, readbyte, 0);
			sendbyte += readbyte;

			printf("\r%.0f%%", (double)sendbyte / (double)fsize * 100);
		}
	}

	printf("\n업로드가 완료되었습니다.\n");
	close(_fd);
}

//void Progress(char label[], int step, int total)
//
//{
//
//	const int pwidth = 36;
//
//
//
//	int width = pwidth - strlen(label);
//
//	int pos = (step * width) / total;
//
//
//
//	int percent = (step * 100) / total;
//
//
//
//	printf("%s[", label);
//
//
//
//	for (int i = 0; i < pos; i++)  printf("%c", '=');
//
//
//
//	printf("% *c", width - pos + 1, ']');
//
//	printf(" %3d%%\r", percent);
//
//}

void download()
{
	int _fd, recvbyte, writebyte = 0;
	off_t fsize; // 파일 사이즈
	mode_t fmode; // 파일 권한

	//다운로드받을 준비가 됐다고 알림
	sig = ACK;
	send(fd, &sig, 1, 0);

	//파일 사이즈를 받아옴
	recv(fd, &fsize, sizeof(fsize), 0);
	printf("파일 크기 : %ld byte\n", fsize);

	//파일사이즈를 받았다고 알림
	sig = ACK;
	send(fd, &sig, 1, 0);

	//파일의 권한을 받아옴
	recv(fd, &fmode, sizeof(fmode), 0);

	//빈 파일 생성
	printf("저장할 파일 명을 입력하세요.\n");
	if (fgets(buf, BUF_LEN, stdin) == NULL)
	{
		perror("fgets()");
		exit(1);
	}
	msg_size = strlen(buf); // 파일 이름 사이즈


	buf[msg_size - 1] = '\0';		//엔터키까지 버퍼에 들어가므로 엔터자리에 NULL값 대입
	_fd = open(buf, O_CREAT | O_WRONLY | O_TRUNC, fmode);
	
	//파일 권한을 받았다고 알림
	sig = ACK;
	send(fd, &sig, 1, 0);

	//파일 내용을 받아옴
	while (writebyte < fsize)
	{
		if (fsize - writebyte < BUF_LEN)	
			recvbyte = fsize - writebyte;
		else	
			recvbyte = BUF_LEN;

		recv(fd, buf, recvbyte, 0);
		write(_fd, buf, recvbyte);
		writebyte += recvbyte;

		printf("\r%.0f%%", (double)writebyte / (double)fsize * 100);
	}
	
	printf("\n다운로드가 완료되었습니다.\n\n");
	close(_fd);
}

int main(int argc, char* argv[])
{
	int maxfdp;				//소켓의 갯수
	struct sockaddr_in server_addr;		//서버소켓의 정보를 담을 구조체
	fd_set read_fds;			//fdset 구조체
	char* haddr;				//통신할 IPv4주소를 가르킬 포인터

	if (argc != 3)				//IP주소와 Port번호 받도록 수정
	{
		printf("usage : %s [IP_Address] [Port_Num] \n", argv[0]);
		exit(0);
	}

	haddr = argv[1];		//통신할 IPv4주소를 저장

	//소켓 생성(프로토콜을 IPv4형식, 연결형 서비스로 설정)
	if ((fd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("socket()");
		exit(1);
	}

	bzero((char*)&server_addr, sizeof(server_addr));	//소켓구조체를 0으로 초기화시킴
	server_addr.sin_family = AF_INET;			//주소 체계를 IPv4형식을 사용함
	server_addr.sin_addr.s_addr = inet_addr(haddr);		//입력받은 IPv4주소로 연결
	server_addr.sin_port = htons(atoi(argv[2]));		//입력받은 포트번호로 설정

	//서버로 연결 요청을 보냄
	if (connect(fd, (struct sockaddr*) & server_addr, sizeof(server_addr)) < 0)
	{
		perror("socket()");
		exit(1);
	}

	//파일디스크립터의 갯수와 fdset을 초기화
	maxfdp = fd + 1;
	FD_ZERO(&read_fds);

	while (1)
	{
		FD_SET(0, &read_fds);		//표준입력소켓 세트
		FD_SET(fd, &read_fds);		//개설된 클라이언트소켓 세트

		if (select(maxfdp, &read_fds, NULL, NULL, NULL) < 0)		//세트된 소켓의 변화 감지 시 리턴, -1(오류), 0(시간 만료), 준비된 fd 갯수
		{
			perror("select()");
			exit(1);
		}

		if (FD_ISSET(fd, &read_fds))		//클라이언트 소켓에 데이터가 들어왔을 시
		{
			if ((msg_size = recv(fd, buf, BUF_LEN, 0)) > 0)
			{
				if (buf[0] == UPL)
					upload();
				else if (buf[0] == DNL)	//파일을 받을 시
					download();
				else // ls, help
				{
					buf[msg_size] = '\0';
					printf("%s\n", buf);
				}
			}
			else
			{
				printf("CANCELED CONNECTION\n");	//서버 종료시
				exit(0);
			}
		}

		if (FD_ISSET(0, &read_fds))		//키보드에서 입력을 받았을 시
		{
			if (fgets(buf, BUF_LEN, stdin))	
				msg_size = strlen(buf);
			else
			{
				perror("fgets()");
				exit(1);
			}

			if (send(fd, buf, msg_size, 0) < 0)	
				printf("Error : Write error on socket.\n");

			//q입력시 종료
			if (!strcmp(buf, "q\n"))
			{
				printf("Quit the Program\n");
				break;
			}
		}
	}

	close(fd);

	return 0;
}
