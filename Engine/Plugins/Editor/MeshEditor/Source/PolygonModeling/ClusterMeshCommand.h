// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "MeshEditorCommands.h"
#include "EditableMesh.h"
#include "GeometryCollectionCommandCommon.h"
#include "ClusterMeshCommand.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogClusterCommand, Log, All);

/** Performs merging of the currently selected meshes */
UCLASS()
class UClusterMeshCommand : public UMeshEditorInstantCommand, public FGeometryCollectionCommandCommon
{
public:
	GENERATED_BODY()

protected:

	// Overrides
	virtual EEditableMeshElementType GetElementType() const override
	{
		return EEditableMeshElementType::Fracture;
	}
	virtual void RegisterUICommand(class FBindingContext* BindingContext) override;
	virtual void Execute(class IMeshEditorModeEditingContract& MeshEditorMode) override;

private:
	void ClusterMeshes(IMeshEditorModeEditingContract& MeshEditorMode, TArray<AActor*>& SelectedActors);
	void ClusterMultipleMeshes(IMeshEditorModeEditingContract& MeshEditorMode, TArray<AActor*>& SelectedActors);
	void ClusterChildBonesOfASingleMesh(IMeshEditorModeEditingContract& MeshEditorMode, TArray<AActor*>& SelectedActors);
	void ClusterSelectedBones(UEditableMesh* EditableMesh, UGeometryCollectionComponent* GeometryCollectionComponent);
};
