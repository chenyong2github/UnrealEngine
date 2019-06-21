// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LiveLinkFramePreProcessor.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkBasicRole.h"
#include "LiveLinkTransformRole.generated.h"

/**
 * Role associated for Camera data.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Transform Role"))
class LIVELINKINTERFACE_API ULiveLinkTransformRole : public ULiveLinkBasicRole
{
	GENERATED_BODY()

public:
	virtual UScriptStruct* GetStaticDataStruct() const override;
	virtual UScriptStruct* GetFrameDataStruct() const override;
	virtual UScriptStruct* GetBlueprintDataStruct() const override;
	
	virtual bool InitializeBlueprintData(const FLiveLinkSubjectFrameData& InSourceData, FLiveLinkBlueprintDataStruct& OutBlueprintData) const override;

	virtual FText GetDisplayName() const override;
};

UENUM()
enum class ELiveLinkAxis : uint8
{
	X UMETA(DisplayName = "X-Axis"),
	Y UMETA(DisplayName = "Y-Axis"),
	Z UMETA(DisplayName = "Z-Axis"),
	XNeg UMETA(DisplayName = "-X-Axis"),
	YNeg UMETA(DisplayName = "-Y-Axis"),
	ZNeg UMETA(DisplayName = "-Z-Axis"),
};

/**
 * Allows to switch any axis of an incoming transform with another axis.
 * @note For example the Z-Axis of an incoming transform can be set to the (optionally negated) Y-Axis of the transform in UE.
 * @note This implies that translation, rotation and scale will be affected by switching an axis.
 */
UCLASS(meta = (DisplayName = "Axis Switch"))
class LIVELINKINTERFACE_API ULiveLinkAxisSwitchPreProcessor : public ULiveLinkFramePreProcessor
{
	GENERATED_BODY()

public:
	class FLiveLinkAxisSwitchPreProcessorWorker : public ILiveLinkFramePreProcessorWorker
	{
	public:
		ELiveLinkAxis AxisX = ELiveLinkAxis::X;
		ELiveLinkAxis AxisY = ELiveLinkAxis::Y;
		ELiveLinkAxis AxisZ = ELiveLinkAxis::Z;

		virtual TSubclassOf<ULiveLinkRole> GetRole() const override;
		virtual bool PreProcessFrame(FLiveLinkFrameDataStruct& InOutFrame) const override;
	};

protected:
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	ELiveLinkAxis AxisX = ELiveLinkAxis::X;
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	ELiveLinkAxis AxisY = ELiveLinkAxis::Y;
	UPROPERTY(EditAnywhere, Category = "LiveLink")
	ELiveLinkAxis AxisZ = ELiveLinkAxis::Z;


public:
	virtual TSubclassOf<ULiveLinkRole> GetRole() const override;
	virtual ULiveLinkFramePreProcessor::FWorkerSharedPtr FetchWorker() override;

public:
	//~ UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR
	//~ End UObject interface

private:
	TSharedPtr<FLiveLinkAxisSwitchPreProcessorWorker, ESPMode::ThreadSafe> Instance;
};

