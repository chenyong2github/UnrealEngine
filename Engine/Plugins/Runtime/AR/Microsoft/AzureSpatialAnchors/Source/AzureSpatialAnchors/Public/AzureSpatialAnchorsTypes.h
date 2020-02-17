// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "AzureSpatialAnchorsTypes.generated.h"

AZURESPATIALANCHORS_API DECLARE_LOG_CATEGORY_EXTERN(LogAzureSpatialAnchors, Log, All);

// Note: this must match winrt::Microsoft::Azure::SpatialAnchors::SessionLogLevel
UENUM(BlueprintType, Category = "AzureSpatialAnchors")
enum class EAzureSpatialAnchorsLogVerbosity : uint8
{
	None = 0,
	Error = 1,
	Warning = 2,
	Information = 3,
	Debug = 4,
	All = 5,
};

// Note: this Result enum must match AzureSpatialAnchorsInterop::AsyncResult in MixedRealityInterop.h
UENUM(BlueprintType, Category = "AzureSpatialAnchors")
enum class EAzureSpatialAnchorsResult : uint8
{
	NotStarted,
	Started,
	FailBadAnchorId,
	FailAnchorIdAlreadyUsed,
	FailAnchorDoesNotExist,
	FailAnchorAlreadyTracked,
	FailNoAnchor,
	FailNoLocalAnchor,
	FailNoSession,
	FailNotEnoughData,
	FailSeeErrorString,
	NotLocated,
	Canceled,
	Success
};