// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LiveLinkSourceSettings.h"
#include "LiveLinkCurveRemapSettings.generated.h" 

class UPoseAsset;

USTRUCT(BlueprintType)
struct FLiveLinkCurveConversionSettings
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = Settings, meta = (AllowedClasses = "PoseAsset"))
	TMap<FString, FSoftObjectPath> CurveConversionAssetMap;
};

UCLASS(config=Engine, defaultconfig, meta=(DisplayName="LiveLink"))
class LIVELINKINTERFACE_API ULiveLinkCurveRemapSettings : public ULiveLinkSourceSettings
{
public:
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, config, Category = "Curve Conversion Settings")
	FLiveLinkCurveConversionSettings CurveConversionSettings;

#if WITH_EDITOR

	//UObject override so we can change this setting when changed in editor
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;

#endif // WITH_EDITOR
};
