// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "Components/SplineComponent.h"

#include "VPSplineMetadata.h"
#include "VPSplinePointData.h"

#if WITH_EDITOR
#include "ILevelSequenceEditorToolkit.h"
#include "ISequencer.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"

#include "Channels/MovieSceneChannelEditorData.h"
#include "MovieSceneSection.h"
#endif

#include "VPSplineComponent.generated.h"

UCLASS(ClassGroup = Utility, ShowCategories = (Mobility), HideCategories = (Physics, Collision, Lighting, Rendering, Mobile), meta = (BlueprintSpawnableComponent))
class VPSPLINE_API UVPSplineComponent : public USplineComponent
{
	GENERATED_BODY()

public:
	UVPSplineComponent(const FObjectInitializer& ObjectInitializer);

	/**
	* Defaults which are used to propagate values to spline points on instances of this in the world
	*/
	UPROPERTY(Category = Camera, EditDefaultsOnly)
	FVPSplineCurveDefaults CameraSplineDefaults;

	// UObject interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void PostLoad() override;
	// End of UObject interface

	/** Spline component interface */
	virtual USplineMetadata* GetSplinePointsMetadata() override;
	virtual const USplineMetadata* GetSplinePointsMetadata() const override;
	virtual bool AllowsSplinePointScaleEditing() const override { return false; }

	void ApplyComponentInstanceData(struct FVPSplineInstanceData* ComponentInstanceData, const bool bPostUCS);

	// UActorComponent Interface
	virtual TStructOnScope<FActorComponentInstanceData> GetComponentInstanceData() const override;

	/** Pointer to metadata */
	UPROPERTY(Instanced)
	TObjectPtr<UVPSplineMetadata> VPSplineMetadata;

	/** Set focal lenght metadata at a given splint point*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	void SetFocalLengthAtSplinePoint(const int32 PointIndex, const float Value );

	/** Set aperture metadata at a given spline point*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	void SetApertureAtSplinePoint(const int32 PointIndex, const float value );

	/** Set focus distance metadata at a given spline point*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	void SetFocusDistanceAtSplinePoint(const int32 PointIndex, const float value);

	/** Set normalized position metadata at a given spoint point*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	void SetNormalizedPositionAtSplinePoint(const int32 PointIndex, const float value);

	/** Returns true if there is a spline point at given normalized position*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	bool FindSplineDataAtPosition(const float InPosition, int32& OutIndex) const;

	/* Update spline point data at the given spline point*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	void UpdateSplineDataAtIndex(const int InIndex, const FVPSplinePointData& InPointData );

	/* Add a new spline point data at the given normalized position*/
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	void AddSplineDataAtPosition(const float InPosition, const FVPSplinePointData& InPointData);

	/*Calculates input key from normalized position */
	UFUNCTION(BlueprintCallable, Category = "VPSpline")
	float GetInputKeyAtPosition(const float InPosition);

protected:
	void SynchronizeProperties();

public:
	// Triggered when the spline is edited
	FSimpleDelegate OnSplineEdited;

};

USTRUCT()
struct FVPSplineInstanceData : public FSplineInstanceData
{
	GENERATED_BODY()
public:
	FVPSplineInstanceData() = default;
	explicit FVPSplineInstanceData(const UVPSplineComponent* SourceComponent) : FSplineInstanceData(SourceComponent) {}
	virtual ~FVPSplineInstanceData() = default;
	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override;

	UPROPERTY()
	TObjectPtr<UVPSplineMetadata> VPSplineMetadata;
};