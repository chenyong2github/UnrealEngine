// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionActorDescUtils.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/CoreRedirects.h"
#include "Misc/Base64.h"
#include "WorldPartition/WorldPartitionActorDesc.h"

static FName NAME_ActorMetaDataClass(TEXT("ActorMetaDataClass"));
static FName NAME_ActorMetaData(TEXT("ActorMetaData"));

FName FWorldPartitionActorDescUtils::ActorMetaDataClassTagName()
{
	return NAME_ActorMetaDataClass;
}

FName FWorldPartitionActorDescUtils::ActorMetaDataTagName()
{
	return NAME_ActorMetaData;
}

static FString ResolveClassRedirector(const FString& InClassName)
{
	FString ClassName;
	FString ClassPackageName;
	if (!InClassName.Split(TEXT("."), &ClassPackageName, &ClassName))
	{
		ClassName = *InClassName;
	}

	// Look for a class redirectors
	const FCoreRedirectObjectName OldClassName = FCoreRedirectObjectName(*ClassName, NAME_None, *ClassPackageName);
	const FCoreRedirectObjectName NewClassName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, OldClassName);

	return NewClassName.ToString();
}

bool FWorldPartitionActorDescUtils::IsValidActorDescriptorFromAssetData(const FAssetData& InAssetData)
{
	return InAssetData.FindTag(NAME_ActorMetaDataClass) && InAssetData.FindTag(NAME_ActorMetaData);
}

UClass* FWorldPartitionActorDescUtils::GetActorNativeClassFromAssetData(const FAssetData& InAssetData)
{
	FString ActorMetaDataClass;
	if (InAssetData.GetTagValue(NAME_ActorMetaDataClass, ActorMetaDataClass))
	{
		// Look for a class redirectors
		const FString ActorNativeClassName = ResolveClassRedirector(ActorMetaDataClass);
		
		// Handle deprecated short class names
		const FTopLevelAssetPath ClassPath = FAssetData::TryConvertShortClassNameToPathName(*ActorNativeClassName, ELogVerbosity::Log);

		// Lookup the native class
		return UClass::TryFindTypeSlow<UClass>(ClassPath.ToString(), EFindFirstObjectOptions::ExactClass);
	}
	return nullptr;
}

TUniquePtr<FWorldPartitionActorDesc> FWorldPartitionActorDescUtils::GetActorDescriptorFromAssetData(const FAssetData& InAssetData)
{
	if (IsValidActorDescriptorFromAssetData(InAssetData))
	{
		FWorldPartitionActorDescInitData ActorDescInitData;
		ActorDescInitData.NativeClass = GetActorNativeClassFromAssetData(InAssetData);
		ActorDescInitData.PackageName = InAssetData.PackageName;
		ActorDescInitData.ActorPath = InAssetData.GetSoftObjectPath();

		FString ActorMetaDataStr;
		verify(InAssetData.GetTagValue(NAME_ActorMetaData, ActorMetaDataStr));
		verify(FBase64::Decode(ActorMetaDataStr, ActorDescInitData.SerializedData));

		TUniquePtr<FWorldPartitionActorDesc> NewActorDesc(AActor::StaticCreateClassActorDesc(ActorDescInitData.NativeClass ? ActorDescInitData.NativeClass : AActor::StaticClass()));

		NewActorDesc->Init(ActorDescInitData);
			
		if (!ActorDescInitData.NativeClass)
		{
			UE_LOG(LogWorldPartition, Warning, TEXT("Invalid class for actor guid `%s` ('%s') from package '%s'"), *NewActorDesc->GetGuid().ToString(), *NewActorDesc->GetActorName().ToString(), *NewActorDesc->GetActorPackage().ToString());
			NewActorDesc->NativeClass.Reset();
		}

		return NewActorDesc;
	}

	return nullptr;
}

