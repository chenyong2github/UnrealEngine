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

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, Abstract, DisplayName = "Strata Expression")
class UMaterialExpressionStrataBSDF : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
		virtual int32 CompilePreview(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
#endif
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, DisplayName = "Strata Slab")
class UMaterialExpressionStrataSlabBSDF : public UMaterialExpressionStrataBSDF
{
	GENERATED_UCLASS_BODY()

	/**
	 * Defines the overall color of the Material. (type = float3, unit = unitless, defaults to 0.18)
	 */
	UPROPERTY()
	FExpressionInput BaseColor;

	/**
	 * Defines the edge color of the Material. This is only applied on metallic material (type = float3, unit = unitless, defaults to 1.0)
	 */
	UPROPERTY()
	FExpressionInput EdgeColor;

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
	
	/**
	 * Defines the diffused albedo, the percentage of light reflected as diffuse from the surface. (type = float3, unit = unitless, defaults to 0.18)
	 */
	UPROPERTY()
	FExpressionInput DiffuseAlbedo;

	/**
	 * Defines F0, the percentage of light reflected as specular from a surface when the view is perpendicular to the surface. (type = float3, unit = unitless, defaults to plastic 0.04)
	 */
	UPROPERTY()
	FExpressionInput F0;

	/**
	 * Defines F90, the percentage of light reflected as specular from a surface when the view is tangent to the surface. (type = float3, unit = unitless, defaults to 1.0f)
	 */
	UPROPERTY()
	FExpressionInput F90;

	/**
	 * Controls how rough the Material is. Roughness of 0 (smooth) is a mirror reflection and 1 (rough) is completely matte or diffuse. When using anisotropy, it is the roughness used along the Tangent axis. (type = float, unit = unitless, defaults to 0.5)
	 */
	UPROPERTY()
	FExpressionInput Roughness;
		
	/**
	 * Controls the anisotropy factor of the roughness. Positive value elongates the specular lobe along the Tangent vector, Negative value elongates the specular lobe along the perpendiculat of the Tangent. (type = float, unit = unitless).
	 */
	UPROPERTY()
	FExpressionInput Anisotropy;

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
	 * Chromatic mean free path . Only used when there is not any sub-surface profile provided. (type = float3, unit = centimeters, default = 0)
	 */
	UPROPERTY()
	FExpressionInput SSSDMFP;

	/**
	 * Scale the mean free path radius of the SSS profile according to a value between 0 and 1. Always used, when a subsurface profile is provided or not. (type = float, unitless, defaults to 1)
	 */
	UPROPERTY()
	FExpressionInput SSSDMFPScale;

	/**
	 * Emissive color on top of the surface (type = float3, unit = luminance, default = 0)
	 */
	UPROPERTY()
	FExpressionInput EmissiveColor;

	/**
	 * Haziness controls the relative roughness of a second specular lobe. 0 means disabled and 1 means the second lobe specular lobe will lerp the current roughness to fully rough. (type = float, unitless, default = 0)
	 */
	UPROPERTY()
	FExpressionInput Haziness;

	/**
	 * Thin film controls the thin film layer coating the current slab. 0 means disabled and 1 means a coating layer of 10 micrometer. (type = float, unitless, default = 0)
	 */
	UPROPERTY()
	FExpressionInput ThinFilmThickness;

	/**
	 * The slab thickness. (type = float, centimeters, default = 0.01 centimeter = 0.1 millimeter)
	 */
	UPROPERTY()
	FExpressionInput Thickness;

	/**
	 * The amount of fuzz on top of the surface used to simulate cloth-like appearance.
	 */
	UPROPERTY()
	FExpressionInput FuzzAmount;

	/**
	 * The base colod of the fuzz.
	 */
	UPROPERTY()
	FExpressionInput FuzzColor;

	/** SubsurfaceProfile, for Screen Space Subsurface Scattering. The profile needs to be set up on both the Strata diffuse node, and the material node at the moment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Material, meta = (DisplayName = "Subsurface Profile"))
	TObjectPtr<class USubsurfaceProfile> SubsurfaceProfile;

	/** Whether to use the metalness workflow relying on BaseColor, Specular, EdgeColor and Metallic inputs. Or use the DiffuseColor, F0 and F90 specification. */
	UPROPERTY(EditAnywhere, Category = Mode)
	uint32 bUseMetalness : 1;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
	virtual bool IsResultStrataMaterial(int32 OutputIndex) override;
	virtual void GatherStrataMaterialInfo(FStrataMaterialInfo& StrataMaterialInfo, int32 OutputIndex) override;
	virtual FName GetInputName(int32 InputIndex) const override;
	virtual void GetConnectorToolTip(int32 InputIndex, int32 OutputIndex, TArray<FString>& OutToolTip) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual const TArray<FExpressionInput*> GetInputs() override;

