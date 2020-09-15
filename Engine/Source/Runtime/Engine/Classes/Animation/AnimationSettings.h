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
#include "CustomAttributes.h"

#include "AnimationSettings.generated.h"

/**
 * Default animation settings.
 */
UCLASS(config=Engine, defaultconfig, meta=(DisplayName="Animation"))
class ENGINE_API UAnimationSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

	/** Compression version for recompress commandlet, bump this to trigger full recompressed, otherwise only new imported animations will be recompressed */
	UPROPERTY(config, VisibleAnywhere, Category = Compression)
	int32 CompressCommandletVersion;

	/** List of bone names to treat with higher precision, in addition to any bones with sockets */
	UPROPERTY(config, EditAnywhere, Category = Compression)
	TArray<FString> KeyEndEffectorsMatchNameArray;

	/** If true, this will forcibly recompress every animation, this should not be checked in enabled */
	UPROPERTY(config, EditAnywhere, Category = Compression)
	bool ForceRecompression;

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

	/** If true, recompression will log performance information */
	UPROPERTY(config, EditAnywhere, Category = Performance)
	bool bEnablePerformanceLog;

	/** If true, animation track data will be stripped from dedicated server cooked data */
	UPROPERTY(config, EditAnywhere, Category = Performance)
	bool bStripAnimationDataOnDedicatedServer;

	/** If true, pre-4.19 behavior of zero-ticking animations during skeletal mesh init */
	UPROPERTY(config, EditAnywhere, Category = Performance)
	bool bTickAnimationOnSkeletalMeshInit;

	/** List of custom attribute to import directly on their corresponding bone. The meaning field allows to contextualize the attribute name and customize tooling for it. */
	UPROPERTY(config, EditAnywhere, Category = CustomAttributes)
	TArray<FCustomAttributeSetting> BoneCustomAttributesNames;

	/** List of bone names for which all custom attributes are directly imported on the bone. */
	UPROPERTY(config, EditAnywhere, Category = CustomAttributes)
	TArray<FString> BoneNamesWithCustomAttributes;

	/** Custom Attribute specific blend types (by name) */
	UPROPERTY(config, EditAnywhere, Category = CustomAttributes)
	TMap<FName, ECustomAttributeBlendType> AttributeBlendModes;

	/** Default Custom Attribute blend type */
	UPROPERTY(config, EditAnywhere, Category = CustomAttributes)
	ECustomAttributeBlendType DefaultAttributeBlendMode;

public:
	static UAnimationSettings * Get() { return CastChecked<UAnimationSettings>(UAnimationSettings::StaticClass()->GetDefaultObject()); }

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
};
