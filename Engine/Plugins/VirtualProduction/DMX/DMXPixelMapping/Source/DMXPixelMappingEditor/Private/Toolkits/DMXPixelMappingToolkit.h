// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TickableEditorObject.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
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
	DECLARE_MULTICAST_DELEGATE(FOnComponentsAddedOrDeletedDelegate)
	UE_DEPRECATED(4.27, "Use UDMXPixelMappingBaseComponent::GetOnComponentAdded and  UDMXPixelMappingBaseComponent::GetOnComponentRemoved instead.")
	FOnComponentsAddedOrDeletedDelegate& GetOnComponentsAddedOrDeletedDelegate() { return OnComponentsAddedOrDeletedDelegate_DEPRECATED; }

	DECLARE_MULTICAST_DELEGATE(FOnSelectedComponentsChangedDelegate)
	FOnSelectedComponentsChangedDelegate& GetOnSelectedComponentsChangedDelegate() { return OnSelectedComponentsChangedDelegate; }

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

	/** DEPRECATED 4.27 */
	UE_DEPRECATED(4.27, "No longer needed")
	void HandleAddComponents();

	/** DEPRECATED 4.27 */
	UE_DEPRECATED(4.27, "No longer needed")
	void HandleRemoveComponents();

	/** Creates an array of components given specifed component references */
	template <typename ComponentType>
	TArray<ComponentType> MakeComponentArray(const TSet<FDMXPixelMappingComponentReference>& Components) const;

	void SelectComponents(const TSet<FDMXPixelMappingComponentReference>& Components);

	/** Returns true if the component is selected */
	bool IsComponentSelected(UDMXPixelMappingBaseComponent* Component) const;

	/** Gives each component widget a color depending on selection and whether it's over its parent */
	void UpdateComponentWidgetColors();

	void AddRenderer();

	void DeleteSelectedComponents();

	void OnComponentRenamed(UDMXPixelMappingBaseComponent* InComponent);

	/** 
	 * Creates components from the template. Returns the new components.
	 * If many component were created, the first component is the topmost parent.
	 */
	TArray<UDMXPixelMappingBaseComponent*> CreateComponentsFromTemplates(UDMXPixelMappingRootComponent* RootComponent, UDMXPixelMappingBaseComponent* Target, const TArray<TSharedPtr<FDMXPixelMappingComponentTemplate>>& Templates);

	UE_DEPRECATED(4.27, "Handled in UDMXPixelMappingMatrixComponent internally instead. No longer needs to be call explicitly.")
	void DeleteMatrixPixels(UDMXPixelMappingMatrixComponent* InMatrixComponent) { checkNoEntry();}

	UE_DEPRECATED(4.27, "Handled in UDMXPixelMappingMatrixComponent internally instead.  No longer needs to be call explicitly.")
	void CreateMatrixPixels(UDMXPixelMappingMatrixComponent* InMatrixComponent) {}

private:
	void PlayDMX();

	void StopPlayingDMX();

	void ExecutebTogglePlayDMXAll();

	void UpdateBlueprintNodes(UDMXPixelMapping* InDMXPixelMapping);

	void OnSaveThumbnailImage();

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

	/** Called when a component was added to the pixel mapping */
	void OnComponentAdded(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* InComponent);

	/** Called when a component was removed from the pixel mapping */
	void OnComponentRemoved(UDMXPixelMapping* PixelMapping, UDMXPixelMappingBaseComponent* InComponent);

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

	FOnSelectedComponentsChangedDelegate OnSelectedComponentsChangedDelegate;

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

	FOnComponentsAddedOrDeletedDelegate OnComponentsAddedOrDeletedDelegate_DEPRECATED;

public:
	static const FName PaletteViewTabID;

	static const FName HierarchyViewTabID;

	static const FName DesignerViewTabID;

	static const FName PreviewViewTabID;

	static const FName DetailsViewTabID;
};
