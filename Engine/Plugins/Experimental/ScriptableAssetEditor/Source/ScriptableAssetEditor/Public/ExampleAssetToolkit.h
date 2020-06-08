// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Tools/BaseAssetToolkit.h"
#include "Delegates/IDelegateInstance.h"

class FToolsContextQueriesImpl;
class FToolsContextTransactionImpl;
class SEditorViewport;
class FEditorViewportClient;
class UAssetEditor;
class UInteractiveToolsContext;

class FExampleAssetToolkit : public FBaseAssetToolkit
{
public:
	FExampleAssetToolkit(UAssetEditor* InOwningAssetEditor, UInteractiveToolsContext* InContext);
	virtual ~FExampleAssetToolkit();


protected:
	virtual TFunction<TSharedRef<SEditorViewport>(void)> GetViewportDelegate() override;
	virtual TSharedPtr<FEditorViewportClient> CreateEditorViewportClient() const override;
	void OnAssetOpened(UObject* Asset, IAssetEditorInstance* AssetEditorInstance);

	FDelegateHandle WindowOpenedDelegateHandle {};
	UInteractiveToolsContext* ToolsContext;
	TSharedPtr<FToolsContextQueriesImpl> ToolsContextQueries;
	TSharedPtr<FToolsContextTransactionImpl> ToolsContextTransactions;
};
