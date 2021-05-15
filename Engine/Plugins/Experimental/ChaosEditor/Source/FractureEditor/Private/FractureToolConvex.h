// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"

#include "FractureToolConvex.generated.h"

class FFractureToolContext;

/** Settings controlling how convex hulls are generated for geometry collections */
UCLASS(config = EditorPerProjectUserSettings)
class UFractureConvexSettings : public UFractureToolSettings
{
public:

	GENERATED_BODY()

	UFractureConvexSettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
	{}

	/** Fraction of the convex hulls for a transform that we can remove before instead using the hulls of the children */
	UPROPERTY(EditAnywhere, Category = MapSettings, meta = (ClampMin = ".01", ClampMax = "1"))
	double FractionAllowRemove = .5;
};


UCLASS(DisplayName = "Convex Tool", Category = "FractureTools")
class UFractureToolConvex : public UFractureModalTool
{
public:
	GENERATED_BODY()

	UFractureToolConvex(const FObjectInitializer& ObjInit);

	///
	/// UFractureModalTool Interface
	///

	/** This is the Text that will appear on the tool button to execute the tool **/
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	/** This is the Text that will appear on the button to execute the fracture **/
	virtual FText GetApplyText() const override { return FText(NSLOCTEXT("Convex", "ExecuteConvex", "Make Convex Hulls")); }
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


protected:
	UPROPERTY(EditAnywhere, Category = Slicing)
	TObjectPtr<UFractureConvexSettings> ConvexSettings;

	TArray<FVector> HullPoints;
	TArray<TPair<int32, int32>> HullEdges;
};
