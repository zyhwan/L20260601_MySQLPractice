#include "pch.h"
#include "SessionManager.h"
#include <algorithm>

void SessionManager::Add(Session InSession)
{
	//찾아보고 이미 있는 클라이언트라면 반환
	for (auto Iter = SessionList.begin(); Iter != SessionList.end(); ++Iter)
	{
		if ((*Iter).ClientSocket == InSession.ClientSocket)
		{
			return;
		}
	}

	SessionList.push_back(InSession);
}

//[][][][][][]
void SessionManager::Delete(Session InSession)
{
	SessionList.erase(std::find(SessionList.begin(), SessionList.end(), InSession));
}

Session* SessionManager::GetSession(int Index)
{
	// TODO: insert return statement here
	return &SessionList[Index];
}

Session* SessionManager::GetSession(const SOCKET& InClentSocket)
{
	// TODO: insert return statement here
	for (auto Iter = SessionList.begin(); Iter != SessionList.end(); ++Iter)
	{
		if ((*Iter).ClientSocket == InClentSocket)
		{
			return &(*Iter);
		}
	}

	return nullptr;
}

Session* SessionManager::GetSession(const Session& InSession)
{
	return &(*std::find(SessionList.begin(), SessionList.end(), InSession));
}
