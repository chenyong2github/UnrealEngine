// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/AnimSequence.h"
#include "Templates/SubclassOf.h"
#include "ContextualAnimTypes.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogContextualAnim, Log, All);

class AActor;
class UAnimMontage;
class UContextualAnimMetadata;

/** Container for alignment tracks */
USTRUCT()
struct CONTEXTUALANIMATION_API FContextualAnimAlignmentTrackContainer
{
	GENERATED_BODY()

	UPROPERTY()
	FAnimSequenceTrackContainer Tracks;

	UPROPERTY()
	float SampleInterval = 0.f;

	FTransform ExtractTransformAtTime(int32 TrackIndex, float Time) const;
	FTransform ExtractTransformAtTime(const FName& TrackName, float Time) const;
};


USTRUCT(BlueprintType)
struct CONTEXTUALANIMATION_API FContextualAnimData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	UAnimMontage* Animation = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	float AnimMaxStartTime = 0.f;

	UPROPERTY()
	FContextualAnimAlignmentTrackContainer AlignmentData;

	UPROPERTY(EditAnywhere, Instanced, Category = "Defaults")
	UContextualAnimMetadata* Metadata = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FTransform MeshToScene;

	/** deprecated */
	UPROPERTY()
	float SyncTime = 0.f;

	float GetSyncTimeForWarpSection(int32 WarpSectionIndex) const;
	float GetSyncTimeForWarpSection(const FName& WarpSectionName) const;

	FORCEINLINE FTransform GetAlignmentTransformAtTime(float Time) const { return AlignmentData.ExtractTransformAtTime(0, Time); }
	FORCEINLINE FTransform GetAlignmentTransformAtEntryTime() const { return AlignmentData.ExtractTransformAtTime(0, 0.f); }
	FORCEINLINE FTransform GetAlignmentTransformAtSyncTime() const { return AlignmentData.ExtractTransformAtTime(0, GetSyncTimeForWarpSection(0)); }

	float FindBestAnimStartTime(const FVector& LocalLocation) const;

	static const FContextualAnimData EmptyAnimData;
};

/** Defines when the actor should start playing the animation */
UENUM(BlueprintType)
enum class EContextualAnimJoinRule : uint8
{
	Default,
	Late
};

USTRUCT(BlueprintType)
struct CONTEXTUALANIMATION_API FContextualAnimTrackSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TSubclassOf<AActor> PreviewActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	EContextualAnimJoinRule JoinRule = EContextualAnimJoinRule::Default;
};

USTRUCT(BlueprintType)
struct FContextualAnimTrack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FContextualAnimTrackSettings Settings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FContextualAnimData AnimData;
};

USTRUCT(BlueprintType)
struct FContextualAnimCompositeTrack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FContextualAnimTrackSettings Settings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TArray<FContextualAnimData> AnimDataContainer;
};