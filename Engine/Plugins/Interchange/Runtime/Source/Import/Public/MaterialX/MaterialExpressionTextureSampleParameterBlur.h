// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "MaterialExpressionTextureSampleParameterBlur.generated.h"

UENUM()
enum class ETextureSampleBlurFilter : uint8
{
	Box,
	Gaussian
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionTextureSampleParameterBlur: public UMaterialExpressionTextureSampleParameter2D
{
	GENERATED_UCLASS_BODY()

	/** The size of the blur kernel, relative to 0-1 UV space, only 3x3, 5x5 and 7x7 kernel are supported.
	  * Basically depending on the KernelSize value:
	  * KernelSize == 0   -> 1x1 Kernel (Meaning no Blur)
	  * KernelSize <= 1/3 -> 3x3 Kernel
	  * KernelSize <= 2/3 -> 5x5 Kernel
	  * Otherwise         -> 7x7 Kernel
	  */
	UPROPERTY(EditAnywhere, Category = Filtering, Meta = (ShowAsInputPin = "Advanced"))
	float KernelSize = 0.f;

	/** Size of the filter when we sample a texture coordinate */
	UPROPERTY(EditAnywhere, Category = Filtering, Meta = (ShowAsInputPin = "Advanced"))
	float FilterSize = 1.f;

	/** Offset of the filter when we sample a texture coordinate */
	UPROPERTY(EditAnywhere, Category = Filtering, Meta = (ShowAsInputPin = "Advanced"))
	float FilterOffset = 0.f;

	/** Filter to use when we blur a Texture: Gaussian or Box Linear filter*/
	UPROPERTY(EditAnywhere, Category = Filtering, Meta = (ShowAsInputPin = "Advanced"))
	ETextureSampleBlurFilter Filter = ETextureSampleBlurFilter::Gaussian;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif
	//~ End UMaterialExpression Interface
};