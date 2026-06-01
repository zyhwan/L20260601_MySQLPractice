#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "NetUtil.h"

#include <winsock2.h>
#include <iostream>
#include "SessionManager.h"

#pragma comment(lib, "ws2_32")

#pragma comment(lib, "NetCommon")

//MySQL 전용
//---------------------------------------------
#include "jdbc/mysql_connection.h"
#include "jdbc/cppconn/driver.h"
#include "jdbc/cppconn/exception.h"
#include "jdbc/cppconn/resultset.h"
#include "jdbc/cppconn/statement.h"
#include "jdbc/cppconn/prepared_statement.h"

#ifndef _DEBUG
#pragma comment(lib, "mysqlcppconn")
#else
#pragma comment(lib, "debug/mysqlcppconn")
#endif //_DEBUG
//---------------------------------------------

using namespace std;

char Buffer[1024] = { 0, };

SessionManager MySessionManager;

//DB 전역 변수
sql::Connection* MyConnection = nullptr;

bool DB_Login(const string& user_id, const string& user_PW)
{
	sql::ResultSet* MyResultSet{}; //결과 창
	sql::PreparedStatement* MyPreparedStatement; //쿼리를 만들때 injection 방어함.

	sql::SQLString Query = "select * from user where `user_id` = ? and `user_pw` = sha2( ?, 512) and is_delete = 'N';";

	MyPreparedStatement = MyConnection->prepareStatement(Query);
	MyPreparedStatement->setString(1, user_id);
	MyPreparedStatement->setString(2, user_PW);
	MyResultSet = MyPreparedStatement->executeQuery();

	std::cout << Query << std::endl;

	if (MyResultSet->rowsCount() == 0)
	{
		cout << "로그인 실패" << endl;
		return false;
	}
	else
	{
		cout << "로그인 성공" << endl;
		return true;
	}
}

bool DB_Register(const string& user_id, const string& user_PW, const string& user_name)
{
	//중복 아이디 체크
	{
		sql::SQLString CheckQuery =
			"select count(*) as cnt from user where `user_id` = ?";

		sql::PreparedStatement* MyPreparedStatement = MyConnection->prepareStatement(CheckQuery);
		MyPreparedStatement->setString(1, user_id);

		sql::ResultSet* result = MyPreparedStatement->executeQuery();
		result->next();
		int cnt = result->getInt("cnt");

		delete result;
		delete MyPreparedStatement;

		if (cnt > 0)
		{
			return false;	// 이미 존재하는 아이디
		}
	}

	// 중복이 없다면 삽입
	{
		sql::SQLString InsertQuery =
			"insert into `user` (`user_id`, `user_pw`, `name`, `is_delete`, `create_at`) "
			"values (?, sha2(?, 512), ?, 'N', now())";

		sql::PreparedStatement* MyPreparedStatement = MyConnection->prepareStatement(InsertQuery);
		MyPreparedStatement->setString(1, user_id);
		MyPreparedStatement->setString(2, user_PW);
		MyPreparedStatement->setString(3, user_name);
		MyPreparedStatement->executeUpdate();

		delete MyPreparedStatement;
	}
	return true;
}

void DisconnectSocket(SOCKET DisconnectedSocket, fd_set* Sockets)
{
	SOCKET ClosedSocket = DisconnectedSocket;

	SOCKADDR_IN ClosedSockAddr;
	memset(&ClosedSockAddr, 0, sizeof(ClosedSockAddr));
	int ClosedSockAddrLength = sizeof(ClosedSockAddr);

	getpeername(ClosedSocket, (SOCKADDR*)&ClosedSockAddr, &ClosedSockAddrLength);

	cout << "disconnect : " << inet_ntoa(ClosedSockAddr.sin_addr) << endl;

	//FD_CLR(ClosedSocket, Sockets);
	//closesocket(ClosedSocket);

	//flatbuffer로 컨버팅
	flatbuffers::FlatBufferBuilder SendBuilder;

	auto DestroyData = UserPacket::CreateS2C_Destroy(
		SendBuilder,
		(uint16_t)ClosedSocket
	);


	//[ [][][]   ]
	auto UserPacketData = UserPacket::CreatePacketData(
		SendBuilder,
		UserPacket::PacketType_S2C_Destroy,
		DestroyData.Union()
	);

	SendBuilder.Finish(UserPacketData);

	//dangling pointer
	Session* FindSession = MySessionManager.GetSession(ClosedSocket);
	MySessionManager.Delete(*FindSession);

	//모든 유저한테 이동 패킷 보내줌
	for (auto Receiver : MySessionManager.SessionList)
	{
		SendAll(Receiver.ClientSocket, SendBuilder);
	}
	FD_CLR(ClosedSocket, Sockets);
	closesocket(ClosedSocket);
}

