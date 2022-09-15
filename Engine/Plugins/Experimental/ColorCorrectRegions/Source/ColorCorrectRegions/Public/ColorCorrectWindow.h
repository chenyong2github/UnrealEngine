// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ColorCorrectRegion.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Engine/Classes/Components/MeshComponent.h"
#include "Engine/Scene.h"
#include "ColorCorrectWindow.generated.h"


class UColorCorrectRegionsSubsystem;
class UBillboardComponent;

UENUM(BlueprintType)
enum class EColorCorrectWindowType : uint8
{
	Square		UMETA(DisplayName = "Square"),
	Circle		UMETA(DisplayName = "Circle"),
	MAX
};

/**
 * 
 */
UCLASS(Blueprintable, notplaceable)
class COLORCORRECTREGIONS_API AColorCorrectWindow : public AColorCorrectRegion
{
	GENERATED_UCLASS_BODY()
public:

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> MeshComponents;

	/** Region type. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Color Correction", Meta = (DisplayName = "Type"))
	EColorCorrectWindowType WindowType;

#if WITH_EDITOR
	/** Called when any of the properties are changed. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
#if WITH_METADATA
	void CreateIcon();
#endif // WITH_METADATA

	/** Swaps meshes for different CCW. */
	void SetMeshVisibilityForWindowType();

};
