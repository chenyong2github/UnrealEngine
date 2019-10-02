// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshBrushTool.h"
#include "SelectionSet.h"
#include "Changes/MeshSelectionChange.h"
#include "Changes/ValueWatcher.h"
#include "DynamicMeshOctree3.h"
#include "MeshSelectionTool.generated.h"

/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshSelectionToolBuilder : public UMeshSurfacePointToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI = nullptr;

	virtual UMeshSurfacePointTool* CreateNewTool(const FToolBuilderState& SceneState) const override;
};




UENUM()
enum class EMeshSelectionToolActions
{
	NoAction,

	ClearSelection,
	InvertSelection,
	GrowSelection,
	ShrinkSelection,
	ExpandToConnected,

	DeleteSelected,
	SeparateSelected
};



UCLASS()
class MESHMODELINGTOOLS_API UMeshSelectionToolActionPropertySet : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UMeshSelectionTool> ParentTool;

	void Initialize(UMeshSelectionTool* ParentToolIn) { ParentTool = ParentToolIn; }

	void PostAction(EMeshSelectionToolActions Action);
};




UCLASS()
class MESHMODELINGTOOLS_API UMeshSelectionEditActions : public UMeshSelectionToolActionPropertySet
{
	GENERATED_BODY()

public:
	UFUNCTION(CallInEditor, Category = Selection, meta = (DisplayName = "Clear", DisplayPriority = 1))
	void Clear()
	{
		PostAction(EMeshSelectionToolActions::ClearSelection);
	}

	UFUNCTION(CallInEditor, Category = Selection, meta = (DisplayName = "Invert", DisplayPriority = 2))
	void Invert()
	{
		PostAction(EMeshSelectionToolActions::InvertSelection);
	}


	UFUNCTION(CallInEditor, Category = Selection, meta = (DisplayName = "Grow", DisplayPriority = 3))
	void Grow()
	{
		PostAction(EMeshSelectionToolActions::GrowSelection);
	}

	UFUNCTION(CallInEditor, Category = Selection, meta = (DisplayName = "Shrink", DisplayPriority = 4))
	void Shrink()
	{
		PostAction(EMeshSelectionToolActions::ShrinkSelection);
	}

	UFUNCTION(CallInEditor, Category = Selection, meta = (DisplayName = "ExpandToConnected", DisplayPriority = 5))
	void ExpandToConnected()
	{
		PostAction(EMeshSelectionToolActions::ExpandToConnected);
	}

};




UCLASS()
class MESHMODELINGTOOLS_API UMeshSelectionMeshEditActions : public UMeshSelectionToolActionPropertySet
{
	GENERATED_BODY()

public:
	UFUNCTION(CallInEditor, Category = MeshEdits, meta = (DisplayName = "Delete"))
	void DeleteTriangles()
	{
		PostAction(EMeshSelectionToolActions::DeleteSelected);
	}

	UFUNCTION(CallInEditor, Category = MeshEdits, meta = (DisplayName = "Separate"))
	void SeparateTriangles() 
	{
		PostAction(EMeshSelectionToolActions::SeparateSelected);
	}
};






UENUM()
enum class EMeshSelectionToolPrimaryMode
{
	/** Select all triangles inside the brush */
	Brush,

	/** Select all triangles inside brush with normal within angular tolerance of hit triangle */
	AngleFiltered,

	/** Select all triangles inside brush that are visible from current view */
	Visible,

	/** Select all triangles connected to any triangle inside the brush */
	AllConnected,

	/** Select all triangles in groups connected to any triangle inside the brush */
	AllInGroup,

	/** Select all triangles with normal within angular tolerance of hit triangle */
	AllWithinAngle
};



