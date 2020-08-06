// Copyright Epic Games, Inc. All Rights Reserverd.

#include "LiveStreamAnimationDataHandler.h"
#include "LiveStreamAnimationSubsystem.h"
#include "LiveStreamAnimationPacket.h"
#include "LiveStreamAnimationLog.h"

ELiveStreamAnimationRole ULiveStreamAnimationDataHandler::GetRole() const
{
	return OwningSubsystem ? OwningSubsystem->GetRole() : ELiveStreamAnimationRole::Proxy;
}

void ULiveStreamAnimationDataHandler::Startup(ULiveStreamAnimationSubsystem* InOwningSubsystem, const uint32 InAssignedPacketType)
{
	check(InOwningSubsystem != nullptr);
	check(InAssignedPacketType != 0);

	OwningSubsystem = InOwningSubsystem;
	PacketType = InAssignedPacketType;

	OnStartup();
}

void ULiveStreamAnimationDataHandler::Shutdown()
{
	OnShutdown();

	OwningSubsystem = nullptr;
	PacketType = INDEX_NONE;
}

bool ULiveStreamAnimationDataHandler::SendPacketToServer(TArray<uint8>&& PacketData, const bool bReliable)
{
	using namespace LiveStreamAnimation;

	if (OwningSubsystem != nullptr)
	{
		TSharedPtr<FLiveStreamAnimationPacket> PacketToSend = FLiveStreamAnimationPacket::CreateFromData(PacketType, MoveTemp(PacketData));
		if (PacketToSend.IsValid())
		{
			TSharedRef<FLiveStreamAnimationPacket> CreatedPacket = PacketToSend.ToSharedRef();
			CreatedPacket->SetReliable(bReliable);
			return OwningSubsystem->SendPacketToServer(CreatedPacket);
		}
		else
		{
			UE_LOG(LogLiveStreamAnimation, Warning, TEXT("ULiveStreamAnimationDataHandler::SendPacketToServer: Unable to create packet! Class = %s"), *GetPathNameSafe(GetClass()));
		}
	}
	else
	{
		UE_LOG(LogLiveStreamAnimation, Warning, TEXT("ULiveStreamAnimationDataHandler::SendPacketToServer: Attempting to send packet when OwningSubsystem is invalid! Class = %s"), *GetPathNameSafe(GetClass()));
	}

	return false;
}