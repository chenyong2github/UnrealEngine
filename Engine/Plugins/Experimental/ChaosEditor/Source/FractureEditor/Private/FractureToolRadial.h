// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureToolVoronoiBase.h"

#include "FractureToolRadial.generated.h"


UCLASS()
class UFractureRadialSettings
	: public UObject
{
	GENERATED_BODY()
public:

	UFractureRadialSettings()
	: Center(FVector(0,0,0))
	, Normal(FVector(0, 0, 1))
	, Radius(50.0f)
	, AngularSteps(5)
	, RadialSteps(5)
	, AngleOffset(0.0f)
	, Variability(0.0f)
	{}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	/** Center of generated pattern */
	UPROPERTY(EditAnywhere, Category = RadialVoronoi)
	FVector Center;

	/** Normal to plane in which sites are generated */
	UPROPERTY(EditAnywhere, Category = RadialVoronoi)
	FVector Normal;

	/** Pattern radius */
	UPROPERTY(EditAnywhere, Category = RadialVoronoi, meta = (DisplayName = "Radius", UIMin = "0.0", ClampMin = "0.0"))
	float Radius;

	/** Number of angular steps */
	UPROPERTY(EditAnywhere, Category = RadialVoronoi, meta = (DisplayName = "Angular Steps", UIMin = "1", UIMax = "50", ClampMin = "1"))
	int AngularSteps;

	/** Number of radial steps */
	UPROPERTY(EditAnywhere, Category = RadialVoronoi, meta = (DisplayName = "Radial Steps", UIMin = "1", UIMax = "50", ClampMin = "1"))
	int RadialSteps;

	/** Angle offset at each radial step */
	UPROPERTY(EditAnywhere, Category = RadialVoronoi, meta = (DisplayName = "Angle Offset", UIMin = "0.0", ClampMin = "0.0"))
	float AngleOffset;

	/** Randomness of sites distribution */
	UPROPERTY(EditAnywhere, Category = RadialVoronoi, meta = (DisplayName = "Variability", UIMin = "0.0", ClampMin = "0.0"))
	float Variability;

	UPROPERTY()
	UFractureTool *OwnerTool;
};


UCLASS(DisplayName="Radial Voronoi", Category="FractureTools")
class UFractureToolRadial : public UFractureToolVoronoiBase
{
public:
	GENERATED_BODY()

	UFractureToolRadial(const FObjectInitializer& ObjInit);//  : Super(ObjInit) {}

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual TArray<UObject*> GetSettingsObjects() const override;// { return TArray<UObject*>(); }

	virtual void RegisterUICommand( FFractureEditorCommands* BindingContext );
	
	// virtual void ExecuteFracture() {}
	// virtual bool CanExecuteFracture() { return true; }
protected:
	void GenerateVoronoiSites(const FFractureContext &Context, TArray<FVector>& Sites) override;

};