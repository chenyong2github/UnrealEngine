// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpression.h"
#include "UObject/ObjectMacros.h"
#include "VT/RuntimeVirtualTexture.h"
#include "MaterialExpressionRuntimeVirtualTextureSample.generated.h"

/**
 * Defines how MipValue is used.
 * Internally we will convert to ETextureMipValueMode which is used by internal APIs.
 * ETextureMipValueMode has more options then are valid for runtime virtual texture.
 */
UENUM()
enum ERuntimeVirtualTextureMipValueMode
{
	/* Use hardware computed sample's mip level with automatic anisotropic filtering support. */
	RVTMVM_None UMETA(DisplayName = "None (use computed mip level)"),

	/* Explicitly compute the sample's mip level. Disables anisotropic filtering. */
	RVTMVM_MipLevel UMETA(DisplayName = "MipLevel (absolute, 0 is full resolution)"),

	/* Bias the hardware computed sample's mip level. Disables anisotropic filtering. */
	RVTMVM_MipBias UMETA(DisplayName = "MipBias (relative to the computed mip level)"),

	RVTMVM_MAX,
};

/** Material expression for sampling from a runtime virtual texture. */
UCLASS(collapsecategories, hidecategories=Object)
class ENGINE_API UMaterialExpressionRuntimeVirtualTextureSample : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/** UV coordinates input if we want to override standard world position based coordinates. */
	UPROPERTY()
	FExpressionInput Coordinates;

	/** Meaning depends on MipValueMode. A single unit is one mip level.  */
	UPROPERTY(meta = (RequiredInput = "false"))
	FExpressionInput MipValue;

	/** The virtual texture object to sample. */
	UPROPERTY(EditAnywhere, Category = VirtualTexture)
	class URuntimeVirtualTexture* VirtualTexture;

	/** How to interpret the virtual texture contents. Note that the bound Virtual Texture should have the same setting for sampling to work correctly. */
	UPROPERTY(EditAnywhere, Category = VirtualTexture, meta = (DisplayName = "Virtual texture content"))
	ERuntimeVirtualTextureMaterialType MaterialType = ERuntimeVirtualTextureMaterialType::BaseColor;

	/** Enable page table channel packing. Note that the bound Virtual Texture should have the same setting for sampling to work correctly. */
	UPROPERTY(EditAnywhere, Category = VirtualTexture, meta = (DisplayName = "Enable packed page table"))
	bool bSinglePhysicalSpace = true;

	/** Defines how the MipValue property is applied to the virtual texture lookup. */
	UPROPERTY(EditAnywhere, Category = TextureSample)
	TEnumAsByte<enum ERuntimeVirtualTextureMipValueMode> MipValueMode = RVTMVM_None;

	/** Init settings that affect shader compilation and need to match the current VirtualTexture */
	bool InitVirtualTextureDependentSettings();

protected:
	/** Initialize the output pins. */
	void InitOutputs();

	//~ Begin UMaterialExpression Interface
	virtual UObject* GetReferencedTexture() const override;
	virtual bool CanReferenceTexture() const { return true; }

#if WITH_EDITOR
	virtual void PostLoad() override;
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
public:
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UMaterialExpression Interface
};
