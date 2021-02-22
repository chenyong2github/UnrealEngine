// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Animation/AnimSequence.h"
#include "ContextualAnimMetadata.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimUtilities.h"
#include "ContextualAnimCompositeSceneAsset.h"
#include "ContextualAnimAsset.generated.h"

/** Deprecated, it has been replaced by ContextualAnimCompositeSceneAsset. This file will be removed very soon */
UCLASS(Blueprintable)
class CONTEXTUALANIMATION_API UContextualAnimAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
	FName AlignmentJoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment", meta = (ClampMin = "1", ClampMax = "60"))
	int32 SampleRate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
	FName MotionWarpSyncPointName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
	FTransform MeshToComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
	TArray<FContextualAnimData> DataContainer;

	UContextualAnimAsset(const FObjectInitializer& ObjectInitializer);

	bool QueryData(FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const { return false; } 
};
