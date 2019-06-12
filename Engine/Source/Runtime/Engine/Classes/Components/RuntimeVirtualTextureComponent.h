// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneComponent.h"
#include "RuntimeVirtualTextureComponent.generated.h"

class URuntimeVirtualTexture;

/** Component used to place a URuntimeVirtualTexture in the world. */
UCLASS(ClassGroup = Rendering, collapsecategories, hidecategories = (Activation, Collision, Cooking, Mobility, LOD, Object, Physics, Rendering), editinlinenew)
class ENGINE_API URuntimeVirtualTextureComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()

private:
	/** The virtual texture object to use. */
	UPROPERTY(EditAnywhere, DuplicateTransient, Category = VirtualTexture)
	URuntimeVirtualTexture* VirtualTexture = nullptr;

	/** Actor to copy the bounds from to set up the transform. */
	UPROPERTY(EditAnywhere, DuplicateTransient, Category = TransformFromBounds, meta = (DisplayName = "Source Actor"))
	AActor* BoundsSourceActor = nullptr;

public:
	/** Get the runtime virtual texture object set on this component */
	URuntimeVirtualTexture* GetVirtualTexture() const { return VirtualTexture; }

#if WITH_EDITOR
	/** Copy the rotation from BoundsSourceActor to this component. Called by our UI details customization. */
	void SetRotation();

	/** Set this component transform to include the BoundsSourceActor bounds. Called by our UI details customization. */
	void SetTransformToBounds();
#endif

protected:
	//~ Begin UActorComponent Interface
	virtual void CreateRenderState_Concurrent() override;
	virtual void SendRenderTransform_Concurrent() override;
	virtual void DestroyRenderState_Concurrent() override;
	//~ End UActorComponent Interface

public:
	/** Scene proxy object. Managed by the scene but stored here. */
	class FRuntimeVirtualTextureSceneProxy* SceneProxy;
};
