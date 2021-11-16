// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "BoneContainer.h"
#include "BoneControllerTypes.generated.h"

struct FAnimInstanceProxy;

UENUM(BlueprintInternalUseOnly)
enum class EWarpingEvaluationMode : uint8
{
	// Pose warping node parameters are entirely driven by the user
	Manual,
	// Pose warping nodes may participate in being graph-driven. This means some
	// properties of the warp may be automatically configured by the accumulated 
	// root motion delta contribution of the animation graph leading into the node
	Graph
};

// The supported spaces of a corresponding input vector value
UENUM(BlueprintInternalUseOnly)
enum class EWarpingVectorMode : uint8
{
	// Component-space input vector
	ComponentSpaceVector,
	// Actor-space input vector
	ActorSpaceVector,
	// World-space input vector
	WorldSpaceVector,
	// IK Foot Root relative local-space input vector
	IKFootRootLocalSpaceVector,
};

USTRUCT(BlueprintType)
struct ANIMGRAPHRUNTIME_API FWarpingVectorValue
{
	GENERATED_BODY()

	// Space of the corresponding Vector value
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinShownByDefault))
	EWarpingVectorMode Mode = EWarpingVectorMode::ComponentSpaceVector;

	// Specifies a vector relative to the space defined by Mode
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinShownByDefault))
	FVector Value = FVector::ZeroVector;

	// Retrieves a normalized Component-space direction from the specified DirectionMode and Direction value
	FVector AsComponentSpaceDirection(const FAnimInstanceProxy* AnimInstanceProxy, const FTransform& IKFootRootTransform) const;
};