UCLASS()
class MESHMODELINGTOOLS_API UMeshSelectionToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** The Selection Mode defines the behavior of the selection brush */
	UPROPERTY(EditAnywhere, Category = Selection)
	EMeshSelectionToolPrimaryMode SelectionMode = EMeshSelectionToolPrimaryMode::Brush;

	/** Angle in Degrees used for Angle-based Selection Modes */
	UPROPERTY(EditAnywhere, Category = Selection, meta = (UIMin = "0.0", UIMax = "90.0") )
	float AngleTolerance = 1.0;

	/** A Volumetric brush selects anything within a 3D sphere, rather than only elements connected to the Brush Position  */
	UPROPERTY(EditAnywhere, Category = Selection)
	bool bVolumetricBrush = false;

	/** Allow the brush to hit back-facing parts of the surface  */
	UPROPERTY(EditAnywhere, Category = Selection)
	bool bHitBackFaces = true;

	/** Toggle drawing of wireframe overlay on/off [Alt+W] */
	UPROPERTY(EditAnywhere, Category = Selection)
	bool bShowWireframe = true;

	virtual void SaveProperties(UInteractiveTool* SaveFromTool) override;
	virtual void RestoreProperties(UInteractiveTool* RestoreToTool) override;
};



/**
 *
 */
UCLASS()
class MESHMODELINGTOOLS_API UMeshSelectionTool : public UDynamicMeshBrushTool
{
	GENERATED_BODY()

public:
	UMeshSelectionTool();

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void RegisterActions(FInteractiveToolActionSet& ActionSet) override;

	virtual void Setup() override;

	virtual void Tick(float DeltaTime) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	virtual bool HasCancel() const override { return true; }
	virtual bool HasAccept() const override { return true; }
	virtual bool CanAccept() const override { return bHaveModifiedMesh; }

	// UBaseBrushTool overrides
	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;

public:

	virtual void RequestAction(EMeshSelectionToolActions ActionType);

	UPROPERTY()
	UMeshSelectionToolProperties* SelectionProps;

	UPROPERTY()
	UMeshSelectionEditActions* SelectionActions;

	UPROPERTY()
	UMeshSelectionMeshEditActions* EditActions;

protected:

	virtual void ApplyStamp(const FBrushStampData& Stamp);

	virtual void OnShutdown(EToolShutdownType ShutdownType) override;

protected:
	UPROPERTY()
	UMeshSelectionSet* Selection;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	// note: ideally this octree would be part of PreviewMesh!
	TUniquePtr<FDynamicMeshOctree3> Octree;
	bool bOctreeValid = false;
	TUniquePtr<FDynamicMeshOctree3>& GetOctree();

	EMeshSelectionElementType SelectionType = EMeshSelectionElementType::Face;

	TValueWatcher<bool> ShowWireframeWatcher;


	bool bInRemoveStroke = false;
	FBrushStampData StartStamp;
	FBrushStampData LastStamp;
	bool bStampPending;

	void UpdateFaceSelection(const FBrushStampData& Stamp, const TArray<int>& BrushROI);


	// temp
	TArray<int> IndexBuf;
	TArray<int32> TemporaryBuffer;
	TSet<int32> TemporarySet;
	void CalculateVertexROI(const FBrushStampData& Stamp, TArray<int>& VertexROI);
	void CalculateTriangleROI(const FBrushStampData& Stamp, TArray<int>& TriangleROI);
	TArray<int> PreviewBrushROI;
	TBitArray<> SelectedVertices;
	TBitArray<> SelectedTriangles;
	void OnExternalSelectionChange();

	void OnSelectionUpdated();
	void UpdateVisualization();

	// selection change
	FMeshSelectionChangeBuilder* ActiveSelectionChange = nullptr;
	void BeginChange(bool bAdding);
	TUniquePtr<FMeshSelectionChange> EndChange();
	void CancelChange();


	// actions

	bool bHavePendingAction = false;
	EMeshSelectionToolActions PendingAction;
	virtual void ApplyAction(EMeshSelectionToolActions ActionType);

	void ClearSelection();
	void InvertSelection();
	void GrowShrinkSelection(bool bGrow);
	void ExpandToConnected();

	void DeleteSelectedTriangles();
	void SeparateSelectedTriangles();

	bool bHaveModifiedMesh = false;
};



