// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Factories/Factory.h"
#include "LensFile.h"
#include "Toolkits/SimpleAssetEditor.h"

class FExtender;
class IToolkitHost;
class SDockableTab;
class IDetailsView;
class ULensFile;

/** Viewer/editor for a LensFile */
class FLensFileEditorToolkit : public FSimpleAssetEditor
{

private:
	using Super = FSimpleAssetEditor;

public:
	static TSharedRef<FLensFileEditorToolkit> CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, ULensFile* InLensFile);

	virtual ~FLensFileEditorToolkit();

	/**
	 * Edits the specified table
	 *
	 * @param	Mode					Asset editing mode for this editor (standalone or world-centric)
	 * @param	InitToolkitHost			When Mode is WorldCentric, this is the level editor instance to spawn this editor within
	 * @param	InLensFile				The LensFile asset to edit
	 */
	void InitLensFileEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, ULensFile* InLensFile);

protected:

	//~ Begin FSimpleAssetEditor interface
	virtual void SaveAsset_Execute() override;
	virtual void SaveAssetAs_Execute() override;
	virtual bool OnRequestClose() override;
	//~ End FSimpleAssetEditor interface

private:
	/** Dockable tab for properties */
	TSharedPtr<SDockableTab> PropertiesTab;

	/** Details view */
	TSharedPtr<IDetailsView> DetailsView;
};