	bool HasEdgeColor() const;
	bool HasThinFilm() const;
	bool HasFuzz() const;
	bool HasSSS() const;
	bool HasSSSProfile() const;
	bool HasDMFPPluggedIn() const;
	bool HasAnisotropy() const;
#endif
	//~ End UMaterialExpression Interface
};

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, DisplayName = "Strata Volumetric-Fog-Cloud BSDF")
class UMaterialExpressionStrataVolumetricFogCloudBSDF : public UMaterialExpressionStrataBSDF
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
	 * Emissive color of the medium (type = float3, unit = luminance, default = 0)
	 */
	UPROPERTY()
	FExpressionInput EmissiveColor;

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

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, DisplayName = "Strata Unlit BSDF")
class UMaterialExpressionStrataUnlitBSDF : public UMaterialExpressionStrataBSDF
{
	GENERATED_UCLASS_BODY()

	/**
	* Emissive color on top of the surface (type = float3, unit = Luminance, default = 0)
	*/
	UPROPERTY()
	FExpressionInput EmissiveColor;

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

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, DisplayName = "Strata Hair BSDF")
class UMaterialExpressionStrataHairBSDF : public UMaterialExpressionStrataBSDF
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
	 * Emissive color on top of the surface (type = float3, unit = luminance, defaults to 0.0)
	 */
	UPROPERTY()
	FExpressionInput EmissiveColor;

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

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, DisplayName = "Strata Single Layer Water BSDF")
class UMaterialExpressionStrataSingleLayerWaterBSDF : public UMaterialExpressionStrataBSDF
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
	 * Emissive color on top of the surface (type = float3, unit = luminance, defaults to 0.0)
	 */
	UPROPERTY()
	FExpressionInput EmissiveColor;

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

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, DisplayName = "Strata BSDF Horizontal Blend")
class UMaterialExpressionStrataHorizontalMixing : public UMaterialExpressionStrataBSDF
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

	UPROPERTY(EditAnywhere, Category = Mode)
	uint32 bUseParameterBlending : 1;

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

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, DisplayName = "Strata BSDF Vertical Layer")
class UMaterialExpressionStrataVerticalLayering : public UMaterialExpressionStrataBSDF
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

	UPROPERTY(EditAnywhere, Category = Mode)
	uint32 bUseParameterBlending : 1;

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

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, DisplayName = "Strata BSDF Add")
class UMaterialExpressionStrataAdd : public UMaterialExpressionStrataBSDF
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

	UPROPERTY(EditAnywhere, Category = Mode)
	uint32 bUseParameterBlending : 1;

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

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, DisplayName = "Strata BSDF Weight")
class UMaterialExpressionStrataWeight : public UMaterialExpressionStrataBSDF
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

UCLASS(MinimalAPI, collapsecategories, hidecategories = Object, DisplayName = "Strata Transmittance-To-MeanFreePath")
class UMaterialExpressionStrataTransmittanceToMFP : public UMaterialExpressionStrataBSDF
{
	GENERATED_UCLASS_BODY()

	/**
	* The colored transmittance for a view perpendicular to the surface. The transmittance or other view orientation will automatically deduced according to surface thickness.
	*/
	UPROPERTY()
	FExpressionInput TransmittanceColor;

	/**
	* The desired thickness in centimers. This can be set lower than 0.1mm (= 0.01cm) to enable the Thin lighting model on the slab node for instance.
	* Another use case exemple: the thickenss ouput can be modulated to have simple scattering/transmittance variation of the same material before it is plugged in a slab node.
	*/
	UPROPERTY()
	FExpressionInput Thickness;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual uint32 GetOutputType(int32 OutputIndex) override;
	virtual uint32 GetInputType(int32 InputIndex) override;
	virtual void GetConnectorToolTip(int32 InputIndex, int32 OutputIndex, TArray<FString>& OutToolTip) override;
	virtual void GetExpressionToolTip(TArray<FString>& OutToolTip) override;
#endif
	//~ End UMaterialExpression Interface
};

