// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "DisplayClusterLightCardActor.generated.h"

class ADisplayClusterRootActor;
class UDisplayClusterLabelComponent;
class USceneComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UStaticMesh;

UENUM(BlueprintType)
enum class EDisplayClusterLightCardMask : uint8
{
	Circle,
	Square,
	UseTextureAlpha,
	Polygon,
};

USTRUCT(Blueprintable)
struct FLightCardAlphaGradientSettings
{
	GENERATED_BODY()

	/** Enables/disables alpha gradient effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	bool bEnableAlphaGradient = false;

	/** Starting alpha value in the gradient */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (EditCondition = "bEnableAlphaGradient"))
	float StartingAlpha = 0;

	/** Ending alpha value in the gradient */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (EditCondition = "bEnableAlphaGradient"))
	float EndingAlpha = 1;

	/** The angle (degrees) determines the gradient direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (EditCondition = "bEnableAlphaGradient"))
	float Angle = 0;
};

UCLASS(Blueprintable)
class DISPLAYCLUSTER_API ADisplayClusterLightCardActor : public AActor
{
	GENERATED_BODY()

public:

	struct PositionalParams
	{
		double DistanceFromCenter;
		double Longitude;
		double Latitude;
		double Spin;
		double Pitch;
		double Yaw;
		double RadialOffset;
	};

public:
	/** The rotation used to orient the plane mesh used for the light card so that its normal points radially inwards */
	static const FRotator PlaneMeshRotation;

	/** The default size of the porjection plane UV light cards are rendered to */
	static const float UVPlaneDefaultSize;

	/** The default distance from the view of the projection plane UV light cards are rendered to */
	static const float UVPlaneDefaultDistance;

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

	/** Gets the object oriented bounding box of the light card component */
	FBox GetLightCardBounds(bool bLocalSpace = false) const;

	/** Gets the light card mesh component */
	UStaticMeshComponent* GetLightCardMeshComponent() const { return LightCardComponent.Get(); }

	/** Returns the current static mesh used by this light card */
	UStaticMesh* GetStaticMesh() const;

	/** Sets a new static mesh for the light card */
	void SetStaticMesh(UStaticMesh* InStaticMesh);

	/** Updates the Light Card transform based on its positional properties (Lat, Long, etc.) */
	void UpdateLightCardTransform();

	/** Retrieves positional parameters */
	PositionalParams GetPositionalParams();

	/** Set positional parameters */
	void SetPositionalParams(const PositionalParams& Params);

	/** Updates the card's material instance parameters */
	void UpdateLightCardMaterialInstance();

	/** Updates the polygon texture from the polygon points */
	void UpdatePolygonTexture();

	/** Updates the light card actor visibility */
	void UpdateLightCardVisibility();

	/** Show or hide the light card label  */
	void ShowLightCardLabel(bool bValue, float ScaleValue, ADisplayClusterRootActor* InRootActor);

	/** Set the current owner of the light card */
	void SetRootActorOwner(ADisplayClusterRootActor* InRootActor);
	
	/** Return the current owner, providing one was set */
	TWeakObjectPtr<ADisplayClusterRootActor> GetRootActorOwner() const { return RootActorOwner; }
	
public:

	/** Radius of light card polar coordinates. Does not include the effect of RadialOffset */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (EditCondition = "!bIsUVLightCard", HideEditConditionToggle, EditConditionHides))
	double DistanceFromCenter;

	/** Related to the Azimuth of light card polar coordinates */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (UIMin = 0, ClampMin = 0, UIMax = 360, ClampMax = 360, EditCondition = "!bIsUVLightCard", HideEditConditionToggle, EditConditionHides))
	double Longitude;

	/** Related to the Elevation of light card polar coordinates */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (UIMin = -90, ClampMin = -90, UIMax = 90, ClampMax = 90, EditCondition = "!bIsUVLightCard", HideEditConditionToggle, EditConditionHides))
	double Latitude;

	/** The UV coordinates of the light card, if it is in UV space */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (DisplayName = "UV Coodinates", EditCondition = "bIsUVLightCard", HideEditConditionToggle, EditConditionHides))
	FVector2D UVCoordinates = FVector2D(0.5, 0.5);

	/** Roll rotation of light card around its plane axis */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (UIMin = -360, ClampMin = -360, UIMax = 360, ClampMax = 360))
	double Spin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (UIMin = -360, ClampMin = -360, UIMax = 360, ClampMax = 360, EditCondition = "!bIsUVLightCard", HideEditConditionToggle, EditConditionHides))
	double Pitch;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (UIMin = -360, ClampMin = -360, UIMax = 360, ClampMax = 360, EditCondition = "!bIsUVLightCard", HideEditConditionToggle, EditConditionHides))
	double Yaw;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation")
	FVector2D Scale;

	/** Used by the flush constraint to offset the location of the light card form the wall */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orientation", meta = (EditCondition = "!bIsUVLightCard", HideEditConditionToggle, EditConditionHides))
	double RadialOffset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	EDisplayClusterLightCardMask Mask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TObjectPtr<UTexture> Texture;

	/** Light card color, before any modifier is applied */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor Color;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (UIMin = 0, ClampMin = 0, UIMax = 10000, ClampMax = 10000))
	float Temperature;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (UIMin = -1, ClampMin = -1, UIMax = 1, ClampMax = 1))
	float Tint;

	/** 2^Exposure color value multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	float Exposure;

	/** Linear color value multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (UIMin = 0, ClampMin = 0))
	float Gain;

	/** Linear alpha multiplier */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (UIMin = 0, ClampMin = 0, UIMax = 1, ClampMax = 1))
	float Opacity;

	/** Feathers in the alpha from the edges */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (UIMin = 0, ClampMin = 0))
	float Feathering;

	/** Settings related to an alpha gradient effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLightCardAlphaGradientSettings AlphaGradient;

	/** A flag that controls wether the light card's location and rotation are locked to its "owning" root actor */
	UPROPERTY()
	bool bLockToOwningRootActor = true;

	/** Indicates if the light card exists in 3D space or in UV space */
	UPROPERTY()
	bool bIsUVLightCard = false;

	/** Used to flag this light card as a proxy of a "real" light card. Used by the LightCard Editor */
	UPROPERTY(Transient)
	bool bIsProxy = false;

	/** Polygon points when using this type of mask */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TArray<FVector2D> Polygon;

	/** Used to flag this light card as a proxy of a "real" light card. Used by the LightCard Editor */
	UPROPERTY(Transient)
	TObjectPtr<UTexture> PolygonMask = nullptr;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default")
	TObjectPtr<USceneComponent> DefaultSceneRootComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default")
	TObjectPtr<USpringArmComponent> MainSpringArmComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default")
	TObjectPtr<USceneComponent> LightCardTransformerComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Default")
	TObjectPtr<UStaticMeshComponent> LightCardComponent;
	
	UPROPERTY(VisibleAnywhere, transient, BlueprintReadOnly, Category = "Default")
	TObjectPtr<UDisplayClusterLabelComponent> LabelComponent;

	/** The current owner of the light card */
	TWeakObjectPtr<ADisplayClusterRootActor> RootActorOwner;

private:
	/** Stores the user translucency value when labels are displayed */
	TOptional<int32> SavedTranslucencySortPriority;
	
};