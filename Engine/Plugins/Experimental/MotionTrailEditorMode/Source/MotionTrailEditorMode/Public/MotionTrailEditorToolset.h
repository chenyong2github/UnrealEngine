// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "MultiSelectionTool.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "Framework/Commands/UICommandInfo.h"

#include "MotionTrailEditorToolset.generated.h"

class UMotionTrailEditorMode;
class UTrailToolEditor;

namespace UE
{
namespace MotionTrailEditor
{

class FInteractiveTrailTool
{
public:

	virtual ~FInteractiveTrailTool() {}

	virtual void Setup() {}
	virtual void Render(IToolsContextRenderAPI* RenderAPI) {}
	virtual void Tick(float DeltaTime) {}


	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) { return FInputRayHit(); }
	virtual void OnClicked(const FInputDeviceRay& ClickPos) {};

	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) { return FInputRayHit(); }
	virtual void OnClickPress(const FInputDeviceRay& PressPos) {}
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) {}
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) {}
	virtual void OnTerminateDragSequence() {}

	virtual TArray<UObject*> GetStaticToolProperties() const { return TArray<UObject*>(); }
	virtual TSharedPtr<FUICommandInfo> GetStaticUICommandInfo() { return nullptr; } // null to mark as default tool

	void SetMotionTrailEditorMode(TWeakObjectPtr<class UMotionTrailEditorMode> InWeakEditorMode) { WeakEditorMode = InWeakEditorMode; }
	bool IsActive() const { return WeakEditorMode.IsValid(); }

protected:
	TWeakObjectPtr<class UMotionTrailEditorMode> WeakEditorMode;
};

} // namespace MovieScene
} // namespace UE

/**
 * Builder for UTrailToolManager
 */
UCLASS()
class UTrailToolManagerBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override { return true; }
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

	void SetTrailToolName(const FString& InTrailToolName) { TrailToolName = InTrailToolName; }
	void SetMotionTrailEditorMode(const TWeakObjectPtr<UMotionTrailEditorMode> InEditorMode) { EditorMode = InEditorMode; }

private:
	FString TrailToolName;
	TWeakObjectPtr<UMotionTrailEditorMode> EditorMode;
};

UCLASS()
class UTrailToolManager : public UMultiSelectionTool, public IClickDragBehaviorTarget, public IClickBehaviorTarget
{
	GENERATED_BODY()

public:

	// IClickBehaviorTarget interface
	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos);
	virtual void OnClicked(const FInputDeviceRay& ClickPos);

	// IClickDragBehaviorTarget interface
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;
	
	// UInteractiveTool overrides
	virtual	void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;

	virtual TArray<UObject*> GetToolProperties(bool bEnabledOnly = true) const override;
	// End interfaces

	void SetTrailToolName(const FString& InTrailToolName) { TrailToolName = InTrailToolName; }
	void SetMotionTrailEditorMode(const TWeakObjectPtr<UMotionTrailEditorMode> InEditorMode) { EditorMode = InEditorMode; }
	void SetWorld(UWorld* InTargetWorld, UInteractiveGizmoManager* InGizmoManager)
	{
		TargetWorld = InTargetWorld;
		GizmoManager = InGizmoManager;
	}

	UWorld* GetWorld() const { return TargetWorld; }
	UInteractiveGizmoManager* GetGizmoManager() const { return GizmoManager; }
	const TArray<TUniquePtr<FPrimitiveComponentTarget>>& GetSelection() const { return ComponentTargets; }


	static FString TrailKeyTransformGizmoInstanceIdentifier;

protected:
	UPROPERTY(Transient)
	TArray<UObject*> ToolProperties;

	UWorld* TargetWorld;
	UInteractiveGizmoManager* GizmoManager;

	FString TrailToolName;
	TWeakObjectPtr<UMotionTrailEditorMode> EditorMode;
};
