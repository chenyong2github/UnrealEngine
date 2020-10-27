// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "DMXSubsystem.h"
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
	UltraQuality		UMETA(DisplayName = "Ultra")
};


UCLASS(HideCategories = ("Rendering", "Variable", "Input", "Tags", "Activation", "Cooking", "Replication", "AssetUserData", "Collision", "LOD", "Actor"))
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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Light Fixture", meta = (DisplayPriority = 0))
	TEnumAsByte<EDMXFixtureQualityLevel> QualityLevel;

	// HIERARCHY---------------------------------
	UPROPERTY()
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

protected:
	/** Sets the fixture in a defaulted state using default values of its Fixture Components */
	void SetDefaultFixtureState();
	
	/*
	 * Pushes DMX Values to the Fixture. Does not interpolate, causing fixtures to jump to the value directly. 
	 * Useful for initalization when data is first received.
	 */
	virtual void InitalizeAttributeValueNoInterp(const FDMXAttributeName& AttributeName, float Value);

	/** Contains all attributes that were received at least once and got initalized  */
	TSet<FDMXAttributeName> InitializedAttributes;

	/** Holds invalid attributes. Helps ignoring attributes that don't exist */
	TSet<FDMXAttributeName> InvalidAttributes;

public:
	UFUNCTION(BlueprintCallable, Category = "DMX Fixture", meta = (DeprecatedFunction, DeprecationMessage = "Deprecated 4.26. Use PushDMXValuesPerAttribute instead."))
	void PushDMXData(TMap<FDMXAttributeName, int32> AttributesMap);

	UFUNCTION(BlueprintCallable, Category = "DMX Fixture")
	void InterpolateDMXComponents(float DeltaSeconds);
	
	// PARAMETERS---------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Light Fixture")
	float LightIntensityMax;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Light Fixture")
	float LightDistanceMax;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Light Fixture")
	float LightColorTemp;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Light Fixture")
	float SpotlightIntensityScale;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Light Fixture")
	float PointlightIntensityScale;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Light Fixture")
	bool LightCastShadow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DMX Light Fixture")
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
	UPROPERTY(EditDefaultsOnly, Category = "DMX Light Fixture")
	UMaterialInstance* LensMaterialInstance;

	UPROPERTY(EditDefaultsOnly, Category = "DMX Light Fixture")
	UMaterialInstance* BeamMaterialInstance;

	UPROPERTY(EditDefaultsOnly, Category = "DMX Light Fixture")
	UMaterialInstance* SpotLightMaterialInstance;

	UPROPERTY(EditDefaultsOnly, Category = "DMX Light Fixture")
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
