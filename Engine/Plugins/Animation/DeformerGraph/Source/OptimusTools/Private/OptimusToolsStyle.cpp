// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusToolsStyle.h"

#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"


FOptimusToolsStyle::FOptimusToolsStyle() : 
	FSlateStyleSet("OptimusToolsStyle")
{
	
}


FOptimusToolsStyle& FOptimusToolsStyle::Get()
{
	static FOptimusToolsStyle Instance;
	return Instance;
}


void FOptimusToolsStyle::Register()
{
	FSlateStyleRegistry::RegisterSlateStyle(Get());
}


void FOptimusToolsStyle::Unregister()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(Get());
}
