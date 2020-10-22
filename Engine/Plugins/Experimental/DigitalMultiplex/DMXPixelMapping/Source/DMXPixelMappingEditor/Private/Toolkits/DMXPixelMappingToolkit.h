// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TickableEditorObject.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "DMXPixelMappingComponentReference.h"

class UDMXPixelMapping;
class SDockableTab;
class FTabManager;
class SWidget;

class SDMXPixelMappingPaletteView;
class SDMXPixelMappingHierarchyView;
class SDMXPixelMappingDesignerView;
class SDMXPixelMappingPreviewView;
class SDMXPixelMappingDetailsView;

class FDMXPixelMappingPaletteViewModel;

class UDMXPixelMappingBaseComponent;
class FDMXPixelMappingToolbar;
class UDMXPixelMappingRendererComponent;
class UDMXPixelMappingOutputComponent;
class UDMXPixelMappingMatrixComponent;

/**
 * Implements an Editor toolkit for Pixel Mapping.
 */
class FDMXPixelMappingToolkit
	: public FAssetEditorToolkit
	, public FTickableEditorObject
{
	friend class FDMXPixelMappingToolbar;

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnComponenetAddedOrDeletedDelegate, bool)
	FOnComponenetAddedOrDeletedDelegate& GetOnComponenetAddedOrDeletedDelegate() { return OnComponenetAddedOrDeletedDelegate; }

	DECLARE_MULTICAST_DELEGATE(FOnSelectedComponenetChangedDelegate)
	FOnSelectedComponenetChangedDelegate& GetOnSelectedComponenetChangedDelegate() { return OnSelectedComponenetChangedDelegate; }

public:
	/** Default constructor */
	FDMXPixelMappingToolkit();

	/**
	 * Destructor.
	 */
	virtual ~FDMXPixelMappingToolkit();

public:

	/**
	 * Edits the specified Texture object.
	 *
	 * @param Mode The tool kit mode.
	 * @param InitToolkitHost
	 * @param ObjectToEdit The texture object to edit.
	 */
	void InitPixelMappingEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, UDMXPixelMapping* InDMXPixelMapping);

public:

	//~ Begin FAssetEditorToolkit Interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	//~ End FAssetEditorToolkit Interface

	//~ Begin IToolkit Interface
	virtual FText GetBaseToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor(0.0f, 0.0f, 0.2f, 0.5f); }
	virtual FString GetWorldCentricTabPrefix() const override;
	//~ End IToolkit Interface

	//~ Begin FTickableEditorObject interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual TStatId GetStatId() const override;
	//~ End FTickableEditorObject Interface

	UDMXPixelMapping* GetDMXPixelMapping() const { return DMXPixelMapping; }

	FDMXPixelMappingComponentReference GetReferenceFromComponent(UDMXPixelMappingBaseComponent* InComponent);

	UDMXPixelMappingRendererComponent* GetActiveRendererComponent() const { return ActiveRendererComponent.Get(); }

	TArray<UDMXPixelMappingOutputComponent*> GetActiveOutputComponents() const
	{
		TArray<UDMXPixelMappingOutputComponent*> ValidatedActiveOutputComponents;
		for (TWeakObjectPtr<UDMXPixelMappingOutputComponent> ActiveOutputComponent : ActiveOutputComponents)
		{
			if (ActiveOutputComponent.IsValid())
			{
				ValidatedActiveOutputComponents.Add(ActiveOutputComponent.Get());
			}
		}
		return ValidatedActiveOutputComponents;
	}

	const TSharedPtr<FUICommandList>& GetDesignerCommandList() const { return DesignerCommandList; }

	const TSharedPtr<FDMXPixelMappingPaletteViewModel>& GetPaletteViewModel() const { return PaletteViewModel; }

	const TSet<FDMXPixelMappingComponentReference>& GetSelectedComponents() const { return SelectedComponents; }

	TSharedRef<SWidget> CreateOrGetView_PaletteView();

	TSharedRef<SWidget> CreateOrGetView_HierarchyView();

	TSharedRef<SWidget> CreateOrGetView_DesignerView();

	TSharedRef<SWidget> CreateOrGetView_PreviewView();

	TSharedRef<SWidget> CreateOrGetView_DetailsView();

	bool IsPlayingDMX() const { return bIsPlayingDMX; }

	void SetActiveRenderComponent(UDMXPixelMappingRendererComponent* InComponent);

	void HandleAddComponent(bool bIsSuccess);

	void SelectComponents(const TSet<FDMXPixelMappingComponentReference>& Components);

	void AddRenderer();

	void ClearRenderers();

	void DeleteSelectedComponents(const TSet<FDMXPixelMappingComponentReference>& InComponents);

	bool CanDeleteSelectedComponents(const TSet<FDMXPixelMappingComponentReference>& InComponents);

	void OnComponentRenamed(UDMXPixelMappingBaseComponent* InComponent);

	void BroadcastPostChange(UDMXPixelMapping* InDMXPixelMapping);

	void DeleteMatrixPixels(UDMXPixelMappingMatrixComponent* InMatrixComponent);

	void CreateMatrixPixels(UDMXPixelMappingMatrixComponent* InMatrixComponent);

