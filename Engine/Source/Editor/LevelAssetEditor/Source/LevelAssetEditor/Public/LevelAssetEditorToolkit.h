// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Tools/BaseAssetToolkit.h"
#include "Delegates/IDelegateInstance.h"

class FLevelEditorToolsContextQueriesImpl;
class FLevelEditorContextTransactionImpl;
class SEditorViewport;
class FEditorViewportClient;
class UAssetEditor;
class UInteractiveToolsContext;

class FLevelEditorAssetToolkit : public FBaseAssetToolkit
{
public:
	FLevelEditorAssetToolkit(UAssetEditor* InOwningAssetEditor, UInteractiveToolsContext* InContext);
	virtual ~FLevelEditorAssetToolkit();


protected:
	// Base Asset Toolkit overrides
	virtual AssetEditorViewportFactoryFunction GetViewportDelegate() override;
	virtual TSharedPtr<FEditorViewportClient> CreateEditorViewportClient() const override;
	virtual void PostInitAssetEditor() override;
	// End Base Asset Toolkit overrides

	void AddInputBehaviorsForEditorClientViewport(TSharedPtr<FEditorViewportClient>& InViewportClient) const;

	UInteractiveToolsContext* ToolsContext;
	TSharedPtr<FLevelEditorToolsContextQueriesImpl> ToolsContextQueries;
	TSharedPtr<FLevelEditorContextTransactionImpl> ToolsContextTransactions;
};
