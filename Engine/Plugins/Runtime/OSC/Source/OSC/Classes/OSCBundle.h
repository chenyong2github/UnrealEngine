// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once 

#include "OSCPacket.h"
#include "OSCBundle.generated.h"

class FOSCBundlePacket : public FOSCPacket
{
public:
	FOSCBundlePacket();
	~FOSCBundlePacket();
	
	/** Set the bundle time tag. */
	void SetTimeTag(uint64 NewTimeTag);

	/** Get the bundle time tag. */
	uint64 GetTimeTag() const;

	/** Get the number of OSC packets stored in the bundle. */
	int32 GetNumPackets() const { return Packets.Num(); }

	/** Add OSC packet in the bundle. */
	void AddPacket(TSharedPtr<FOSCPacket> Packet);
	
	/** Get OSC packet by index. */
	TSharedPtr<FOSCPacket> GetPacket(int Index) const { return Packets[Index]; }
	
	/** Clear bundle packet list. */
	void Clear() { Packets.Reset(); }

	/** Returns true. */
	virtual bool IsBundle() { return true; }

	/** Write Bundle data into the OSC stream. */
	virtual void WriteData(FOSCStream& Stream);

	/** Read Bundle data from an OSC stream*/
	virtual void ReadData(FOSCStream& Stream);

private:

	/** Bundle OSC packets. */
	TArray<TSharedPtr<FOSCPacket>> Packets;
	
	/** Bundle time tag. */
	FOSCType TimeTag;
};

USTRUCT(BlueprintType)
struct OSC_API FOSCBundle
{
	GENERATED_USTRUCT_BODY()

	FOSCBundle() = default;
	~FOSCBundle()
	{
		Packet.Reset();
	}

	bool IsValid() const 
	{
		return Packet.IsValid();
	}

	void SetPacket(TSharedPtr<FOSCBundlePacket> InPacket)
	{
		this->Packet = InPacket;
	}

	const TSharedPtr<FOSCBundlePacket> GetPacket() const
	{
		return Packet;
	}

	const TSharedPtr<FOSCBundlePacket> GetOrCreatePacket();

private:
	TSharedPtr<FOSCBundlePacket> Packet;
};