void FWorldPartitionActorDescUtils::AppendAssetDataTagsFromActor(const AActor* InActor, TArray<UObject::FAssetRegistryTag>& OutTags)
{
	check(InActor->IsPackageExternal());
	
	TUniquePtr<FWorldPartitionActorDesc> ActorDesc(InActor->CreateActorDesc());

	// If the actor is not added to a world, we can't retrieve its bounding volume, so try to get the existing one
	if (ULevel* Level = InActor->GetLevel(); !Level || !Level->Actors.Contains(InActor))
	{
		// Avoid an assert when calling StaticFindObject during save to retrieve the actor's class.
		// Since we are only looking for a native class, the call to StaticFindObject is legit.
		TGuardValue<bool> GIsSavingPackageGuard(GIsSavingPackage, false);

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		FARFilter Filter;
		Filter.bIncludeOnlyOnDiskAssets = true;
		Filter.PackageNames.Add(InActor->GetPackage()->GetFName());

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

	const FString ActorMetaDataClass = GetParentNativeClass(InActor->GetClass())->GetPathName();
	OutTags.Add(UObject::FAssetRegistryTag(NAME_ActorMetaDataClass, ActorMetaDataClass, UObject::FAssetRegistryTag::TT_Hidden));

	const FString ActorMetaData = GetAssetDataFromActorDescriptor(ActorDesc);
	OutTags.Add(UObject::FAssetRegistryTag(NAME_ActorMetaData, ActorMetaData, UObject::FAssetRegistryTag::TT_Hidden));
}

FString FWorldPartitionActorDescUtils::GetAssetDataFromActorDescriptor(TUniquePtr<FWorldPartitionActorDesc>& InActorDesc)
{
	TArray<uint8> SerializedData;
	InActorDesc->SerializeTo(SerializedData);
	return FBase64::Encode(SerializedData);
}

void FWorldPartitionActorDescUtils::UpdateActorDescriptorFromActor(const AActor* InActor, TUniquePtr<FWorldPartitionActorDesc>& OutActorDesc)
{
	TUniquePtr<FWorldPartitionActorDesc> NewActorDesc(InActor->CreateActorDesc());
	UpdateActorDescriptorFromActorDescriptor(NewActorDesc, OutActorDesc);
}

void FWorldPartitionActorDescUtils::UpdateActorDescriptorFromActorDescriptor(TUniquePtr<FWorldPartitionActorDesc>& InActorDesc, TUniquePtr<FWorldPartitionActorDesc>& OutActorDesc)
{
	InActorDesc->TransferFrom(OutActorDesc.Get());
	OutActorDesc = MoveTemp(InActorDesc);
}

void FWorldPartitionActorDescUtils::ReplaceActorDescriptorPointerFromActor(const AActor* InOldActor, AActor* InNewActor, FWorldPartitionActorDesc* InActorDesc)
{
	check(!InNewActor || (InOldActor->GetActorGuid() == InNewActor->GetActorGuid()));
	check(!InNewActor || (InNewActor->GetActorGuid() == InActorDesc->GetGuid()));
	check(!InActorDesc->ActorPtr.IsValid() || (InActorDesc->ActorPtr == InOldActor));
	InActorDesc->ActorPtr = InNewActor;
}

bool FWorldPartitionActorDescUtils::ValidateActorDescClass(FWorldPartitionActorDesc* InActorDesc)
{
	// If the native class in invalid (potentially deleted), it means we parsed the actor descriptor with AActor::StaticClass and explicitly
	// marked the class as invalid.
	if (!InActorDesc->GetNativeClass().IsValid())
	{
		UE_LOG(LogWorldPartition, Warning, TEXT("Failed to find native class for actor '%s"), *InActorDesc->GetActorSoftPath().ToString());
		return false;
	}

	// If the base class is invalid, it means the actor is from a native class.
	if (!InActorDesc->GetBaseClass().IsValid())
	{
		return true;
	}

	// Lookup the Bp class, if it's already loaded we don't need to validate anything.
	if (UClass* BaseClass = FindObject<UClass>(InActorDesc->GetBaseClass()))
	{
		return true;
	}

	// The actor is from a BP class which isn't loaded. To avoid loading the class, go through the asset registry to validate the class.
	const FAssetData* ClassData;
	FString ActorDescBaseClass = InActorDesc->GetBaseClass().ToString();

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	for (;;)
	{
		ActorDescBaseClass.RemoveFromEnd(TEXT("_C"), ESearchCase::CaseSensitive);

		// Look for a class redirectors
		const FString ActorClassName = ResolveClassRedirector(ActorDescBaseClass);

		FTopLevelAssetPath AssetClassPath(*ActorClassName);

		TArray<FAssetData> BlueprintAssets;							
		AssetRegistry.ScanFilesSynchronous({ AssetClassPath.GetPackageName().ToString() }, /*bForceRescan*/false);
		AssetRegistry.GetAssetsByPackageName(AssetClassPath.GetPackageName(), BlueprintAssets, /*bIncludeOnlyOnDiskAssets*/true);

		if (!BlueprintAssets.Num())
		{
			UE_LOG(LogWorldPartition, Warning, TEXT("Failed to find assets for class '%s' for actor '%s"), *AssetClassPath.ToString(), *InActorDesc->GetActorSoftPath().ToString());
			return false;
		}

		ClassData = BlueprintAssets.FindByPredicate([&AssetClassPath](const FAssetData& AssetData) { return AssetData.ToSoftObjectPath().GetAssetPath() == AssetClassPath; });
		if (!ClassData)
		{
			UE_LOG(LogWorldPartition, Warning, TEXT("Failed to find class asset '%s' for actor '%s"), *AssetClassPath.ToString(), *InActorDesc->GetActorSoftPath().ToString());
			return false;
		}

		if (!ClassData->IsRedirector())
		{
			break;
		}

		FString DestinationObjectPath;
		if (!ClassData->GetTagValue(TEXT("DestinationObject"), DestinationObjectPath))
		{
			UE_LOG(LogWorldPartition, Warning, TEXT("Failed to follow class redirector for '%s' for actor '%s"), *AssetClassPath.ToString(), *InActorDesc->GetActorSoftPath().ToString());
			return false;
		}

		ActorDescBaseClass = DestinationObjectPath;
	}

	return true;
};
#endif
