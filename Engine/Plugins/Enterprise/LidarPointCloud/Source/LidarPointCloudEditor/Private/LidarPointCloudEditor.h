// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Toolkits/IToolkitHost.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Text/STextBlock.h"
#include "AssetData.h"
#include "ConvexVolume.h"
#include "LidarPointCloudShared.h"

class ULidarPointCloud;
struct FLidarPointCloudPoint;
class SWidget;
class SLidarPointCloudEditorViewport;

class FLidarPointCloudEditor : public FAssetEditorToolkit, public FGCObject
{
private:
	ULidarPointCloud* PointCloudBeingEdited;

	TArray64<FLidarPointCloudPoint*> SelectedPoints;
	
	bool bEditMode;

	/** Preview Viewport widget */
	TSharedPtr<SLidarPointCloudEditorViewport> Viewport;

public:
	FLidarPointCloudEditor();
	~FLidarPointCloudEditor();

	// IToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	// End of IToolkit interface

	// FAssetEditorToolkit
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;

	virtual void OnToolkitHostingStarted(const TSharedRef<class IToolkit>& Toolkit) override {}
	virtual void OnToolkitHostingFinished(const TSharedRef<class IToolkit>& Toolkit) override {}
	// End of FAssetEditorToolkit

	// FSerializableObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	// End of FSerializableObject interface

	void InitPointCloudEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, class ULidarPointCloud* InitPointCloud);

	ULidarPointCloud* GetPointCloudBeingEdited() const { return PointCloudBeingEdited; }

	void SelectPointsByConvexVolume(const FConvexVolume& ConvexVolume, bool bAdditive);
	void DeselectPointsByConvexVolume(const FConvexVolume& ConvexVolume);
	void SelectPointsBySphere(const FSphere& Sphere);
	void DeselectPointsBySphere(const FSphere& Sphere);
	void DeselectPoints();
	void InvertSelection();
	void DeletePoints();
	void DeleteHiddenPoints();
	void HidePoints();
	void UnhideAll();
	TArray64<FLidarPointCloudPoint*>& GetSelectedPoints() { return SelectedPoints; }

	bool HasSelectedPoints() const { return SelectedPoints.Num() > 0; }

	bool IsEditMode() const { return bEditMode; }

	TSharedPtr<SLidarPointCloudEditorViewport> GetViewport() { return Viewport; }

private:
	bool ConfirmCollisionChange();

	TSharedRef<SWidget> BuildPointCloudStatistics();

	/** Builds the Point Cloud Editor toolbar. */
	void ExtendToolBar();

	void BindEditorCommands();

	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);

	/** Called when the owned cloud is rebuilt */
	void OnPointCloudRebuilt();

	void OnPreSaveCleanup();

	void Extract();
	void ExtractCopy();
	bool IsCentered() const;
	void ToggleCenter();
	void ToggleEditMode();
	void Merge();
	void BuildCollision();
	void RemoveCollision();
	void Align();
	void CalculateNormals();
	void CalculateNormalsSelection();
	bool HasCollisionData() const;

	TSharedRef<SWidget> GenerateNormalsMenuContent();
	TSharedRef<SWidget> GenerateExtractionMenuContent();
	TSharedRef<SWidget> GenerateCollisionMenuContent();
	TSharedRef<SWidget> GenerateDeleteMenuContent();
	TSharedRef<SWidget> GenerateSelectionMenuContent();

	TArray<FAssetData> SelectAssets(const FText& Title);
	FString GetSaveAsLocation();
	ULidarPointCloud* CreateNewAsset();

private:
	/**	The tab ids for all the tabs used */
	static const FName DetailsTabId;
	static const FName ViewportTabId;
};