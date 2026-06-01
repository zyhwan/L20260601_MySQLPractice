#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "NetUtil.h"

#include <winsock2.h>
#include <Windows.h>
#include <iostream>
#include <process.h>
#include <conio.h>
#include "SDL.h"
#include <mutex>
#include <queue>

#pragma comment(lib, "ws2_32")
#pragma comment(lib, "NetCommon")

using namespace std;

char SendBuffer[1024] = { 0, };
char RecvBuffer[1024] = { 0, };

bool IsRecvThreadRunning = true;
bool IsSendThreadRunning = true;

SessionManager MySessionManager;
SOCKET MyClientID;

SDL_Event Event;
std::queue<int> KeyBuffer;

std::mutex SessionLock;
std::mutex KeyBufferLock;

void Render(SDL_Renderer* MyRender)
{
	if (!MyRender) return;

	SDL_SetRenderDrawColor(MyRender, 0, 0, 0, 255);
	SDL_RenderClear(MyRender);

	{

		lock_guard<std::mutex> Lock(SessionLock);
		for (auto& Player : MySessionManager.SessionList)
		{
			SDL_Rect Rect;
			Rect.x = Player.X * 40;
			Rect.y = Player.Y * 40;
			Rect.w = 40;
			Rect.h = 40;

			SDL_SetRenderDrawColor(MyRender, Player.R, Player.G, Player.B, 255);
			SDL_RenderFillRect(MyRender, &Rect);
			SDL_SetRenderDrawColor(MyRender, 255, 255, 255, 255);
			SDL_RenderDrawRect(MyRender, &Rect);
		}
	}

	SDL_RenderPresent(MyRender);
}


void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer)
{
	//여기서 역직렬화로 데이터 받기
	auto UserPacketData = UserPacket::GetPacketData(InBuffer);

	switch (UserPacketData->data_type())
	{
	case UserPacket::PacketType_S2C_Login:
	{
		MyClientID = UserPacketData->data_as_S2C_Login()->clientsocket_id();
	}
	break;
	case UserPacket::PacketType_S2C_Spawn:
	{
		Session InSession;
		auto SpawnData = UserPacketData->data_as_S2C_Spawn();
		InSession.ClientSocket = (SOCKET)SpawnData->clientsocket_id();
		InSession.Shape = SpawnData->shape();
		InSession.X = SpawnData->position()->x();
		InSession.Y = SpawnData->position()->y();
		InSession.R = SpawnData->color()->r();
		InSession.G = SpawnData->color()->g();
		InSession.B = SpawnData->color()->b();

		{
			lock_guard<std::mutex> lock(SessionLock);
			MySessionManager.Add(InSession);
		}
	}
	break;
	case UserPacket::PacketType_S2C_Move:
	{
		auto MoveData = UserPacketData->data_as_S2C_Move();

		Session* FindSession = MySessionManager.GetSession((SOCKET)MoveData->clientsocket_id());
		FindSession->X = MoveData->position()->x();
		FindSession->Y = MoveData->position()->y();
	}
	break;
	case UserPacket::PacketType_S2C_Destroy:
	{
		auto DestroyPacket = UserPacketData->data_as_S2C_Destroy();

		Session* FindSession = MySessionManager.GetSession((SOCKET)DestroyPacket->clientsocket_id());
		{
			lock_guard<std::mutex> lock(SessionLock);
			MySessionManager.Delete(*FindSession);
		}
	}
	break;
	case UserPacket::PacketType_S2C_ChangeColor:
	{
		auto ColorData = UserPacketData->data_as_S2C_ChangeColor();
		{
			lock_guard<std::mutex> lock(SessionLock);
			Session* FindSession = MySessionManager.GetSession((SOCKET)ColorData->clientsocket_id());
			FindSession->R = ColorData->color()->r();
			FindSession->G = ColorData->color()->g();
			FindSession->B = ColorData->color()->b();
		}
	}
	break;
	}
}

unsigned WINAPI RecvThread(void* Argument)
{
	SOCKET ServerSocket = *(SOCKET*)Argument;

	while (IsRecvThreadRunning)
	{
		int RecvBytes = RecvAll(ServerSocket, RecvBuffer);
		if (RecvBytes <= 0)
		{
			std::cout << "recv fail " << endl;
			break;
		}

		ProcessPacket(ServerSocket, RecvBuffer);
	}


	return 0;
}

