// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Toolkits/BaseToolkit.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "GeometryCollection/GeometryCollectionActor.h"

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

	static bool IsLeafBoneSelected();

	static ULevel* GetSelectedLevel();
 	static AActor* AddActor(ULevel* InLevel, UClass* Class);
	class AGeometryCollectionActor* CreateNewGeometryActor(const FString& Name, const FTransform& Transform, bool AddMaterials /*= false*/);

 	void OpenGenerateAssetDialog(TArray<AActor*>& Actors);
 	void OnGenerateAssetPathChosen(const FString& InAssetPath, TArray<AActor*> Actors);

	static void GetFractureContexts(TArray<FFractureContext>& FractureContexts);
	void ExecuteFracture(FFractureContext& FractureContext);

	static void GetSelectedGeometryCollectionComponents(TSet<UGeometryCollectionComponent*>& GeomCompSelection);

	AGeometryCollectionActor* ConvertStaticMeshToGeometryCollection(const FString& InAssetPath, TArray<AActor*>& Actors);

	static void AddSingleRootNodeIfRequired(UGeometryCollection* GeometryCollectionObject);
	static void AddAdditionalAttributesIfRequired(UGeometryCollection* GeometryCollectionObject);
	// static int32 GetLevelCount();
	int32 GetLevelCount();

	FText GetStatisticsSummary() const;

	/** Returns the number of Mode specific tabs in the mode toolbar **/ 
	const static TArray<FName> PaletteNames;
	virtual void GetToolPaletteNames( TArray<FName>& InPaletteName ) const { InPaletteName = PaletteNames; }
	virtual FText GetToolPaletteDisplayName(FName PaletteName); 
	virtual void BuildToolPalette(FName PaletteName, class FToolBarBuilder& ToolbarBuilder);
	virtual void OnToolPaletteChanged(FName PaletteName) override;

	TSharedPtr<SWidget> ExplodedViewWidget;
	TSharedPtr<SWidget> LevelViewWidget;
	TSharedPtr<SWidget> ShowBoneColorsWidget;

protected:
	static bool IsGeometryCollectionSelected();
	static bool IsStaticMeshSelected();
	static bool IsSelectedActorsInEditorWorld();
	void UpdateExplodedVectors(UGeometryCollectionComponent* GeometryCollectionComponent) const;
private:
	void OnOutlinerBoneSelectionChanged(UGeometryCollectionComponent* RootComponent, TArray<int32>& SelectedBones);
	void BindCommands();
private:
	float ExplodeAmount;
	int32 FractureLevel;

	UFractureTool* ActiveTool;

	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<SWidget> ToolkitWidget;
	TSharedPtr<SGeometryCollectionOutliner> OutlinerView;

};
