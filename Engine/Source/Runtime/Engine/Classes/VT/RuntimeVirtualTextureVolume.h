// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RuntimeVirtualTextureVolume.generated.h"

/** Actor used to place a URuntimeVirtualTexture in the world. */
UCLASS(HideCategories = (Actor, Collision, Cooking, Input, LOD, Physics, Replication, Rendering), MinimalAPI)
class ARuntimeVirtualTextureVolume : public AActor
{
	GENERATED_UCLASS_BODY()

private:
	/** Component that owns the runtime virtual texture. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = VirtualTexture, meta = (AllowPrivateAccess = "true"))
	class URuntimeVirtualTextureComponent* VirtualTextureComponent;

#if WITH_EDITORONLY_DATA
	/** Component that enables Editor bounds copy functionality. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Transform, meta = (AllowPrivateAccess = "true"))
	class UBoundsCopyComponent* BoundsCopyComponent;

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
