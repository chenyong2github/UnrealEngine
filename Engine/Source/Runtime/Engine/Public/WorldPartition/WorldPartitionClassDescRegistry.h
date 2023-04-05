// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR
#include "CoreMinimal.h"
#include "Misc/LazySingleton.h"
#include "UObject/ObjectMacros.h"
#include "WorldPartition/ActorDescList.h"
#include "ModuleDescriptor.h"

class UActorDescContainer;

class ENGINE_API FWorldPartitionClassDescRegistry : FActorDescList
{
	using TNameClassDescMap = TMap<FTopLevelAssetPath, TUniquePtr<FWorldPartitionActorDesc>*>;
	using TParentClassMap = TMap<FTopLevelAssetPath, FTopLevelAssetPath>;
	using TRedirectClassMap = TMap<FTopLevelAssetPath, FTopLevelAssetPath>;

public:
	static FWorldPartitionClassDescRegistry& Get();
	static void TearDown();

	void Initialize();
	void Uninitialize();
	
	bool IsInitialized() const;

	void PrefetchClassDescs(const TArray<FTopLevelAssetPath>& InClassPaths);

	bool IsRegisteredClass(const FTopLevelAssetPath& InClassPath) const;

	const TParentClassMap& GetParentClassMap() const { check(IsInitialized()); return ParentClassMap; }

private:
	void RegisterClassDescriptor(FWorldPartitionActorDesc* InClassDesc);
	void UnregisterClassDescriptor(FWorldPartitionActorDesc* InClassDesc);

	void RegisterClassDescriptorFromAssetData(const FAssetData& InAssetData);
	void RegisterClassDescriptorFromActorClass(const UClass* InActorClass);

	friend class FActorDescArchive;
	const FWorldPartitionActorDesc* GetClassDescDefault(const FTopLevelAssetPath& InClassPath) const;
	const FWorldPartitionActorDesc* GetClassDescDefaultForActor(const FTopLevelAssetPath& InClassPath) const;
	const FWorldPartitionActorDesc* GetClassDescDefaultForClass(const FTopLevelAssetPath& InClassPath) const;

	void OnAssetLoaded(UObject* InAssetLoaded);
	void OnObjectPreSave(UObject* InObject, FObjectPreSaveContext InSaveContext);
	void OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent);

	void OnPluginLoadingPhaseComplete(ELoadingPhase::Type InLoadingPhase, bool bInPhaseSuccessful);

	void OnAssetRemoved(const FAssetData& InAssetData);
	void OnAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath);

	void RegisterClass(UClass* Class);
	void RegisterClasses();

	void UpdateClassDescriptor(UObject* InObject, bool bOnlyIfExists);

	void ValidateInternalState();

	FTopLevelAssetPath RedirectClassPath(const FTopLevelAssetPath& InClassPath) const;

	TNameClassDescMap ClassByPath;
	TParentClassMap ParentClassMap;
	TRedirectClassMap RedirectClassMap;
};
#endif