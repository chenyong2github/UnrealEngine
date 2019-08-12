// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkFrameInterpolationProcessor.h"
#include "LiveLinkRole.h"
#include "LiveLinkTypes.h"
#include "LiveLinkBasicRole.generated.h"

/**
 * Role associated for no specific role data.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Basic Role"))
class LIVELINKINTERFACE_API ULiveLinkBasicRole : public ULiveLinkRole
{
	GENERATED_BODY()

public:
	virtual UScriptStruct* GetStaticDataStruct() const override;
	virtual UScriptStruct* GetFrameDataStruct() const override;
	virtual UScriptStruct* GetBlueprintDataStruct() const override;
	
	virtual bool InitializeBlueprintData(const FLiveLinkSubjectFrameData& InSourceData, FLiveLinkBlueprintDataStruct& OutBlueprintData) const override;

	virtual FText GetDisplayName() const override;
};

/**
 * Default blending method for any type of frames.
 * It will interpolate numerical properties that are mark with "Interp".
 */
UCLASS(meta = (DisplayName = "Base Interpolation"))
class LIVELINKINTERFACE_API ULiveLinkBasicFrameInterpolateProcessor : public ULiveLinkFrameInterpolationProcessor
{
	GENERATED_BODY()

public:
	class LIVELINKINTERFACE_API FLiveLinkBasicFrameInterpolateProcessorWorker : public ILiveLinkFrameInterpolationProcessorWorker
	{
	public:
		FLiveLinkBasicFrameInterpolateProcessorWorker(bool bInterpolatePropertyValues);

		virtual TSubclassOf<ULiveLinkRole> GetRole() const override;
		virtual void Interpolate(double InTime, const FLiveLinkStaticDataStruct& InStaticData, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, FLiveLinkSubjectFrameData& OutBlendedFrame) override;
		virtual void Interpolate(const FQualifiedFrameTime& InTime, const FLiveLinkStaticDataStruct& InStaticData, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, FLiveLinkSubjectFrameData& OutBlendedFrame) override;

		struct FGenericInterpolateOptions
		{
			bool bInterpolatePropertyValues = true;
			bool bCopyClosestFrame = true;
			bool bCopyClosestMetaData = true; // only used if bCopyClosestFrame is false
			bool bInterpolateInterpProperties = true;
		};

		static void GenericInterpolate(double InBlendFactor, const FGenericInterpolateOptions& Options, const FLiveLinkFrameDataStruct& FrameDataA, const FLiveLinkFrameDataStruct& FrameDataB, FLiveLinkFrameDataStruct& OutBlendedFrame);
		static double GetBlendFactor(double InTime, const FLiveLinkFrameDataStruct& FrameDataA, const FLiveLinkFrameDataStruct& FrameDataB);
		static double GetBlendFactor(FQualifiedFrameTime InTime, const FLiveLinkFrameDataStruct& FrameDataA, const FLiveLinkFrameDataStruct& FrameDataB);
		static bool FindInterpolateIndex(double InTime, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, int32& OutFrameIndexA, int32& OutFrameIndexB);
		static bool FindInterpolateIndex(FQualifiedFrameTime InTime, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, int32& OutFrameIndexA, int32& OutFrameIndexB);

	protected:
		bool bInterpolatePropertyValues = true;
	};

public:
	virtual TSubclassOf<ULiveLinkRole> GetRole() const override;
	virtual ULiveLinkFrameInterpolationProcessor::FWorkerSharedPtr FetchWorker() override;

public:
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	bool bInterpolatePropertyValues = true;

private:
	TSharedPtr<FLiveLinkBasicFrameInterpolateProcessorWorker, ESPMode::ThreadSafe> BaseInstance;
};