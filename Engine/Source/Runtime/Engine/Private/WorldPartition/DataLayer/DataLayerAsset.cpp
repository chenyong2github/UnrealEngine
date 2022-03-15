// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/DataLayer/DataLayerAsset.h"

UDataLayerAsset::UDataLayerAsset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	DataLayerType(EDataLayerType::Editor),
	DebugColor(FColor::Black)
{

}

void UDataLayerAsset::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	if (DebugColor == FColor::Black)
	{
		FRandomStream RandomStream(GetFName());
		const uint8 R = (uint8)(RandomStream.GetFraction() * 255.f);
		const uint8 G = (uint8)(RandomStream.GetFraction() * 255.f);
		const uint8 B = (uint8)(RandomStream.GetFraction() * 255.f);
		DebugColor = FColor(R, G, B);
	}
#endif
}