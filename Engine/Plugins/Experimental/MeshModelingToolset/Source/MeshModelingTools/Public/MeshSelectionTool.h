// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshBrushTool.h"
#include "SelectionSet.h"
#include "MeshSelectionChange.h"
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
	virtual void OnBeginDrag(const FRay& Ray) override;
	virtual void OnUpdateDrag(const FRay& Ray) override;
	virtual void OnEndDrag(const FRay& Ray) override;
	virtual void OnUpdateHover(const FInputDeviceRay& DevicePos) override;

public:


	UFUNCTION(CallInEditor, Category = Options, meta = (DisplayName = "Delete"))
	void DeleteSelectedTriangles();

	UFUNCTION(CallInEditor, Category = Options, meta = (DisplayName = "Separate"))
	void SeparateSelectedTriangles();


protected:

	virtual void ApplyStamp(const FBrushStampData& Stamp);


	virtual void OnShutdown(EToolShutdownType ShutdownType) override;

protected:
	UPROPERTY()
	UMeshSelectionSet* Selection;

	UWorld* TargetWorld;
	IToolsContextAssetAPI* AssetAPI;

	EMeshSelectionElementType SelectionType = EMeshSelectionElementType::Face;

	bool bInRemoveStroke = false;

	FBrushStampData StartStamp;
	FBrushStampData LastStamp;

	bool bStampPending;


	// temp
	TArray<int> IndexBuf;
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


	bool bHaveModifiedMesh = false;
};



