// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkFramePreProcessor.h"
#include "LiveLinkAxisSwitchPreProcessor.generated.h"


UENUM()
enum class ELiveLinkAxis : uint8
{
	X = 0 UMETA(DisplayName = "X-Axis"),
	Y = 1 UMETA(DisplayName = "Y-Axis"),
	Z = 2 UMETA(DisplayName = "Z-Axis"),
	XNeg = 3 UMETA(DisplayName = "-X-Axis"),
	YNeg = 4 UMETA(DisplayName = "-Y-Axis"),
	ZNeg = 5 UMETA(DisplayName = "-Z-Axis"),
};

/**
 * Allows to switch any axis of an incoming transform with another axis.
 * @note For example the Z-Axis of an incoming transform can be set to the (optionally negated) Y-Axis of the transform in UE.
 * @note This implies that translation, rotation and scale will be affected by switching an axis.
 */
UCLASS(meta = (DisplayName = "Transform Axis Switch"))
class LIVELINK_API ULiveLinkTransformAxisSwitchPreProcessor : public ULiveLinkFramePreProcessor
{
	GENERATED_BODY()

public:
	class FLiveLinkTransformAxisSwitchPreProcessorWorker : public ILiveLinkFramePreProcessorWorker
	{
	public:
		ELiveLinkAxis OrientationAxisX = ELiveLinkAxis::X;
		ELiveLinkAxis OrientationAxisY = ELiveLinkAxis::Y;
		ELiveLinkAxis OrientationAxisZ = ELiveLinkAxis::Z;
		ELiveLinkAxis TranslationAxisX = ELiveLinkAxis::X;
		ELiveLinkAxis TranslationAxisY = ELiveLinkAxis::Y;
		ELiveLinkAxis TranslationAxisZ = ELiveLinkAxis::Z;

		virtual TSubclassOf<ULiveLinkRole> GetRole() const override;
		virtual bool PreProcessFrame(FLiveLinkFrameDataStruct& InOutFrame) const override;
	};

protected:
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	ELiveLinkAxis OrientationAxisX = ELiveLinkAxis::X;
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	ELiveLinkAxis OrientationAxisY = ELiveLinkAxis::Y;
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	ELiveLinkAxis OrientationAxisZ = ELiveLinkAxis::Z;
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	ELiveLinkAxis TranslationAxisX = ELiveLinkAxis::X;
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	ELiveLinkAxis TranslationAxisY = ELiveLinkAxis::Y;
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	ELiveLinkAxis TranslationAxisZ = ELiveLinkAxis::Z;

public:
	virtual TSubclassOf<ULiveLinkRole> GetRole() const override;
	virtual ULiveLinkFramePreProcessor::FWorkerSharedPtr FetchWorker() override;

public:
	//~ UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR
	//~ End UObject interface

protected:
	TSharedPtr<FLiveLinkTransformAxisSwitchPreProcessorWorker, ESPMode::ThreadSafe> Instance;
};

/**
 * Allows to switch any axis of an incoming animation with another axis.
 * @note For example the Z-Axis of an incoming transform can be set to the (optionally negated) Y-Axis of the transform in UE.
 * @note This implies that translation, rotation and scale will be affected by switching an axis.
 */
UCLASS(meta = (DisplayName = "Animation Axis Switch"))
class LIVELINK_API ULiveLinkAnimationAxisSwitchPreProcessor : public ULiveLinkTransformAxisSwitchPreProcessor
{
	GENERATED_BODY()

public:
	class FLiveLinkAnimationAxisSwitchPreProcessorWorker : public ULiveLinkTransformAxisSwitchPreProcessor::FLiveLinkTransformAxisSwitchPreProcessorWorker
	{
	public:
		virtual TSubclassOf<ULiveLinkRole> GetRole() const override;
		virtual bool PreProcessFrame(FLiveLinkFrameDataStruct& InOutFrame) const override;
	};

public:
	virtual TSubclassOf<ULiveLinkRole> GetRole() const override;
	virtual ULiveLinkFramePreProcessor::FWorkerSharedPtr FetchWorker() override;
};
