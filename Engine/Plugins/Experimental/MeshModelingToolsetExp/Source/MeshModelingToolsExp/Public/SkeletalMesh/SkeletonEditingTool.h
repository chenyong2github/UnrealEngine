// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMeshBrushTool.h"
#include "BaseTools/MeshSurfacePointMeshEditingTool.h"

#include "SkeletalMeshNotifier.h"
#include "SkeletonModifier.h"
#include "SkeletalMesh/SkeletalMeshEditionInterface.h"
#include "Engine/World.h"

#include "SkeletonEditingTool.generated.h"

class USingleClickInputBehavior;
class UGizmoViewContext;
class UTransformGizmo;
class USkeletonEditingTool;

namespace SkeletonEditingTool
{

/**
 * A wrapper change class that stores a reference skeleton and the bones' indexes trackers to be used for undo/redo.
 */
class FRefSkeletonChange : public FToolCommandChange
{
public:
	FRefSkeletonChange(const USkeletonEditingTool* InTool);

	virtual FString ToString() const override
	{
		return FString(TEXT("Edit Skeleton"));
	}

	virtual void Apply(UObject* Object) override;
	virtual void Revert(UObject* Object) override;

	void StoreSkeleton(const USkeletonEditingTool* InTool);

private:
	FReferenceSkeleton PreChangeSkeleton;
	TArray<int32> PreBoneTracker;
	FReferenceSkeleton PostChangeSkeleton;
	TArray<int32> PostBoneTracker;
};
	
}

/**
 * USkeletonEditingToolBuilder
 */

UCLASS()
class MESHMODELINGTOOLSEXP_API USkeletonEditingToolBuilder : public UInteractiveToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:
	// UInteractiveToolBuilder overrides
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
	
protected:
	// UInteractiveToolWithToolTargetsBuilder overrides
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
};

/**
 * EEditingOperation represents the current tool's operation 
 */

UENUM()
enum class EEditingOperation : uint8
{
	Select,		// Selecting bones in the viewport.
	Create,		// Creating bones in the viewport.
	Remove,		// Removing current selection.
	Transform,	// Transforming bones in the viewport or thru the details panel.
	Parent,		// Parenting bones in the viewport.
	Rename,		// Renaming bones thru the details panel.
	Mirror		// Mirroring bones thru the details panel.
};

/**
 * USkeletonEditingTool is a tool to edit a the ReferenceSkeleton of a SkeletalMesh (target)
 * Changed are actually commit to the SkeletalMesh and it's mesh description on Accept.
 */

UCLASS()
class MESHMODELINGTOOLSEXP_API USkeletonEditingTool :
	public USingleSelectionTool,
	public IClickDragBehaviorTarget,
	public ISkeletalMeshEditionInterface
{
	GENERATED_BODY()

public:

	void Init(const FToolBuilderState& InSceneState);
	
	// UInteractiveTool overrides
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;

	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	
	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return true; }

	// ICLickDragBehaviorTarget overrides
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override;
	
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;

	// IInteractiveToolCameraFocusAPI overrides
	virtual FBox GetWorldSpaceFocusBox() override;

	// Modifier functions
	void MirrorBones();
	void RenameBones();
	void MoveBones();
	void OrientBones();
	
	int32 ParentIndex = INDEX_NONE;
	FName CurrentBone = NAME_None;

	FSkeletonModifier SkeletonModifier;
	
