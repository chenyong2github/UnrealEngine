// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXFixtureComponent.h"
#include "DMXFixtureComponentDouble.h"
#include "DMXFixtureComponentSingle.h"
#include "DMXFixtureComponentColor.h"
#include "GameFramework/Actor.h"
#include "Game/DMXComponent.h"
#include "Components/SpotLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/ArrowComponent.h"
#include "DMXProtocolTypes.h"
#include "Library/DMXEntityFixturePatch.h"
#include "DMXFixtureActor.generated.h"


UENUM()
enum EDMXFixtureQualityLevel
{
	LowQuality			UMETA(DisplayName = "Low"),
	MediumQuality		UMETA(DisplayName = "Medium"),
	HighQuality			UMETA(DisplayName = "High"),
	UltraQuality		UMETA(DisplayName = "Ultra"),
	Custom				UMETA(DisplayName = "Custom")
};

UCLASS()
class DMXFIXTURES_API ADMXFixtureActor : public AActor
{
	GENERATED_BODY()

protected:

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	ADMXFixtureActor();

	bool HasBeenInitialized;
	float LensRadius;
	void FeedFixtureData();

	// VISUAL QUALITY LEVEL----------------------

	// Visual quality level that changes the number of samples in the volumetric beam
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DMX Light Fixture", meta = (DisplayPriority = 0))
	TEnumAsByte<EDMXFixtureQualityLevel> QualityLevel;

	// Visual quality when using smaller zoom angle (thin beam). Small value is visually better but cost more on GPU
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DMX Light Fixture", meta = (EditCondition = "QualityLevel == EDMXFixtureQualityLevel::Custom", EditConditionHides))
	float MinQuality;

	// Visual quality when using bigger zoom angle (wide beam). Small value is visually better but cost more on GPU
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DMX Light Fixture", meta = (EditCondition = "QualityLevel == EDMXFixtureQualityLevel::Custom", EditConditionHides))
	float MaxQuality;

	// HIERARCHY---------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DMX Light Fixture")
	USceneComponent* Base;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DMX Light Fixture")
	USceneComponent* Yoke;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DMX Light Fixture")
	USceneComponent* Head;

	// FUNCTIONS---------------------------------

	UFUNCTION(BlueprintCallable, Category = "DMX Fixture")
	void InitializeFixture(UStaticMeshComponent* StaticMeshLens, UStaticMeshComponent* StaticMeshBeam);

	/** Pushes DMX Values to the Fixture. Expects normalized values in the range of 0.0f - 1.0f */
	UFUNCTION(BlueprintCallable, Category = "DMX Fixture")
	void PushNormalizedValuesPerAttribute(const FDMXNormalizedAttributeValueMap& ValuePerAttributeMap);
	
public:
	UFUNCTION(BlueprintCallable, Category = "DMX Fixture")
	void InterpolateDMXComponents(float DeltaSeconds);
	
	// PARAMETERS---------------------------------

	// Light intensity at 1 steradian (32.77deg half angle)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DMX Light Fixture")
	float LightIntensityMax;

	// Sets Attenuation Radius on the spotlight and pointlight
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DMX Light Fixture")
	float LightDistanceMax;

	// Light color temperature on the spotlight and pointlight
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DMX Light Fixture")
	float LightColorTemp;

	// Scales spotlight intensity
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DMX Light Fixture")
	float SpotlightIntensityScale;

	// Scales pointlight intensity
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DMX Light Fixture")
	float PointlightIntensityScale;

	// Enable/disable cast shadow on the spotlight and pointlight
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DMX Light Fixture")
	bool LightCastShadow;

	// Simple solution useful for walls, 1 linetrace from the center
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DMX Light Fixture")
	bool UseDynamicOcclusion;



	// DMX COMPONENT -----------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DMX Light Fixture")
	class UDMXComponent* DMX;

	// COMPONENTS ---------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "DMX Light Fixture")
	TArray<UStaticMeshComponent*> StaticMeshComponents;
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DMX Light Fixture")
	class USpotLightComponent* SpotLight;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DMX Light Fixture")
	class UPointLightComponent* PointLight;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DMX Light Fixture")
	class UArrowComponent* OcclusionDirection;


	// MATERIALS ---------------------------------

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "DMX Light Fixture")
	UMaterialInstance* LensMaterialInstance;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "DMX Light Fixture")
	UMaterialInstance* BeamMaterialInstance;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "DMX Light Fixture")
	UMaterialInstance* SpotLightMaterialInstance;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "DMX Light Fixture")
	UMaterialInstance* PointLightMaterialInstance;

	UPROPERTY(BlueprintReadOnly, Category = "DMX Light Fixture")
	UMaterialInstanceDynamic* DynamicMaterialLens;

	UPROPERTY(BlueprintReadOnly, Category = "DMX Light Fixture")
	UMaterialInstanceDynamic* DynamicMaterialBeam;

	UPROPERTY(BlueprintReadOnly, Category = "DMX Light Fixture")
	UMaterialInstanceDynamic* DynamicMaterialSpotLight;

	UPROPERTY(BlueprintReadOnly, Category = "DMX Light Fixture")
	UMaterialInstanceDynamic* DynamicMaterialPointLight;

};
