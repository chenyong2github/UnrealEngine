// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionStrata.generated.h"



///////////////////////////////////////////////////////////////////////////////
// BSDF nodes

// This would be needed to for a common node interface and weight input and normal too?
/*UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataBSDF : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	FExpressionInput Weight;

	// Normal?
}*/

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataDiffuseBSDF : public UMaterialExpression // STRATA_TODO the single diffuse model to keep when we remove al lthe tests
{
	GENERATED_UCLASS_BODY()

	/**
	 * float3
	 */
	UPROPERTY()
	FExpressionInput Albedo;

	/**
	 * float
	 */
	UPROPERTY()
	FExpressionInput Roughness;

	/**
	 * float3
	 */
	UPROPERTY()
	FExpressionInput Normal;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataDiffuseChanBSDF : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/**
	 * float3
	 */
	UPROPERTY()
	FExpressionInput Albedo;

	/**
	 * float
	 */
	UPROPERTY()
	FExpressionInput Roughness;

	/**
	 * float3
	 */
	UPROPERTY()
	FExpressionInput Normal;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataDielectricBSDF : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()
	
	/**
	 * float
	 */
	UPROPERTY()
	FExpressionInput IOR;
	// STRATA_TODO Refraction IOR?

	/**
	 * float3
	 */
	UPROPERTY()
	FExpressionInput Tint;
		
	/**
	 * float2
	 */
	UPROPERTY()
	FExpressionInput Roughness;

	/**
	 * float3
	 */
	UPROPERTY()
	FExpressionInput Normal;
	// STRATA_TODO Tangent
	// STRATA_TODO Distribution
	// STRATA_TODO ScatterMode

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataConductorBSDF : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()
		
	/**
	 * float3
	 */
	UPROPERTY()
	FExpressionInput Reflectivity;
	
	/**
	 * float3
	 */
	UPROPERTY()
	FExpressionInput EdgeColor;

	/**
	 * float2
	 */
	UPROPERTY()
	FExpressionInput Roughness;

	/**
	 * float3
	 */
	UPROPERTY()
	FExpressionInput Normal;

	// STRATA_TODO Tangent
	// STRATA_TODO Distribution
	// STRATA_TODO ScatterMode

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataVolumeBSDF : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()
		
	/**
	 * Albedo (type = float3, unit = 1/m)
	 */
	UPROPERTY()
	FExpressionInput Albedo;
	
	/**
	 * Extinction (type = float3, unit = 1/m)
	 */
	UPROPERTY()
	FExpressionInput Extinction;

	/**
	 * Anisotropy (type = float, unitless)
	 */
	UPROPERTY()
	FExpressionInput Anisotropy;

	/**
	 * Thickness (type = float, unit = meters, default = 1mm)
	 */
	UPROPERTY()
	FExpressionInput Thickness;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

// STRATA_TODO Sheen, Subsurface, thinfilm, generalised schlick



///////////////////////////////////////////////////////////////////////////////
// Operator nodes

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataHorizontalMixing : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()
		
	/**
	 * Strata material
	 */
	UPROPERTY()
	FExpressionInput Foreground;

	/**
	 * Strata material
	 */
	UPROPERTY()
	FExpressionInput Background;

	/**
	 * float
	 */
	UPROPERTY()
	FExpressionInput Mix;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataVerticalLayering : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/**
	 * Strata material
	 */
	UPROPERTY()
	FExpressionInput Top;
	
	/**
	 * Strata material
	 */
	UPROPERTY()
	FExpressionInput Base;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataAdd : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/**
	 * Strata material
	 */
	UPROPERTY()
	FExpressionInput A;
	
	/**
	 * Strata material
	 */
	UPROPERTY()
	FExpressionInput B;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataMultiply : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/**
	 * Strata material
	 */
	UPROPERTY()
	FExpressionInput A;
	
	/**
	 * Strata material
	 */
	UPROPERTY()
	FExpressionInput Weight;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};



///////////////////////////////////////////////////////////////////////////////
// Utilities

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataArtisticIOR : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/**
	* TODO F0
	*/
	UPROPERTY()
	FExpressionInput Reflectivity;

	/**
	 * TODO Color for tangent view direction
	 */
	UPROPERTY()
	FExpressionInput EdgeColor;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataPhysicalIOR : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/**
	* TODO 
	*/
	UPROPERTY()
	FExpressionInput IOR;

	/**
	 * TODO 
	 */
	UPROPERTY()
	FExpressionInput Extinction;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};


