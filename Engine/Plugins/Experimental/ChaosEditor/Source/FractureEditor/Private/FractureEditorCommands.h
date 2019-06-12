// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Framework/Commands/Commands.h"

class FFractureEditorCommands : public TCommands<FFractureEditorCommands>
{
	public:
		FFractureEditorCommands(); 

		virtual void RegisterCommands() override;

	public:

		// Selection Commands
		TSharedPtr< FUICommandInfo > SelectAll;
		TSharedPtr< FUICommandInfo > SelectNone;
		TSharedPtr< FUICommandInfo > SelectNeighbors;
		TSharedPtr< FUICommandInfo > SelectSiblings;
		TSharedPtr< FUICommandInfo > SelectAllInCluster;
		TSharedPtr< FUICommandInfo > SelectInvert;

		// View Settings
		TSharedPtr< FUICommandInfo > ToggleShowBoneColors;
		TSharedPtr< FUICommandInfo > ViewUpOneLevel;
		TSharedPtr< FUICommandInfo > ViewDownOneLevel;
		TSharedPtr< FUICommandInfo > ExplodeMore;
		TSharedPtr< FUICommandInfo > ExplodeLess;

		// Cluster Commands
		TSharedPtr< FUICommandInfo > AutoCluster;
		TSharedPtr< FUICommandInfo > Cluster;
		TSharedPtr< FUICommandInfo > Uncluster;
		TSharedPtr< FUICommandInfo > Flatten;
		TSharedPtr< FUICommandInfo > FlattenToLevel;
		TSharedPtr< FUICommandInfo > Merge;
		TSharedPtr< FUICommandInfo > MoveUp;
		TSharedPtr< FUICommandInfo > GenerateAsset;

		// Fracture Commands
		TSharedPtr< FUICommandInfo > Uniform;
		TSharedPtr< FUICommandInfo > Radial;
		TSharedPtr< FUICommandInfo > Clustered;
		TSharedPtr< FUICommandInfo > Planar;
		TSharedPtr< FUICommandInfo > Slice;
		TSharedPtr< FUICommandInfo > Brick;
		TSharedPtr< FUICommandInfo > Texture;

};

