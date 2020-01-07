// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "LiveLinkRole.h"
#include "LiveLinkTypes.h"
#include "Templates/SubclassOf.h"

#include "LiveLinkFrameInterpolationProcessor.generated.h"


/**
 * Basic object to interpolate live link frames
 * Inherit from it to do custom blending for your data type
 * @note It can be called from any thread
 */
class LIVELINKINTERFACE_API ILiveLinkFrameInterpolationProcessorWorker
{
public:
	virtual TSubclassOf<ULiveLinkRole> GetRole() const = 0;
	virtual void Interpolate(double InTime, const FLiveLinkStaticDataStruct& InStaticData, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, FLiveLinkSubjectFrameData& OutBlendedFrame) = 0;
	virtual void Interpolate(const FQualifiedFrameTime& InTime, const FLiveLinkStaticDataStruct& InStaticData, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, FLiveLinkSubjectFrameData& OutBlendedFrame) = 0;
};


/**
 * Basic object to interpolate live link frames
 * Inherit from it to do custom blending for your data type
 * @note It can only be used on the Game Thread. See ILiveLinkFrameInterpolationProcessorWorker for the any thread implementation.
 */
UCLASS(Abstract, editinlinenew, ClassGroup = (LiveLink))
class LIVELINKINTERFACE_API ULiveLinkFrameInterpolationProcessor : public UObject
{
	GENERATED_BODY()

public:
	using FWorkerSharedPtr = TSharedPtr<ILiveLinkFrameInterpolationProcessorWorker, ESPMode::ThreadSafe>;

	virtual TSubclassOf<ULiveLinkRole> GetRole() const PURE_VIRTUAL(ULiveLinkFrameInterpolationProcessor::GetFromRole, return TSubclassOf<ULiveLinkRole>(););
	virtual FWorkerSharedPtr FetchWorker() PURE_VIRTUAL(ULiveLinkFrameInterpolationProcessor::FetchWorker, return FWorkerSharedPtr(););
};
