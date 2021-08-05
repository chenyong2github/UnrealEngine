// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Engine/Classes/Components/MeshComponent.h"
#include "ColorCorrectRegion.generated.h"


class UColorCorrectRegionsSubsystem;

UENUM(BlueprintType)
enum class EColorCorrectRegionsType : uint8 
{
	Sphere		UMETA(DisplayName = "Sphere"),
	Box			UMETA(DisplayName = "Box"),
	Cylinder	UMETA(DisplayName = "Cylinder"),
	Cone		UMETA(DisplayName = "Cone"),
	MAX
};

UENUM(BlueprintType)
enum class EColorCorrectRegionTemperatureType : uint8
{
	LegacyTemperature		UMETA(DisplayName = "Temperature (Legacy)"),
	WhiteBalance			UMETA(DisplayName = "White Balance"),
	ColorTemperature		UMETA(DisplayName = "Color Temperature"),
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

	/** Type of algorithm to be used to control color temperature or white balance. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Color Correction")
	EColorCorrectRegionTemperatureType TemperatureType;

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

	virtual void BeginDestroy() override;

	virtual void TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction);
	virtual bool ShouldTickIfViewportsOnly() const;

	/** 
	* We have to manage the lifetime of the region ourselves, because EndPlay is not guaranteed to be called 
	* and BeginDestroy could be called from GC when it is too late.
	*/
	void Cleanup();

	/**
	* This is used on render thread, and not atomic on purpose to avoid stalling Render thread even for a little bit. 
	*/
	void GetBounds(FVector& InOutOrigin, FVector& InOutBoxExtent) const
	{
		InOutOrigin = BoxOrigin;
		InOutBoxExtent = BoxExtent;
	};

private:
	UColorCorrectRegionsSubsystem* ColorCorrectRegionsSubsystem;

	FVector BoxOrigin;
	FVector BoxExtent;

	FTransform PreviousFrameTransform;
};
