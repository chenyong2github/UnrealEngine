// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraRibbonRendererProperties.generated.h"

class FNiagaraEmitterInstance;
class FAssetThumbnailPool;
class SWidget;

UENUM()
enum class ENiagaraRibbonFacingMode : uint8
{
	/** Have the ribbon face the screen. */
	Screen = 0,

	/** Use Particles.RibbonFacing as the facing vector. */
	Custom,

	/** Use Particles.RibbonFacing as the side vector, and calculate the facing vector from that.
	 *  Using ribbon twist with this mode is NOT supported.
	 */
	CustomSideVector
};

/** Defines different modes for offsetting UVs by age when ordering ribbon particles using normalized age. */
UENUM()
enum class ENiagaraRibbonAgeOffsetMode : uint8
{
	/** Offset the UVs by age for smooth texture movement, but scale the 0-1 UV range to the current normalized age range of the particles. */
	Scale,
	/** Offset the UVs by age for smooth texture movement, but use the normalized age range directly as the UV range which will clip the texture for normalized age ranges that don't go from 0-1. */
	Clip
};

/** This enum decides in which order the ribbon segments will be rendered */
UENUM()
enum class ENiagaraRibbonDrawDirection : uint8
{
	FrontToBack,
	BackToFront
};

UENUM()
enum class ENiagaraRibbonTessellationMode : uint8
{
	/** Default tessellation parameters. */
	Automatic,
	/** Custom tessellation parameters. */
	Custom,
	/** Disable tessellation entirely. */
	Disabled
};

/** Specifies options for handling UVs at the leading and trailing edges of ribbons. */
UENUM()
enum class ENiagaraRibbonUVEdgeMode
{
	/** The UV value at the edge will smoothly transition across the segment using normalized age.
	This will result in	UV values which are outside of the standard 0-1 range and works best with
	clamped textures. */
	SmoothTransition,
	/** The UV value at the edge will be locked to 0 at the leading edge, or locked to 1 at the
	Trailing edge. */
	Locked,
};

/** Specifies options for distributing UV values across ribbon segments. */
UENUM()
enum class ENiagaraRibbonUVDistributionMode
{
	/** Ribbon UVs will be scaled to the 0-1 range and distributed evenly across uv segments regardless of segment length. */
	ScaledUniformly,
	/** Ribbon UVs will be scaled to the 0-1 range and will be distributed along the ribbon segments based on their length, i.e. short segments get less UV range and large segments get more. */
	ScaledUsingRibbonSegmentLength,
	/** Ribbon UVs will be tiled along the length of the ribbon based on segment length and the Tile Over Length Scale value. NOTE: This is not equivalent to distance tiling which tiles over owner distance traveled, this requires per particle U override values and can be setup with modules. */
	TiledOverRibbonLength
};

/** Defines settings for UV behavior for a UV channel on ribbons. */
USTRUCT()
struct FNiagaraRibbonUVSettings
{
	GENERATED_BODY();

	FNiagaraRibbonUVSettings();

	/** Specifies how UVs behave at the leading edge of the ribbon where particles are being added. */
	UPROPERTY(EditAnywhere, Category = UVs)
	ENiagaraRibbonUVEdgeMode LeadingEdgeMode;

	/** Specifies how UVs behave at the trailing edge of the ribbon where particles are being removed. */
	UPROPERTY(EditAnywhere, Category = UVs)
	ENiagaraRibbonUVEdgeMode TrailingEdgeMode;

	/** Specifies how ribbon UVs are distributed along the length of a ribbon. */
	UPROPERTY(EditAnywhere, Category = UVs)
	ENiagaraRibbonUVDistributionMode DistributionMode;

	/** Specifies the length in world units to use when tiling UVs across the ribbon when using the tiled distribution mode. */
	UPROPERTY(EditAnywhere, Category = UVs)
	float TilingLength;

	/** Specifies and additional offsets which are applied to the UV range */
	UPROPERTY(EditAnywhere, Category = UVs)
	FVector2D Offset;

	/** Specifies and additional scalers which are applied to the UV range. */
	UPROPERTY(EditAnywhere, Category = UVs)
	FVector2D Scale;

	/** Enables overriding overriding the U componenet with values read from the particles.  When enabled edge behavior and distribution are ignored. */
	UPROPERTY(EditAnywhere, Category = UVs)
	bool bEnablePerParticleUOverride;

	/** Enables overriding the range of the V component with values read from the particles. */
	UPROPERTY(EditAnywhere, Category = UVs)
	bool bEnablePerParticleVRangeOverride;
};

