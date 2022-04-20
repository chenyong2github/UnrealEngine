// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVR/DMXMVRAssetUserData.h"

#include "GameFramework/Actor.h"


const FName UDMXMVRAssetUserData::MVRUUIDMetaDataKey = TEXT("MVRUUID");


UDMXMVRAssetUserData* UDMXMVRAssetUserData::GetMVRAssetUserData(AActor* Actor)
{
	if (USceneComponent* RootComponent = Actor->GetRootComponent())
	{
		return RootComponent->GetAssetUserData<UDMXMVRAssetUserData>();
	}

	return nullptr;
}

FString UDMXMVRAssetUserData::GetMVRAssetUserDataValueForkey(AActor* Actor, const FName Key)
{
	if (UDMXMVRAssetUserData* MVRAssetUserData = GetMVRAssetUserData(Actor))
	{
		const FString* const ValuePtr = MVRAssetUserData->MetaData.Find(Key);
		return ValuePtr ? *ValuePtr : FString();
	}

	return FString();
}

bool UDMXMVRAssetUserData::SetMVRAssetUserDataValueForKey(AActor* Actor, FName Key, const FString& Value)
{
	// For AActor, the interface is actually implemented by the ActorComponent
	if (USceneComponent* RootComponent = Actor->GetRootComponent())
	{
		if (IInterface_AssetUserData* AssetUserData = Cast<IInterface_AssetUserData>(RootComponent))
		{
			UDMXMVRAssetUserData* MVRUserData = AssetUserData->GetAssetUserData<UDMXMVRAssetUserData>();

			if (!MVRUserData)
			{
				MVRUserData = NewObject<UDMXMVRAssetUserData>(RootComponent, NAME_None, RF_Public | RF_Transactional);
				AssetUserData->AddAssetUserData(MVRUserData);
			}

			// Add Datasmith meta data
			MVRUserData->MetaData.Add(Key, Value);
			MVRUserData->MetaData.KeySort(FNameLexicalLess());

			return true;
		}
	}

	return false;
}
