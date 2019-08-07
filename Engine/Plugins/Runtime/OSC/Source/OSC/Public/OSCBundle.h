// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once 

#include "OSCPacket.h"
#include "OSCBundle.generated.h"


USTRUCT(BlueprintType)
struct OSC_API FOSCBundle
{
	GENERATED_USTRUCT_BODY()

	FOSCBundle()
		: Packet(MakeShareable(new FOSCBundlePacket()))
	{
	}

	FOSCBundle(const TSharedPtr<FOSCBundlePacket>& InPacket)
		: Packet(InPacket)
	{
	}

	~FOSCBundle()
	{
		Packet.Reset();
	}

	void SetPacket(const TSharedPtr<FOSCBundlePacket>& InPacket)
	{
		Packet = InPacket;
	}

	const TSharedPtr<FOSCBundlePacket>& GetPacket() const
	{
		return Packet;
	}

private:
	TSharedPtr<FOSCBundlePacket> Packet;
};