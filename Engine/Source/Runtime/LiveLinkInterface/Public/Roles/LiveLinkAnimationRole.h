// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkFrameTranslator.h"
#include "LiveLinkFrameInterpolationProcessor.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Roles/LiveLinkBasicRole.h"
#include "LiveLinkAnimationRole.generated.h"

/**
 * Role associated for Animation / Skeleton data.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Animation Role"))
class LIVELINKINTERFACE_API ULiveLinkAnimationRole : public ULiveLinkBasicRole
{
	GENERATED_BODY()

public:
	virtual UScriptStruct* GetStaticDataStruct() const override;
	virtual UScriptStruct* GetFrameDataStruct() const override;
	virtual UScriptStruct* GetBlueprintDataStruct() const override;

	bool InitializeBlueprintData(const FLiveLinkSubjectFrameData& InSourceData, FLiveLinkBlueprintDataStruct& OutBlueprintData) const override;

	virtual FText GetDisplayName() const override;
};


/**
 * Default blending method for animation frames
 */
UCLASS(meta=(DisplayName="Animation Interpolation"))
class LIVELINKINTERFACE_API ULiveLinkAnimationFrameInterpolateProcessor : public ULiveLinkBasicFrameInterpolateProcessor
{
	GENERATED_BODY()

public:
	class FLiveLinkAnimationFrameInterpolateProcessorWorker : public ULiveLinkBasicFrameInterpolateProcessor::FLiveLinkBasicFrameInterpolateProcessorWorker
	{
	public:
		FLiveLinkAnimationFrameInterpolateProcessorWorker(bool bInterpolatePropertyValues);

		virtual TSubclassOf<ULiveLinkRole> GetRole() const override;
		virtual void Interpolate(double InTime, const FLiveLinkStaticDataStruct& InStaticData, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, FLiveLinkSubjectFrameData& OutBlendedFrame) override;
		virtual void Interpolate(const FQualifiedFrameTime& InTime, const FLiveLinkStaticDataStruct& InStaticData, const TArray<FLiveLinkFrameDataStruct>& InSourceFrames, FLiveLinkSubjectFrameData& OutBlendedFrame) override;
	};

public:
	virtual TSubclassOf<ULiveLinkRole> GetRole() const override;
	virtual ULiveLinkFrameInterpolationProcessor::FWorkerSharedPtr FetchWorker() override;

private:
	TSharedPtr<FLiveLinkAnimationFrameInterpolateProcessorWorker, ESPMode::ThreadSafe> Instance;
};


/**
 * Basic object to translate data from one role to another
 */
UCLASS(meta=(DisplayName="Animation To Transform"))
class LIVELINKINTERFACE_API ULiveLinkAnimationRoleToTransform : public ULiveLinkFrameTranslator
{
	GENERATED_BODY()

public:
	class FLiveLinkAnimationRoleToTransformWorker : public ILiveLinkFrameTranslatorWorker
	{
	public:
		FName BoneName;
		virtual TSubclassOf<ULiveLinkRole> GetFromRole() const override;
		virtual TSubclassOf<ULiveLinkRole> GetToRole() const override;
		virtual bool Translate(const FLiveLinkStaticDataStruct& InStaticData, const FLiveLinkFrameDataStruct& InFrameData, FLiveLinkSubjectFrameData& OutTranslatedFrame) const override;
	};

protected:
	UPROPERTY(EditAnywhere, Category="LiveLink")
	FName BoneName;

public:
	virtual TSubclassOf<ULiveLinkRole> GetFromRole() const override;
	virtual TSubclassOf<ULiveLinkRole> GetToRole() const override;
	virtual ULiveLinkFrameTranslator::FWorkerSharedPtr FetchWorker() override;

public:
	//~ UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR
	//~ End UObject interface

private:
	TSharedPtr<FLiveLinkAnimationRoleToTransformWorker, ESPMode::ThreadSafe> Instance;
};
