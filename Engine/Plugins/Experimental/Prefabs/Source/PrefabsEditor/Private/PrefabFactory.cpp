// Copyright Epic Games, Inc. All Rights Reserved.

#include "PrefabFactory.h"
#include "PrefabUncooked.h"

UPrefabFactory::UPrefabFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UPrefabUncooked::StaticClass();
}

UObject* UPrefabFactory::FactoryCreateNew( UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UPrefabUncooked* NewPrefab = NewObject<UPrefabUncooked>(InParent, Class, Name, Flags | RF_Transactional);
	return NewPrefab;
}

FString UPrefabFactory::GetDefaultNewAssetName() const
{
	return TEXT("NewPrefab");
}
