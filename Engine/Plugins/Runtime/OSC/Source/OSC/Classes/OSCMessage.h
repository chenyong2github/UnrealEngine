// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"
#include "OSCPacket.h"
#include "OSCMessage.generated.h"

class FOSCStream;
class FOSCMessagePacket : public FOSCPacket
{
public:
	FOSCMessagePacket();
	~FOSCMessagePacket();

	/** Set OSC message address. */
	void SetAddress(const FString& InAddress) { this->Address = InAddress; }
	
	/** Get OSC message address. */
	const FString& GetAddress() const { return Address; }
	
	/** Add OSC Type as an argument into the OSC message. */
	void AddArgument(FOSCType type) { Arguments.Add(type); }

	/** Get argument by index. */
	const FOSCType& GetArgument(int i) const { return Arguments[i]; }
	
	/** Get number of arguments. */
	int GetNumArguments() const { return Arguments.Num(); }

	/** Clear OSC Message. */
	void Clear() { Arguments.Reset(); }

	/** Returns true to indicate its an OSC message. */
	virtual bool IsMessage() override { return true; }

	/** Write message data into an OSC stream. */
	virtual void WriteData(FOSCStream& Stream);

	/** Read message data from an OSC stream. */
	virtual void ReadData(FOSCStream& Stream);

private:

	/** Message address. */
	FString Address; 

	/** List of argument types. */
	TArray<FOSCType> Arguments;
};

USTRUCT(BlueprintType)
struct OSC_API FOSCMessage
{
	GENERATED_USTRUCT_BODY()

	FOSCMessage() = default;
	~FOSCMessage()
	{
		Packet.Reset();
	}

	bool IsValid() const
	{
		return Packet.IsValid();
	}

	void SetPacket(TSharedPtr<FOSCMessagePacket> InPacket)
	{
		this->Packet = InPacket;
	}

	const TSharedPtr<FOSCMessagePacket> GetPacket() const
	{
		return Packet;
	}

	const TSharedPtr<FOSCMessagePacket> GetOrCreatePacket();

private:

	/** The OSC Message packet */
	TSharedPtr<FOSCMessagePacket> Packet;
};