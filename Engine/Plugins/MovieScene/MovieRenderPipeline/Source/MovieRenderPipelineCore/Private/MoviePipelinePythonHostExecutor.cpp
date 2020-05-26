// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelinePythonHostExecutor.h"
#include "Misc/CoreDelegates.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "Interfaces/Ipv4/IPv4Address.h"
#include "Engine/Engine.h"

void UMoviePipelinePythonHostExecutor::Execute_Implementation(UMoviePipelineQueue* InPipelineQueue)
{
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (WorldContext.WorldType == EWorldType::Game)
		{
			LastLoadedWorld = WorldContext.World();
		}
	}

	PipelineQueue = InPipelineQueue;
	
	// Register C++ only callbacks that we will forward onto BP/Python
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMoviePipelinePythonHostExecutor::OnMapLoadFinished);
	FCoreDelegates::OnBeginFrame.AddUObject(this, &UMoviePipelinePythonHostExecutor::OnBeginFrame);

	// Attempt a socket connection so we can recieve external messages.
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("Attempting Socket connection to host..."));

	ExternalSocket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, TEXT("MoviePipelineExternalSocket"), false);

	// Port needs to match in your external connection.
	TSharedRef<FInternetAddr> Address = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	bool bIsValid = false;
	Address->SetIp(TEXT("127.0.0.1"), bIsValid);
	Address->SetPort(6783);

	// Attempt a connection.
	bSocketConnected = ExternalSocket->Connect(*Address);

	UE_LOG(LogMovieRenderPipeline, Log, TEXT("Socket Connected: %d"), bSocketConnected);

	// Now that we've done some C++ only things, call the Python version of this.
	ExecuteDelayed(InPipelineQueue);
}
	
void UMoviePipelinePythonHostExecutor::OnMapLoadFinished(UWorld* NewWorld)
{
	LastLoadedWorld = NewWorld;

	// This executor is only created after the world is loaded
	OnMapLoad(NewWorld);
}
	
void UMoviePipelinePythonHostExecutor::OnBeginFrame()
{
	if (ExternalSocket)
	{
		uint32 PendingDataSize = 0;
		while (ExternalSocket->HasPendingData(PendingDataSize) && PendingDataSize > 0)
		{
			TArray<uint8> ReceivedData;
			ReceivedData.SetNumZeroed(PendingDataSize + 1);

			int32 BytesRead = 0;
			if (ExternalSocket->Recv(ReceivedData.GetData(), ReceivedData.Num(), BytesRead) && BytesRead > 0)
			{
				ReceivedData.Last() = 0; // Ensure null terminator

				// Convert the message to a string
				FString Message = FString(UTF8_TO_TCHAR((char*)ReceivedData.GetData()));

				OnSocketMessageRecieved.Broadcast(Message);
			}
		}

	}
	
	OnTick();
}
	
void UMoviePipelinePythonHostExecutor::SendMessage(const char* InMessage)
{
	check(ExternalSocket);

	// Specify how long the message will be so on the recieving end we can tell stacked messages apart.
	int32 NumBytes = FCStringAnsi::Strlen(InMessage);
	int32 BytesSent = 0;
	ExternalSocket->Send((uint8*)&NumBytes, sizeof(int32), BytesSent);

	// Now send the actual message.
	ExternalSocket->Send((uint8*)InMessage, FCStringAnsi::Strlen(InMessage), BytesSent);
}

void UMoviePipelinePythonHostExecutor::SendSocketMessage(const FString& InMessage)
{
	if(!bSocketConnected || !ExternalSocket)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Attempted to send message but no socket connected, ignoring..."));
		return;
	}

	SendMessage(TCHAR_TO_UTF8(*InMessage));
}