unsigned WINAPI SendThread(void* Argument)
{
	//책임은 사용하는 놈이 진다.
	SOCKET ServerSocket = *(SOCKET*)Argument;

	while (IsSendThreadRunning)
	{
		if (KeyBuffer.empty())
		{
			YieldProcessor();
			continue;

		}
		flatbuffers::FlatBufferBuilder SendBuilder;

		flatbuffers::Offset<UserPacket::C2S_Move> C2S_MoveData;
		{
			lock_guard<std::mutex> KeyLock(KeyBufferLock);
			C2S_MoveData = UserPacket::CreateC2S_Move(
				SendBuilder,
				(uint16_t)MyClientID,
				KeyBuffer.front()
			);
			KeyBuffer.pop();
		}

		auto UserPacketData = UserPacket::CreatePacketData(
			SendBuilder,
			UserPacket::PacketType_C2S_Move,
			C2S_MoveData.Union()
		);

		SendBuilder.Finish(UserPacketData);

		SendAll(ServerSocket, SendBuilder);
	}

	return 0;
}

int SDL_main(int argc, char* argv[])
{
	std::cout << "client " << endl;


	SDL_Init(SDL_INIT_EVERYTHING);
	SDL_Window* MyWindow = SDL_CreateWindow("ThreadProgramming", 100, 100, 1024, 768, SDL_WINDOW_SHOWN);
	SDL_Renderer* MyRender = SDL_CreateRenderer(MyWindow, -1, 0);

	WSAData wsaData;

	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKET ServerSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	SOCKADDR_IN ServerSockAddr;
	memset(&ServerSockAddr, 0, sizeof(ServerSockAddr));
	ServerSockAddr.sin_family = AF_INET;
	ServerSockAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); //192.168.0.95	127.0.0.1
	ServerSockAddr.sin_port = htons(35000);

	connect(ServerSocket, (SOCKADDR*)&ServerSockAddr, sizeof(ServerSockAddr));

	std::cout << "client connect" << endl;

	string user_id{};
	string password{};

	cout << "User_id : " << endl;
	cin >> user_id;

	cout << "Password : " << endl;
	cin >> password;

	flatbuffers::FlatBufferBuilder SendBuilder;
	auto C2S_LoginData = UserPacket::CreateC2S_Login(
		SendBuilder,
		SendBuilder.CreateString(user_id),
		SendBuilder.CreateString(password),
		SendBuilder.CreateString("1as3f356dsd6gyhg")
	);

	auto UserPacketData = UserPacket::CreatePacketData(
		SendBuilder,
		UserPacket::PacketType_C2S_Login,
		C2S_LoginData.Union()
	);

	SendBuilder.Finish(UserPacketData);

	SendAll(ServerSocket, SendBuilder);

	HANDLE ThreadHandles[2] = { 0, };

	//nonblocking, asynchrous
	ThreadHandles[0] = (HANDLE)_beginthreadex(0, 0, RecvThread, &ServerSocket, /*CREATE_SUSPENDED*/0, 0);
	ThreadHandles[1] = (HANDLE)_beginthreadex(0, 0, SendThread, &ServerSocket, /*CREATE_SUSPENDED*/0, 0);

	const Uint8* KeyState = SDL_GetKeyboardState(NULL);

	bool Running = true;
	while (Running)
	{
		SDL_PollEvent(&Event);
		// 이벤트 처리
		if (Event.type == SDL_QUIT)
		{
			IsRecvThreadRunning = false;
			IsSendThreadRunning = false;
			break;
		}
		else if (Event.type == SDL_KEYDOWN)
		{
			if (KeyState[SDL_SCANCODE_ESCAPE])
			{
				IsRecvThreadRunning = false;
				IsSendThreadRunning = false;
				break;
			}
			if (KeyState[SDL_SCANCODE_W])
			{
				KeyBuffer.push('W');
			}
			if (KeyState[SDL_SCANCODE_S])
			{
				KeyBuffer.push('S');
			}
			if (KeyState[SDL_SCANCODE_A])
			{
				KeyBuffer.push('A');
			}
			if (KeyState[SDL_SCANCODE_D])
			{
				KeyBuffer.push('D');
			}
			if (KeyState[SDL_SCANCODE_C])
			{
				KeyBuffer.push('C');
			}
		}

		Render(MyRender);
	}

	//blocking
	WaitForMultipleObjects(2, ThreadHandles, FALSE, INFINITE);

	closesocket(ServerSocket);

	std::cout << "End Thread" << endl;

	IsSendThreadRunning = false;
	IsRecvThreadRunning = false;

	CloseHandle(ThreadHandles[0]);
	CloseHandle(ThreadHandles[1]);

	WSACleanup();


	//삭제
	SDL_DestroyWindow(MyWindow);
	SDL_DestroyRenderer(MyRender);

	SDL_Quit();

	return 0;
}