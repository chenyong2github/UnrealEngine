// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RuntimeVirtualTexturePlane.generated.h"

class UMaterialInterface;
class URuntimeVirtualTexture;
class URuntimeVirtualTextureComponent;

/** Actor used to place a URuntimeVirtualTexture in the world. */
UCLASS(hidecategories=(Actor, Collision, Cooking, Input, LOD, Replication), MinimalAPI)
class ARuntimeVirtualTexturePlane : public AActor
{
	GENERATED_UCLASS_BODY()

private:
	/** Component that owns the runtime virtual texture. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = VirtualTexture, meta = (AllowPrivateAccess = "true"))
	class URuntimeVirtualTextureComponent* VirtualTextureComponent;

#if WITH_EDITORONLY_DATA
	/** Box for visualizing virtual texture extents. */
	UPROPERTY(Transient)
	class UBoxComponent* Box = nullptr;
#endif // WITH_EDITORONLY_DATA

protected:
	//~ Begin UObject Interface.
	virtual bool NeedsLoadForServer() const override { return false; }
	//~ End UObject Interface.
	//~ Begin AActor Interface.
	virtual bool IsLevelBoundsRelevant() const override { return false; }
	//~ End AActor Interface
};

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

	/** Flag used for deferred material notification after render state changes. */
	bool bNotifyInNextTick;

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
	/** Apply any deferred material notifications. */
	void NotifyMaterials();

	//~ Begin UActorComponent Interface
	virtual void CreateRenderState_Concurrent() override;
	virtual void SendRenderTransform_Concurrent() override;
	virtual void DestroyRenderState_Concurrent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

public:
	/** Scene proxy object. Managed by the scene but stored here. */
	class FRuntimeVirtualTextureSceneProxy* SceneProxy;
};
