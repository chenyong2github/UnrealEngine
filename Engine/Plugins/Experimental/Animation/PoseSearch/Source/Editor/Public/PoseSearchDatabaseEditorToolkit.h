// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Misc/NotifyHook.h"

class IDetailsView;
class FToolBarBuilder;
class UPoseSearchDatabase;
class SPoseSearchDatabaseViewport;
class FPoseSearchDatabasePreviewScene;
class FPoseSearchDatabaseViewModel;

class FPoseSearchDatabaseEditorToolkit : public FAssetEditorToolkit, public FNotifyHook
{
public:

	FPoseSearchDatabaseEditorToolkit();
	virtual ~FPoseSearchDatabaseEditorToolkit();

	void InitAssetEditor(
		const EToolkitMode::Type Mode, 
		const TSharedPtr<IToolkitHost>& InitToolkitHost, 
		UPoseSearchDatabase* DatabaseAsset);

	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitName() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;

	const UPoseSearchDatabase* GetPoseSearchDatabase() const;
	FPoseSearchDatabaseViewModel* GetViewModel() const { return ViewModel.Get(); }
	TSharedPtr<FPoseSearchDatabaseViewModel> GetViewModelSharedPtr() const { return ViewModel; }

	void StopPreviewScene();
	void ResetPreviewScene();
	void BuildSearchIndex();

private:
	
	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_AssetDetails(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_PreviewSettings(const FSpawnTabArgs& Args);

	void BindCommands();
	void ExtendToolbar();
	void FillToolbar(FToolBarBuilder& ToolbarBuilder);

	void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);

	TSharedPtr<SPoseSearchDatabaseViewport> ViewportWidget;

	TSharedPtr<IDetailsView> EditingAssetWidget;

	TSharedPtr<FPoseSearchDatabasePreviewScene> PreviewScene;

	TSharedPtr<FPoseSearchDatabaseViewModel> ViewModel;
};
