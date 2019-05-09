// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RuntimeVirtualTexturePlane.generated.h"

class UMaterialInterface;
class URuntimeVirtualTexture;

/** Actor used to place a URuntimeVirtualTexture in the world.
 *  todo[vt]: I think that this belongs in the Engine lib (not Landscape) but that requires removing the dependency on Renderer for direct creation of a FRuntimeVirtualTextureProducer
 *  todo[vt]: If we fix that and move to Engine then remove build dependency on Landscape in VirtualTexturingEditor.Build.cs
 */
UCLASS(hidecategories=(Actor, Collision, Cooking, Input, LOD, Replication), MinimalAPI)
class ARuntimeVirtualTexturePlane : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	/** Actor to copy the bounds from to set up the transform. */
	UPROPERTY(EditAnywhere, DuplicateTransient, Category = TransformFromBounds)
	AActor* SourceActor = nullptr;

	/** The virtual texture object to use. */
	UPROPERTY(EditAnywhere, DuplicateTransient, Category = VirtualTexture)
	URuntimeVirtualTexture* VirtualTexture = nullptr;

private:
#if WITH_EDITORONLY_DATA
	/** Box for visualizing virtual texture extents. */
	UPROPERTY(Transient)
	class UBoxComponent* Box = nullptr;
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	UFUNCTION(CallInEditor)
	void OnVirtualTextureEditProperty(URuntimeVirtualTexture const* InVirtualTexture);
#endif

public:
#if WITH_EDITOR
	/** Copy the rotation from SourceActor to this component. Called by our UI details customization. */
	LANDSCAPE_API void SetRotation();

	/** Set this component transform to include the SourceActor bounds. Called by our UI details customization. */
	LANDSCAPE_API void SetTransformToBounds();
#endif

protected:
	/** Call whenever we need to update the underlying URuntimeVirtualTexture. */
	void UpdateVirtualTexture();
	/** Call when we need to disconnect from the underlying URuntimeVirtualTexture. */
	void ReleaseVirtualTexture();

	//~ Begin AActor Interface.
	virtual void PostRegisterAllComponents() override;
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual bool IsLevelBoundsRelevant() const override { return false; }
#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished);
#endif
	//~ End AActor Interface.
};
