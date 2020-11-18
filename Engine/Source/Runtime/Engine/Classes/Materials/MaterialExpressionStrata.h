// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionStrata.generated.h"


/**
 * Compile a special blend function for strata when blending material attribute
 *
 * @param Compiler				The compiler to add code to
 * @param Foreground			Entry A, has a bigger impact when Alpha is close to 0
 * @param Background			Entry B, has a bigger impact when Alpha is close to 1
 * @param Alpha					Blend factor [0..1], when 0
 * @return						Index to a new code chunk
 */
extern int32 CompileStrataBlendFunction(FMaterialCompiler* Compiler, const int32 A, const int32 B, const int32 Alpha);


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
	 * Albedo (type = float3, unit = unitless)
	 */
	UPROPERTY()
	FExpressionInput Albedo;

	/**
	 * Roughness (type = float, unit = unitless)
	 */
	UPROPERTY()
	FExpressionInput Roughness;

	/**
	 * Normal (type = float3, unit = unitless)
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
	 * The index of refraction of the surface (type = float, unit = unitless, default=1.5 for glass)
	 */
	UPROPERTY()
	FExpressionInput IOR;

	/**
	 * A global color tint multiplied with the specular color, not physically based (type = float3, unit = unitless)
	 */
	UPROPERTY()
	FExpressionInput Tint;
		
	/**
	 * RoughnessX (type = float, unit = unitless)
	 */
	UPROPERTY()
	FExpressionInput RoughnessX;
		
	/**
	 * RoughnessY (type = float, unit = unitless), If not plugged in, RoughnessY is set to RoughnessX to get an isotropic behavior.
	 */
	UPROPERTY()
	FExpressionInput RoughnessY;

	/**
	 * Normal (type = float3, unit = unitless)
	 */
	UPROPERTY()
	FExpressionInput Normal;

	/**
	* Tangent (type = float3, unit = unitless)
	*/
	UPROPERTY()
	FExpressionInput Tangent;

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
	 * Reflectivity when view direction is perpendicular to the surface, also known as F0 (type = float3, unit = unitless, defaults to gold)
	 */
	UPROPERTY()
	FExpressionInput Reflectivity;
	
	/**
	 * Reflectivity when the view direction is tangent to the surface (type = float3, unit = unitless, defaults to gold)
	 */
	UPROPERTY()
	FExpressionInput EdgeColor;
		
	/**
	 * RoughnessX (type = float, unit = unitless)
	 */
	UPROPERTY()
	FExpressionInput RoughnessX;
		
	/**
	 * RoughnessY (type = float, unit = unitless), If not plugged in, RoughnessY is set to RoughnessX to get an isotropic behavior.
	 */
	UPROPERTY()
	FExpressionInput RoughnessY;

	/**
	 * Normal (type = float3, unit = unitless)
	 */
	UPROPERTY()
	FExpressionInput Normal;

	/**
	 * Tangent (type = float3, unit = unitless)
	 */
	UPROPERTY()
	FExpressionInput Tangent;

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
	 * Albedo (type = float3, unit = unitless)
	 */
	UPROPERTY()
	FExpressionInput Albedo;
	
	/**
	 * The rate at which light is absorbed or scattered by the medium. Mean Free Path = 1 / Extinction. (type = float3, unit = 1/m)
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
	 * Lerp factor between Background (Mix == 0) and Foreground (Mix == 1).
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
	 * Strata material layer on top of the Base material layer
	 */
	UPROPERTY()
	FExpressionInput Top;
	
	/**
	 * Strata material layer below the Top material layer
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
	 * Weight to apply to the strata material BSDFs
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
	 * Reflectivity when view direction is perpendicular to the surface, also known as F0 (type = float3, unit = unitless)
	 */
	UPROPERTY()
	FExpressionInput Reflectivity;

	/**
	 * Reflectivity when the view direction is tangent to the surface (type = float3, unit = unitless)
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
	* The index of refraction of the surface (type = float, unit = unitless)
	*/
	UPROPERTY()
	FExpressionInput IOR;

	/**
	 * The rate at which light is absorbed or scattered by the medium. Mean Free Path = 1 / Extinction. (type = float3, unit = 1/m)
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


UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataAnisotropyToRoughness : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/**
	 * Input roughness
	 */
	UPROPERTY()
	FExpressionInput Roughness;

	/**
	 * Anisotropy factor - 0: isotropic behavior, -1: anisotropy along the bitangent vector, 1: anisotropy along the tangent vector
	 */
	UPROPERTY()
	FExpressionInput Anisotropy;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

