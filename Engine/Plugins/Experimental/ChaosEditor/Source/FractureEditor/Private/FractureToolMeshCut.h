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

	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;

	virtual TArray<UObject*> GetSettingsObjects() const override;

	virtual int32 ExecuteFracture(const FFractureToolContext& FractureContext) override;

private:
	// Slicing
	UPROPERTY(EditAnywhere, Category = Slicing)
	TObjectPtr<UFractureMeshCutSettings> MeshCutSettings;

	// check if the chosen actor can be used to cut the geometry collection (i.e. if it is a valid static mesh actor with a non-empty static mesh)
	bool IsCuttingActorValid();
};


