// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Toolkits/BaseToolkit.h"
#include "Widgets/Input/SCheckBox.h"
#include "GeometryCollection/GeometryCollectionActor.h"
#include "AutoClusterFracture.h"

class IDetailsView;
class SScrollBox;
class UFractureTool;
class UEditableMesh;
struct FFractureContext;
class FGeometryCollection;
class SGeometryCollectionOutliner;

class FFractureEditorModeToolkit : public FModeToolkit, public FGCObject
{
public:

	FFractureEditorModeToolkit();
	
	/** FModeToolkit interface */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<class SWidget> GetInlineContent() const override { return ToolkitWidget; }

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	void SetActiveTool(UFractureTool* InActiveTool);
	UFractureTool* GetActiveTool() const;
	bool IsActiveTool(UFractureTool* InActiveTool);

	void SetOutlinerComponents(const TArray<UGeometryCollectionComponent*>& InNewComponents);
	void SetBoneSelection(UGeometryCollectionComponent* InRootComponent, const TArray<int32>& InSelectedBones, bool bClearCurrentSelection);

	// Selection Command Callbacks;
	void OnSelectByMode(GeometryCollection::ESelectionMode);

	// View Settings
	float GetExplodedViewValue() const;
	int32 GetLevelViewValue() const;
	bool GetShowBoneColors() const;
	void OnSetExplodedViewValue(float NewValue);
	void OnSetLevelViewValue(int32 NewValue);
	void OnSetShowBoneColors();

	TSharedRef<SWidget> GetLevelViewMenuContent();
	TSharedRef<SWidget> GetViewMenuContent();


	// Cluster Command Callbacks
	void OnAutoCluster();
	void OnCluster();
	void OnUncluster();
	void OnFlatten();
	void OnFlattenToLevel(){}
	void OnMerge(){}
	void OnMoveUp();
	void GenerateAsset();

	void ViewUpOneLevel();
	void ViewDownOneLevel();

	// Fracture Command Callback
	FReply OnFractureClicked();
	bool CanExecuteFracture() const;
	bool IsLeafBoneSelected() const;

	static ULevel* GetSelectedLevel();
 	static AActor* AddActor(ULevel* InLevel, UClass* Class);
	class AGeometryCollectionActor* CreateNewGeometryActor(const FString& Name, const FTransform& Transform, bool AddMaterials /*= false*/);

 	void OpenGenerateAssetDialog(FFractureContext InFractureContext);
 	void OnGenerateAssetPathChoosen(const FString& InAssetPath, FFractureContext InFractureContext);

	static void GetFractureContexts(TArray<FFractureContext>& FractureContexts);
	void ExecuteFracture(FFractureContext& FractureContext);

	static void GetSelectedGeometryCollectionComponents(TSet<UGeometryCollectionComponent*>& GeomCompSelection);

	AGeometryCollectionActor* ConvertStaticMeshToGeometryCollection(const FString& InAssetPath, FFractureContext& FractureContext);

	static void AddSingleRootNodeIfRequired(UGeometryCollection* GeometryCollectionObject);
	static void AddAdditionalAttributesIfRequired(UGeometryCollection* GeometryCollectionObject);
	// static int32 GetLevelCount();
	int32 GetLevelCount();


	TSharedRef<SWidget> GetAutoClusterModesMenu();	
	void SetAutoClusterMode(EFractureAutoClusterMode InAutoClusterMode);
	FText GetAutoClusterModeDisplayName() const;

	void SetAutoClusterSiteCount(uint32 InSiteCount);
	uint32 GetAutoClusterSiteCount() const;

protected:
	static bool IsGeometryCollectionSelected();
	static bool IsStaticMeshSelected();
	void UpdateExplodedVectors(UGeometryCollectionComponent* GeometryCollectionComponent) const;
private:
	void OnOutlinerBoneSelectionChanged(UGeometryCollectionComponent* RootComponent, TArray<int32>& SelectedBones);
	void BindCommands();
private:
	float ExplodeAmount;
	int32 FractureLevel;

	EFractureAutoClusterMode AutoClusterMode;
	uint32 AutoClusterSiteCount;

	UFractureTool* ActiveTool;

	// TSharedPtr<SVerticalBox::Slot> FractureAreaSlot;
	SVerticalBox::FSlot* InnerFractureSlot = nullptr;
	SVerticalBox::FSlot* InnerOutlinerSlot = nullptr;
	
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<SWidget> ToolkitWidget;
	TSharedPtr<SGeometryCollectionOutliner> OutlinerView;

};
