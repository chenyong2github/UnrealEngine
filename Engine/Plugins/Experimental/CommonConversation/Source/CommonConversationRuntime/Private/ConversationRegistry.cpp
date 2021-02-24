// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConversationRegistry.h"
#include "CommonConversationRuntimeLogging.h"
#include "AssetRegistryModule.h"
#include "Engine/AssetManager.h"
#include "Stats/StatsMisc.h"
#include "ConversationNode.h"
#include "ConversationParticipantComponent.h"
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"
#include "ConversationContext.h"
#include "Engine/StreamableManager.h"

//======================================================================================

void FNetSerializeScriptStructCache_ConvVersion::InitForType(UScriptStruct* InScriptStruct)
{
	ScriptStructsToIndex.Reset();
	IndexToScriptStructs.Reset();

	// Find all script structs of this type and add them to the list
	// (not sure of a better way to do this but it should only happen once at startup)
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (It->IsChildOf(InScriptStruct))
		{
			IndexToScriptStructs.Add(*It);
		}
	}

	IndexToScriptStructs.Sort([](const UScriptStruct& A, const UScriptStruct& B) { return A.GetName().ToLower() > B.GetName().ToLower(); });

	for (int Index = 0; Index < IndexToScriptStructs.Num(); Index++)
	{
		ScriptStructsToIndex.Add(IndexToScriptStructs[Index], Index);
	}
}

bool FNetSerializeScriptStructCache_ConvVersion::NetSerialize(FArchive& Ar, UScriptStruct*& Struct)
{
	if (Ar.IsSaving())
	{
		if (int32* IndexPtr = ScriptStructsToIndex.Find(Struct))
		{
			int32 Index = *IndexPtr;
			if (Index <= 127)
			{
				int8 l = (int8)Index;
				Ar.SerializeBits(&l, 8);

			}
			//else
			//{
			//	check(Index <= 32767)

			//	uint8 l = (uint8)(((Index << 24) >> 24) | 128);
			//	uint8 h = (uint8)(Index >> 8);
			//	Ar.SerializeBits(&l, 8);
			//	Ar.SerializeBits(&h, 8);
			//}

			return true;
		}

		UE_LOG(LogCommonConversationRuntime, Error, TEXT("Could not find %s in ScriptStructCache"), *GetNameSafe(Struct));
		return false;
	}
	else
	{
		uint8 Index = 0;
		Ar.SerializeBits(&Index, 8);

		//if (l & 128)
		//{
		//	//int8 h = 0;
		//	//Ar.SerializeBits(&h, 8);
		//}
		//else
		//{
		//	Index = l;
		//}

		if (IndexToScriptStructs.IsValidIndex(Index))
		{
			Struct = IndexToScriptStructs[Index];
			return true;
		}

		UE_LOG(LogCommonConversationRuntime, Error, TEXT("Could not script struct at idx %d"), Index);
		return false;
	}
}

//======================================================================================

const UConversationNode* FConversationNodeHandle::TryToResolve_Slow(UWorld* InWorld) const
{
	UConversationRegistry* Registry = UConversationRegistry::GetFromWorld(InWorld);
	return Registry->GetRuntimeNodeFromGUID(NodeGUID);
}

const UConversationNode* FConversationNodeHandle::TryToResolve(const FConversationContext& Context) const
{
	return Context.GetConversationRegistry().GetRuntimeNodeFromGUID(NodeGUID);
}

UConversationRegistry::UConversationRegistry()
{
	ConversationChoiceDataStructCache.InitForType(FConversationChoiceData::StaticStruct());
}

UConversationRegistry* UConversationRegistry::GetFromWorld(const UWorld* World)
{
	return UWorld::GetSubsystem<UConversationRegistry>(World);
}

