// Copyright Epic Games, Inc. All Rights Reserved.

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

	/** Use any streaming low mips when rendering in editor. Set true to view and debug the baked streaming low mips. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = VirtualTexture)
	bool bUseStreamingLowMipsInEditor = false;

	/** Actor to copy the bounds from to set up the transform. */
	UPROPERTY(EditAnywhere, DuplicateTransient, Category = TransformFromBounds, meta = (DisplayName = "Source Actor"))
	AActor* BoundsSourceActor = nullptr;

public:
	/** Get the runtime virtual texture object on this component. */
	URuntimeVirtualTexture* GetVirtualTexture() const { return VirtualTexture; }

	/** Get the runtime virtual texture UV to World transform on this component. */
	FTransform GetVirtualTextureTransform() const;

	/** Get if we want use any streaming low mips in the runtime virtual texture set on this component. */
	bool IsStreamingLowMips() const;

#if WITH_EDITOR
	/** Copy the rotation from BoundsSourceActor to this component. Called by our UI details customization. */
	void SetRotation();

	/** Set this component transform to include the BoundsSourceActor bounds. Called by our UI details customization. */
	void SetTransformToBounds();
#endif

protected:
	//~ Begin UActorComponent Interface
	virtual bool IsVisible() const override;
	virtual void CreateRenderState_Concurrent() override;
	virtual void SendRenderTransform_Concurrent() override;
	virtual void DestroyRenderState_Concurrent() override;
	//~ End UActorComponent Interface

public:
	/** Scene proxy object. Managed by the scene but stored here. */
	class FRuntimeVirtualTextureSceneProxy* SceneProxy;
};
