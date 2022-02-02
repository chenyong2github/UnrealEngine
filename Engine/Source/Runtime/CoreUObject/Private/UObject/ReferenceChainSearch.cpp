// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ReferenceChainSearch.h"
#include "HAL/PlatformStackWalk.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UObject/FastReferenceCollector.h"
#include "UObject/GCObject.h"
#include "HAL/ThreadHeartBeat.h"

DEFINE_LOG_CATEGORY_STATIC(LogReferenceChain, Log, All);

// Returns true if the object can't be collected by GC
static FORCEINLINE bool IsNonGCObject(FGCObjectInfo* Object, EReferenceChainSearchMode SearchMode)
{
	return Object->HasAnyInternalFlags(EInternalObjectFlags::GarbageCollectionKeepFlags | EInternalObjectFlags::RootSet) ||
		(GARBAGE_COLLECTION_KEEPFLAGS != RF_NoFlags && Object->HasAnyFlags(GARBAGE_COLLECTION_KEEPFLAGS) && !(SearchMode & EReferenceChainSearchMode::FullChain));
}

FReferenceChainSearch::FGraphNode* FReferenceChainSearch::FindOrAddNode(FGCObjectInfo* InObjectInfo)
{
	FGraphNode* ObjectNode = nullptr;
	FGraphNode** ExistingObjectNode = AllNodes.Find(InObjectInfo);
	if (ExistingObjectNode)
	{
		ObjectNode = *ExistingObjectNode;
		check(ObjectNode->ObjectInfo == InObjectInfo);
	}
	else
	{
		ObjectNode = new FGraphNode();
		ObjectNode->ObjectInfo = InObjectInfo;
		AllNodes.Add(InObjectInfo, ObjectNode);
	}
	return ObjectNode;
}

FReferenceChainSearch::FGraphNode* FReferenceChainSearch::FindOrAddNode(UObject* InObjectToFindNodeFor)
{
	return FindOrAddNode(FGCObjectInfo::FindOrAddInfoHelper(InObjectToFindNodeFor, ObjectToInfoMap));
}

int32 FReferenceChainSearch::BuildReferenceChains(FGraphNode* TargetNode, TArray<FReferenceChain*>& ProducedChains, int32 ChainDepth, const int32 VisitCounter, EReferenceChainSearchMode SearchMode)
{
	int32 ProducedChainsCount = 0;
	if (TargetNode->Visited != VisitCounter)
	{
		TargetNode->Visited = VisitCounter;

		// Stop at root objects
		if (!IsNonGCObject(TargetNode->ObjectInfo, SearchMode))
		{			
			for (FGraphNode* ReferencedByNode : TargetNode->ReferencedByObjects)
			{
				// For each of the referencers of this node, duplicate the current chain and continue processing
				if (ReferencedByNode->Visited != VisitCounter)
				{
					int32 OldChainsCount = ProducedChains.Num();
					int32 NewChainsCount = BuildReferenceChains(ReferencedByNode, ProducedChains, ChainDepth + 1, VisitCounter, SearchMode);
					// Insert the current node to all chains produced recursively
					for (int32 NewChainIndex = OldChainsCount; NewChainIndex < (NewChainsCount + OldChainsCount); ++NewChainIndex)
					{
						FReferenceChain* Chain = ProducedChains[NewChainIndex];
						Chain->InsertNode(TargetNode);
					}
					ProducedChainsCount += NewChainsCount;
				}
			}			
		}
		else
		{
			// This is a root so we can construct a chain from this node up to the target node
			FReferenceChain* Chain = new FReferenceChain(ChainDepth);
			Chain->InsertNode(TargetNode);
			ProducedChains.Add(Chain);
			ProducedChainsCount = 1;
		}
	}

	return ProducedChainsCount;
}

