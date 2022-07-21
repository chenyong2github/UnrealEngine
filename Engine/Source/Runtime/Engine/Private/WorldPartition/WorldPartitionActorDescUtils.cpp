// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionActorDescUtils.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "GameFramework/Actor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/CoreRedirects.h"
#include "Misc/Base64.h"

static FName NAME_ActorMetaDataClass(TEXT("ActorMetaDataClass"));
static FName NAME_ActorMetaData(TEXT("ActorMetaData"));

bool FWorldPartitionActorDescUtils::IsValidActorDescriptorFromAssetData(const FAssetData& InAssetData)
{
	return InAssetData.FindTag(NAME_ActorMetaDataClass) && InAssetData.FindTag(NAME_ActorMetaData);
}

TUniquePtr<FWorldPartitionActorDesc> FWorldPartitionActorDescUtils::GetActorDescriptorFromAssetData(const FAssetData& InAssetData)
{
	FString ActorMetaDataClass;
	FString ActorMetaDataStr;
	if (InAssetData.GetTagValue(NAME_ActorMetaDataClass, ActorMetaDataClass) && InAssetData.GetTagValue(NAME_ActorMetaData, ActorMetaDataStr))
	{
		FString ActorClassName;
		FString ActorPackageName;
		if (!ActorMetaDataClass.Split(TEXT("."), &ActorPackageName, &ActorClassName))
		{
			ActorClassName = *ActorMetaDataClass;
		}

		// Look for a class redirectors
		const FCoreRedirectObjectName OldClassName = FCoreRedirectObjectName(*ActorClassName, NAME_None, *ActorPackageName);
		const FCoreRedirectObjectName NewClassName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, OldClassName);

		bool bIsValidClass = true;
		UClass* ActorClass = UClass::TryFindTypeSlow<UClass>(NewClassName.ToString(), EFindFirstObjectOptions::ExactClass);

		if (!ActorClass)
		{
			ActorClass = AActor::StaticClass();
			bIsValidClass = false;
		}

		FWorldPartitionActorDescInitData ActorDescInitData;
		ActorDescInitData.NativeClass = ActorClass;
		ActorDescInitData.PackageName = InAssetData.PackageName;
		ActorDescInitData.ActorPath = InAssetData.ObjectPath;
		FBase64::Decode(ActorMetaDataStr, ActorDescInitData.SerializedData);

		TUniquePtr<FWorldPartitionActorDesc> NewActorDesc(AActor::StaticCreateClassActorDesc(ActorDescInitData.NativeClass));

		NewActorDesc->Init(ActorDescInitData);
			
		if (!bIsValidClass)
		{
			UE_LOG(LogWorldPartition, Warning, TEXT("Invalid class `%s` for actor guid `%s` ('%s') from package '%s'"), *NewClassName.ToString(), *NewActorDesc->GetGuid().ToString(), *NewActorDesc->GetActorName().ToString(), *NewActorDesc->GetActorPackage().ToString());
			return nullptr;
		}

		return NewActorDesc;
	}

	return nullptr;
}

void FWorldPartitionActorDescUtils::AppendAssetDataTagsFromActor(const AActor* Actor, TArray<UObject::FAssetRegistryTag>& OutTags)
{
	check(Actor->IsPackageExternal());
	
	TUniquePtr<FWorldPartitionActorDesc> ActorDesc(Actor->CreateActorDesc());

	// If the actor is not added to a world, we can't retrieve its bounding volume, so try to get the existing one
	if (ULevel* Level = Actor->GetLevel(); !Level || !Level->Actors.Contains(Actor))
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		FARFilter Filter;
		Filter.bIncludeOnlyOnDiskAssets = true;
		Filter.PackageNames.Add(Actor->GetPackage()->GetFName());

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);

		if (Assets.Num() == 1)
		{
			if (TUniquePtr<FWorldPartitionActorDesc> NewActorDesc = FWorldPartitionActorDescUtils::GetActorDescriptorFromAssetData(Assets[0]))
			{
				ActorDesc->TransferWorldData(NewActorDesc.Get());
			}
		}
	}

	const FString ActorMetaDataClass = GetParentNativeClass(Actor->GetClass())->GetPathName();
	OutTags.Add(UObject::FAssetRegistryTag(NAME_ActorMetaDataClass, ActorMetaDataClass, UObject::FAssetRegistryTag::TT_Hidden));

	TArray<uint8> SerializedData;
	ActorDesc->SerializeTo(SerializedData);
	const FString ActorMetaData = FBase64::Encode(SerializedData);
	OutTags.Add(UObject::FAssetRegistryTag(NAME_ActorMetaData, ActorMetaData, UObject::FAssetRegistryTag::TT_Hidden));
}

void FWorldPartitionActorDescUtils::UpdateActorDescriptorFomActor(const AActor* Actor, TUniquePtr<FWorldPartitionActorDesc>& ActorDesc)
{
	TUniquePtr<FWorldPartitionActorDesc> NewActorDesc(Actor->CreateActorDesc());
	NewActorDesc->TransferFrom(ActorDesc.Get());
	ActorDesc = MoveTemp(NewActorDesc);
}

void FWorldPartitionActorDescUtils::ReplaceActorDescriptorPointerFromActor(const AActor* OldActor, AActor* NewActor, FWorldPartitionActorDesc* ActorDesc)
{
	check(OldActor->GetActorGuid() == NewActor->GetActorGuid());
	check(NewActor->GetActorGuid() == ActorDesc->GetGuid());
	check(!ActorDesc->ActorPtr.IsValid() || (ActorDesc->ActorPtr == OldActor));
	ActorDesc->ActorPtr = NewActor;
}
#endif