UConversationNode* UConversationRegistry::GetRuntimeNodeFromGUID(const FGuid& NodeGUID) const
{
	UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("Start UConversationRegistry::GetRuntimeNodeFromGUID with NodeGUID: (%s)"), *NodeGUID.ToString());

	const_cast<UConversationRegistry*>(this)->BuildDependenciesGraph();

	// It's possible this is just a null/empty guid, if that happens just return null.
	if (!NodeGUID.IsValid())
	{
		UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("UConversationRegistry::GetRuntimeNodeFromGUID - NodeGUID not valid"));

		return nullptr;
	}

	if (const UConversationDatabase* ConversationDB = GetConversationFromNodeGUID(NodeGUID))
	{
		return ConversationDB->ReachableNodeMap.FindRef(NodeGUID);
	}

	ensureMsgf(false, TEXT("Unexpected GetRuntimeNodeFromGUID(%s) Failed. Nodes Searched: %d"), *NodeGUID.ToString(), NodeGuidToConversation.Num());
	return nullptr;
}

TArray<FGuid> UConversationRegistry::GetEntryPointGUIDs(FGameplayTag EntryPoint) const
{
	const_cast<UConversationRegistry*>(this)->BuildDependenciesGraph();

	return EntryTagToEntryList.FindRef(EntryPoint);
}

TArray<FGuid> UConversationRegistry::GetOutputLinkGUIDs(FGameplayTag EntryPoint) const
{
	TArray<FGuid> SourceGUIDs = GetEntryPointGUIDs(EntryPoint);
	return GetOutputLinkGUIDs(SourceGUIDs);
}

TArray<FGuid> UConversationRegistry::GetOutputLinkGUIDs(const FGuid& SourceGUID) const
{
	return GetOutputLinkGUIDs(TArray<FGuid>({ SourceGUID }));
}

TArray<FGuid> UConversationRegistry::GetOutputLinkGUIDs(const TArray<FGuid>& SourceGUIDs) const
{
	const_cast<UConversationRegistry*>(this)->BuildDependenciesGraph();

	TArray<FGuid> Result;

	for (const FGuid& SourceGUID : SourceGUIDs)
	{
		if (const UConversationDatabase* SourceConversation = GetConversationFromNodeGUID(SourceGUID))
		{
			UConversationNode* SourceNode = SourceConversation->ReachableNodeMap.FindRef(SourceGUID);
			if (ensure(SourceNode))
			{
				if (UConversationNodeWithLinks* SourceNodeWithLinks = CastChecked<UConversationNodeWithLinks>(SourceNode))
				{
					Result.Append(SourceNodeWithLinks->OutputConnections);
				}
			}
		}
	}

	return Result;
}

UConversationDatabase* UConversationRegistry::GetConversationFromNodeGUID(const FGuid& NodeGUID) const
{
	if (const FSoftObjectPath* ConversationPathPtr = NodeGuidToConversation.Find(NodeGUID))
	{
		if (UConversationDatabase* ConversationDB = Cast<UConversationDatabase>(ConversationPathPtr->ResolveObject()))
		{
			return ConversationDB;
		}

		if (UConversationDatabase* ConversationDB = Cast<UConversationDatabase>(UAssetManager::GetStreamableManager().LoadSynchronous(*ConversationPathPtr, false)))
		{
			return ConversationDB;
		}
	}

	return nullptr;
}

TSharedPtr<FStreamableHandle> UConversationRegistry::LoadConversationsFor(const FGameplayTag& ConversationEntryTag) const
{
	return LoadConversationsFor(TArray<FGameplayTag>({ ConversationEntryTag }));
}

