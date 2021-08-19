// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InteractiveToolBuilder.h"
#include "MultiSelectionTool.h"

#include "AddPivotActorTool.generated.h"

class UDragAlignmentMechanic;
class UTransformGizmo;
class UTransformProxy;

UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAddPivotActorToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()
public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

protected:
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};

/** 
 * Given selected actors, creates an empty actor as the parent of those actors, at a location
 * specified using the gizmo. This is useful for creating a permanent alternate pivot to use in
 * animation.
 */
UCLASS()
class MESHMODELINGTOOLSEDITORONLY_API UAddPivotActorTool : public UMultiSelectionTool
{
	GENERATED_BODY()
public:

	virtual void SetWorld(UWorld* World) { TargetWorld = World; }

	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasAccept() const override { return true; }
	virtual bool HasCancel() const override { return true; }
	// Uses the base class CanAccept

protected:

	UPROPERTY()
	TObjectPtr<UDragAlignmentMechanic> DragAlignmentMechanic = nullptr;

	UPROPERTY()
	TObjectPtr<UTransformGizmo> TransformGizmo = nullptr;

	UPROPERTY()
	TObjectPtr<UTransformProxy> TransformProxy = nullptr;

	TObjectPtr<UWorld> TargetWorld;
};