void ProcessPacket(SOCKET ProcessSocket, const char* InBuffer)
{
	auto UserPacketData = UserPacket::GetPacketData(InBuffer);

	string user_id{};
	string user_pw{};
	string user_name{};

	switch (UserPacketData->data_type())
	{
	case UserPacket::PacketType_C2S_Register:
	{
		auto RegisterData = UserPacketData->data_as_C2S_Register();

		user_id = RegisterData->user_id()->c_str();
		user_pw = RegisterData->user_pw()->c_str();
		user_name = RegisterData->name()->c_str();

		bool Register = DB_Register(user_id, user_pw, user_name);

		if (!Register)
		{
			cout << "중복되는 아이디입니다." << endl;
			flatbuffers::FlatBufferBuilder builder;
			auto S2C_LoginData = UserPacket::CreateS2C_Register(
				builder,
				false,
				builder.CreateString("중복되는 아이디입니다.")
			);
			auto Packet = UserPacket::CreatePacketData(
				builder,
				UserPacket::PacketType_S2C_Register,
				S2C_LoginData.Union()
			);
			builder.Finish(Packet);
			SendAll(ProcessSocket, builder);
			break;
		}
		else
		{
			cout << "회원가입이 완료되었습니다." << endl;
			flatbuffers::FlatBufferBuilder builder;
			auto S2C_LoginData = UserPacket::CreateS2C_Register(
				builder,
				true,
				builder.CreateString("회원가입이 완료되었습니다.")
			);
			auto Packet = UserPacket::CreatePacketData(
				builder,
				UserPacket::PacketType_S2C_Register,
				S2C_LoginData.Union()
			);
			builder.Finish(Packet);
			SendAll(ProcessSocket, builder);
			break;
		}
	}
		break;
	case UserPacket::PacketType_C2S_Login:
	{
		auto LoginData = UserPacketData->data_as_C2S_Login();

		user_id = LoginData->user_id()->c_str();
		user_pw = LoginData->user_pw()->c_str();

		bool login = DB_Login(user_id, user_pw);

		//틀렸을 경우.
		if (!login)
		{
			flatbuffers::FlatBufferBuilder builder;
			auto S2C_LoginData = UserPacket::CreateS2C_Login(
				builder,
				0,
				builder.CreateString("아이디 또는 비밀번호가 틀렸습니다."),
				false
			);
			auto Packet = UserPacket::CreatePacketData(
				builder,
				UserPacket::PacketType_S2C_Login,
				S2C_LoginData.Union()
			);
			builder.Finish(Packet);
			SendAll(ProcessSocket, builder);
			break;
		}

		//접속 한 유저 정보 업데이트(Session)
		Session InSession;
		InSession.ClientSocket = ProcessSocket;
		InSession.UserID = LoginData->user_id()->c_str();
		InSession.X = rand() % 24 + 1; // 1 ~ 25;
		InSession.Y = rand() % 24 + 1; // 1 ~ 25;
		InSession.Shape = 65 + (rand() % 26);

		InSession.R = rand() % 255;
		InSession.G = rand() % 255;
		InSession.B = rand() % 255;

		MySessionManager.Add(InSession);


		{
			flatbuffers::FlatBufferBuilder builder;
			auto S2C_LoginData = UserPacket::CreateS2C_Login(
				builder,
				(uint16_t)ProcessSocket,
				builder.CreateString("Welcome."),
				true
			);
			auto UserPacketData = UserPacket::CreatePacketData(
				builder,
				UserPacket::PacketType_S2C_Login,
				S2C_LoginData.Union()
			);

			builder.Finish(UserPacketData);
			SendAll(ProcessSocket, builder);
		}


		//접속한 모든 유저한테 현재 모든 유저의 정보를 보내준다.
		for (auto Item : MySessionManager.SessionList)
		{
			UserPacket::FVector2D pos((uint16_t)Item.X, (uint16_t)Item.Y);
			UserPacket::FColor col(Item.R, Item.G, Item.B);

			for (auto Receiver : MySessionManager.SessionList)
			{
				flatbuffers::FlatBufferBuilder builder;
				auto spawnData = UserPacket::CreateS2C_Spawn(
					builder,
					(uint16_t)Item.ClientSocket,  // clientsocket_id
					&pos,                          // position (struct 포인터)
					(int8_t)Item.Shape,            // shape
					&col                           // color (struct 포인터)
				);
				auto UserPacketData = UserPacket::CreatePacketData(
					builder,
					UserPacket::PacketType_S2C_Spawn,
					spawnData.Union()
				);

				builder.Finish(UserPacketData);
				SendAll(Receiver.ClientSocket, builder);
			}
		}
	}
	break;

	case UserPacket::PacketType_C2S_Move:
	{
		auto MoveData = UserPacketData->data_as_C2S_Move();

		Session* FindSession = MySessionManager.GetSession((SOCKET)MoveData->clientsocket_id());
		if (!FindSession) break;

		switch (MoveData->direction())
		{
		case 'W':
		case 'w':
			FindSession->Y--;
			break;
		case 'S':
		case 's':
			FindSession->Y++;
			break;
		case 'A':
		case 'a':
			FindSession->X--;
			break;
		case 'D':
		case 'd':
			FindSession->X++;
			break;
		case 'C':
		case 'c':
			FindSession->R = rand() % 255;
			FindSession->G = rand() % 255;
			FindSession->B = rand() % 255;
			break;
		}

		UserPacket::FVector2D newPos((uint16_t)FindSession->X, (uint16_t)FindSession->Y);

		//모든 유저한테 이동 패킷 보내줌
		for (auto Receiver : MySessionManager.SessionList)
		{
			flatbuffers::FlatBufferBuilder builder;
			auto movedata = UserPacket::CreateS2C_Move(
				builder,
				(uint16_t)FindSession->ClientSocket,
				&newPos
			);

			auto UserPacketData = UserPacket::CreatePacketData(
				builder,
				UserPacket::PacketType_S2C_Move,
				movedata.Union()
			);

			builder.Finish(UserPacketData);
			SendAll(Receiver.ClientSocket, builder);
		}

		//컬러값 보내기
		UserPacket::FColor newColor((uint8_t)FindSession->R, (uint8_t)FindSession->G, (uint8_t)FindSession->B);
		for (auto Receiver : MySessionManager.SessionList)
		{
			flatbuffers::FlatBufferBuilder builder;
			auto movedata = UserPacket::CreateS2C_ChangeColor(
				builder,
				(uint16_t)FindSession->ClientSocket,
				&newColor
			);

			auto UserPacketData = UserPacket::CreatePacketData(
				builder,
				UserPacket::PacketType_S2C_ChangeColor,
				movedata.Union()
			);

			builder.Finish(UserPacketData);
			SendAll(Receiver.ClientSocket, builder);
		}



	}
	break;
	}


}

