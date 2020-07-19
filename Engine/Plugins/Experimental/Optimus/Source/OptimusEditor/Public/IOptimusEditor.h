// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Toolkits/AssetEditorToolkit.h"

class UOptimusNode;

class IOptimusEditor 
	: public FAssetEditorToolkit
//	, public IHasMenuExtensibility
//	, public IHasToolBarExtensibility
{
public:
	virtual ~IOptimusEditor() {}
};
