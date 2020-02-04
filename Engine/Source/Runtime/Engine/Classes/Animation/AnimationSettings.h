// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AnimationSettings.h: Declares the AnimationSettings class.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Animation/AnimSequence.h"
#include "Engine/DeveloperSettings.h"
#include "AnimationSettings.generated.h"

/**
 * Default animation settings.
 */
UCLASS(config=Engine, defaultconfig, meta=(DisplayName="Animation"))
class ENGINE_API UAnimationSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

	// compression upgrade version
	UPROPERTY(config, VisibleAnywhere, Category = Compression)
	int32 CompressCommandletVersion;

	UPROPERTY(config, EditAnywhere, Category = Compression)
	TArray<FString> KeyEndEffectorsMatchNameArray;

	UPROPERTY(config, EditAnywhere, Category = Compression)
	bool ForceRecompression;

	UPROPERTY(config, EditAnywhere, Category = Compression)
	bool bOnlyCheckForMissingSkeletalMeshes;

	/** If true and the existing compression error is greater than Alternative Compression Threshold, then any compression technique (even one that increases the size) with a lower error will be used until it falls below the threshold */
	UPROPERTY(config, EditAnywhere, Category = Compression)
	bool bForceBelowThreshold;

	/** If true, then the animation will be first recompressed with it's current compressor if non-NULL, or with the global default compressor (specified in the engine ini) 
	* Also known as "Run Current Default Compressor"
	*/
	UPROPERTY(config, EditAnywhere, Category = Compression)
	bool bFirstRecompressUsingCurrentOrDefault;

	/** If true and the existing compression error is greater than Alternative Compression Threshold, then Alternative Compression Threshold will be effectively raised to the existing error level */
	UPROPERTY(config, EditAnywhere, Category = Compression)
	bool bRaiseMaxErrorToExisting;

	UPROPERTY(config, EditAnywhere, Category = Performance)
	bool bEnablePerformanceLog;

	/** If true, animation track data will be stripped from dedicated server cooked data */
	UPROPERTY(config, EditAnywhere, Category = Performance)
	bool bStripAnimationDataOnDedicatedServer;

	/** If true, pre-4.19 behavior of zero-ticking animations during skeletal mesh init */
	UPROPERTY(config, EditAnywhere, Category = Performance)
	bool bTickAnimationOnSkeletalMeshInit;

public:
	static UAnimationSettings * Get() { return CastChecked<UAnimationSettings>(UAnimationSettings::StaticClass()->GetDefaultObject()); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
};