void FReferenceChainSearch::RemoveChainsWithDuplicatedRoots(TArray<FReferenceChain*>& AllChains)
{
	// This is going to be rather slow but it depends on the number of chains which shouldn't be too bad (usually)
	for (int32 FirstChainIndex = 0; FirstChainIndex < AllChains.Num(); ++FirstChainIndex)
	{
		const FGraphNode* RootNode = AllChains[FirstChainIndex]->GetRootNode();
		for (int32 SecondChainIndex = AllChains.Num() - 1; SecondChainIndex > FirstChainIndex; --SecondChainIndex)
		{
			if (AllChains[SecondChainIndex]->GetRootNode() == RootNode)
			{
				delete AllChains[SecondChainIndex];
				AllChains.RemoveAt(SecondChainIndex);
			}
		}
	}
}

typedef TPair<FReferenceChainSearch::FGraphNode*, FReferenceChainSearch::FGraphNode*> FRootAndReferencerPair;

void FReferenceChainSearch::RemoveDuplicatedChains(TArray<FReferenceChain*>& AllChains)
{
	// We consider chains identical if the direct referencer of the target node and the root node are identical
	TMap<FRootAndReferencerPair, FReferenceChain*> UniqueChains;

	for (int32 ChainIndex = 0; ChainIndex < AllChains.Num(); ++ChainIndex)
	{
		FReferenceChain* Chain = AllChains[ChainIndex];
		FRootAndReferencerPair ChainRootAndReferencer(Chain->Nodes[1], Chain->Nodes.Last());
		FReferenceChain** ExistingChain = UniqueChains.Find(ChainRootAndReferencer);
		if (ExistingChain)
		{
			if ((*ExistingChain)->Nodes.Num() > Chain->Nodes.Num())
			{
				delete (*ExistingChain);
				UniqueChains[ChainRootAndReferencer] = Chain;
			}
			else
			{
				delete Chain;
			}
		}
		else
		{
			UniqueChains.Add(ChainRootAndReferencer, Chain);
		}
	}
	AllChains.Reset();
	UniqueChains.GenerateValueArray(AllChains);
}

void FReferenceChainSearch::BuildReferenceChains(FGraphNode* TargetNode, TArray<FReferenceChain*>& Chains, EReferenceChainSearchMode SearchMode)
{	
	TArray<FReferenceChain*> AllChains;

	// Recursively construct reference chains	
	int32 VisitCounter = 0;
	for (FGraphNode* ReferencedByNode : TargetNode->ReferencedByObjects)
	{
		TargetNode->Visited = ++VisitCounter;
		
		AllChains.Reset();
		const int32 MinChainDepth = 2; // The chain will contain at least the TargetNode and the ReferencedByNode
		BuildReferenceChains(ReferencedByNode, AllChains, MinChainDepth, VisitCounter, SearchMode);
		for (FReferenceChain* Chain : AllChains)
		{
			Chain->InsertNode(TargetNode);
		}

		// Filter based on search mode	
		if (!!(SearchMode & EReferenceChainSearchMode::ExternalOnly))
		{
			for (int32 ChainIndex = AllChains.Num() - 1; ChainIndex >= 0; --ChainIndex)
			{
				FReferenceChain* Chain = AllChains[ChainIndex];	
				if (!Chain->IsExternal())
				{
					// Discard the chain
					delete Chain;
					AllChains.RemoveAtSwap(ChainIndex);
				}
			}
		}

		Chains.Append(AllChains);
	}

	// Reject duplicates
	if (!!(SearchMode & (EReferenceChainSearchMode::Longest | EReferenceChainSearchMode::Shortest)))
	{
		RemoveChainsWithDuplicatedRoots(Chains);
	}
	else
	{
		RemoveDuplicatedChains(Chains);
	}

	// Sort all chains based on the search criteria
	if (!(SearchMode & EReferenceChainSearchMode::Longest))
	{
		// Sort from the shortest to the longest chain
		Chains.Sort([](const FReferenceChain& LHS, const FReferenceChain& RHS) { return LHS.Num() < RHS.Num(); });
	}
	else
	{
		// Sort from the longest to the shortest chain
		Chains.Sort([](const FReferenceChain& LHS, const FReferenceChain& RHS) { return LHS.Num() > RHS.Num(); });
	}

	// Finally, fill extended reference info for the remaining chains
	for (FReferenceChain* Chain : Chains)
	{
		Chain->FillReferenceInfo();
	}
}

