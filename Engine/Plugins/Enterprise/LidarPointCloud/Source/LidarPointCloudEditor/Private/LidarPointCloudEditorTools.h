// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "InteractiveToolManager.h"
#include "InteractiveToolQueryInterfaces.h"
#include "LidarPointCloudEditorHelper.h"
#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "LidarPointCloudEditorTools.generated.h"

class UMouseHoverBehavior;
class URectangleMarqueeMechanic;
class UClickDragInputBehavior;

UCLASS()
class ULidarEditorTool_Base : public UInteractiveTool
{
public:
	GENERATED_BODY()
	virtual void Setup() override;
	virtual TObjectPtr<UInteractiveToolPropertySet> CreateToolActions() { return nullptr; }
	virtual FText GetToolMessage() const;

	UPROPERTY()
	TObjectPtr<UInteractiveToolPropertySet> ToolActions = nullptr;
};

UCLASS()
class ULidarEditorToolBuilder_Base : public UInteractiveToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override { return true; }
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override { return NewObject<ULidarEditorTool_Base>(SceneState.ToolManager); }
};

UCLASS()
class ULidarEditorTool_ClickDragBase :
	public ULidarEditorTool_Base,
	public IClickDragBehaviorTarget,
	public IHoverBehaviorTarget,
	public IInteractiveToolNestedAcceptCancelAPI
{
public:
	GENERATED_BODY()
	
	virtual void Setup() override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	// IClickDragBehaviorTarget implementation
	virtual FInputRayHit CanBeginClickDragSequence(const FInputDeviceRay& PressPos) override { return FInputRayHit(TNumericLimits<float>::Max()); }
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override {}
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override {}
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override {}
	virtual void OnTerminateDragSequence() override {}

	// IHoverBehaviorTarget implementation
	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override { return FInputRayHit(TNumericLimits<float>::Max()); }
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override {}
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override { return true; }
	virtual void OnEndHover() override {}
	virtual void OnUpdateModifierState(int ModifierID, bool bIsOn) override;

	// IInteractiveToolNestedAcceptCancelAPI implementation
	virtual bool SupportsNestedCancelCommand() override { return true; }
	virtual bool CanCurrentlyNestedCancel() override { return true; }
	virtual bool ExecuteNestedCancelCommand() override { return false; }

	UPROPERTY()
	TObjectPtr<UClickDragInputBehavior> ClickDragBehavior = nullptr;

	UPROPERTY()
	TObjectPtr<UMouseHoverBehavior> HoverBehavior = nullptr;

protected:
	FViewCameraState CameraState;
	
	bool bShiftToggle = false;
	bool bCtrlToggle = false;
};

// -------------------------------------------------------

UCLASS()
class ULidarEditorToolBuilder_Select : public ULidarEditorToolBuilder_Base
{
	GENERATED_BODY()
};

UCLASS()
class ULidarToolActions_Align : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UFUNCTION(CallInEditor, Category = Actions)
	void AlignAroundWorldOrigin();

	UFUNCTION(CallInEditor, Category = Actions)
	void AlignAroundOriginalCoordinates();

	UFUNCTION(CallInEditor, Category = Actions)
	void ResetAlignment();
};

UCLASS()
class ULidarEditorTool_Align : public ULidarEditorTool_Base
{
public:
	GENERATED_BODY()
	virtual TObjectPtr<UInteractiveToolPropertySet> CreateToolActions() override { return NewObject<ULidarToolActions_Align>(this); }
};

UCLASS()
class ULidarEditorToolBuilder_Align : public ULidarEditorToolBuilder_Base
{
public:
	GENERATED_BODY()
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override { return NewObject<ULidarEditorTool_Align>(SceneState.ToolManager); }
};

UCLASS()
class ULidarToolActions_Merge : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Options)
	bool bReplaceSourceActorsAfterMerging = false;
	
	UFUNCTION(CallInEditor, Category = Actions)
	void MergeActors();

	UFUNCTION(CallInEditor, Category = Actions)
	void MergeData();
};

UCLASS()
class ULidarEditorTool_Merge : public ULidarEditorTool_Base
{
public:
	GENERATED_BODY()
	virtual TObjectPtr<UInteractiveToolPropertySet> CreateToolActions() override { return NewObject<ULidarToolActions_Merge>(this); }
};

UCLASS()
class ULidarEditorToolBuilder_Merge : public ULidarEditorToolBuilder_Base
{
public:
	GENERATED_BODY()
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override { return NewObject<ULidarEditorTool_Merge>(SceneState.ToolManager); }
};

