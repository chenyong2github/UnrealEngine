// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Blueprints/OutputRemapBlueprintLib.h"
#include "Blueprints/OutputRemapBlueprintAPIImpl.h"
#include "UObject/Package.h"


UOutputRemapIBlueprintLib::UOutputRemapIBlueprintLib(class FObjectInitializer const & ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UOutputRemapIBlueprintLib::GetAPI(TScriptInterface<IOutputRemapBlueprintAPI>& OutAPI)
{
	static UOutputRemapAPIImpl* Obj = NewObject<UOutputRemapAPIImpl>(GetTransientPackage(), NAME_None, RF_MarkAsRootSet);
	OutAPI = Obj;
}