void FReferenceChainSearch::BuildReferenceChainsForDirectReferences(FGraphNode* TargetNode, TArray<FReferenceChain*>& AllChains, EReferenceChainSearchMode SearchMode)
{
	for (FGraphNode* ReferencedByNode : TargetNode->ReferencedByObjects)
	{
		if (!(SearchMode & EReferenceChainSearchMode::ExternalOnly) || !ReferencedByNode->ObjectInfo->IsIn(TargetNode->ObjectInfo))
		{
			FReferenceChain* Chain = new FReferenceChain();
			Chain->AddNode(TargetNode);
			Chain->AddNode(ReferencedByNode);
			Chain->FillReferenceInfo();
			AllChains.Add(Chain);
		}
	}
}

FString FReferenceChainSearch::GetObjectFlags(FGCObjectInfo* InObject)
{
	FString Flags;
	if (InObject->IsRooted())
	{
		Flags += TEXT("(root) ");
	}

	CA_SUPPRESS(6011)
	if (InObject->IsNative())
	{
		Flags += TEXT("(native) ");
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (InObject->HasAnyInternalFlags(EInternalObjectFlags::PendingKill))
	{
		Flags += TEXT("(PendingKill) ");
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (InObject->HasAnyInternalFlags(EInternalObjectFlags::Garbage))
	{
		Flags += TEXT("(Garbage) ");
	}

	if (InObject->HasAnyFlags(RF_Standalone))
	{
		Flags += TEXT("(standalone) ");
	}

	if (InObject->HasAnyInternalFlags(EInternalObjectFlags::Async))
	{
		Flags += TEXT("(async) ");
	}

	if (InObject->HasAnyInternalFlags(EInternalObjectFlags::AsyncLoading))
	{
		Flags += TEXT("(asyncloading) ");
	}

	if (InObject->IsDisregardForGC())
	{
		Flags += TEXT("(NeverGCed) ");
	}

	if (InObject->HasAnyInternalFlags(EInternalObjectFlags::ClusterRoot))
	{
		Flags += TEXT("(ClusterRoot) ");
	}
	if (InObject->GetOwnerIndex() > 0)
	{
		Flags += TEXT("(Clustered) ");
	}
	return Flags;
}

static void ConvertStackFramesToCallstack(uint64* StackFrames, int32 NumStackFrames, int32 Indent, FOutputDevice& Out)
{
	// Convert the stack trace to text
	for (int32 Idx = 0; Idx < NumStackFrames; Idx++)
	{
		ANSICHAR Buffer[1024];
		Buffer[0] = '\0';
		FPlatformStackWalk::ProgramCounterToHumanReadableString(Idx, StackFrames[Idx], Buffer, sizeof(Buffer));

		if (FCStringAnsi::Strstr(Buffer, "TFastReferenceCollector") != nullptr)
		{
			break;
		}

		if (FCStringAnsi::Strstr(Buffer, "FWindowsPlatformStackWalk") == nullptr &&
			FCStringAnsi::Strstr(Buffer, "FDirectReferenceProcessor") == nullptr)
		{
			ANSICHAR* TrimmedBuffer = FCStringAnsi::Strstr(Buffer, "!");
			if (!TrimmedBuffer)
			{
				TrimmedBuffer = Buffer;
			}
			else
			{
				TrimmedBuffer++;
			}

			Out.Logf(ELogVerbosity::Log, TEXT("%s   ^ %s"), FCString::Spc(Indent), *FString(TrimmedBuffer));
		}
	}
}

void FReferenceChainSearch::DumpChain(FReferenceChainSearch::FReferenceChain* Chain, TFunctionRef<bool(FCallbackParams& Params)> ReferenceCallback, FOutputDevice& Out)
{
	if (Chain->Num())
	{
		bool bPostCallbackContinue = true;
		const int32 RootIndex = Chain->Num() - 1;
		FNodeReferenceInfo ReferenceInfo = Chain->GetReferenceInfo(RootIndex);
		FGCObjectInfo* ReferencerObject = Chain->GetNode(RootIndex)->ObjectInfo;
		{
			FCallbackParams Params;
			Params.Referencer = nullptr;
			Params.Object = ReferencerObject;
			Params.ReferenceInfo = nullptr;
			Params.Indent = FMath::Min<int32>(TCStringSpcHelper<TCHAR>::MAX_SPACES, Chain->Num() - RootIndex);
			Params.Out = &Out;

			Out.Logf(ELogVerbosity::Log, TEXT("%s%s %s"),
				FCString::Spc(Params.Indent),
				*GetObjectFlags(ReferencerObject),
				*ReferencerObject->GetFullName());

			bPostCallbackContinue = ReferenceCallback(Params);
		}

		// Roots are at the end so iterate from the last to the first node
		for (int32 NodeIndex = RootIndex - 1; NodeIndex >= 0 && bPostCallbackContinue; --NodeIndex)
		{
			FGCObjectInfo* Object = Chain->GetNode(NodeIndex)->ObjectInfo;

			FCallbackParams Params;
			Params.Referencer = ReferencerObject;
			Params.Object = Object;
			Params.ReferenceInfo = &ReferenceInfo;
			Params.Indent = FMath::Min<int32>(TCStringSpcHelper<TCHAR>::MAX_SPACES, Chain->Num() - NodeIndex - 1);
			Params.Out = &Out;

			if (ReferenceInfo.Type == EReferenceType::Property)
			{
				FString ReferencingPropertyName;
				UClass* ReferencerClass = Cast<UClass>(ReferencerObject->GetClass()->TryResolveObject());
				TArray<FProperty*> ReferencingProperties;

				if (ReferencerClass && FGCStackSizeHelper::ConvertPathToProperties(ReferencerClass, ReferenceInfo.ReferencerName, ReferencingProperties))
				{
					FProperty* InnermostProperty = ReferencingProperties.Last();
					FProperty* OutermostProperty = ReferencingProperties[0];

					ReferencingPropertyName = FString::Printf(TEXT("%s %s%s::%s"),
						*InnermostProperty->GetCPPType(),
						OutermostProperty->GetOwnerClass()->GetPrefixCPP(),
						*OutermostProperty->GetOwnerClass()->GetName(),
						*ReferenceInfo.ReferencerName.ToString());
				}
				else
				{
					// Handle base UObject referencer info (it's only exposed to the GC token stream and not to the reflection system)
					static const FName ClassPropertyName(TEXT("Class"));
					static const FName OuterPropertyName(TEXT("Outer"));
					
					FString ClassName;
					if (ReferenceInfo.ReferencerName == ClassPropertyName || ReferenceInfo.ReferencerName == OuterPropertyName)
					{
						ClassName = TEXT("UObject");
					}
					else if (ReferencerClass)
					{
						// Use the native class name when possible
						ClassName = ReferencerClass->GetPrefixCPP();
						ClassName += ReferencerClass->GetName();
					}
					else
					{
						// Revert to the internal class name if not
						ClassName = ReferencerObject->GetClassName();
					}
					ReferencingPropertyName = FString::Printf(TEXT("UObject* %s::%s"), *ClassName, *ReferenceInfo.ReferencerName.ToString());
				}

				Out.Logf(ELogVerbosity::Log, TEXT("%s-> %s = %s %s"),
					FCString::Spc(Params.Indent),
					*ReferencingPropertyName,
					*GetObjectFlags(Object),
					*Object->GetFullName());
			}
			else if (ReferenceInfo.Type == EReferenceType::AddReferencedObjects)
			{
				FString UObjectOrGCObjectName;
				if (ReferenceInfo.ReferencerName.IsNone())
				{
					UClass* ReferencerClass = Cast<UClass>(ReferencerObject->GetClass()->TryResolveObject());
					if (ReferencerClass)
					{
						UObjectOrGCObjectName = ReferencerClass->GetPrefixCPP();
						UObjectOrGCObjectName += ReferencerClass->GetName();
					}
					else
					{
						UObjectOrGCObjectName += ReferencerObject->GetClassName();
					}
				}
				else
				{
					UObjectOrGCObjectName = ReferenceInfo.ReferencerName.ToString();
				}

				Out.Logf(ELogVerbosity::Log, TEXT("%s-> %s::AddReferencedObjects(%s %s)"),
					FCString::Spc(Params.Indent),
					*UObjectOrGCObjectName,
					*GetObjectFlags(Object),
					*Object->GetFullName());

				if (ReferenceInfo.NumStackFrames)
				{
					ConvertStackFramesToCallstack(ReferenceInfo.StackFrames, ReferenceInfo.NumStackFrames, Params.Indent, Out);
				}
			}

			bPostCallbackContinue = ReferenceCallback(Params);

			ReferencerObject = Object;
			ReferenceInfo = Chain->GetReferenceInfo(NodeIndex);			
		}
		Out.Logf(ELogVerbosity::Log, TEXT("  "));
	}
}

void FReferenceChainSearch::FReferenceChain::FillReferenceInfo()
{
	// The first entry is the object we were looking for references to so add an empty entry for it
	ReferenceInfos.Add(FNodeReferenceInfo());

	// Iterate over all nodes and add reference info based on the next node (which is the object that referenced the current node)
	for (int32 NodeIndex = 1; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		FGraphNode* PreviousNode = Nodes[NodeIndex - 1];
		FGraphNode* CurrentNode = Nodes[NodeIndex];

		// Found the PreviousNode in the list of objects referenced by the CurrentNode
		FNodeReferenceInfo* FoundInfo = nullptr;
		for (FNodeReferenceInfo& Info : CurrentNode->ReferencedObjects)
		{
			if (Info.Object == PreviousNode)
			{
				FoundInfo = &Info;
				break;
			}
		}
		check(FoundInfo); // because there must have been a reference since we created this chain using it
		check(FoundInfo->Object == PreviousNode);
		ReferenceInfos.Add(*FoundInfo);
	}
	check(ReferenceInfos.Num() == Nodes.Num());
}

bool FReferenceChainSearch::FReferenceChain::IsExternal() const
{
	if (Nodes.Num() > 1)
	{
		// Reference is external if the root (the last node) is not in the first node (target)
		return !Nodes.Last()->ObjectInfo->IsIn(Nodes[0]->ObjectInfo);
	}
	else
	{
		return false;
	}
}

/**
* Handles UObject references found by TFastReferenceCollector
*/
class FDirectReferenceProcessor : public FSimpleReferenceProcessorBase
{	
	UObject* ObjectToFindReferencesTo;
	TSet<FReferenceChainSearch::FObjectReferenceInfo>& ReferencedObjects;
	TMap<UObject*, FGCObjectInfo*>& ObjectToInfoMap;

public:

	FDirectReferenceProcessor(UObject* InObjectToFindReferencesTo, TSet<FReferenceChainSearch::FObjectReferenceInfo>& InReferencedObjects, TMap<UObject*, FGCObjectInfo*>& InObjectToInfoMap)
		: ObjectToFindReferencesTo(InObjectToFindReferencesTo)
		, ReferencedObjects(InReferencedObjects)
		, ObjectToInfoMap(InObjectToInfoMap)
	{
	}

	FORCEINLINE void HandleTokenStreamObjectReference(FGCArrayStruct& ObjectsToSerializeStruct, UObject* ReferencingObject, UObject*& Object, const int32 TokenIndex, const EGCTokenType TokenType, bool bAllowReferenceElimination)
	{		
		if (Object)
		{
			FGCObjectInfo* ObjectInfo = FGCObjectInfo::FindOrAddInfoHelper(Object, ObjectToInfoMap);

			FReferenceChainSearch::FObjectReferenceInfo RefInfo(ObjectInfo);
			if (!ReferencedObjects.Contains(RefInfo))
			{
				if (TokenIndex >= 0)
				{
					FTokenInfo TokenInfo = ReferencingObject->GetClass()->ReferenceTokenStream.GetTokenInfo(TokenIndex);
					RefInfo.ReferencerName = TokenInfo.Name;
					RefInfo.Type = FReferenceChainSearch::EReferenceType::Property;
				}
				else
				{
					RefInfo.Type = FReferenceChainSearch::EReferenceType::AddReferencedObjects;
					RefInfo.NumStackFrames = FPlatformStackWalk::CaptureStackBackTrace(RefInfo.StackFrames, FReferenceChainSearch::FObjectReferenceInfo::MaxStackFrames);

					if (FGCObject::GGCObjectReferencer && (!ReferencingObject || ReferencingObject == FGCObject::GGCObjectReferencer))
					{
						FString RefName;
						if (FGCObject::GGCObjectReferencer->GetReferencerName(Object, RefName, true))
						{
							RefInfo.ReferencerName = FName(*RefName);
						}
						else if (ReferencingObject)
						{
							RefInfo.ReferencerName = *ReferencingObject->GetFullName();
						}
					}
					else if (ReferencingObject)
					{
						RefInfo.ReferencerName = *ReferencingObject->GetFullName();
					}
				}

				ReferencedObjects.Add(RefInfo);
			}
		}
	}
};

class FDirectReferenceCollector : public TDefaultReferenceCollector<FDirectReferenceProcessor>
{
public:
	FDirectReferenceCollector(FDirectReferenceProcessor& InProcessor, FGCArrayStruct& InObjectArrayStruct)
		: TDefaultReferenceCollector<FDirectReferenceProcessor>(InProcessor, InObjectArrayStruct)
	{
	}

	virtual bool MarkWeakObjectReferenceForClearing(UObject** WeakReference) override
	{
		// To avoid false positives we need to implement this method just like GC does
		// as these references will be treated as weak and should not be reported
		return true;
	}
};

FReferenceChainSearch::FReferenceChainSearch(UObject* InObjectToFindReferencesTo, EReferenceChainSearchMode Mode /*= EReferenceChainSearchMode::PrintResults*/)
	: ObjectToFindReferencesTo(InObjectToFindReferencesTo)
	, SearchMode(Mode)
{
	FSlowHeartBeatScope DisableHangDetection; // This function can be very slow

	check(ObjectToFindReferencesTo);
	ObjectInfoToFindReferencesTo = FGCObjectInfo::FindOrAddInfoHelper(ObjectToFindReferencesTo, ObjectToInfoMap);

	// First pass is to find all direct references for each object
	FindDirectReferencesForObjects();

	// Second pass creates all reference chains
	PerformSearch();

	if (!!(Mode & (EReferenceChainSearchMode::PrintResults|EReferenceChainSearchMode::PrintAllResults)))
	{
		PrintResults(!!(Mode & EReferenceChainSearchMode::PrintAllResults));
	}
}

FReferenceChainSearch::FReferenceChainSearch(EReferenceChainSearchMode Mode)
	: SearchMode(Mode)
{
}

FReferenceChainSearch::~FReferenceChainSearch()
{
	Cleanup();
}

void FReferenceChainSearch::PerformSearch()
{
	FGraphNode* ObjectNodeToFindReferencesTo = FindOrAddNode(ObjectToFindReferencesTo);
	check(ObjectNodeToFindReferencesTo);

	// Now it's time to build the reference chain from all of the objects that reference the object to find references to
	if (!(SearchMode & EReferenceChainSearchMode::Direct))
	{		
		BuildReferenceChains(ObjectNodeToFindReferencesTo, ReferenceChains, SearchMode);
	}
	else
	{
		BuildReferenceChainsForDirectReferences(ObjectNodeToFindReferencesTo, ReferenceChains, SearchMode);
	}
}

#if ENABLE_GC_HISTORY
void FReferenceChainSearch::PerformSearchFromGCSnapshot(UObject* InObjectToFindReferencesTo, FGCSnapshot& InSnapshot)
{
	FSlowHeartBeatScope DisableHangDetection; // This function can be very slow

	Cleanup();

	// Temporarily move the generated object info structs. We don't want to copy everything to minimize memory usage and save a few ms
	ObjectToInfoMap = MoveTemp(InSnapshot.ObjectToInfoMap);

	ObjectToFindReferencesTo = InObjectToFindReferencesTo;
	ObjectInfoToFindReferencesTo = FGCObjectInfo::FindOrAddInfoHelper(ObjectToFindReferencesTo, ObjectToInfoMap);
	FGCObjectInfo* GCObjectReferencerInfo = FGCObjectInfo::FindOrAddInfoHelper(FGCObject::GGCObjectReferencer, ObjectToInfoMap);

	// We can avoid copying object infos but we need to regenerate direct reference infos
	for (TPair<FGCObjectInfo*, TArray<FGCDirectReferenceInfo>*>& DirectReferencesInfo : InSnapshot.DirectReferences)
	{
		FGraphNode* ObjectNode = FindOrAddNode(DirectReferencesInfo.Key);
		for (FGCDirectReferenceInfo& ReferenceInfo : *DirectReferencesInfo.Value)
		{
			FGraphNode* ReferencedObjectNode = FindOrAddNode(ReferenceInfo.ReferencedObjectInfo);
			EReferenceType ReferenceType = EReferenceType::Unknown;
			if (GCObjectReferencerInfo == DirectReferencesInfo.Key || ReferenceInfo.ReferencerName == NAME_None)
			{
				ReferenceType = EReferenceType::AddReferencedObjects;
			}
			else
			{
				ReferenceType = EReferenceType::Property;
			}
			ObjectNode->ReferencedObjects.Add(FNodeReferenceInfo(ReferencedObjectNode, ReferenceType, ReferenceInfo.ReferencerName, nullptr, 0));
			ReferencedObjectNode->ReferencedByObjects.Add(ObjectNode);
		}
	}

	// Second pass creates all reference chains
	PerformSearch();

	if (!!(SearchMode & (EReferenceChainSearchMode::PrintResults | EReferenceChainSearchMode::PrintAllResults)))
	{
		PrintResults(!!(SearchMode & EReferenceChainSearchMode::PrintAllResults));
	}

	// Return the object info struct back to the snapshot
	InSnapshot.ObjectToInfoMap = MoveTemp(ObjectToInfoMap);
}
#endif // ENABLE_GC_HISTORY

void FReferenceChainSearch::FindDirectReferencesForObjects()
{
	TSet<FObjectReferenceInfo> ReferencedObjects;
	FDirectReferenceProcessor Processor(ObjectToFindReferencesTo, ReferencedObjects, ObjectToInfoMap);
	TFastReferenceCollector<
		FDirectReferenceProcessor, 
		FDirectReferenceCollector, 
		FGCArrayPool, 
		EFastReferenceCollectorOptions::AutogenerateTokenStream | EFastReferenceCollectorOptions::ProcessNoOpTokens
	> ReferenceCollector(Processor, FGCArrayPool::Get());
	FGCArrayStruct ArrayStruct;
	TArray<UObject*>& ObjectsToProcess = ArrayStruct.ObjectsToSerialize;

	for (FRawObjectIterator It; It; ++It)
	{
		FUObjectItem* ObjItem = *It;
		UObject* Object = static_cast<UObject*>(ObjItem->Object);
		FGraphNode* ObjectNode = FindOrAddNode(Object);

		// Find direct references
		ReferencedObjects.Reset();
		ObjectsToProcess.Reset();
		ObjectsToProcess.Add(Object);
		ReferenceCollector.CollectReferences(ArrayStruct);

		// Build direct reference tree
		for (FObjectReferenceInfo& ReferenceInfo : ReferencedObjects)
		{
			FGraphNode* ReferencedObjectNode = FindOrAddNode(ReferenceInfo.Object);
			ObjectNode->ReferencedObjects.Add(FNodeReferenceInfo(ReferencedObjectNode, ReferenceInfo.Type, ReferenceInfo.ReferencerName, ReferenceInfo.StackFrames, ReferenceInfo.NumStackFrames));
			ReferencedObjectNode->ReferencedByObjects.Add(ObjectNode);
		}
	}
}

void FReferenceChainSearch::PrintResults(bool bDumpAllChains /*= false*/) const
{
	PrintResults([](FCallbackParams& Params) { return true; }, bDumpAllChains);
}

void FReferenceChainSearch::PrintResults(TFunctionRef<bool(FCallbackParams& Params)> ReferenceCallback, bool bDumpAllChains /*= false*/) const
{
	if (ReferenceChains.Num())
	{
		FSlowHeartBeatScope DisableHangDetection; // This function can be very slow

		const int32 MaxChainsToPrint = 100;
		int32 NumPrintedChains = 0;

		for (FReferenceChain* Chain : ReferenceChains)
		{
			if (bDumpAllChains || NumPrintedChains < MaxChainsToPrint)
			{
				DumpChain(Chain, ReferenceCallback, *GLog);
				NumPrintedChains++;
			}
			else
			{
				UE_LOG(LogReferenceChain, Log, TEXT("Referenced by %d more reference chain(s)."), ReferenceChains.Num() - NumPrintedChains);
				break;
			}
		}
	}
	else
	{
		check(ObjectInfoToFindReferencesTo);
		UE_LOG(LogReferenceChain, Log, TEXT("%s%s is not currently reachable."),
			*GetObjectFlags(ObjectInfoToFindReferencesTo),
			*ObjectInfoToFindReferencesTo->GetFullName()
		);
	}
}

FString FReferenceChainSearch::GetRootPath() const
{
	return GetRootPath([](FCallbackParams& Params) { return true; });
}

FString FReferenceChainSearch::GetRootPath(TFunctionRef<bool(FCallbackParams& Params)> ReferenceCallback) const
{
	if (ReferenceChains.Num())
	{
		FReferenceChain* Chain = ReferenceChains[0];
		FStringOutputDevice OutString;
		OutString.SetAutoEmitLineTerminator(true);
		DumpChain(Chain, ReferenceCallback, OutString);
		return MoveTemp(OutString);
	}
	else
	{
		return FString::Printf(TEXT("%s%s is not currently reachable."),
			*GetObjectFlags(ObjectInfoToFindReferencesTo),
			*ObjectInfoToFindReferencesTo->GetFullName()
		);
	}
}

void FReferenceChainSearch::Cleanup()
{
	for (int32 ChainIndex = 0; ChainIndex < ReferenceChains.Num(); ++ChainIndex)
	{
		delete ReferenceChains[ChainIndex];
	}
	ReferenceChains.Empty();

	for (TPair<FGCObjectInfo*, FGraphNode*>& ObjectNodePair : AllNodes)
	{
		delete ObjectNodePair.Value;
	}
	AllNodes.Empty();

	for (TPair<UObject*, FGCObjectInfo*>& ObjectToInfoPair : ObjectToInfoMap)
	{
		delete ObjectToInfoPair.Value;
	}
	ObjectToInfoMap.Empty();
}
