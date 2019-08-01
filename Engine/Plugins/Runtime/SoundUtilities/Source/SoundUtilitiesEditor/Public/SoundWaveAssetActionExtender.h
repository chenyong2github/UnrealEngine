// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

class FSoundWaveAssetActionExtender
{
public:
	static void RegisterMenus();
	static void GetExtendedActions(const struct FEditorMenuContext& MenuContext);
	static void ExecuteCreateSimpleSound(const struct FEditorMenuContext& MenuContext);
};

