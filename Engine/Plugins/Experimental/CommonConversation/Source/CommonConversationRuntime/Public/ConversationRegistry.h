// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "ConversationDatabase.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"

#include "ConversationRegistry.generated.h"

class UConversationDatabase;
class UWorld;
struct FStreamableHandle;

//  Container for safely replicating  script struct references (constrained to a specified parent struct)
USTRUCT()
struct COMMONCONVERSATIONRUNTIME_API FNetSerializeScriptStructCache_ConvVersion
{
	GENERATED_BODY()

	void InitForType(UScriptStruct* InScriptStruct);

	// Serializes reference to given script struct (must be in the cache)
	bool NetSerialize(FArchive& Ar, UScriptStruct*& Struct);

	UPROPERTY()
	TMap<UScriptStruct*, int32> ScriptStructsToIndex;

	UPROPERTY()
	TArray<UScriptStruct*> IndexToScriptStructs;
};

/**
 * A registry that can answer questions about all available dialogue assets
 */
UCLASS()
class COMMONCONVERSATIONRUNTIME_API UConversationRegistry : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UConversationRegistry();

	static UConversationRegistry* GetFromWorld(const UWorld* World);

	UConversationNode* GetRuntimeNodeFromGUID(const FGuid& NodeGUID) const;
	TArray<FGuid> GetEntryPointGUIDs(FGameplayTag EntryPoint) const;

	TArray<FGuid> GetOutputLinkGUIDs(FGameplayTag EntryPoint) const;
	TArray<FGuid> GetOutputLinkGUIDs(const FGuid& SourceGUID) const;
	TArray<FGuid> GetOutputLinkGUIDs(const TArray<FGuid>& SourceGUIDs) const;

	TSharedPtr<FStreamableHandle> LoadConversationsFor(const FGameplayTag& ConversationEntryTag) const;
	TSharedPtr<FStreamableHandle> LoadConversationsFor(const TArray<FGameplayTag>& ConversationEntryTags) const;

	TArray<FPrimaryAssetId> GetPrimaryAssetIdsForEntryPoint(FGameplayTag EntryPoint) const;
	
	UPROPERTY(Transient)
	FNetSerializeScriptStructCache_ConvVersion ConversationChoiceDataStructCache;

private:
	UConversationDatabase* GetConversationFromNodeGUID(const FGuid& NodeGUID) const;

	void BuildDependenciesGraph();
	void GetAllDependenciesForConversation(const FSoftObjectPath& Parent, TSet<FSoftObjectPath>& OutConversationsToLoad) const;

private:
	TMap<FSoftObjectPath, TArray<FSoftObjectPath>> RuntimeDependencyGraph;
	TMap<FGameplayTag, TArray<FSoftObjectPath>> EntryTagToConversations;
	TMap<FGameplayTag, TArray<FGuid>> EntryTagToEntryList;
	TMap<FGuid, FSoftObjectPath> NodeGuidToConversation;

private:
	bool bDependenciesBuilt = false;
};