// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneComponent.h"
#include "RuntimeVirtualTextureComponent.generated.h"

class URuntimeVirtualTexture;
class UVirtualTextureBuilder;

/** Component used to place a URuntimeVirtualTexture in the world. */
UCLASS(Blueprintable, ClassGroup = Rendering, HideCategories = (Activation, Collision, Cooking, Mobility, LOD, Object, Physics, Rendering))
class ENGINE_API URuntimeVirtualTextureComponent : public USceneComponent
{
	GENERATED_UCLASS_BODY()

protected:
	/** The virtual texture object to use. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, DuplicateTransient, Category = VirtualTexture)
	URuntimeVirtualTexture* VirtualTexture = nullptr;

	/** Texture object containing streamed low mips. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, DuplicateTransient, Category = VirtualTextureBuild)
	UVirtualTextureBuilder* StreamingTexture = nullptr;

	/** Number of low mips to serialize and stream for the virtual texture. This can reduce rendering update cost. */
	UPROPERTY(EditAnywhere, Category = VirtualTextureBuild, meta = (UIMin = "0", UIMax = "6", DisplayName = "Num Streaming Mips"))
	int32 StreamLowMips = 0;

	/** Enable Crunch compression. ZLib compression is used when Crunch is disabled. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = VirtualTextureBuild, meta = (DisplayName = "Enable Crunch"))
	bool bEnableCompressCrunch = false;

	/** Use any streaming low mips when rendering in editor. Set true to view and debug the baked streaming low mips. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = VirtualTextureBuild, meta = (DisplayName = "View Streaming Mips in Editor"))
	bool bUseStreamingLowMipsInEditor = false;

public:
	/** Get the runtime virtual texture object on this component. */
	URuntimeVirtualTexture* GetVirtualTexture() const { return VirtualTexture; }

	/** Get the streaming virtual texture object on this component. */
	class UVirtualTextureBuilder* GetStreamingTexture() const { return StreamingTexture; }

	/** Get the runtime virtual texture UV to World transform on this component. */
	UFUNCTION(BlueprintPure, Category = VirtualTexture)
	FTransform GetVirtualTextureTransform() const;

	/** Public getter for virtual texture streaming low mips */
	int32 NumStreamingMips() const { return FMath::Clamp(StreamLowMips, 0, 6); }

	/** Get if we want to use any streaming low mips on this component. */
	bool IsStreamingLowMips() const;

	/** Public getter for crunch compression flag. */
	bool IsCrunchCompressed() const { return bEnableCompressCrunch; }

#if WITH_EDITOR
	/** Initialize the low mip streaming texture with the passed in size and data. */
	void InitializeStreamingTexture(uint32 InSizeX, uint32 InSizeY, uint8* InData);
#endif

protected:
	//~ Begin UActorComponent Interface
	virtual void CreateRenderState_Concurrent(FRegisterComponentContext* Context) override;
	virtual void SendRenderTransform_Concurrent() override;
	virtual void DestroyRenderState_Concurrent() override;
	//~ End UActorComponent Interface

	//~ Begin USceneComponent Interface
	virtual bool IsVisible() const override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End USceneComponent Interface

	/** Calculate a hash used to determine if the StreamingTexture contents are valid for use. The hash doesn't include whether the contents are up to date. */
	uint32 CalculateStreamingTextureSettingsHash() const;
	/** Returns true if the StreamingTexure contents are valid for use. */
	bool IsStreamingTextureValid() const;

public:
	/** Scene proxy object. Managed by the scene but stored here. */
	class FRuntimeVirtualTextureSceneProxy* SceneProxy;
};
