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
class UMaterialExpressionStrataSlabBSDF : public UMaterialExpression // STRATA_TODO the single diffuse model to keep when we remove al lthe tests
{
	GENERATED_UCLASS_BODY()

	/**
	 * Defines the overall color of the Material. (type = float3, unit = unitless, defaults to 0.18)
	 */
	UPROPERTY()
	FExpressionInput BaseColor;

	/**
	 * Controls how \"metal-like\" your surface looks like. 0 means dielectric, 1 means conductor (type = float, unit = unitless, defaults to 0)
	 */
	UPROPERTY()
	FExpressionInput Metallic;
	
	/**
	 * Used to scale the current amount of specularity on non-metallic surfaces and is a value between 0 and 1 (type = float, unit = unitless, defaults to plastic 0.5)
	 */
	UPROPERTY()
	FExpressionInput Specular;

	// STRATA_TODO: edge or F82 EdgeColor?

	/**
	 * Controls how rough the Material is. Roughness of 0 (smooth) is a mirror reflection and 1 (rough) is completely matte or diffuse. When using anisotropy, it is the roughness used along the Tangent axis. (type = float, unit = unitless, defaults to 0)
	 */
	UPROPERTY()
	FExpressionInput RoughnessX;
		
	/**
	 * Controls the roughness along the secondary surface tangent vector (perpendicular to Tangent). (type = float, unit = unitless). If not plugged in, RoughnessY is set to RoughnessX to disable anisotropy, resulting in an isotropic behavior.
	 */
	UPROPERTY()
	FExpressionInput RoughnessY;

	/**
	 * Take the surface normal as input. The normal is considered tangent or world space according to the space properties on the main material node. (type = float3, unit = unitless, defaults to vertex normal)
	 */
	UPROPERTY()
	FExpressionInput Normal;

	/**
	* Take a surface tangent as input. The tangent is considered tangent or world space according to the space properties on the main material node. (type = float3, unit = unitless, defaults to vertex tangent)
	*/
	UPROPERTY()
	FExpressionInput Tangent;

	/**
	 * Mean free path albedo. Only used when no sub-surface profile is provided. (type = float3, unit = unitless)
	 */
	UPROPERTY(meta = (DisplayName = "DMFP Albedo"))
	FExpressionInput DiffuseMeanFreePathAlbedo;

	/**
	 * Mean free path radius. Only used when no sub-surface profile is provided. (type = float, unit = cm)
	 */
	UPROPERTY(meta = (DisplayName = "DMFP Radius"))
	FExpressionInput DiffuseMeanFreePathRadius;

	/** SubsurfaceProfile, for Screen Space Subsurface Scattering. The profile needs to be set up on both the Strata diffuse node, and the material node at the moment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Material, meta = (DisplayName = "Subsurface Profile"))
	class USubsurfaceProfile* SubsurfaceProfile;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
	virtual bool IsResultStrataMaterial(int32 OutputIndex) override;
	virtual void GatherStrataMaterialInfo(FStrataMaterialInfo& StrataMaterialInfo, int32 OutputIndex) override;
	virtual FName GetInputName(int32 InputIndex) const override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataSheenBSDF : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/**
	* Defines the overall color of the Material. (type = float3, unit = unitless)
	*/
	UPROPERTY()
	FExpressionInput BaseColor;

	/**
	 * Roughness (type = float, unit = unitless)
	 */
	UPROPERTY()
	FExpressionInput Roughness;

