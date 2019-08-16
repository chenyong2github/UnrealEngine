// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Textures/SlateIcon.h"
#include "SceneManagement.h"

#include "Framework/Commands/UICommandInfo.h"
#include "FractureEditorCommands.h"

#include "FractureTool.generated.h"

class UEditableMesh;
class UGeometryCollection;
class UFractureTool;

struct FFractureContext
{
	AActor* OriginalActor;
	UPrimitiveComponent* OriginalPrimitiveComponent;
// 	const UEditableMesh* SourceMesh;

	UGeometryCollection* FracturedGeometryCollection;
	FString ParentName;
	FTransform Transform;
	FBox Bounds;
// 	FVector InBoundsOffset;
// 	int32 FracturedChunkIndex;
	int32 RandomSeed;
	TArray<int32> SelectedBones;
};

/** Settings specifically related to the one-time destructive fracturing of a mesh **/
UCLASS(config = EditorPerProjectUserSettings)
class UFractureCommonSettings: public UObject
{
	GENERATED_BODY()
public:
	UFractureCommonSettings(const FObjectInitializer& ObjInit);

#if WITH_EDITOR

	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;

#endif
	/** Random number generator seed for repeatability */
	UPROPERTY(EditAnywhere, Category = CommonFracture, meta = (DisplayName = "Random Seed", UIMin = "-1", UIMax = "1000", ClampMin = "-1"))
	int32 RandomSeed;

	/** Chance to shatter each mesh.  Useful when shattering multiple selected meshes.  */
	UPROPERTY(EditAnywhere, Category = CommonFracture, meta = (DisplayName = "Chance To Fracture Per Mesh", UIMin = "0.0", UIMax = "1.0", ClampMin = "0.0", ClampMax = "1.0"))
	float ChanceToFracture;

	/** Generate a fracture pattern across all selected meshes.  */
	UPROPERTY(EditAnywhere, Category = CommonFracture, meta = (DisplayName = "Group Fracture"))
	bool bGroupFracture;

	/** Generate a fracture pattern across all selected meshes.  */
	UPROPERTY(EditAnywhere, Category = CommonFracture, meta = (DisplayName = "Draw Sites"))
	bool bDrawSites;

	/** Generate a fracture pattern across all selected meshes.  */
	UPROPERTY(EditAnywhere, Category = CommonFracture, meta = (DisplayName = "Draw Diagram"))
	bool bDrawDiagram;

	/** Size of the noise displacement in centimeters */
	UPROPERTY(EditAnywhere, Category = Noise, meta = (UIMin = "0.0"))
	float Amplitude;

	/** Period of the Perlin noise.  Smaller values will create noise faces that are smoother */
	UPROPERTY(EditAnywhere, Category = Noise)
	float Frequency;

	/** Number of fractal layers of Perlin noise to apply.  Smaller values (1 or 2) will create noise that looks like gentle rolling hills, while larger values (> 4) will tend to look more like craggy mountains */
	UPROPERTY(EditAnywhere, Category = Noise, meta = (UIMin = "1"))
	int32 OctaveNumber;

	/** Spacing between vertices on cut surfaces, where noise is added.  Larger spacing between vertices will create more efficient meshes with fewer triangles, but less resolution to see the shape of the added noise  */
	UPROPERTY(EditAnywhere, Category = Noise, meta = (UIMin = "1"))
	int32 SurfaceResolution;

	UPROPERTY()
	UFractureTool *OwnerTool;
};

UCLASS(Abstract, Blueprintable, BlueprintType)
class UFractureTool : public UObject
{
public:
	GENERATED_BODY()

	UFractureTool(const FObjectInitializer& ObjInit) : Super(ObjInit) {}

	virtual FText GetDisplayText() const  { return FText(); }
	virtual FText GetTooltipText() const { return FText(); }

	// TODO: What's the correct thing to return here?
	virtual FSlateIcon GetToolIcon() const { return FSlateIcon(); }

	virtual TArray<UObject*> GetSettingsObjects() const { return TArray<UObject*>(); }

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) {}

	/** Draw callback from edmode*/
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) {}

	/** Executes the fracture command.  Derived types need to be implemented in a thread safe way*/
	virtual void FractureContextChanged() {}
	virtual void ExecuteFracture(const FFractureContext& FractureContext) {}
	virtual bool CanExecuteFracture() const { return true; }


	// Fracture Settings
	UPROPERTY(EditAnywhere, Category = Slicing)
	UFractureCommonSettings* CommonSettings;


	/** Gets the UI command info for this command */
	const TSharedPtr<FUICommandInfo>& GetUICommandInfo() const;

	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) {}

	// virtual FUIAction MakeUIAction( class IMeshEditorModeUIContract& MeshEditorMode ) PURE_VIRTUAL(,return FUIAction(););

protected:

	TSharedPtr<FUICommandInfo> UICommandInfo;

};
