// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/OutputRemapBlueprintAPIImpl.h"

#include "UObject/Package.h"
#include "IOutputRemap.h"


void UOutputRemapAPIImpl::ReloadChangedExternalFiles()
{
	IOutputRemap& OutputRemapModule = IOutputRemap::Get();
	OutputRemapModule.ReloadAll();
}

