// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureToolCutter.h"
#include "Engine/StaticMeshActor.h"

#include "FractureToolMeshCut.generated.h"

class FFractureToolContext;


UCLASS(config = EditorPerProjectUserSettings)
class UFractureMeshCutSettings : public UFractureToolSettings
{
public:
	GENERATED_BODY()

	UFractureMeshCutSettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit) {}

	/** Static mesh actor to be used as a cutting surface; should be a closed, watertight mesh */
	UPROPERTY(EditAnywhere, Category = MeshCut, meta = (DisplayName = "Cutting Actor"))
	TLazyObjectPtr<AStaticMeshActor> CuttingActor;

	/** Number of meshes to random scatter; if 0, the selected mesh will be used in its current position, with no random scattering*/
	UPROPERTY(EditAnywhere, Category = Distribution)
	int NumberToScatter = 0;

	UPROPERTY(EditAnywhere, Category = Distribution, meta = (ClampMin = "0.001", EditCondition = "NumberToScatter > 0"))
	float MinScaleFactor = .5;

	UPROPERTY(EditAnywhere, Category = Distribution, meta = (ClampMin = "0.001", EditCondition = "NumberToScatter > 0"))
	float MaxScaleFactor = 1.5;

	UPROPERTY(EditAnywhere, Category = Distribution, meta = (EditCondition = "NumberToScatter > 0"))
	bool bRandomOrientation = true;

	/** Roll will be chosen between -Range to +Range */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DisplayName = "+/- Roll Range", EditCondition = "NumberToScatter > 0 && bRandomOrientation", ClampMin = "0", ClampMax = "180"))
	float RollRange = 180;

	/** Pitch will be chosen between -Range to +Range */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DisplayName = "+/- Pitch Range", EditCondition = "NumberToScatter > 0 && bRandomOrientation", ClampMin = "0", ClampMax = "180"))
	float PitchRange = 180;

	/** Yaw will be chosen between -Range to +Range */
	UPROPERTY(EditAnywhere, Category = Distribution, meta = (DisplayName = "+/- Yaw Range", EditCondition = "NumberToScatter > 0 && bRandomOrientation", ClampMin = "0", ClampMax = "180"))
	float YawRange = 180;
};


UCLASS(DisplayName = "Mesh Cut Tool", Category = "FractureTools")
class UFractureToolMeshCut : public UFractureToolCutterBase
{
public:
	GENERATED_BODY()

	UFractureToolMeshCut(const FObjectInitializer& ObjInit);

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FSlateIcon GetToolIcon() const override;

	void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;

	virtual TArray<UObject*> GetSettingsObjects() const override;

	virtual void FractureContextChanged() override;
	virtual int32 ExecuteFracture(const FFractureToolContext& FractureContext) override;

protected:
	virtual void ClearVisualizations() override
	{
		Super::ClearVisualizations();
		RenderMeshTransforms.Empty();
		TransformsMappings.Empty();
	}

private:
	// Slicing
	UPROPERTY(EditAnywhere, Category = Slicing)
	TObjectPtr<UFractureMeshCutSettings> MeshCutSettings;

	// check if the chosen actor can be used to cut the geometry collection (i.e. if it is a valid static mesh actor with a non-empty static mesh)
	bool IsCuttingActorValid();

	void GenerateMeshTransforms(const FFractureToolContext& Context, TArray<FTransform>& MeshTransforms);

	TArray<FTransform> RenderMeshTransforms;
	FVisualizationMappings TransformsMappings;
};


