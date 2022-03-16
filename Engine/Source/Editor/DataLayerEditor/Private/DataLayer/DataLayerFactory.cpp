// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayer/DataLayerFactory.h"

#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "Math/RandomStream.h"

UDataLayerFactory::UDataLayerFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)

{
	SupportedClass = UDataLayerAsset::StaticClass();
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UDataLayerFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UDataLayerAsset* DataLayerAsset =   NewObject<UDataLayerAsset>(InParent, InName, Flags);

	FRandomStream RandomStream(FName(DataLayerAsset->GetFullName()));
	const uint8 R = (uint8)(RandomStream.GetFraction() * 255.f);
	const uint8 G = (uint8)(RandomStream.GetFraction() * 255.f);
	const uint8 B = (uint8)(RandomStream.GetFraction() * 255.f);
	DataLayerAsset->SetDebugColor(FColor(R, G, B));

	return DataLayerAsset;
}