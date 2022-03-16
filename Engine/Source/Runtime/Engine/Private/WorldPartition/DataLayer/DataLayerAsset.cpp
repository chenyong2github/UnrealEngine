// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerAsset.h"

UDataLayerAsset::UDataLayerAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	DataLayerType(EDataLayerType::Editor),
	DebugColor(FColor::Black)
{

}
