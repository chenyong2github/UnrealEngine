// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Commands/Commands.h"

/**
 * Unreal StaticMesh editor actions
 */
class FCustomizableObjectInstanceEditorCommands : public TCommands<FCustomizableObjectInstanceEditorCommands>
{

public:
	FCustomizableObjectInstanceEditorCommands();
	
	
	/**  */
	TSharedPtr< FUICommandInfo > SetDrawUVs;
	TSharedPtr< FUICommandInfo > SetShowGrid;
	TSharedPtr< FUICommandInfo > SetShowSky;
	TSharedPtr< FUICommandInfo > SetShowBounds;
	TSharedPtr< FUICommandInfo > SetShowCollision;
	TSharedPtr< FUICommandInfo > SetCameraLock;
	TSharedPtr< FUICommandInfo > SaveThumbnail;

	// View Menu Commands
	TSharedPtr< FUICommandInfo > SetShowNormals;
	TSharedPtr< FUICommandInfo > SetShowTangents;
	TSharedPtr< FUICommandInfo > SetShowBinormals;
	TSharedPtr< FUICommandInfo > SetShowPivot;

	TSharedPtr< FUICommandInfo > BakeInstance;
	TSharedPtr< FUICommandInfo > StateChangeTest;

	//Toolbar Commands
	TSharedPtr< FUICommandInfo > ShowParentCO;
	TSharedPtr< FUICommandInfo > EditParentCO;
	TSharedPtr< FUICommandInfo > TextureAnalyzer;

	/**
	 * Initialize commands
	 */
	virtual void RegisterCommands() override;

};
