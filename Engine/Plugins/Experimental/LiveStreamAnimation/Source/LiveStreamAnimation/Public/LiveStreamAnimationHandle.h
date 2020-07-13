// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LiveStreamAnimationHandle.generated.h"

/**
 * Generic handle that can be used to identify things over the network.
 * This works by using a preconfigured / preshared list of names (see ULiveStreamAnimationSubsystem::HandleNames)
 * and only replicating indices of that list.
 */
USTRUCT(BlueprintType, Category = "Live Stream Animation")
struct LIVESTREAMANIMATION_API FLiveStreamAnimationHandle
{
	GENERATED_BODY()

public:

	/**
	 * Create a default / invalid handle.
	 * Mainly used for serialization purposes.
	 */
	FLiveStreamAnimationHandle():
		Handle(INDEX_NONE)
	{
	}

	/**
	 * Create a handle from the given name.
	 * We will validate the name is in the prefconfigured list and convert
	 * it to the appropriate index.
	 * This handle will be invalid if the name isn't found.
	 */
	FLiveStreamAnimationHandle(FName InName):
		Handle(ValidateHandle(InName))
	{
	}

	/**
	 * Create a handle from the given index.
	 * We will validate the index is within bounds of the preconfigured list.
	 * If it is not, this handle will be invalid.
	 */
	FLiveStreamAnimationHandle(int32 InHandle):
		Handle(ValidateHandle(InHandle))
	{
	}

	bool IsValid() const
	{
		return INDEX_NONE != Handle;
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("%d"), Handle);
	}

	/** @return The matching handle name, or NAME_None if the handle is invalid. */
	FName GetName() const;

	int32 GetValue() const
	{
		return Handle;
	}

public:

	friend class FArchive& operator<<(class FArchive& InAr, FLiveStreamAnimationHandle& SubjectHandle);

	friend uint32 GetTypeHash(const FLiveStreamAnimationHandle& ToHash)
	{
		return static_cast<uint32>(ToHash.Handle);
	}

	friend bool operator==(const FLiveStreamAnimationHandle& LHS, const FLiveStreamAnimationHandle& RHS)
	{
		return LHS.Handle == RHS.Handle;
	}

private:

	static int32 ValidateHandle(FName NameToCheck);
	static int32 ValidateHandle(int32 Handle);
	
	int32 Handle = INDEX_NONE;
};