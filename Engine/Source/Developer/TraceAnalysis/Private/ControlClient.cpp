// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Trace/ControlClient.h"
#include "IPAddress.h"
#include "Misc/CString.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FControlClient::~FControlClient()
{
    Disconnect();
}

////////////////////////////////////////////////////////////////////////////////
bool FControlClient::Connect(const TCHAR* Host, uint16 Port)
{
    if (IsConnected())
    {
        return false;
    }

	ISocketSubsystem& Sockets = *(ISocketSubsystem::Get());
	TSharedPtr<FInternetAddr> Addr = Sockets.CreateInternetAddr();
	bool bIsHostValid = false;
	Addr->SetIp(Host, bIsHostValid);
	if (!bIsHostValid)
	{
		ESocketErrors SocketErrors = Sockets.GetHostByName(TCHAR_TO_ANSI(Host), *Addr);
		if (SocketErrors != SE_NO_ERROR)
		{
			return false;
		}
	}
	Addr->SetPort(Port);
	return Connect(*Addr);
}

////////////////////////////////////////////////////////////////////////////////
bool FControlClient::Connect(const FInternetAddr& Address)
{
	if (IsConnected())
	{
		return false;
	}

	ISocketSubsystem& Sockets = *(ISocketSubsystem::Get());

	FSocket* ClientSocket = Sockets.CreateSocket(NAME_Stream, TEXT("TraceControlClient"));
	if (ClientSocket == nullptr)
	{
		return false;
	}

	if (!ClientSocket->Connect(Address))
	{
		Sockets.DestroySocket(ClientSocket);
		return false;
	}

	ClientSocket->SetLinger();

    Socket = ClientSocket;
    return true;
}

////////////////////////////////////////////////////////////////////////////////
void FControlClient::Disconnect()
{
    if (!IsConnected())
    {
        return;
    }

	Socket->Shutdown(ESocketShutdownMode::ReadWrite);
	Socket->Close();

	ISocketSubsystem& Sockets = *(ISocketSubsystem::Get());
	Sockets.DestroySocket(Socket);
	Socket = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
bool FControlClient::IsConnected() const
{
    return (Socket != nullptr);
}

////////////////////////////////////////////////////////////////////////////////
void FControlClient::SendConnect(const TCHAR* Path)
{
    if (!IsConnected())
    {
        return;
    }

    FormatAndSend(TEXT("Connect %s"), Path);
}

////////////////////////////////////////////////////////////////////////////////
void FControlClient::SendToggleEvent(const TCHAR* EventMask, bool bState)
{
    FormatAndSend(TEXT("ToggleEvent %s %d"), EventMask, bState);
}

////////////////////////////////////////////////////////////////////////////////
void FControlClient::Send(const TCHAR* Command)
{
	int Length = FCString::Strlen(Command);
	Send((const uint8*)TCHAR_TO_ANSI(Command), Length);
}

////////////////////////////////////////////////////////////////////////////////
void FControlClient::FormatAndSend(const TCHAR* Format, ...)
{
    if (!IsConnected())
    {
        return;
    }

    TCHAR Buffer[512];
	va_list Args;
	va_start(Args, Format);
	int Length = FCString::GetVarArgs(Buffer, ARRAY_COUNT(Buffer), Format, Args);
    if (Length > sizeof(Buffer))
    {
        Length = sizeof(Buffer);
    }
	va_end(Args);

	Send((const uint8*)TCHAR_TO_ANSI(Buffer), Length);
}

////////////////////////////////////////////////////////////////////////////////
void FControlClient::Send(const uint8* Data, int Length)
{
    int32 SentBytes = 0;
    if (!Socket->Send(Data, Length, SentBytes) || SentBytes != Length)
    {
        Disconnect();
		return;
    }

    if (!Socket->Send((const uint8*)"\n", 1, SentBytes) || SentBytes != 1)
    {
        Disconnect();
		return;
    }
}

} // namesapce Trace
