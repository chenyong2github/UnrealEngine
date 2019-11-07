// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SteamVREditorStyle.h"

class FSteamVREditorCommands : public TCommands<FSteamVREditorCommands>
{
public:

	FSteamVREditorCommands()
		: TCommands<FSteamVREditorCommands>(TEXT("SteamVREditor"), NSLOCTEXT("Contexts", "SteamVREditor", "SteamVREditor Plugin"), NAME_None, TEXT("SteamVREditor.Common.Icon"))
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> PluginAction;
	TSharedPtr<FUICommandInfo> JsonActionManifest;
	TSharedPtr<FUICommandInfo> JsonControllerBindings;
	TSharedPtr<FUICommandInfo> ReloadActionManifest;
	TSharedPtr<FUICommandInfo> LaunchBindingsURL;
	TSharedPtr<FUICommandInfo> AddSampleInputs;
};
