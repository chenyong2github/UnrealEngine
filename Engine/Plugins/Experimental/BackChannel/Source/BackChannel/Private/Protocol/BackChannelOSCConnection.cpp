// Copyright Epic Games, Inc. All Rights Reserved.

#include "BackChannel/Protocol/OSC/BackChannelOSCConnection.h"
#include "BackChannel/Private/BackChannelCommon.h"
#include "BackChannel/Transport/IBackChannelTransport.h"
#include "BackChannel/Protocol/OSC/BackChannelOSC.h"
#include "Sockets.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"

FBackChannelOSCConnection::FBackChannelOSCConnection(TSharedRef<IBackChannelSocketConnection> InConnection)
	: Connection(InConnection)
{
	const int32 kDefaultBufferSize = 4096;
	ReceiveBuffer.AddUninitialized(kDefaultBufferSize);
}

FBackChannelOSCConnection::~FBackChannelOSCConnection()
{
	UE_LOG(LogBackChannel, Verbose, TEXT("Destroying OSC Connection to %s"), *GetDescription());
	if (IsRunning)
	{
		Stop();
	}
}

FString FBackChannelOSCConnection::GetProtocolName() const
{
	return TEXT("BOSC");
}

TBackChannelSharedPtr<IBackChannelPacket> FBackChannelOSCConnection::CreatePacket()
{
	return MakeShared<FBackChannelOSCMessage, ESPMode::ThreadSafe>(OSCPacketMode::Write);
}

int FBackChannelOSCConnection::SendPacket(const TBackChannelSharedPtr<IBackChannelPacket>& Packet)
{
	FBackChannelOSCMessage* Message = static_cast<FBackChannelOSCMessage*>(Packet.Get());

	if (!SendPacket(*Message))
	{
		return -1;
	}

	return 0;
}


/* Bind a delegate to a message address */
FDelegateHandle FBackChannelOSCConnection::AddRouteDelegate(FStringView Path, FBackChannelRouteDelegate::FDelegate Delegate)
{
	FScopeLock Lock(&PacketMutex);
	return DispatchMap.AddRoute(Path, Delegate);
}

/* Remove a delegate handle */
void FBackChannelOSCConnection::RemoveRouteDelegate(FStringView Path, FDelegateHandle& InHandle)
{
	FScopeLock Lock(&PacketMutex);
	DispatchMap.RemoveRoute(Path, InHandle);
}

void FBackChannelOSCConnection::SetBufferSizes(int32 DesiredSendSize, int32 DesiredReceiveSize)
{
	if (Connection.IsValid())
	{
		Connection->SetBufferSizes(DesiredSendSize, DesiredReceiveSize);
	}
}


/*
FDelegateHandle FBackChannelOSCConnection::AddMessageHandler(const TCHAR* Path, FBackChannelDispatchDelegate::FDelegate Delegate)
{
	FScopeLock Lock(&PacketMutex);
	return LegacyDispatchMap.GetAddressHandler(Path).Add(Delegate);
}


void FBackChannelOSCConnection::RemoveMessageHandler(const TCHAR* Path, FDelegateHandle& InHandle)
{
	LegacyDispatchMap.GetAddressHandler(Path).Remove(InHandle);
	InHandle.Reset();
}*/

void FBackChannelOSCConnection::ReceiveAndDispatchMessages(const float MaxTime /*= 0*/)
{
	ReceiveMessages(MaxTime);
	DispatchMessages();
}