namespace ENiagaraRibbonVFLayout
{
	enum Type
	{
		Position,
		Velocity,
		Color,
		Width,
		Twist,
		Facing,
		NormalizedAge,
		MaterialRandom,
		MaterialParam0,
		MaterialParam1,
		MaterialParam2,
		MaterialParam3,
		U0Override,
		V0RangeOverride,
		U1Override,
		V1RangeOverride,
		Num,
	};
};

UCLASS(editinlinenew, meta = (DisplayName = "Ribbon Renderer"))
class NIAGARA_API UNiagaraRibbonRendererProperties : public UNiagaraRendererProperties
{
public:
	GENERATED_BODY()

	UNiagaraRibbonRendererProperties();

	//UObject Interface
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	//UObject Interface END

	static void InitCDOPropertiesAfterModuleStartup();

	//UNiagaraRendererProperties Interface
	virtual FNiagaraRenderer* CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter, const UNiagaraComponent* InComponent) override;
	virtual class FNiagaraBoundsCalculator* CreateBoundsCalculator() override;
	virtual void GetUsedMaterials(const FNiagaraEmitterInstance* InEmitter, TArray<UMaterialInterface*>& OutMaterials) const override;
	virtual bool IsSimTargetSupported(ENiagaraSimTarget InSimTarget) const override { return (InSimTarget == ENiagaraSimTarget::CPUSim); };
#if WITH_EDITOR
	virtual bool IsMaterialValidForRenderer(UMaterial* Material, FText& InvalidMessage) override;
	virtual void FixMaterial(UMaterial* Material);
	virtual const TArray<FNiagaraVariable>& GetOptionalAttributes() override;
	virtual void GetRendererWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const override;
	virtual void GetRendererTooltipWidgets(const FNiagaraEmitterInstance* InEmitter, TArray<TSharedPtr<SWidget>>& OutWidgets, TSharedPtr<FAssetThumbnailPool> InThumbnailPool) const override;
	virtual void GetRendererFeedback(const UNiagaraEmitter* InEmitter, TArray<FText>& OutErrors, TArray<FText>& OutWarnings, TArray<FText>& OutInfo) const override;
#endif
	virtual void CacheFromCompiledData(const FNiagaraDataSetCompiledData* CompiledData) override;
	//UNiagaraRendererProperties Interface END

	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	UMaterialInterface* Material;

	/** Use the UMaterialInterface bound to this user variable if it is set to a valid value. If this is bound to a valid value and Material is also set, UserParamBinding wins.*/
	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	FNiagaraUserParameterBinding MaterialUserParamBinding;

	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	ENiagaraRibbonFacingMode FacingMode;

	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	FNiagaraRibbonUVSettings UV0Settings;

	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	FNiagaraRibbonUVSettings UV1Settings;

#if WITH_EDITORONLY_DATA
private:
	/** Tiles UV0 based on the distance traversed by the ribbon. Disables offsetting UVs by age. */
	UPROPERTY()
	float UV0TilingDistance_DEPRECATED;
	UPROPERTY()
	FVector2D UV0Scale_DEPRECATED;
	UPROPERTY()
	FVector2D UV0Offset_DEPRECATED;

	/** Defines the mode to use when offsetting UV channel 0 by age which enables smooth texture movement when particles are added and removed at the end of the ribbon.  Not used when the RibbonLinkOrder binding is in use or when tiling distance is in use. */
	UPROPERTY()
	ENiagaraRibbonAgeOffsetMode UV0AgeOffsetMode_DEPRECATED;

	/** Tiles UV1 based on the distance traversed by the ribbon. Disables offsetting UVs by age. */
	UPROPERTY()
	float UV1TilingDistance_DEPRECATED;
	UPROPERTY()
	FVector2D UV1Scale_DEPRECATED;
	UPROPERTY()
	FVector2D UV1Offset_DEPRECATED;

	/** Defines the mode to use when offsetting UV channel 1 by age which enables smooth texture movement when particles are added and removed at the end of the ribbon.  Not used when the RibbonLinkOrder binding is in use or when tiling distance is in use. */
	UPROPERTY()
	ENiagaraRibbonAgeOffsetMode UV1AgeOffsetMode_DEPRECATED;
#endif

