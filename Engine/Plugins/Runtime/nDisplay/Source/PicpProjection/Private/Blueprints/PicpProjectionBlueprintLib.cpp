// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/PicpProjectionBlueprintLib.h"
#include "Blueprints/PicpProjectionBlueprintAPIImpl.h"
#include "UObject/Package.h"


UPicpProjectionIBlueprintLib::UPicpProjectionIBlueprintLib(class FObjectInitializer const & ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UPicpProjectionIBlueprintLib::GetAPI(TScriptInterface<IPicpProjectionBlueprintAPI>& OutAPI)
{
	static UPicpProjectionAPIImpl* Obj = NewObject<UPicpProjectionAPIImpl>(GetTransientPackage(), NAME_None, RF_MarkAsRootSet);
	OutAPI = Obj;
}