private:
	void PlayDMX();

	void StopPlayingDMX();

	void ExecutebTogglePlayDMXAll();

	void OnSaveThumbnailImage();

	void DeleteSelectedComponents_Internal();

	bool CanDeleteSelectedComponents_Internal();

	void InitializeInternal(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, const FGuid& MessageLogGuid);

	TSharedRef<SDockTab> SpawnTab_PaletteView(const FSpawnTabArgs& Args);

	TSharedRef<SDockTab> SpawnTab_HierarchyView(const FSpawnTabArgs& Args);

	TSharedRef<SDockTab> SpawnTab_DesignerView(const FSpawnTabArgs& Args);

	TSharedRef<SDockTab> SpawnTab_PreviewView(const FSpawnTabArgs& Args);

	TSharedRef<SDockTab> SpawnTab_DetailsView(const FSpawnTabArgs& Args);

	void SetupCommands();

	void ExtendToolbar();

	void CreateInternalViewModels();

	void CreateInternalViews();

	void OnDMXPixelMappingDeleteChildrenComponents(UDMXPixelMappingBaseComponent* InParentComponent);

private:
	UDMXPixelMapping* DMXPixelMapping;

	/** List of open tool panels; used to ensure only one exists at any one time */
	TMap<FName, TWeakPtr<SDockableTab>> SpawnedToolPanels;

	TSharedPtr<SDMXPixelMappingPaletteView> PaletteView;

	TSharedPtr<SDMXPixelMappingHierarchyView> HierarchyView;

	TSharedPtr<SDMXPixelMappingDesignerView> DesignerView;

	TSharedPtr<SDMXPixelMappingPreviewView> PreviewView;

	TSharedPtr<SDMXPixelMappingDetailsView> DetailsView;

	TSharedPtr<FDMXPixelMappingPaletteViewModel> PaletteViewModel;

	FOnComponenetAddedOrDeletedDelegate OnComponenetAddedOrDeletedDelegate;

	FOnSelectedComponenetChangedDelegate OnSelectedComponenetChangedDelegate;

	TSet<FDMXPixelMappingComponentReference> SelectedComponents;

	TSharedPtr<FDMXPixelMappingToolbar> Toolbar;

	TWeakObjectPtr<UDMXPixelMappingRendererComponent> ActiveRendererComponent;

	TArray<TWeakObjectPtr<UDMXPixelMappingOutputComponent>> ActiveOutputComponents;

	/** Command list for handling widget actions in the PixelMapping Toolkit */
	TSharedPtr<FUICommandList> DesignerCommandList;

	bool bIsPlayingDMX;

	bool bTogglePlayDMXAll;

	bool bRequestStopSendingDMX;

	uint8 RequestStopSendingTicks;

	static const uint8 RequestStopSendingMaxTicks;

public:
	static const FName PaletteViewTabID;

	static const FName HierarchyViewTabID;

	static const FName DesignerViewTabID;

	static const FName PreviewViewTabID;

	static const FName DetailsViewTabID;
};
