// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "EditableMesh.h"
#include "EngineDefines.h"
#include "GeometryCollectionCommandCommon.h"
#include "MeshEditorCommands.h"

#include "SelectToggleGeometryCollectionCommand.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSelectToggleGeometryCommand, Log, All);

/** Toggle Selection of chunks in mesh */
UCLASS()
class USelectToggleGeometryCollectionCommand : public UMeshEditorInstantCommand, public FGeometryCollectionCommandCommon
{
public:
	GENERATED_BODY()

protected:

	// Overrides
	virtual EEditableMeshElementType GetElementType() const override
	{
		return EEditableMeshElementType::Fracture;
	}
	virtual FUIAction MakeUIAction(class IMeshEditorModeUIContract& MeshEditorMode) override;
	virtual void RegisterUICommand(class FBindingContext* BindingContext) override;
	virtual void Execute(class IMeshEditorModeEditingContract& MeshEditorMode) override;

private:
};
