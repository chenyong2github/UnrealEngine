// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "OSCLog.h"

#include "OSCAddress.generated.h"


namespace OSC
{
	const FString BundleTag		= TEXT("#bundle");
	const FString PathSeparator = TEXT("/");
} // namespace OSC

USTRUCT(BlueprintType)
struct OSC_API FOSCAddress
{
	GENERATED_USTRUCT_BODY()

public:
	FOSCAddress()
	{
		Value = OSC::PathSeparator;
	}

	FOSCAddress(const FString& InValue)
		: Value(InValue)
	{
		if (!IsBundle())
		{
			if (!Value.StartsWith(OSC::PathSeparator))
			{
				Value = OSC::PathSeparator + Value;
			}
		}
	}

	UPROPERTY(BlueprintReadWrite, Category = "Audio|OSC")
	FString Value;

	FORCEINLINE bool IsBundle() const
	{
		return Value.StartsWith(OSC::BundleTag);
	}

	FORCEINLINE bool IsMessage() const
	{
		return Value.StartsWith(OSC::PathSeparator);
	}

	FORCEINLINE bool IsValid() const
	{
		return IsMessage() || IsBundle();
	}

	FORCEINLINE void Append(const FString& ToAppend)
	{
		if (!IsMessage())
		{
			UE_LOG(LogOSC, Warning, TEXT("Append failed for OSCAddress '%s'. Address is either invalid/non-message type."), *Value);
			return;
		}

		Value.PathAppend(*ToAppend, ToAppend.Len());
	}

	FORCEINLINE void Split(TArray<FString>& OutArray) const
	{
		if (!IsMessage())
		{
			UE_LOG(LogOSC, Warning, TEXT("Split failed for OSCAddress '%s'. Address is invalid/non-message type."), *Value);
			return;
		}

		Value.ParseIntoArray(OutArray, *OSC::PathSeparator, true);
	}
};
