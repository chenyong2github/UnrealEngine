// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "GeometryCollection/GeometryCollectionProximityUtility.h"

#include "FractureToolProximity.generated.h"

class FFractureToolContext;

/** Settings controlling how proximity is detected for geometry collections */
UCLASS(config = EditorPerProjectUserSettings)
class UFractureProximitySettings : public UFractureToolSettings
{
public:

	GENERATED_BODY()

	UFractureProximitySettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
	{}

	// Which method to use to decide whether a given piece of geometry is in proximity with another
	UPROPERTY(EditAnywhere, Category = Automatic)
	EProximityMethod Method = EProximityMethod::Precise;

	// If hull-based proximity detection is enabled, amount to expand hulls when searching for overlapping neighbors
	UPROPERTY(EditAnywhere, Category = Automatic, meta = (ClampMin = "0", EditCondition = "Method == EProximityMethod::ConvexHull"))
	double DistanceThreshold = 1;

	// Whether to automatically transform the proximity graph into a connection graph to be used for simulation
	UPROPERTY(EditAnywhere, Category = Automatic)
	bool bUseAsConnectionGraph = false;

	// Whether to display the proximity graph edges
	UPROPERTY(EditAnywhere, Category = Visualization)
	bool bShowProximity = true;

	// Whether to only show the proximity graph connections for selected bones
	UPROPERTY(EditAnywhere, Category = Visualization, meta = (EditCondition = "bShowProximity"))
	bool bOnlyShowForSelected = false;
};



UCLASS(DisplayName = "Proximity Tool", Category = "FractureTools")
class UFractureToolProximity : public UFractureModalTool
{
public:
	GENERATED_BODY()

	UFractureToolProximity(const FObjectInitializer& ObjInit);

	///
	/// UFractureModalTool Interface
	///

	/** This is the Text that will appear on the tool button to execute the tool **/
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	/** This is the Text that will appear on the button to execute the fracture **/
	virtual FText GetApplyText() const override { return FText(NSLOCTEXT("FractureProximity", "ExecuteProximity", "Update Bone Proximity")); }
	virtual FSlateIcon GetToolIcon() const override;
	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;
	virtual TArray<UObject*> GetSettingsObjects() const override;
	virtual void FractureContextChanged() override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	/** Executes function that generates new geometry. Returns the first new geometry index. */
	virtual int32 ExecuteFracture(const FFractureToolContext& FractureContext) override;
	virtual bool CanExecute() const override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;

	/** Gets the UI command info for this command */
	const TSharedPtr<FUICommandInfo>& GetUICommandInfo() const;

	virtual TArray<FFractureToolContext> GetFractureToolContexts() const override;

	virtual void Setup() override;

private:

	UPROPERTY(EditAnywhere, Category = Proximity)
	TObjectPtr<UFractureProximitySettings> ProximitySettings;

	void UpdateVisualizations();
	virtual void ClearVisualizations() override
	{
		Super::ClearVisualizations();
		ProximityVisualizations.Empty();
	}

	struct FEdgeVisInfo
	{
		int32 A, B;
	};

	struct FCollectionVisInfo
	{
		TArray<FEdgeVisInfo> ProximityEdges;
		TArray<FVector> GeoCenters;
		int32 CollectionIndex;
	};

	TArray<FCollectionVisInfo> ProximityVisualizations;

};
