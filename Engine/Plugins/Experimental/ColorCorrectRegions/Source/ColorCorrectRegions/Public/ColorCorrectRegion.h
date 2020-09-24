// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Engine/Classes/Components/MeshComponent.h"
#include "ColorCorrectRegion.generated.h"



UENUM(BlueprintType)
enum class EColorCorrectRegionsType : uint8 
{
	Sphere		UMETA(DisplayName = "Sphere"),
	Box			UMETA(DisplayName = "Box"),
	Cylinder	UMETA(DisplayName = "Cylinder"),
	Cone		UMETA(DisplayName = "Cone"),
	MAX
};

/**
 * An instance of Color Correction Region. Used to aggregate all active regions.
 * This actor is aggregated by ColorCorrectRegionsSubsystem which handles:
 *   - Level Loaded, Undo/Redo, Added to level, Removed from level events. 
 * AActor class itself is not aware of when it is added/removed, Undo/Redo etc in the Editor. 
 * AColorCorrectRegion reaches out to UColorCorrectRegionsSubsystem when its priority is changed, requesting regions to be sorted 
 * or during BeginPlay/EndPlay to register itself. 
 * More information in ColorCorrectRegionsSubsytem.h
 */
UCLASS(Blueprintable)
class COLORCORRECTREGIONS_API AColorCorrectRegion : public AActor
{
	GENERATED_UCLASS_BODY()
public:
	/** Region type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Color Correction")
	EColorCorrectRegionsType Type;

	/** Render priority/order. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Color Correction")
	int32 Priority;

	/** Color correction intensity. Clamped to 0-1 range. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Color Correction", meta = (UIMin = 0.0, UIMax = 1.0))
	float Intensity;

	/** Inner of the region. Swapped with Outer in case it is higher than Outer. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Color Correction", meta = (UIMin = 0.0, UIMax = 1.0))
	float Inner;

	/** Outer of the region. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Color Correction", meta = (UIMin = 0.0, UIMax = 1.0))
	float Outer;

	/** Falloff. Softening the region. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Color Correction", meta = (UIMin = 0.0))
	float Falloff;

	/** Invert region. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Color Correction")
	bool Invert;

	/** Color correction temperature. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Color Correction")
	float Temperature;

	/** Color correction settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Color Correction")
	FColorGradingSettings ColorGradingSettings;

	/** Enable/Disable color correction provided by this region. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Color Correction")
	bool Enabled;

	/** Enable stenciling. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Color Correction")
	bool ExcludeStencil;

#if WITH_EDITOR
	/** Called when any of the properties are changed. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** To handle play in Editor, PIE and Standalone. These methods aggregate objects in play mode similarly to 
	* Editor methods in FColorCorrectRegionsSubsystem
	*/
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