public:

	/** If true, the particles are only sorted when using a translucent material. */
	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	ENiagaraRibbonDrawDirection DrawDirection;

	/** Defines the curve tension, or how long the curve's tangents are.
	  * Ranges from 0 to 1. The higher the value, the sharper the curve becomes.
	  */
	UPROPERTY(EditAnywhere, Category = "Tessellation", meta = (ClampMin = "0", ClampMax = "1"))
	float CurveTension;

	/** Defines the tessellation mode allowing custom tessellation parameters or disabling tessellation entirely. */
	UPROPERTY(EditAnywhere, Category = "Tessellation", meta = (DisplayName = "Mode"))
	ENiagaraRibbonTessellationMode TessellationMode;

	/** Custom tessellation factor.
	  * Ranges from 1 to 16. Greater values increase amount of tessellation.
	  */
	UPROPERTY(EditAnywhere, Category = "Tessellation", meta = (DisplayName = "Max Tessellation Factor", ClampMin = "1", ClampMax = "16"))
	int32 TessellationFactor;

	/** If checked, use the above constant factor. Otherwise, adaptively select the tessellation factor based on the below parameters. */
	UPROPERTY(EditAnywhere, Category = "Tessellation")
	bool bUseConstantFactor;

	/** Defines the angle in degrees at which tessellation occurs.
	  * Ranges from 1 to 180. Smaller values increase amount of tessellation.
	  * If set to 0, use the maximum tessellation set above.
	  */
	UPROPERTY(EditAnywhere, Category = "Tessellation", meta = (EditCondition = "!bUseConstantFactor", ClampMin = "0", ClampMax = "180", UIMin = "1", UIMax = "180"))
	float TessellationAngle;

	/** If checked, use the ribbon's screen space percentage to adaptively adjust the tessellation factor. */
	UPROPERTY(EditAnywhere, Category = "Tessellation", meta = (DisplayName = "Screen Space", EditCondition = "!bUseConstantFactor"))
	bool bScreenSpaceTessellation;

	/** Which attribute should we use for position when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding PositionBinding;

	/** Which attribute should we use for color when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding ColorBinding;

	/** Which attribute should we use for velocity when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding VelocityBinding;

	/** Which attribute should we use for normalized age when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding NormalizedAgeBinding;

	/** Which attribute should we use for ribbon twist when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding RibbonTwistBinding;

	/** Which attribute should we use for ribbon width when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding RibbonWidthBinding;

	/** Which attribute should we use for ribbon facing when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding RibbonFacingBinding;
	
	/** Which attribute should we use for ribbon id when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding RibbonIdBinding;

	/** Which attribute should we use for RibbonLinkOrder when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding RibbonLinkOrderBinding;

	/** Which attribute should we use for MaterialRandom when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding MaterialRandomBinding;

	/** Which attribute should we use for dynamic material parameters when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding DynamicMaterialBinding;

	/** Which attribute should we use for dynamic material parameters when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding DynamicMaterial1Binding;

	/** Which attribute should we use for dynamic material parameters when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding DynamicMaterial2Binding;

	/** Which attribute should we use for dynamic material parameters when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding DynamicMaterial3Binding;

	/** Which attribute should we use for UV0 U when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding U0OverrideBinding;

	/** Which attribute should we use for UV0 V when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding V0RangeOverrideBinding;

	/** Which attribute should we use for UV1 U when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding U1OverrideBinding;

	/** Which attribute should we use for UV1 V when generating ribbons?*/
	UPROPERTY(EditAnywhere, Category = "Bindings")
	FNiagaraVariableAttributeBinding V1RangeOverrideBinding;

	bool								bSortKeyDataSetAccessorIsAge = false;
	FNiagaraDataSetAccessor<float>		SortKeyDataSetAccessor;
	FNiagaraDataSetAccessor<FVector>	PositionDataSetAccessor;
	FNiagaraDataSetAccessor<float>		SizeDataSetAccessor;
	FNiagaraDataSetAccessor<float>		TwistDataSetAccessor;
	FNiagaraDataSetAccessor<FVector>	FacingDataSetAccessor;
	FNiagaraDataSetAccessor<FVector4>	MaterialParam0DataSetAccessor;
	FNiagaraDataSetAccessor<FVector4>	MaterialParam1DataSetAccessor;
	FNiagaraDataSetAccessor<FVector4>	MaterialParam2DataSetAccessor;
	FNiagaraDataSetAccessor<FVector4>	MaterialParam3DataSetAccessor;
	bool								U0OverrideIsBound;
	bool								U1OverrideIsBound;

	FNiagaraDataSetAccessor<int32>		RibbonIdDataSetAccessor;
	FNiagaraDataSetAccessor<FNiagaraID>	RibbonFullIDDataSetAccessor;

	uint32 MaterialParamValidMask = 0;
	FNiagaraRendererLayout RendererLayout;

protected:
	void InitBindings();

private: 
	static TArray<TWeakObjectPtr<UNiagaraRibbonRendererProperties>> RibbonRendererPropertiesToDeferredInit;
};