TSharedPtr<FStreamableHandle> UConversationRegistry::LoadConversationsFor(const TArray<FGameplayTag>& ConversationEntryTags) const
{
	const_cast<UConversationRegistry*>(this)->BuildDependenciesGraph();

	TSet<FSoftObjectPath> ConversationsToLoad;
	for (const FGameplayTag& ConversationEntryTag : ConversationEntryTags)
	{
		UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("UConversationRegistry::LoadConversationsFor - ConversationEntryTag to find: %s"), *ConversationEntryTag.ToString());

		if (const TArray<FSoftObjectPath>* EntryConversations = EntryTagToConversations.Find(ConversationEntryTag))
		{
			ConversationsToLoad.Append(*EntryConversations);

			UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("UConversationRegistry::LoadConversationsFor - ConversationEntryTag that has been found: %s"), *ConversationEntryTag.ToString());

			for (const FSoftObjectPath& EntryConversation : *EntryConversations)
			{
				GetAllDependenciesForConversation(EntryConversation, OUT ConversationsToLoad);

				UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("UConversationRegistry::LoadConversationsFor - Dependency found: %s"), *EntryConversation.ToString());
			}
		}
	}

	if (ConversationsToLoad.Num() > 0)
	{
		UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("LoadConversationsFor %s %s"),
			*FString::JoinBy(ConversationEntryTags, TEXT(", "), [](const FGameplayTag& Tag) { return FString::Printf(TEXT("'%s'"), *Tag.ToString()); }),
			*FString::JoinBy(ConversationsToLoad, TEXT(", "), [](const FSoftObjectPath& SoftObjectPath) { return FString::Printf(TEXT("'%s'"), *SoftObjectPath.ToString()); })
		);

		return UAssetManager::Get().LoadAssetList(ConversationsToLoad.Array());
	}
	else
	{
		UE_LOG(LogCommonConversationRuntime, Warning, TEXT("LoadConversationsFor %s - NO CONVERSATIONS FOUND"),
			*FString::JoinBy(ConversationEntryTags, TEXT(", "), [](const FGameplayTag& Tag) { return FString::Printf(TEXT("'%s'"), *Tag.ToString()); })
		);
	}

	return TSharedPtr<FStreamableHandle>();
}

TArray<FPrimaryAssetId> UConversationRegistry::GetPrimaryAssetIdsForEntryPoint(FGameplayTag EntryPoint) const
{
	const_cast<UConversationRegistry*>(this)->BuildDependenciesGraph();

	TArray<FPrimaryAssetId> AssetsWithTheEntryPoint;

	UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("Finding PrimaryAssetIds For EntryPoint [%s] among %d"), *EntryPoint.ToString(), EntryTagToConversations.Num());

	if (const TArray<FSoftObjectPath>* ConversationPaths = EntryTagToConversations.Find(EntryPoint))
	{
		UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("UConversationRegistry::GetPrimaryAssetIdsForEntryPoint - %d Conversation paths found"), ConversationPaths->Num());

		UAssetManager& AssetManager = UAssetManager::Get();
		for (const FSoftObjectPath& ConversationPath : *ConversationPaths)
		{
			UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("UConversationRegistry::GetPrimaryAssetIdsForEntryPoint - Found conversation path: %s"), *ConversationPath.ToString());

			FPrimaryAssetId ConversationAssetId = AssetManager.GetPrimaryAssetIdForPath(ConversationPath);
			if (ensure(ConversationAssetId.IsValid()))
			{
				AssetsWithTheEntryPoint.AddUnique(ConversationAssetId);

				UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("UConversationRegistry::GetPrimaryAssetIdsForEntryPoint - Valid conversation path added to AssetsWithTheEntryPoint: %s"), *ConversationAssetId.PrimaryAssetName.ToString());
			}
			else
			{
				UE_LOG(LogCommonConversationRuntime, Error, TEXT("GetPrimaryAssetIdsForEntryPoint Invalid PrimaryAssetId for %s"), *ConversationPath.ToString());
			}
		}
	}

	UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("For EntryPoint [%s], Found [%s]"),
		*EntryPoint.ToString(),
		*FString::JoinBy(AssetsWithTheEntryPoint, TEXT(", "), [](const FPrimaryAssetId& PrimaryAssetId) { return *PrimaryAssetId.ToString(); })
	);

	return AssetsWithTheEntryPoint;
}

