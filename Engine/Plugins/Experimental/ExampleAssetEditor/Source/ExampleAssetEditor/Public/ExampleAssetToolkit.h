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
	// Base Asset Toolkit overrides
	virtual TFunction<TSharedRef<SEditorViewport>(void)> GetViewportDelegate() override;
	virtual TSharedPtr<FEditorViewportClient> CreateEditorViewportClient() const override;
	virtual void CreateEditorModeManager() override;
	// End Base Asset Toolkit overrides

	void AddInputBehaviorsForEditorClientViewport(TSharedPtr<FEditorViewportClient>& InViewportClient) const;

	UInteractiveToolsContext* ToolsContext;
	TSharedPtr<FToolsContextQueriesImpl> ToolsContextQueries;
	TSharedPtr<FToolsContextTransactionImpl> ToolsContextTransactions;
};