//blocking, synchrous, multiplexing(polling)
int main()
{
	srand((unsigned int)time(nullptr));
	cout << "server start" << endl;

	string DB_Password = {};

	cout << "DB 비밀번호를 입력하세요: ";
	cin >> DB_Password;

	//DB 연결
	try
	{
		sql::Driver* driver = get_driver_instance();
		MyConnection = driver->connect("tcp://127.0.0.1", "zyhwan", DB_Password);
		MyConnection->setSchema("membership");
		cout << "[DB] Connected." << endl;
	}
	catch (sql::SQLException& e)
	{
		cout << "[DB] Connect fail: " << e.what() << endl;
		return -1;
	}



	WSAData wsaData;

	WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKET ListenSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	SOCKADDR_IN ListenSockAddr;
	memset(&ListenSockAddr, 0, sizeof(ListenSockAddr));
	ListenSockAddr.sin_family = AF_INET;
	ListenSockAddr.sin_addr.s_addr = INADDR_ANY;
	ListenSockAddr.sin_port = htons(35000);

	//already use port 이미 포트 사용중
	::bind(ListenSocket, (SOCKADDR*)&ListenSockAddr, sizeof(ListenSockAddr));

	listen(ListenSocket, SOMAXCONN);



	//blocking, synchronous(TimeOut)
	TIMEVAL TimeOut;
	TimeOut.tv_sec = 0;
	TimeOut.tv_usec = 500000;

	fd_set ReadSockets;
	fd_set CopyReadSockets;

	FD_ZERO(&ReadSockets);
	FD_SET(ListenSocket, &ReadSockets);

	while (true)
	{
		CopyReadSockets = ReadSockets;

		//0.5초씩 blocking
		int ChangeCount = select(0, &CopyReadSockets, 0, 0, &TimeOut);

		if (ChangeCount <= 0)
		{
			//Server Work
			//0.5초한번 서버 작업을 하는거
			continue;
		}

		//몬가 자료 있다.
		for (int i = 0; i < (int)ReadSockets.fd_count; ++i)
		{
			if (FD_ISSET(ReadSockets.fd_array[i], &CopyReadSockets))
			{
				if (ReadSockets.fd_array[i] == ListenSocket)
				{
					//connect process
					SOCKADDR_IN ClientSockAddr;
					memset(&ClientSockAddr, 0, sizeof(ClientSockAddr));
					int ClientSockSockLength = sizeof(ClientSockAddr);

					//blocking, synchronous
					SOCKET ClientSocket = accept(ListenSocket, (SOCKADDR*)&ClientSockAddr, &ClientSockSockLength);

					cout << "connect client " << inet_ntoa(ClientSockAddr.sin_addr) << endl;

					FD_SET(ClientSocket, &ReadSockets);
				}
				else
				{
					//Data Receive

					////header
					//Header DataHeader;
					//int RecvBytes = RecvAll(ReadSockets.fd_array[i], (char*)&DataHeader, HeaderSize);
					//if (RecvBytes <= 0)
					//{
					//	cout << "header recv fail " << endl;
					//	DisconnectSocket(ReadSockets.fd_array[i], &ReadSockets);
					//	continue;
					//}

					//DataHeader.NetworkToHost();

					memset(Buffer, 0, sizeof(Buffer));
					//data JSON
					int RecvBytes = RecvAll(ReadSockets.fd_array[i], Buffer);
					if (RecvBytes <= 0)
					{
						cout << "data recv fail " << endl;
						DisconnectSocket(ReadSockets.fd_array[i], &ReadSockets);
						continue;
					}
					else
					{
						ProcessPacket(ReadSockets.fd_array[i], Buffer);
					}
				}
			}
		}
	}

	delete MyConnection;
	closesocket(ListenSocket);
	WSACleanup();

	return 0;
}