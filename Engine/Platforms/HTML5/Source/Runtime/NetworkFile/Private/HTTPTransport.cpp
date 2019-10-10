// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HTTPTransport.h"

#ifdef __EMSCRIPTEN__

#include "Serialization/BufferArchive.h"
#include "NetworkMessage.h"

#include "HTML5JavaScriptFx.h"
#include <emscripten/emscripten.h>


FHTTPTransport::FHTTPTransport()
	:Guid(FGuid::NewGuid())
{
}

bool FHTTPTransport::Initialize(const TCHAR* InHostIp)
{
	// parse out the format
	FString HostIp = InHostIp;

	// make sure that we have the correct protcol
	ensure( HostIp.RemoveFromStart("http://") );

	// strip webserver port
	if ( HostIp.Contains(":") )
	{
		HostIp = HostIp.Left(HostIp.Find(":"));
	}
	// append file server port
	HostIp = FString::Printf(TEXT("%s:%d"), *HostIp, (int)(DEFAULT_HTTP_FILE_SERVING_PORT) );

	// make sure that our string is again correctly formated
	HostIp = FString::Printf(TEXT("http://%s"),*HostIp);

	FCString::Strncpy(Url, *HostIp, UE_ARRAY_COUNT(Url));
	emscripten_log(EM_LOG_CONSOLE , "Unreal File Server URL : %s ", TCHAR_TO_ANSI(Url));

	TArray<uint8> In,Out;
	bool RetResult = SendPayloadAndReceiveResponse(In,Out);
	return RetResult;
}

bool FHTTPTransport::SendPayloadAndReceiveResponse(TArray<uint8>& In, TArray<uint8>& Out)
{
	ReceiveBuffer.Empty();
	ReadPtr = 0;

	FBufferArchive Ar;
	if ( In.Num() )
	{
		Ar << Guid;
		Ar.Append(In);
	}

	unsigned char *OutData = NULL;
	unsigned int OutSize= 0;

	bool RetVal = true;

	UE_SendAndRecievePayLoad(TCHAR_TO_ANSI(Url),(char*)Ar.GetData(),Ar.Num(),(char**)&OutData,(int*)&OutSize);

//	if (!Ar.Num())
	{
		uint32 Size = OutSize;
		uint32 Marker = 0xDeadBeef;
		ReceiveBuffer.Append((uint8*)&Marker,sizeof(uint32));
		ReceiveBuffer.Append((uint8*)&Size,sizeof(uint32));
	}

	if (OutSize)
	{
		ReceiveBuffer.Append(OutData,OutSize);

		// don't go through the Unreal Memory system.
		::free(OutData);
	}

	return RetVal & ReceiveResponse(Out);
}

bool FHTTPTransport::ReceiveResponse(TArray<uint8> &Out)
{
	// Read one Packet from Receive Buffer.
	// read the size.

	uint32 Marker = *(uint32*)(ReceiveBuffer.GetData() + ReadPtr);
	uint32 Size = *(uint32*)(ReceiveBuffer.GetData() + ReadPtr + sizeof(uint32));

	// make sure we have the right amount of data available in the buffer.
	check( (ReadPtr + Size + 2*sizeof(uint32)) <= ReceiveBuffer.Num());

	Out.Append(ReceiveBuffer.GetData() + ReadPtr + 2*sizeof(uint32),Size);

	ReadPtr += 2*sizeof(uint32) + Size;

	return true;
}

#endif // __EMSCRIPTEN__
