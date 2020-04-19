// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActorComponent.h"
#include "BoundsCopyComponent.generated.h"

/** Component used to copy the bounds of another Actor. */
UCLASS(ClassGroup = Rendering, HideCategories = (Activation, AssetUserData, Collision, Cooking, Tags))
class ENGINE_API UBoundsCopyComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()

private:
	/** Actor to copy the bounds from to set up the transform. */
	UPROPERTY(EditAnywhere, Category = TransformFromBounds, meta = (DisplayName = "Source Actor", DisplayPriority = 3))
	TSoftObjectPtr<AActor> BoundsSourceActor = nullptr;

public:
#if WITH_EDITOR
	/** Copy the rotation from BoundsSourceActor to this component */
	UFUNCTION(CallInEditor, Category = TransformFromBounds, meta = (DisplayName = "Copy Rotation", DisplayPriority = 4))
	void SetRotation();

	/** Set this component transform to include the BoundsSourceActor bounds */
	UFUNCTION(CallInEditor, Category = TransformFromBounds, meta = (DisplayName = "Copy Bounds", DisplayPriority = 4))
	void SetTransformToBounds();
#endif
};
