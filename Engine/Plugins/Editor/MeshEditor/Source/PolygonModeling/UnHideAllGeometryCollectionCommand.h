// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "EditableMesh.h"
#include "EngineDefines.h"
#include "GeometryCollectionCommandCommon.h"
#include "MeshEditorCommands.h"

#include "UnHideAllGeometryCollectionCommand.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUnHideAllGeometryCommand, Log, All);

/** Select All chunks in mesh */
UCLASS()
class UUnHideAllGeometryCollectionCommand : public UMeshEditorInstantCommand, public FGeometryCollectionCommandCommon
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
