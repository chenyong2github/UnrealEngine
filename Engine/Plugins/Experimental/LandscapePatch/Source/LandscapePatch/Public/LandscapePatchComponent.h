// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Components/SceneComponent.h"

#include "LandscapePatchComponent.generated.h"

class ALandscape;
class ALandscapePatchManager;
class UTextureRenderTarget2D;

/**
 * Base class for landscape patches: components that can be attached to meshes and moved around to make
 * the meshes affect the landscape around themselves.
 */
//~ TODO: Although this doesn't generate geometry, we are likely to change this to inherit from UPrimitiveComponent
//~ so that we can use render proxies for passing along data to the render thread or perhaps for visualization.
UCLASS(Blueprintable, BlueprintType, Abstract)
class LANDSCAPEPATCH_API ULandscapePatchComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	ULandscapePatchComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void Initialize_Native(const FTransform& InLandscapeTransform,
		const FIntPoint& InLandscapeSize,
		const FIntPoint& InLandscapeRenderTargetSize) {}
	virtual UTextureRenderTarget2D* Render_Native(bool InIsHeightmap,
		UTextureRenderTarget2D* InCombinedResult,
		const FName& InWeightmapLayerName) { return InCombinedResult; }

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	void RequestLandscapeUpdate();

	UFUNCTION(BlueprintCallable, Category = "LandscapePatch")
	FTransform GetLandscapeHeightmapCoordsToWorld() const;

	// For now we keep the patches largely editor-only, since we don't yet support runtime landscape editing.
	// The above functions are also editor-only (and don't work at runtime), but can't be in WITH_EDITOR blocks
	// so that they can be called from non-editor-only classes in editor contexts.
#if WITH_EDITOR
	// USceneComponent
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport) override;

	// UActorComponent
	virtual void OnComponentCreated() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void OnRegister() override;
	virtual void GetActorDescProperties(FPropertyPairsMap& PropertyPairsMap) const override;

	// UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool IsPostLoadThreadSafe() const override { return true; }
	virtual void PostLoad();
#endif
	virtual bool IsEditorOnly() const override { return true; }
	virtual bool NeedsLoadForClient() const override { return false; }
	virtual bool NeedsLoadForServer() const override { return false; }

protected:
	UPROPERTY(EditAnywhere, Category = Settings)
	TWeakObjectPtr<ALandscape> Landscape = nullptr;

	UPROPERTY()
	TWeakObjectPtr<ALandscapePatchManager> PatchManager = nullptr;

	// Determines whether the height patch was made by copying a different height patch.
	bool bWasCopy = false;

	// This is true for existing height patches right after they are loaded, so that we can ignore
	// the first OnRegister call. It remains false from the first OnRegiter call onward, even
	// if the component is unregistered.
	bool bLoadedButNotYetRegistered = false;
private:
	// Starts as false and gets set to true in construction, so gets used to set bWasCopy
	// by checking the indicator value at the start of construction.
	UPROPERTY()
	bool bPropertiesCopiedIndicator = false;
};