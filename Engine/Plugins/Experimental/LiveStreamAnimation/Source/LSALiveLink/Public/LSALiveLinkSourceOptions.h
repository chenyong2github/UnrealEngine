// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LSALiveLinkSourceOptions.generated.h"

/**
 * Options used to specify which parts of FLiveLinkAnimationFrameData should
 * be serialized in packets.
 */
USTRUCT(BlueprintType, Category = "Live Stream Animation|Live Link|Options")
struct LSALIVELINK_API FLSALiveLinkSourceOptions
{
	GENERATED_BODY()

public:

	FLSALiveLinkSourceOptions()
		: bWithSceneTime(false)
		, bWithStringMetaData(false)
		, bWithPropertyValues(false)
		, bWithTransformTranslation(true)
		, bWithTransformRotation(true)
		, bWithTransformScale(true)
	{
	}

	/** Whether or not we're sending Scene Time (Timecode). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Live Stream Animation|Live Link|Options")
	uint8 bWithSceneTime : 1;

	/** Whether or not we're sending String Meta Data (very expensive). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Live Stream Animation|Live Link|Options")
	uint8 bWithStringMetaData : 1;

	/**
	 * Whether or not we're sending generic property values.
	 * Either Property values or one (or more) Transform Components or both must
	 * be enabled
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Live Stream Animation|Live Link|Options")
	uint8 bWithPropertyValues : 1;

	/**
	 * Whether or not we're sending Transform Translations.
	 * Either Property values or one (or more) Transform Components or both must
	 * be enabled
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Live Stream Animation|Live Link|Options")
	uint8 bWithTransformTranslation : 1;

	/**
	 * Whether or not we're sending Transform Rotations.
	 * Either Property values or one (or more) Transform Components or both must
	 * be enabled
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Live Stream Animation|Live Link|Options")
	uint8 bWithTransformRotation : 1;

	/**
	 * Whether or not we're sending Transform Scales.
	 * Either Property values or one (or more) Transform Components or both must
	 * be enabled
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Live Stream Animation|Live Link|Options")
	uint8 bWithTransformScale : 1;

	/** Whether or not we're sending any transform data. */
	bool WithTransforms() const
	{
		return bWithTransformTranslation | bWithTransformRotation | bWithTransformScale;
	}

	/** Whether or not our current settings are valid. */
	bool IsValid() const
	{
		return bWithPropertyValues || WithTransforms();
	}
};