	/**
	 * Take the surface normal as input. The normal is considered tangent or world space according to the space properties on the main material node. (type = float3, unit = unitless)
	 */
	UPROPERTY()
	FExpressionInput Normal;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
	virtual bool IsResultStrataMaterial(int32 OutputIndex) override;
	virtual void GatherStrataMaterialInfo(FStrataMaterialInfo& StrataMaterialInfo, int32 OutputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataVolumetricFogCloudBSDF : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/**
	* The single scattering Albedo defining the overall color of the Material (type = float3, unit = unitless, default = 0)
	*/
	UPROPERTY()
	FExpressionInput Albedo;

	/**
	 * The rate at which light is absorbed or scattered by the medium. Mean Free Path = 1 / Extinction. (type = float3, unit = 1/m, default = 0)
	 */
	UPROPERTY()
	FExpressionInput Extinction;

	/**
	 * Emissive (type = float3, unit = luminance, default = 0)
	 */
	UPROPERTY()
	FExpressionInput Emissive;

	/**
	 * Ambient occlusion: 1 means no occlusion while 0 means fully occluded. (type = float, unit = unitless, default = 1)
	 */
	UPROPERTY()
	FExpressionInput AmbientOcclusion;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
	virtual bool IsResultStrataMaterial(int32 OutputIndex) override;
	virtual void GatherStrataMaterialInfo(FStrataMaterialInfo& StrataMaterialInfo, int32 OutputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataUnlitBSDF : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/**
	* Emissive (type = float3, unit = Luminance, default = 0)
	*/
	UPROPERTY()
	FExpressionInput Emissive;

	/**
	 * The amount of transmitted light from the back side of the surface to the front side of the surface ()
	 */
	UPROPERTY()
	FExpressionInput TransmittanceColor;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
	virtual bool IsResultStrataMaterial(int32 OutputIndex) override;
	virtual void GatherStrataMaterialInfo(FStrataMaterialInfo& StrataMaterialInfo, int32 OutputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataHairBSDF : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()
		
	/**
	 * Hair fiber base color resulting from single and multiple scattering combined. (type = float3, unit = unitless, defaults to black)
	 */
	UPROPERTY()
	FExpressionInput BaseColor;
	
	/**
	 * Amount of light scattering, only available for non-HairStrand rendering (type = float, unit = unitless, defaults to 0.0)
	 */
	UPROPERTY()
	FExpressionInput Scatter;
		
	/**
	 * Specular (type = float, unit = unitless, defaults to 0.5)
	 */
	UPROPERTY()
	FExpressionInput Specular;
		
	/**
	 * Controls how rough the Material is. Roughness of 0 (smooth) is a mirror reflection and 1 (rough) is completely matte or diffuse (type = float, unit = unitless, defaults to 0.5)
	 */
	UPROPERTY()
	FExpressionInput Roughness;

	/**
	 * How much light contributs when lighting hairs from the back side opposite from the view, only available for HairStrand rendering (type = float3, unit = unitless, defaults to 0.0)
	 */
	UPROPERTY()
	FExpressionInput Backlit;

	/**
	 * Tangent (type = float3, unit = unitless, defaults to +X vector)
	 */
	UPROPERTY()
	FExpressionInput Tangent;

	/**
	 * Emissive (type = float3, unit = luminance, defaults to 0.0)
	 */
	UPROPERTY()
	FExpressionInput Emissive;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
	virtual bool IsResultStrataMaterial(int32 OutputIndex) override;
	virtual void GatherStrataMaterialInfo(FStrataMaterialInfo& StrataMaterialInfo, int32 OutputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object)
class UMaterialExpressionStrataSingleLayerWaterBSDF : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/**
	 * Surface base color. (type = float3, unit = unitless, defaults to black)
	 */
	UPROPERTY()
	FExpressionInput BaseColor;

	/**
	 * whether the surface represents a dielectric (such as plastic) or a conductor (such as metal). (type = float, unit = unitless, defaults to 0 = dielectric)
	 */
	UPROPERTY()
	FExpressionInput Metallic;

	/**
	 * Specular amount (type = float, unit = unitless, defaults to 0.5)
	 */
	UPROPERTY()
	FExpressionInput Specular;

	/**
	 * Controls how rough the Material is. Roughness of 0 (smooth) is a mirror reflection and 1 (rough) is completely matte or diffuse (type = float, unit = unitless, defaults to 0.5)
	 */
	UPROPERTY()
	FExpressionInput Roughness;

	/**
	 * The normal of the surface (type = float3, unit = unitless, defaults to +Z vector)
	 */
	UPROPERTY()
	FExpressionInput Normal;

	/**
	 * Emissive RGB (type = float3, unit = luminance, defaults to 0.0)
	 */
	UPROPERTY()
	FExpressionInput Emissive;

	/**
	 * Opacity of the material layered on top of the water (type = float3, unit = unitless, defaults to 0.0)
	 */
	UPROPERTY()
	FExpressionInput TopMaterialOpacity;

	/**
	* The single scattering Albedo defining the overall color of the Material (type = float3, unit = unitless, default = 0)
	 */
	UPROPERTY()
	FExpressionInput WaterAlbedo;

	/**
	 * The rate at which light is absorbed or out-scattered by the medium. Mean Free Path = 1 / Extinction. (type = float3, unit = 1/cm, default = 0)
	 */
	UPROPERTY()
	FExpressionInput WaterExtinction;

	/**
	 * Anisotropy of the volume with values lower than 0 representing back-scattering, equal 0 representing isotropic scattering and greater than 0 representing forward scattering. (type = float, unit = unitless, defaults to 0)
	 */
	UPROPERTY()
	FExpressionInput WaterPhaseG;

	/**
	 * A scale to apply on the scene color behind the water surface. It can be used to approximate caustics for instance. (type = float3, unit = unitless, defaults to 1)
	 */
	UPROPERTY()
	FExpressionInput ColorScaleBehindWater;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
	virtual bool IsResultStrataMaterial(int32 OutputIndex) override;
	virtual void GatherStrataMaterialInfo(FStrataMaterialInfo& StrataMaterialInfo, int32 OutputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};



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
	FExpressionInput Background;

	/**
	 * Strata material
	 */
	UPROPERTY()
	FExpressionInput Foreground;

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
	virtual bool IsResultStrataMaterial(int32 OutputIndex) override;
	virtual void GatherStrataMaterialInfo(FStrataMaterialInfo& StrataMaterialInfo, int32 OutputIndex) override;
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
	virtual bool IsResultStrataMaterial(int32 OutputIndex) override;
	virtual void GatherStrataMaterialInfo(FStrataMaterialInfo& StrataMaterialInfo, int32 OutputIndex) override;
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
	virtual bool IsResultStrataMaterial(int32 OutputIndex) override;
	virtual void GatherStrataMaterialInfo(FStrataMaterialInfo& StrataMaterialInfo, int32 OutputIndex) override;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, DisplayName = "Strata Multiply Weight", collapsecategories, hidecategories = Object)
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
	virtual bool IsResultStrataMaterial(int32 OutputIndex) override;
	virtual void GatherStrataMaterialInfo(FStrataMaterialInfo& StrataMaterialInfo, int32 OutputIndex) override;
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