UCLASS()
class ULidarToolActions_Collision : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "2000", ClampMin = "0"))
	float OverrideMaxCollisionError = 0;
	
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayName = "(Re-)Build Collision"))
	void BuildCollision();

	UFUNCTION(CallInEditor, Category = Actions)
	void RemoveCollision();
};

UCLASS()
class ULidarEditorTool_Collision : public ULidarEditorTool_Base
{
public:
	GENERATED_BODY()
	virtual TObjectPtr<UInteractiveToolPropertySet> CreateToolActions() override { return NewObject<ULidarToolActions_Collision>(this); }
};

UCLASS()
class ULidarEditorToolBuilder_Collision : public ULidarEditorToolBuilder_Base
{
public:
	GENERATED_BODY()
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override { return NewObject<ULidarEditorTool_Collision>(SceneState.ToolManager); }
};

UCLASS()
class ULidarToolActions_Meshing : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Max error around the meshed areas. Leave at 0 for max quality */
	UPROPERTY(EditAnywhere, Category = Options, meta = (UIMin = "0", UIMax = "2000", ClampMin = "0"))
	float MaxMeshingError = 0;
	
	UPROPERTY(EditAnywhere, Category = Options)
	bool bMergeMeshes = true;

	/** If not merging meshes, this will retain the transform of the original cloud */
	UPROPERTY(EditAnywhere, Category = Options, meta=(EditCondition="!bMergeMeshes"))
	bool bRetainTransform = true;
	
	UFUNCTION(CallInEditor, Category = Actions)
	void BuildStaticMesh();
};

UCLASS()
class ULidarEditorTool_Meshing : public ULidarEditorTool_Base
{
public:
	GENERATED_BODY()
	virtual TObjectPtr<UInteractiveToolPropertySet> CreateToolActions() override { return NewObject<ULidarToolActions_Meshing>(this); }
};

UCLASS()
class ULidarEditorToolBuilder_Meshing : public ULidarEditorToolBuilder_Base
{
public:
	GENERATED_BODY()
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override { return NewObject<ULidarEditorTool_Meshing>(SceneState.ToolManager); }
};

UCLASS()
class ULidarToolActions_Normals : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	/** Higher values will generally result in more accurate calculations, at the expense of time */
	UPROPERTY(EditAnywhere, Category = "Options", meta = (ClampMin = "1", ClampMax = "100"))
	int32 Quality = 40;

	/**
	 * Higher values are less susceptible to noise, but will most likely lose finer details, especially around hard edges.
	 * Lower values retain more detail, at the expense of time.
	 * NOTE: setting this too low will cause visual artifacts and geometry holes in noisier datasets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Options", meta = (ClampMin = "0.0"))
	float NoiseTolerance = 1;
	
	UFUNCTION(CallInEditor, Category = Actions, meta = (DisplayName = "(Re-)Build Normals"))
	void CalculateNormals();
};

UCLASS()
class ULidarEditorTool_Normals : public ULidarEditorTool_Base
{
public:
	GENERATED_BODY()
	virtual TObjectPtr<UInteractiveToolPropertySet> CreateToolActions() override { return NewObject<ULidarToolActions_Normals>(this); }
};

UCLASS()
class ULidarEditorToolBuilder_Normals : public ULidarEditorToolBuilder_Base
{
public:
	GENERATED_BODY()
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override { return NewObject<ULidarEditorTool_Normals>(SceneState.ToolManager); }
};

UCLASS()
class ULidarToolActions_Selection : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UFUNCTION(CallInEditor, Category = Visibility)
	void HideSelected();

	UFUNCTION(CallInEditor, Category = Visibility)
	void ResetVisibility();

	UFUNCTION(CallInEditor, Category = Selection)
	void InvertSelection();

	UFUNCTION(CallInEditor, Category = Selection)
	void ClearSelection();

	UFUNCTION(CallInEditor, Category = Deletion)
	void DeleteSelected();
	
	UFUNCTION(CallInEditor, Category = Deletion)
	void DeleteHidden();

	UFUNCTION(CallInEditor, Category = Extraction)
	void Extract();

	UFUNCTION(CallInEditor, Category = Extraction)
	void ExtractAsCopy();

	UFUNCTION(CallInEditor, Category = Normals)
	void CalculateNormals();

	UFUNCTION(CallInEditor, Category = Meshing)
	void BuildStaticMesh();
	
	/** Max error around the meshed areas. Leave at 0 for max quality */
	UPROPERTY(EditAnywhere, Category = Meshing, meta = (UIMin = "0", UIMax = "2000", ClampMin = "0"))
	float MaxMeshingError = 0;

	UPROPERTY(EditAnywhere, Category = Meshing)
	bool bMergeMeshes = true;

	/** If not merging meshes, this will retain the transform of the original cloud */
	UPROPERTY(EditAnywhere, Category = Meshing, meta=(EditCondition="!bMergeMeshes"))
	bool bRetainTransform = true;
};

