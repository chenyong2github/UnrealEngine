// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "PoseWatch.generated.h"

struct FCompactHeapPose;

struct FAnimNodePoseWatch
{
	// Object (anim instance) that this pose came from
	TWeakObjectPtr<const UObject>	Object;
	TSharedPtr<FCompactHeapPose>	PoseInfo;
	FColor							PoseDrawColour;
	int32							NodeID;
};

UCLASS()
class ENGINE_API UPoseWatch
	: public UObject
{
	GENERATED_UCLASS_BODY()

public:
	// Node that we are trying to watch
	UPROPERTY()
	TObjectPtr<class UEdGraphNode> Node;

	UPROPERTY()
	FColor PoseWatchColour;

	/** If true then the watch will be deleted when the connected node is deleted */
	UPROPERTY()
	bool bDeleteOnDeselection = false;
};
