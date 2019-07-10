// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Blueprints/MPCDIBlueprintLib.h"
#include "Blueprints/MPCDIBlueprintAPIImpl.h"
#include "UObject/Package.h"


UMPCDIBlueprintLib::UMPCDIBlueprintLib(class FObjectInitializer const & ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UMPCDIBlueprintLib::GetAPI(TScriptInterface<IMPCDIBlueprintAPI>& OutAPI)
{
	static UMPCDIAPIImpl* Obj = NewObject<UMPCDIAPIImpl>(GetTransientPackage(), NAME_None, RF_MarkAsRootSet);
	OutAPI = Obj;
}