UCLASS()
class ULidarEditorTool_SelectionBase : public ULidarEditorTool_ClickDragBase
{
public:
	GENERATED_BODY()
	virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI) override;
	virtual TArray<FConvexVolume> GetSelectionConvexVolumes();
	virtual TObjectPtr<UInteractiveToolPropertySet> CreateToolActions() override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
	virtual void OnTerminateDragSequence() override;
	virtual bool ExecuteNestedCancelCommand() override;
	virtual FText GetToolMessage() const override;
	virtual FLinearColor GetHUDColor();
	virtual void FinalizeSelection();

	virtual void PostCurrentMousePosChanged() {}
	virtual ELidarPointCloudSelectionMode GetSelectionMode() const;

protected:
	FVector2d CurrentMousePos;
	TArray<FVector2d> Clicks;
	bool bSelecting;
};

UCLASS()
class ULidarEditorTool_BoxSelection : public ULidarEditorTool_SelectionBase
{
public:
	GENERATED_BODY()
	virtual TArray<FConvexVolume> GetSelectionConvexVolumes() override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
};

UCLASS()
class ULidarEditorToolBuilder_BoxSelection : public ULidarEditorToolBuilder_Base
{
public:
	GENERATED_BODY()
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override { return NewObject<ULidarEditorTool_BoxSelection>(SceneState.ToolManager); }
};

UCLASS()
class ULidarEditorTool_PolygonalSelection : public ULidarEditorTool_SelectionBase
{
public:
	GENERATED_BODY()
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual FLinearColor GetHUDColor() override;
	virtual void PostCurrentMousePosChanged() override;

private:
	bool IsWithinSnap();
};

UCLASS()
class ULidarEditorToolBuilder_PolygonalSelection : public ULidarEditorToolBuilder_Base
{
public:
	GENERATED_BODY()
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override { return NewObject<ULidarEditorTool_PolygonalSelection>(SceneState.ToolManager); }
};

UCLASS()
class ULidarEditorTool_LassoSelection : public ULidarEditorTool_SelectionBase
{
public:
	GENERATED_BODY()
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual void OnClickRelease(const FInputDeviceRay& ReleasePos) override;
};

UCLASS()
class ULidarEditorToolBuilder_LassoSelection : public ULidarEditorToolBuilder_Base
{
public:
	GENERATED_BODY()
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override { return NewObject<ULidarEditorTool_LassoSelection>(SceneState.ToolManager); }
};

UCLASS()
class ULidarToolActions_PaintSelection : public ULidarToolActions_Selection
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Brush, meta = (UIMin = "0", UIMax = "8196", ClampMin = "0", DisplayPriority = 1))
	float BrushRadius = 250;
};

UCLASS()
class ULidarEditorTool_PaintSelection : public ULidarEditorTool_SelectionBase
{
public:
	GENERATED_BODY()
	virtual void Setup() override;
	virtual void DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI) override {}
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void PostCurrentMousePosChanged() override;
	virtual void OnClickPress(const FInputDeviceRay& PressPos) override;
	virtual void OnClickDrag(const FInputDeviceRay& DragPos) override;
	virtual TObjectPtr<UInteractiveToolPropertySet> CreateToolActions() override { return NewObject<ULidarToolActions_PaintSelection>(this); }
	virtual void OnPropertyModified(UObject* PropertySet, FProperty* Property) override;

private:
	void Paint();

private:
	FVector3f HitLocation;
	float LastHitDistance;
	bool bHasHit;
	float BrushRadius;
};

UCLASS()
class ULidarEditorToolBuilder_PaintSelection : public ULidarEditorToolBuilder_Base
{
public:
	GENERATED_BODY()
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override { return NewObject<ULidarEditorTool_PaintSelection>(SceneState.ToolManager); }
};