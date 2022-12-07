// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerMappableKeySlot.generated.h"

/**
* Explicitly identifies the slot for a player mappable key
*/
USTRUCT(BlueprintType)
struct ENHANCEDINPUT_API FPlayerMappableKeySlot
{
	GENERATED_BODY()

public:

	FPlayerMappableKeySlot();
	FPlayerMappableKeySlot(int32 InSlotNumber);

	virtual int32 GetSlotNumber() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
	int32 SlotNumber = 0;

	static FPlayerMappableKeySlot FirstKeySlot;
	static FPlayerMappableKeySlot SecondKeySlot;
	static FPlayerMappableKeySlot ThirdKeySlot;
	static FPlayerMappableKeySlot FourthKeySlot;

	bool operator==(const FPlayerMappableKeySlot& OtherKeySlot) const
	{
		return GetSlotNumber() == OtherKeySlot.GetSlotNumber();
	}

};

ENHANCEDINPUT_API uint32 GetTypeHash(const FPlayerMappableKeySlot& Ref);
