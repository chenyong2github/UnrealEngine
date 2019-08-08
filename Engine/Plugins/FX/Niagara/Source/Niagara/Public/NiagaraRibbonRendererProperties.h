// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraCommon.h"
#include "NiagaraRibbonRendererProperties.generated.h"

class FNiagaraEmitterInstance;

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

UCLASS(editinlinenew, meta = (DisplayName = "Ribbon Renderer"))
class UNiagaraRibbonRendererProperties : public UNiagaraRendererProperties
{
public:
	GENERATED_BODY()

	UNiagaraRibbonRendererProperties();

	//UObject Interface
	virtual void PostInitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const UProperty* InProperty) const override;
#endif
	//UObject Interface END

	static void InitCDOPropertiesAfterModuleStartup();

	//UNiagaraRendererProperties Interface
	virtual FNiagaraRenderer* CreateEmitterRenderer(ERHIFeatureLevel::Type FeatureLevel, const FNiagaraEmitterInstance* Emitter) override;
	virtual class FNiagaraBoundsCalculator* CreateBoundsCalculator() override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials) const override;
	virtual bool IsSimTargetSupported(ENiagaraSimTarget InSimTarget) const override { return (InSimTarget == ENiagaraSimTarget::CPUSim); };
#if WITH_EDITOR
	virtual bool IsMaterialValidForRenderer(UMaterial* Material, FText& InvalidMessage) override;
	virtual void FixMaterial(UMaterial* Material);
	virtual const TArray<FNiagaraVariable>& GetRequiredAttributes() override;
	virtual const TArray<FNiagaraVariable>& GetOptionalAttributes() override;
#endif
	//UNiagaraRendererProperties Interface END


	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	UMaterialInterface* Material;

	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	ENiagaraRibbonFacingMode FacingMode;

	/** Tiles UV0 based on the distance traversed by the ribbon. Disables offsetting UVs by age. */
	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	float UV0TilingDistance;
	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	FVector2D UV0Scale;
	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	FVector2D UV0Offset;

	/** Defines the mode to use when offsetting UV channel 0 by age which enables smooth texture movement when particles are added and removed at the end of the ribbon.  Not used when the RibbonLinkOrder binding is in use or when tiling distance is in use. */
	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	ENiagaraRibbonAgeOffsetMode UV0AgeOffsetMode;

	/** Tiles UV1 based on the distance traversed by the ribbon. Disables offsetting UVs by age. */
	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	float UV1TilingDistance;
	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	FVector2D UV1Scale;
	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	FVector2D UV1Offset;

	/** Defines the mode to use when offsetting UV channel 1 by age which enables smooth texture movement when particles are added and removed at the end of the ribbon.  Not used when the RibbonLinkOrder binding is in use or when tiling distance is in use. */
	UPROPERTY(EditAnywhere, Category = "Ribbon Rendering")
	ENiagaraRibbonAgeOffsetMode UV1AgeOffsetMode;

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
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding PositionBinding;

	/** Which attribute should we use for color when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding ColorBinding;

	/** Which attribute should we use for velocity when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding VelocityBinding;

	/** Which attribute should we use for normalized age when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding NormalizedAgeBinding;

	/** Which attribute should we use for ribbon twist when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding RibbonTwistBinding;

	/** Which attribute should we use for ribbon width when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding RibbonWidthBinding;

	/** Which attribute should we use for ribbon facing when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding RibbonFacingBinding;
	
	/** Which attribute should we use for ribbon id when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding RibbonIdBinding;

	/** Which attribute should we use for RibbonLinkOrder when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding RibbonLinkOrderBinding;

	/** Which attribute should we use for MaterialRandom when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding MaterialRandomBinding;

	/** Which attribute should we use for dynamic material parameters when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding DynamicMaterialBinding;

	/** Which attribute should we use for dynamic material parameters when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding DynamicMaterial1Binding;

	/** Which attribute should we use for dynamic material parameters when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding DynamicMaterial2Binding;

	/** Which attribute should we use for dynamic material parameters when generating ribbons?*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Bindings")
	FNiagaraVariableAttributeBinding DynamicMaterial3Binding;

protected:
	void InitBindings();
};
