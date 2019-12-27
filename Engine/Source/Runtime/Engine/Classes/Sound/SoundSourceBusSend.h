// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "SoundSourceBusSend.generated.h"

class USoundSourceBus;

UENUM(BlueprintType)
enum class ESourceBusSendLevelControlMethod : uint8
{
	// A send based on linear interpolation between a distance range and send-level range
	Linear,

	// A send based on a supplied curve
	CustomCurve,

	// A manual send level (Uses the specified constant send level value. Useful for 2D sounds.)
	Manual,
};

USTRUCT(BlueprintType)
struct ENGINE_API FSoundSourceBusSendInfo
{
	GENERATED_USTRUCT_BODY()

	/*
		Manual: Use Send Level only
		Linear: Interpolate between Min and Max Send Levels based on listener distance (between Distance Min and Distance Max)
		Custom Curve: Use the float curve to map Send Level to distance (0.0-1.0 on curve maps to Distance Min - Distance Max)
	*/
	UPROPERTY(EditAnywhere, Category = SourceBusSend)
	ESourceBusSendLevelControlMethod SourceBusSendLevelControlMethod;

	// The Source Bus to send the audio to
	UPROPERTY(EditAnywhere, Category = SourceBusSend)
	USoundSourceBus* SoundSourceBus;

	// The amount of audio to send to the source bus
	UPROPERTY(EditAnywhere, Category = SourceBusSend)
	float SendLevel;

	// The amount to send to the Source Bus when sound is located at a distance equal to value specified in the min send distance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SourceBusSend)
	float MinSendLevel;

	// The amount to send to the Source Bus when sound is located at a distance equal to value specified in the max send distance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SourceBusSend)
	float MaxSendLevel;

	// The distance at which the Min Send Level is sent to the source bus
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SourceBusSend)
	float MinSendDistance;

	// The distance at which the Max Send Level is sent to the source bus
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SourceBusSend)
	float MaxSendDistance;

	// The custom curve to use for distance-based Source Bus send level.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SourceBusSend)
	FRuntimeFloatCurve CustomSendLevelCurve;

	FSoundSourceBusSendInfo()
		: SourceBusSendLevelControlMethod(ESourceBusSendLevelControlMethod::Manual)
		, SoundSourceBus(nullptr)
		, SendLevel(0.0f)
		, MinSendLevel(0.0f)
		, MaxSendLevel(1.0f)
		, MinSendDistance(100.0f)
		, MaxSendDistance(1000.0f)
	{
	}
};