protected:

	// UInteractiveTool overrides
	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	// ISkeletalMeshEditionInterface overrides
	virtual void HandleSkeletalMeshModified(const TArray<FName>& BoneNames, const ESkeletalMeshNotifyType InNotifyType) override;

	// Modifier functions
	void CreateNewBone();
	void RemoveBones();
	void UnParentBones();
	void ParentBones(const FName& InParentName);

	TArray<FName> GetSelectedBones() const;

	UPROPERTY()
	TObjectPtr<USkeletonEditingProperties> Properties;

	UPROPERTY()
	TObjectPtr<UProjectionProperties> ProjectionProperties;
	
	UPROPERTY()
	TObjectPtr<UMirroringProperties> MirroringProperties;

	UPROPERTY()
	TObjectPtr<UOrientingProperties> OrientingProperties;
	
	UPROPERTY()
	TObjectPtr<USingleClickInputBehavior> LeftClickBehavior = nullptr;

	UPROPERTY()
	TObjectPtr<UPreviewMesh> PreviewMesh = nullptr;

	UPROPERTY()
	TWeakObjectPtr<UWorld> TargetWorld = nullptr;

	UPROPERTY()
	TWeakObjectPtr<UGizmoViewContext> ViewContext = nullptr;

	UPROPERTY()
	EEditingOperation Operation = EEditingOperation::Select;

	// ref skeleton transactions
	void BeginChange();
	void EndChange();
	void CancelChange();
	TUniquePtr<SkeletonEditingTool::FRefSkeletonChange> ActiveChange;
};

/**
 * USkeletonEditingProperties
 */

UCLASS()
class MESHMODELINGTOOLSEXP_API USkeletonEditingProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	void Initialize(USkeletonEditingTool* ParentToolIn);

	UPROPERTY(EditAnywhere, Category = "Details")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "Details")
	FTransform Transform;
	
	UPROPERTY(EditAnywhere, Category = "Move")
	bool bUpdateChildren = false;

	UPROPERTY(EditAnywhere, Category = "Create")
	FName DefaultName = "joint";

	UPROPERTY(EditAnywhere, Category = "Viewport Axis Settings",  meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AxisLength = 1.f;

	UPROPERTY(EditAnywhere, Category = "Viewport Axis Settings",  meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AxisThickness = 0.f;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;
#endif
	
private:
	TWeakObjectPtr<USkeletonEditingTool> ParentTool = nullptr;
};

/**
 * EProjectionType
 */

UENUM()
enum class EProjectionType : uint8
{
	CameraPlane,	// The camera plane is used as the projection plane 
	OnMesh,			// The mesh surface is used for projection
	WithinMesh,		// The mesh volume is used for projection
};

/**
 * UProjectionProperties
 */

UCLASS()
class MESHMODELINGTOOLSEXP_API UProjectionProperties: public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	void Initialize(TObjectPtr<UPreviewMesh> PreviewMesh);

	void UpdatePlane(const UGizmoViewContext& InViewContext, const FVector& InOrigin);
	bool GetProjectionPoint(const FInputDeviceRay& InRay, FVector& OutHitPoint) const;
	
	UPROPERTY(EditAnywhere, Category = "Project")
	EProjectionType ProjectionType = EProjectionType::WithinMesh;
	
private:
	TWeakObjectPtr<UPreviewMesh> PreviewMesh = nullptr;
	
	UPROPERTY()
	FVector PlaneOrigin = FVector::ZeroVector;
	
	UPROPERTY()
	FVector PlaneNormal =  FVector::ZAxisVector;
};

/**
 * UMirroringProperties
 */

UCLASS()
class MESHMODELINGTOOLSEXP_API UMirroringProperties: public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	void Initialize(USkeletonEditingTool* ParentToolIn);

	UFUNCTION(CallInEditor, Category = "Mirror")
	void MirrorBones();

	UPROPERTY(EditAnywhere, Category = "Mirror")
	FMirrorOptions Options;

private:
	TWeakObjectPtr<USkeletonEditingTool> ParentTool = nullptr;
};

/**
 * UOrientingProperties
 */

UCLASS()
class MESHMODELINGTOOLSEXP_API UOrientingProperties: public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	void Initialize(USkeletonEditingTool* ParentToolIn);

	UFUNCTION(CallInEditor, Category = "Orient")
	void OrientBones();
	
	UPROPERTY(EditAnywhere, Category = "Orient")
	FOrientOptions Options;

	UPROPERTY(EditAnywhere, Category = "Orient")
	bool bAutoOrient = false;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent) override;
#endif
	
private:
	TWeakObjectPtr<USkeletonEditingTool> ParentTool = nullptr;
};