// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/IToolkitHost.h"
#include "EditorUndoClient.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "IMassEntityEditor.h"

class IDetailsView;
class UMassSchematic;

class FMassEntityEditor : public IMassEntityEditor, public FGCObject//, public FSelfRegisteringEditorUndoClient
{
public:
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;

	void InitEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UMassSchematic& InMassSchematic);

	//~ Begin IToolkit Interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	//~ End IToolkit Interface

	void OnAssetFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);

	/** FEditorUndoClient interface */
	//	virtual void PostUndo(bool bSuccess) override;
	//	virtual void PostRedo(bool bSuccess) override;

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FMassEntityEditor");
	}

protected:
	/** Called when "Save" is clicked for this asset */
	virtual void SaveAsset_Execute() override;

private:
	TSharedRef<SDockTab> SpawnTab_AssetDetails(const FSpawnTabArgs& Args);

	/** Mass Schematic being edited */
	UMassSchematic* MassSchematic;

	/** Asset Property View */
	TSharedPtr<class IDetailsView> AssetDetailsView;

	static const FName AssetDetailsTabId;
};