void FBackChannelOSCConnection::ReceiveMessages(const float MaxTime /*= 0*/)
{
	const double StartTime = FPlatformTime::Seconds();
    
    //UE_LOG(LogBackChannel, Log, TEXT("Starting Packet Check at %.02f"), StartTime);

	bool KeepReceiving = false;

	int PacketsReceived = 0;

	do
	{
		FScopeLock Lock(&ReceiveMutex);

		Connection->GetSocket()->Wait(ESocketWaitConditions::WaitForRead, FTimespan(0, 0, MaxTime));

		int32 Received = Connection->ReceiveData(ReceiveBuffer.GetData() + ReceivedDataSize, ExpectedSizeOfNextPacket - ReceivedDataSize);

		if (Received > 0)
		{
			ReceivedDataSize += Received;

			if (ReceivedDataSize == ExpectedSizeOfNextPacket)
			{
				// reset this
				ReceivedDataSize = 0;

				if (ExpectedSizeOfNextPacket == 4)
				{
					int32 Size(0);
					FMemory::Memcpy(&Size, ReceiveBuffer.GetData(), sizeof(int32));

					if (Size > ReceiveBuffer.Num())
					{
						ReceiveBuffer.AddUninitialized(Size - ReceiveBuffer.Num());
					}

					ExpectedSizeOfNextPacket = Size;
				}
				else
				{
					// read packet
					TSharedPtr<FBackChannelOSCPacket> Packet = FBackChannelOSCPacket::CreateFromBuffer(ReceiveBuffer.GetData(), ExpectedSizeOfNextPacket);

					if (Packet.IsValid())
					{
                        FScopeLock PacketLock(&PacketMutex);
                        
						bool bAddPacket = true;

						if (Packet->GetType() == OSCPacketType::Message)
						{
							auto MsgPacket = StaticCastSharedPtr<FBackChannelOSCMessage>(Packet);

							const FString NewAddress = MsgPacket->GetPath();

							UE_LOG(LogBackChannel, VeryVerbose, TEXT("Received message to %s (tags:%s, size:%d)"), *NewAddress, *MsgPacket->GetTags(), ExpectedSizeOfNextPacket);

							int32 CurrentCount = GetMessageCountForPath(*NewAddress);

							int32 MaxMessages = GetMessageLimitForPath(*NewAddress);

							if (CurrentCount > 0)
							{
								UE_CLOG(GBackChannelLogPackets, LogBackChannel, Log, TEXT("%s has %d unprocessed messages"), *NewAddress, CurrentCount + 1);

								if (MaxMessages > 0 && CurrentCount >= MaxMessages)
								{
									UE_LOG(LogBackChannel, VeryVerbose, TEXT("Discarding old messages due to limit of %d"), MaxMessages);
									RemoveMessagesWithPath(*NewAddress, 1);
								}
							}
						}
						else
						{
							UE_LOG(LogBackChannel, VeryVerbose, TEXT("Received #bundle of %d bytes"), ExpectedSizeOfNextPacket);
						}

						ReceivedPackets.Add(Packet);
					}

					ExpectedSizeOfNextPacket = 4;
					PacketsReceived++;
				}
			}
		}

		const double ElapsedTime = FPlatformTime::Seconds() - StartTime;

        // keep receiving until we run out of time, unless we received a packet
		KeepReceiving = ElapsedTime < MaxTime && PacketsReceived == 0;

	} while (KeepReceiving);
    
    const double ElapsedTime = FPlatformTime::Seconds() - StartTime;
    UE_LOG(LogBackChannel, VeryVerbose, TEXT("Received %d packets in %.03f secs at %.03f"), PacketsReceived, ElapsedTime, FPlatformTime::Seconds());
}

void FBackChannelOSCConnection::DispatchMessages()
{
	FScopeLock Lock(&PacketMutex);

	for (auto& Packet : ReceivedPackets)
	{
		if (Packet->GetType() == OSCPacketType::Message)
		{
			TSharedPtr<FBackChannelOSCMessage> Msg = StaticCastSharedPtr<FBackChannelOSCMessage>(Packet);

			UE_LOG(LogBackChannel, VeryVerbose, TEXT("Dispatching %s to handlers"), *Msg->GetPath());

			if (!DispatchMap.DispatchMessage(*Msg.Get()))
			{
				UE_LOG(LogBackChannel, Log, TEXT("Failed to displatch message to %s. No handler?"), *Msg->GetPath());
			}
			Msg->ResetRead();
		}
	}

	ReceivedPackets.Empty();
}

bool FBackChannelOSCConnection::StartReceiveThread()
{
	check(IsRunning == false);

	ExitRequested = false;

	// todo- expose this priority
	FRunnableThread* Thread = FRunnableThread::Create(this, TEXT("OSCHostConnection"), 1024 * 1024, TPri_AboveNormal);

	if (Thread)
	{
		IsRunning = true;
	}

	UE_LOG(LogBackChannel, Verbose, TEXT("Started OSC Connection to %s"), *GetDescription());

	return Thread != nullptr;
}

/* Returns true if running in the background */
bool FBackChannelOSCConnection::IsThreaded() const
{
	return IsRunning;
}

uint32 FBackChannelOSCConnection::Run()
{
    const float kMaxReceiveTime = 1;
	const int32 kDefaultBufferSize = 4096;

	TArray<uint8> Buffer;
	Buffer.AddUninitialized(kDefaultBufferSize);

	UE_LOG(LogBackChannel, Verbose, TEXT("OSC Connection to %s is Running"), *Connection->GetDescription());

	while (ExitRequested == false)
	{
        // This call itself will yield to the OS while waiting for recv so while this looks like a spinloop
        // it really isn't :)
        ReceiveAndDispatchMessages(kMaxReceiveTime);        
	}

	UE_LOG(LogBackChannel, Verbose, TEXT("OSC Connection to %s is exiting."), *Connection->GetDescription());
	IsRunning = false;
	return 0;
}

