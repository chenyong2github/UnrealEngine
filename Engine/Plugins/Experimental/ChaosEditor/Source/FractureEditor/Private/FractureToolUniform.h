// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureToolVoronoiBase.h"

#include "FractureToolUniform.generated.h"


UCLASS()
class UFractureUniformSettings
	: public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif

	UFractureUniformSettings()
		: NumberVoronoiSitesMin(20)
		, NumberVoronoiSitesMax(20)
		, OwnerTool(nullptr)
	{}


	/** Minimum Number of Voronoi sites - A random number will be chosen between the Min and Max for each bone you have selected */
	UPROPERTY(EditAnywhere, Category = UniformVoronoi, meta = (DisplayName = "Minimum Voronoi Sites", UIMin = "1", UIMax = "5000", ClampMin = "1"))
	int32 NumberVoronoiSitesMin;

	/** Maximum Number of Voronoi sites - A random number will be chosen between the Min and Max for each bone you have selected */
	UPROPERTY(EditAnywhere, Category = UniformVoronoi, meta = (DisplayName = "Maximum Voronoi Sites", UIMin = "1", UIMax = "5000", ClampMin = "1"))
	int32 NumberVoronoiSitesMax;

	UPROPERTY()
	UFractureTool *OwnerTool;
};


UCLASS(DisplayName="Uniform Voronoi", Category="FractureTools")
class UFractureToolUniform : public UFractureToolVoronoiBase
{
public:
	GENERATED_BODY()

	UFractureToolUniform(const FObjectInitializer& ObjInit);//  : Super(ObjInit) {}

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual TArray<UObject*> GetSettingsObjects() const override;// { return TArray<UObject*>(); }

	virtual void RegisterUICommand( FFractureEditorCommands* BindingContext );

// #if WITH_EDITOR
// 	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
// 	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
// #endif
// 	void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	// Uniform Voronoi Fracture Input Settings
	UPROPERTY(EditAnywhere, Category = Uniform)
	UFractureUniformSettings* Settings;

protected:

	void GenerateVoronoiSites(const FFractureContext &Context, TArray<FVector>& Sites) override;
};