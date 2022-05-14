// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "DisplayClusterLightCardActor.generated.h"

class USceneComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UStaticMesh;

UENUM(BlueprintType)
enum class EDisplayClusterLightCardMask : uint8
{
	Circle,
	Square,
	UseTextureAlpha
};

UCLASS(Blueprintable)
class DISPLAYCLUSTER_API ADisplayClusterLightCardActor : public AActor
{
	GENERATED_BODY()

public:
	ADisplayClusterLightCardActor(const FObjectInitializer& ObjectInitializer);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#endif
	
	/**
	 * Gets the transform in world space of the light card component
	 * @param bIgnoreSpinYawPitch - If the light card component's spin, yaw, and pitch should be ignored when computing the transform
	 */
	FTransform GetLightCardTransform(bool bIgnoreSpinYawPitch = false) const;

	/** Returns the current static mesh used by this light card */
	UStaticMesh* GetStaticMesh() const;

	/** Sets a new static mesh for the light card */
	void SetStaticMesh(UStaticMesh* InStaticMesh);
	
protected:
	void UpdateLightCardTransform();
	void UpdateLightCardMaterialInstance();

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation")
	float DistanceFromCenter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (UIMin = 0, ClampMin = 0, UIMax = 360, ClampMax = 360))
	float Longitude;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (UIMin = -90, ClampMin = -90, UIMax = 90, ClampMax = 90))
	float Latitude;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation")
	float Spin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation")
	float Pitch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation")
	float Yaw;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation")
	FVector2D Scale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	EDisplayClusterLightCardMask Mask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	UTexture* Texture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor Color;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	float Exposure;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (UIMin = 0, ClampMin = 0, UIMax = 1, ClampMax = 1))
	float Opacity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (UIMin = 0, ClampMin = 0))
	float Feathering;

	/** Used to flag this light card as a proxy of a "real" light card. Used by the LightCard Editor */
	UPROPERTY(Transient)
	bool bIsProxy = false;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default")
	TObjectPtr<USceneComponent> DefaultSceneRootComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default")
	TObjectPtr<USpringArmComponent> MainSpringArmComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default")
	TObjectPtr<USceneComponent> LightCardTransformerComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default")
	TObjectPtr<UStaticMeshComponent> LightCardComponent;
};