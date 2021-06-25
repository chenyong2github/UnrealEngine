// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookOnTheFly.h"
#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"

namespace UE { namespace Cook
{

FString FCookOnTheFlyMessageHeader::ToString() const
{
	return FString::Printf(TEXT("Message='%s', Status='%s', Sender='%u', CorrelationId='%u'"),
		LexToString(MessageType),
		LexToString(MessageStatus),
		SenderId,
		CorrelationId);
}

FArchive& operator<<(FArchive& Ar, FCookOnTheFlyMessageHeader& Header)
{
	uint32 MessageType = static_cast<uint32>(Header.MessageType);
	uint32 MessageStatus = static_cast<uint32>(Header.MessageStatus);
	
	Ar << MessageType;
	Ar << MessageStatus;
	Ar << Header.SenderId;
	Ar << Header.CorrelationId;
	Ar << Header.Timestamp;

	if (Ar.IsLoading())
	{
		Header.MessageType = static_cast<ECookOnTheFlyMessage>(MessageType);
		Header.MessageStatus = static_cast<ECookOnTheFlyMessageStatus>(MessageStatus);
	}

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FCookOnTheFlyMessage& Message)
{
	Ar << Message.Header;
	Ar << Message.Body;

	return Ar;
}

void FCookOnTheFlyMessage::SetBody(TArray<uint8> InBody)
{
	Body = MoveTemp(InBody);
}

TUniquePtr<FArchive> FCookOnTheFlyMessage::ReadBody() const
{
	return MakeUnique<FMemoryReader>(Body);
}

TUniquePtr<FArchive> FCookOnTheFlyMessage::WriteBody()
{
	return MakeUnique<FMemoryWriter>(Body);
}

bool GetCookOnTheFlyHost(UE::Cook::FCookOnTheFlyHostOptions& OutHostOptions)
{
	bool bOptionsOk = false;

	FString Host;
	if (FParse::Value(FCommandLine::Get(), TEXT("-CookOnTheFlyHost="), Host))
	{
		if (!Host.ParseIntoArray(OutHostOptions.Hosts, TEXT("+"), true))
		{
			OutHostOptions.Hosts.Add(Host);
		}

		bOptionsOk = true;
	}

	double ServerWaitTimeInSeconds;
	if (FParse::Value(FCommandLine::Get(), TEXT("-CookOnTheFlyServerWaitTime="), ServerWaitTimeInSeconds))
	{
		OutHostOptions.ServerStartupWaitTime = FTimespan::FromSeconds(ServerWaitTimeInSeconds);
	}

	return bOptionsOk;
}

bool SendCookOnTheFlyRequest(ECookOnTheFlyMessage RequestType, TFunction<void(FArchive&)>&& FillRequest, TFunction<bool(FArchive&)>&& ProcessResponse)
{
	if (!IsRunningCookOnTheFly())
	{
		return false;
	}

	static FString FileHostIP;
	static bool bUseFileServer = FParse::Value(FCommandLine::Get(), TEXT("filehostip"), FileHostIP);

	if (bUseFileServer)
	{
		struct FFileServerMessageHandler
			: public IPlatformFile::IFileServerMessageHandler
		{
			FFileServerMessageHandler(TFunction<void(FArchive&)>&& InFillRequest, TFunction<bool(FArchive&)>&& InProcessResponse)
				: FillRequestHandler(InFillRequest)
				, ProcessResponseHandler(InProcessResponse)
			{ }

			virtual void FillPayload(FArchive& Payload) override
			{
				FillRequestHandler(Payload);
			}

			virtual void ProcessResponse(FArchive& Response) override
			{
				ProcessResponseHandler(Response);
			}

			TFunction<void(FArchive&)> FillRequestHandler;
			TFunction<bool(FArchive&)> ProcessResponseHandler;
		};

		FFileServerMessageHandler MessageHandler(MoveTemp(FillRequest), MoveTemp(ProcessResponse));
		return IFileManager::Get().SendMessageToServer(LexToString(RequestType), &MessageHandler);
	}
	else
	{
		static ICookOnTheFlyModule& CookOnTheFlyModule = FModuleManager::LoadModuleChecked<ICookOnTheFlyModule>(TEXT("CookOnTheFly"));
		ICookOnTheFlyServerConnection& ServerConnection = CookOnTheFlyModule.GetServerConnection();

		FCookOnTheFlyRequest Request(RequestType);
		{
			TUniquePtr<FArchive> Ar = Request.WriteBody();
			FillRequest(*Ar);
		}

		FCookOnTheFlyResponse Response = ServerConnection.SendRequest(Request).Get();
		if (Response.IsOk())
		{
			TUniquePtr<FArchive> Ar = Response.ReadBody();
			ProcessResponse(*Ar);
		}

		return Response.IsOk();
	}
}

}} // namesapce UE::Cook
