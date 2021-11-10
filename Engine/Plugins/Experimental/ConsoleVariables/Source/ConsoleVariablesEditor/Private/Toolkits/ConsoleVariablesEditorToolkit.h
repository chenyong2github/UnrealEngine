// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"

class FConsoleVariablesEditorMainPanel;

class UConsoleVariablesAsset;

class FConsoleVariablesEditorToolkit
	:
	public FAssetEditorToolkit
{
	using Super = FAssetEditorToolkit;
public:

	static const FName AppIdentifier;
	static const FName ConsoleVariablesToolkitPanelTabId;
	
	static TSharedPtr<FConsoleVariablesEditorToolkit> CreateConsoleVariablesEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost);

	UConsoleVariablesAsset* AllocateTransientPreset() const;

	//~ Begin FBaseToolkit Interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual FText GetToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FText GetBaseToolkitName() const override
	{
		return FText::GetEmpty();
	}
	virtual FName GetToolkitFName() const override
	{
		return FName("Console Variables Editor");
	}
	virtual FString GetWorldCentricTabPrefix() const override
	{
		return FString();
	}
	virtual bool IsAssetEditor() const override
	{
		return false;
	}
	virtual FLinearColor GetWorldCentricTabColorScale() const override
	{
		return FLinearColor(1,1,1);
	}
	//~ End FBaseToolkit Interface

	TWeakPtr<FConsoleVariablesEditorMainPanel> GetMainPanel() const
	{
		return MainPanel;
	}

	virtual ~FConsoleVariablesEditorToolkit() override;

private:

	void Initialize(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost);

	TSharedRef<SDockTab> SpawnTab_MainPanel(const FSpawnTabArgs& Args) const;

	void InvokePanelTab();

	TSharedPtr<FConsoleVariablesEditorMainPanel> MainPanel;
};