void UConversationRegistry::BuildDependenciesGraph()
{
	if (bDependenciesBuilt)
	{
		UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("UConversationRegistry::BuildDependenciesGraph - Dependencies already built"));

		return;
	}

	UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("Registry Building Graph"));

	TArray<FAssetData> AllConversations;
	UAssetManager::Get().GetPrimaryAssetDataList(FPrimaryAssetType(UConversationDatabase::StaticClass()->GetFName()), AllConversations);

	RuntimeDependencyGraph.Reset();
	EntryTagToConversations.Reset();
	NodeGuidToConversation.Reset();

	UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("Building: Total Conversations %d"), AllConversations.Num());

	// Seed
	for (FAssetData& ConversationDataAsset : AllConversations)
	{
		const FString EntryTagsString = ConversationDataAsset.GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UConversationDatabase, EntryTags));
		if (!EntryTagsString.IsEmpty())
		{
			TArray<FConversationEntryList> EntryTags;

			FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(UConversationDatabase::StaticClass(), GET_MEMBER_NAME_CHECKED(UConversationDatabase, EntryTags));
			ArrayProperty->ImportText(*EntryTagsString, &EntryTags, 0, nullptr);

			for (const FConversationEntryList& Entry : EntryTags)
			{
				EntryTagToConversations.FindOrAdd(Entry.EntryTag).Add(ConversationDataAsset.ToSoftObjectPath());
				EntryTagToEntryList.FindOrAdd(Entry.EntryTag).Append(Entry.DestinationList);
			}
		}

		const FString InternalNodeIds = ConversationDataAsset.GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UConversationDatabase, InternalNodeIds));
		if (!InternalNodeIds.IsEmpty())
		{
			TArray<FGuid> NodeIds;

			FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(UConversationDatabase::StaticClass(), GET_MEMBER_NAME_CHECKED(UConversationDatabase, InternalNodeIds));
			ArrayProperty->ImportText(*InternalNodeIds, &NodeIds, 0, nullptr);

			for (FGuid& NodeId : NodeIds)
			{
				check(!NodeGuidToConversation.Contains(NodeId));
				NodeGuidToConversation.Add(NodeId, ConversationDataAsset.ToSoftObjectPath());
			}
		}
	}

	UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("Building: Total Entry Points %d"), EntryTagToConversations.Num());
	UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("Building: Total Nodes %d"), NodeGuidToConversation.Num());

	for (FAssetData& ConversationDataAsset : AllConversations)
	{
		const FString ExitTagsString = ConversationDataAsset.GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UConversationDatabase, ExitTags));
		if (!ExitTagsString.IsEmpty())
		{
			FGameplayTagContainer ExitTags;
			ExitTags.FromExportString(ExitTagsString);

			TArray<FSoftObjectPath>& Conversations = RuntimeDependencyGraph.FindOrAdd(ConversationDataAsset.ToSoftObjectPath());
			for (const FGameplayTag& ExitTag : ExitTags)
			{
				if (TArray<FSoftObjectPath>* Imported = EntryTagToConversations.Find(ExitTag))
				{
					Conversations.Append(*Imported);
				}
			}
		}

		const FString LinkedToNodeIds = ConversationDataAsset.GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UConversationDatabase, LinkedToNodeIds));
		if (!LinkedToNodeIds.IsEmpty())
		{
			TArray<FGuid> NodeIds;

			FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(UConversationDatabase::StaticClass(), GET_MEMBER_NAME_CHECKED(UConversationDatabase, LinkedToNodeIds));
			ArrayProperty->ImportText(*LinkedToNodeIds, &NodeIds, 0, nullptr);

			//@TODO: CONVERSATION: Register that we need to link to other graphs here.
		}
	}

	UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("Building: Runtime Dependency Graph"));
	for (const auto& KVP : RuntimeDependencyGraph)
	{
		UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("Conversation: %s"), *KVP.Key.ToString());
		for (const FSoftObjectPath& Dependency : KVP.Value)
		{
			UE_LOG(LogCommonConversationRuntime, Verbose, TEXT("     Include: %s"), *Dependency.ToString());
		}
	}

	bDependenciesBuilt = true;
}

void UConversationRegistry::GetAllDependenciesForConversation(const FSoftObjectPath& Parent, TSet<FSoftObjectPath>& OutConversationsToLoad) const
{
	if (const TArray<FSoftObjectPath>* Dependencies = RuntimeDependencyGraph.Find(Parent))
	{
		for (const FSoftObjectPath& Dependency : *Dependencies)
		{
			if (!OutConversationsToLoad.Contains(Dependency))
			{
				OutConversationsToLoad.Add(Dependency);
				GetAllDependenciesForConversation(Dependency, OutConversationsToLoad);
			}
		}
	}
}