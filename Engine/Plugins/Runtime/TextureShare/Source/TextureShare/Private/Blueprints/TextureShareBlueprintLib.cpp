// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/TextureShareBlueprintLib.h"
#include "Blueprints/TextureShareBlueprintAPIImpl.h"
#include "UObject/Package.h"


UTextureShareIBlueprintLib::UTextureShareIBlueprintLib(class FObjectInitializer const & ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UTextureShareIBlueprintLib::GetAPI(TScriptInterface<ITextureShareBlueprintAPI>& OutAPI)
{
	static UTextureShareAPIImpl* Obj = NewObject<UTextureShareAPIImpl>(GetTransientPackage(), NAME_None, RF_MarkAsRootSet);
	OutAPI = Obj;
}
