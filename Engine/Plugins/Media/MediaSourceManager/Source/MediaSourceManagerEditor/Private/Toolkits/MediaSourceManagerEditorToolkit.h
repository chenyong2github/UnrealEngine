// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorUndoClient.h"
#include "Framework/Docking/TabManager.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"

class ISlateStyle;
class UMediaSourceManager;

/**
 * Implements an Editor toolkit for the media source manager.
 */
class FMediaSourceManagerEditorToolkit
	: public FAssetEditorToolkit
	, public FEditorUndoClient
	, public FGCObject
{
public:
	/**
	 * Creates and initializes a new instance.
	 * 
	 * @param InStyle The style set to use.
	 */
	FMediaSourceManagerEditorToolkit(const TSharedRef<ISlateStyle>& InStyle);

	/** Virtual destructor. */
	virtual ~FMediaSourceManagerEditorToolkit();

	/**
	 * Initializes the editor tool kit.
	 *
	 * @param InMediaSourceManager The media source manager asset to edit.
	 * @param InMode The mode to create the toolkit in.
	 * @param InToolkitHost The toolkit host.
	 */
	void Initialize(UMediaSourceManager* InMediaSourceManager, const EToolkitMode::Type InMode,
		const TSharedPtr<IToolkitHost>& InToolkitHost);

	//~ FAssetEditorToolkit interface
	virtual FString GetDocumentationLink() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

	//~ IToolkit interface
	virtual FText GetBaseToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;

	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FMediaSourceManagerEditorToolkit");
	}

protected:

	//~ FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

private:

	/** Callback for spawning tabs. */
	TSharedRef<SDockTab> HandleTabManagerSpawnTab(const FSpawnTabArgs& Args, FName TabIdentifier);
	/** Callback for spawning tabs. */
	TSharedRef<SDockTab> HandlePreviewTabManagerSpawnTab(const FSpawnTabArgs& Args, int32 ChannelIndex);

	/** The media source manager asset being edited. */
	TObjectPtr<UMediaSourceManager> MediaSourceManager;

	/** Pointer to the style set to use for toolkits. */
	TSharedRef<ISlateStyle> Style;

	/** Tab Ids for channel preview windows. */
	TArray<FName> PreviewTabIds;
};