void FBackChannelOSCConnection::Stop()
{
	if (IsRunning)
	{
		UE_LOG(LogBackChannel, Verbose, TEXT("Requesting OSC Connection to stop.."));

		ExitRequested = true;

		while (IsRunning)
		{
			FPlatformProcess::SleepNoStats(0.01);
		}
	}

	UE_LOG(LogBackChannel, Verbose, TEXT("OSC Connection is stopped"));
	Connection = nullptr;
}

bool FBackChannelOSCConnection::IsConnected() const
{
	return Connection->IsConnected() && (HasErrorState == false);
}

bool FBackChannelOSCConnection::SendPacket(FBackChannelOSCPacket& Packet)
{
	FBackChannelOSCMessage* MsgPacket = (FBackChannelOSCMessage*)&Packet;

	TArray<uint8> Data = Packet.WriteToBuffer();

	UE_LOG(LogBackChannel, VeryVerbose, TEXT("Sent message to %s (tags:%s, size:%d)"), *MsgPacket->GetPath(), *MsgPacket->GetTags(), Data.Num());

	return SendPacketData(Data.GetData(), Data.Num());
}


bool FBackChannelOSCConnection::SendPacketData(const void* Data, const int32 DataLen)
{
	FScopeLock Lock(&SendMutex);

	if (!IsConnected())
	{
		return false;
	}

	// write size
	int32 Sent = 0;

	// TODO - differentiate between TCP / UDP 
	if (DataLen > 0)
	{
		// OSC over TCP requires a size followed by the packet (TODO - combine these?)
		Connection->SendData(&DataLen, sizeof(DataLen));

		ANSICHAR* RawData = (ANSICHAR*)Data;
		check(FCStringAnsi::Strlen(RawData) < 64);
		Sent = Connection->SendData(Data, DataLen);
	}

	return Sent > 0;
}

FString FBackChannelOSCConnection::GetDescription()
{
	return FString::Printf(TEXT("OSCConnection to %s"), *Connection->GetDescription());
}

void FBackChannelOSCConnection::SetMessageOptions(const TCHAR* Path, int32 MaxQueuedMessages)
{
	FScopeLock Lock(&PacketMutex);
	MessageLimits.FindOrAdd(Path) = MaxQueuedMessages;
}

int32 FBackChannelOSCConnection::GetMessageCountForPath(const TCHAR* Path)
{
	FScopeLock Lock(&PacketMutex);
	
	int32 Count = 0;

	for (auto& Packet : ReceivedPackets)
	{
		if (Packet->GetType() == OSCPacketType::Message)
		{
			auto Msg = StaticCastSharedPtr<FBackChannelOSCMessage>(Packet);

			if (Msg->GetPath() == Path)
			{
				Count++;
			}
		}
	}

	return Count;
}


int32 FBackChannelOSCConnection::GetMessageLimitForPath(const TCHAR* InPath)
{
	FScopeLock Lock(&PacketMutex);

	FString Path = InPath;

	if (Path.EndsWith(TEXT("*")))
	{
		Path.LeftChop(1);
	}

	// todo - search for vest match, not first match
	for (auto KV : MessageLimits)
	{
		const FString& Key = KV.Key;
		if (Path.StartsWith(Key))
		{
			return KV.Value;
		}
	}

	return -1;
}

void FBackChannelOSCConnection::RemoveMessagesWithPath(const TCHAR* Path, const int32 Num /*= 0*/)
{
	FScopeLock Lock(&PacketMutex);

	auto It = ReceivedPackets.CreateIterator();

	int32 RemovedCount = 0;

	while (It)
	{
		auto Packet = *It;
		bool bRemove = false;

		if (Packet->GetType() == OSCPacketType::Message)
		{
			TSharedPtr<FBackChannelOSCMessage> Msg = StaticCastSharedPtr<FBackChannelOSCMessage>(Packet);

			if (Msg->GetPath() == Path)
			{
				bRemove = true;
			}
		}

		if (bRemove)
		{
			It.RemoveCurrent();
			RemovedCount++;

			if (Num > 0 && RemovedCount == Num)
			{
				break;
			}
		}
		else
		{
			It++;
		}
	}
}

