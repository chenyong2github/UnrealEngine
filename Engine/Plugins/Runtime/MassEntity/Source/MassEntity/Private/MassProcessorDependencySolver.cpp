// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassProcessorDependencySolver.h"
#include "MassProcessingTypes.h"
#include "MassProcessor.h"
#include "Logging/MessageLog.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "Mass"

//----------------------------------------------------------------------//
//  FProcessorDependencySolver::FNode
//----------------------------------------------------------------------//
int32 FProcessorDependencySolver::FNode::FindOrAddGroupNodeIndex(const FString& GroupName)
{
	const FName GroupNodeName = FName(*GroupName);
	const int32* GroupNodeIndex = Indices.Find(GroupNodeName);
	if (GroupNodeIndex)
	{
		return *GroupNodeIndex;
	}

	const int32 Index = SubNodes.Add({ GroupNodeName, nullptr });
	Indices.Add(GroupNodeName, Index);
	return Index;
}

int32 FProcessorDependencySolver::FNode::FindNodeIndex(FName InNodeName) const
{
	const int32* GroupNodeIndex = Indices.Find(InNodeName);
	return GroupNodeIndex ? *GroupNodeIndex : INDEX_NONE;
}

bool FProcessorDependencySolver::FNode::HasDependencies() const
{
	return ExecuteBefore.Num() > 0 || ExecuteAfter.Num() > 0;
}

//----------------------------------------------------------------------//
//  FProcessorDependencySolver::FResourceUsage
//----------------------------------------------------------------------//
FProcessorDependencySolver::FResourceUsage::FResourceUsage()
{
	for (int i = 0; i < EMassAccessOperation::MAX; ++i)
	{
		FragmentsAccess[i].Access.AddZeroed(FMassFragmentBitSet::GetMaxNum());
		ChunkFragmentsAccess[i].Access.AddZeroed(FMassChunkFragmentBitSet::GetMaxNum());
		SharedFragmentsAccess[i].Access.AddZeroed(FMassSharedFragmentBitSet::GetMaxNum());
		RequiredSubsystemsAccess[i].Access.AddZeroed(FMassExternalSubystemBitSet::GetMaxNum());
	}
}

template<typename TBitSet>
void FProcessorDependencySolver::FResourceUsage::HandleElementType(TMassExecutionAccess<FResourceAccess>& ElementAccess
	, const TMassExecutionAccess<TBitSet>& TestedRequirements, FProcessorDependencySolver::FNode& InOutNode, const int32 NodeIndex)
{
	// for every bit set in TestedRequirements we do the following
	// 1. For every read only requirement we make InOutNode depend on the currently stored Writer of this resource
	//    - note that this operation is not destructive, meaning we don't destructively consume the data, since all 
	//      subsequent read access to the given resource will also depend on the Writer
	// 2. For every read-write requirement we make InOutNode depend on all the readers and writers currently stored. 
	//    - once that's done we clean currently stored Readers and Writers since every subsequent operation on this 
	//      resource will be blocked by currently considered InOutNode (as the new Writer)
	// 3. For all accessed resources we store information that InOutNode is accessing it

	// 1. For every read only requirement we make InOutNode depend on the currently stored Writer of this resource
	for (auto It = TestedRequirements.Read.GetIndexIterator(); It; ++It)
	{
		InOutNode.OriginalDependencies.Append(ElementAccess.Write.Access[*It].Users);
	}

	// 2. For every read-write requirement we make InOutNode depend on all the readers and writers currently stored. 
	for (auto It = TestedRequirements.Write.GetIndexIterator(); It; ++It)
	{
		InOutNode.OriginalDependencies.Append(ElementAccess.Read.Access[*It].Users);
		ElementAccess.Read.Access[*It].Users.Reset();

		// with the algorithm described above we expect there to only ever be at most one Writer
		ensure(ElementAccess.Write.Access[*It].Users.Num() <= 1);
		InOutNode.OriginalDependencies.Append(ElementAccess.Write.Access[*It].Users);
		ElementAccess.Write.Access[*It].Users.Reset();
	}

	// 3. For all accessed resources we store information that InOutNode is accessing it
	for (auto It = TestedRequirements.Read.GetIndexIterator(); It; ++It)
	{
		// mark Element at index indicated by It as being used in mode EMassAccessOperation(i) by NodeIndex
		ElementAccess.Read.Access[*It].Users.Add(NodeIndex);
	}
	for (auto It = TestedRequirements.Write.GetIndexIterator(); It; ++It)
	{
		// mark Element at index indicated by It as being used in mode EMassAccessOperation(i) by NodeIndex
		ElementAccess.Write.Access[*It].Users.Add(NodeIndex);
	}
}

template<typename TBitSet>
bool FProcessorDependencySolver::FResourceUsage::CanAccess(const TMassExecutionAccess<TBitSet>& StoredElements, const TMassExecutionAccess<TBitSet>& TestedElements)
{
	// see if there's an overlap of tested write operations with existing read & write operations, as well as 
	// tested read operations with existing write operations
	
	return !(
		// if someone's already writing to what I want to write
		TestedElements.Write.HasAny(StoredElements.Write)
		// or if someone's already reading what I want to write
		|| TestedElements.Write.HasAny(StoredElements.Read)
		// or if someone's already writing what I want to read
		|| TestedElements.Read.HasAny(StoredElements.Write)
	);
}

bool FProcessorDependencySolver::FResourceUsage::CanAccessRequirements(const FMassExecutionRequirements& TestedRequirements) const
{
	bool bCanAccess = CanAccess<FMassFragmentBitSet>(Requirements.Fragments, TestedRequirements.Fragments)
		&& CanAccess<FMassChunkFragmentBitSet>(Requirements.ChunkFragments, TestedRequirements.ChunkFragments)
		&& CanAccess<FMassSharedFragmentBitSet>(Requirements.SharedFragments, TestedRequirements.SharedFragments)
		&& CanAccess<FMassExternalSubystemBitSet>(Requirements.RequiredSubsystems, TestedRequirements.RequiredSubsystems);

	return bCanAccess;
}

void FProcessorDependencySolver::FResourceUsage::SubmitNode(const int32 NodeIndex, FNode& InOutNode)
{
	HandleElementType<FMassFragmentBitSet>(FragmentsAccess, InOutNode.Requirements.Fragments, InOutNode, NodeIndex);
	HandleElementType<FMassChunkFragmentBitSet>(ChunkFragmentsAccess, InOutNode.Requirements.ChunkFragments, InOutNode, NodeIndex);
	HandleElementType<FMassSharedFragmentBitSet>(SharedFragmentsAccess, InOutNode.Requirements.SharedFragments, InOutNode, NodeIndex);
	HandleElementType<FMassExternalSubystemBitSet>(RequiredSubsystemsAccess, InOutNode.Requirements.RequiredSubsystems, InOutNode, NodeIndex);

	Requirements += InOutNode.Requirements;
}

//----------------------------------------------------------------------//
//  FProcessorDependencySolver
//----------------------------------------------------------------------//
FProcessorDependencySolver::FProcessorDependencySolver(TArrayView<UMassProcessor*> InProcessors, const FName Name, const FString& InDependencyGraphFileName /*= FString()*/)
	: Processors(InProcessors)
	, GroupRootNode({Name, nullptr})
	, DependencyGraphFileName(InDependencyGraphFileName)
{}

FString FProcessorDependencySolver::NameViewToString(TConstArrayView<FName> View)
{
	if (View.Num() == 0)
	{
		return TEXT("[]");
	}
	FString ReturnVal = FString::Printf(TEXT("[%s"), *View[0].ToString());
	for (int i = 1; i < View.Num(); ++i)
	{
		ReturnVal += FString::Printf(TEXT(", %s"), *View[i].ToString());
	}
	return ReturnVal + TEXT("]");
}

bool FProcessorDependencySolver::PerformSolverStep(FResourceUsage& ResourceUsage, FNode& RootNode, TArray<int32>& InOutIndicesRemaining, TArray<int32>& OutNodeIndices)
{
	int32 AcceptedNodeIndex = INDEX_NONE;
	int32 FallbackAcceptedNodeIndex = INDEX_NONE;
	// note that InOutIndicesRemaining contains indices valid for RootNode.SubNodes
	// but that's ok, since in this step we only care about elements on this "level" (i.e. direct children of RootNode).
	for (int32 i = 0; i < InOutIndicesRemaining.Num(); ++i)
	{
		const int32 NodeIndex = InOutIndicesRemaining[i];
		if (RootNode.SubNodes[NodeIndex].TransientDependencies.Num() == 0)
		{
			if (ResourceUsage.CanAccessRequirements(RootNode.SubNodes[NodeIndex].Requirements))
			{
				AcceptedNodeIndex = NodeIndex;
				break;
			}
			else if (FallbackAcceptedNodeIndex == INDEX_NONE)
			{
				// if none of the nodes left can "cleanly" execute (i.e. without conflicting with already stored nodes)
				// we'll just pick this one up and go with it. 
				FallbackAcceptedNodeIndex = NodeIndex;
			}
		}
	}

	if (AcceptedNodeIndex != INDEX_NONE || FallbackAcceptedNodeIndex != INDEX_NONE)
	{
		const int32 NodeIndex = AcceptedNodeIndex != INDEX_NONE ? AcceptedNodeIndex : FallbackAcceptedNodeIndex;
		ResourceUsage.SubmitNode(NodeIndex, RootNode.SubNodes[NodeIndex]);
		InOutIndicesRemaining.RemoveSingle(NodeIndex);
		OutNodeIndices.Add(NodeIndex);

		for (const int32 RemainingNodeIndex : InOutIndicesRemaining)
		{
			RootNode.SubNodes[RemainingNodeIndex].TransientDependencies.RemoveSingleSwap(NodeIndex, /*bAllowShrinking=*/false);
		}
		
		return true;
	}

	return false;
}

void FProcessorDependencySolver::CreateSubGroupNames(FName InGroupName, TArray<FString>& SubGroupNames)
{
	// the function will convert composite group name into a series of progressively more precise group names
	// so "A.B.C" will result in ["A", "A.B", "A.B.C"]

	SubGroupNames.Reset();
	FString GroupNameAsString = InGroupName.ToString();
	FString TopGroupName;

	while (GroupNameAsString.Split(TEXT("."), &TopGroupName, &GroupNameAsString))
	{
		SubGroupNames.Add(TopGroupName);
	}
	SubGroupNames.Add(GroupNameAsString);
	
	for (int i = 1; i < SubGroupNames.Num(); ++i)
	{
		SubGroupNames[i] = FString::Printf(TEXT("%s.%s"), *SubGroupNames[i - 1], *SubGroupNames[i]);
	}
}

void FProcessorDependencySolver::AddNode(FName InGroupName, UMassProcessor& Processor)
{
	check(Processor.GetClass());
	const FName ProcName = Processor.GetClass()->GetFName();

	if (InGroupName.IsNone())
	{
		if (GroupRootNode.Indices.Find(ProcName) == nullptr)
		{
			GroupRootNode.Indices.Add(ProcName, GroupRootNode.SubNodes.Num());
			GroupRootNode.SubNodes.Add({ProcName, &Processor});
		}
		else
		{
			UE_LOG(LogMass, Warning, TEXT("%s Processor %s already registered. Duplicates are not supported.")
				, ANSI_TO_TCHAR(__FUNCTION__), *ProcName.ToString());
		}
	}
	else
	{
		// The idea here is that we create a tree-like structure to place the Processor as a leaf.
		// The levels of InGroupName are used to create tree successive branch nodes, so a group name A.B.C
		// will result in creating node A, then that node will have node A.B created for it and that one in 
		// turn will get A.B.C; Every node created gets information about the subnodes it hosts for faster 
		// lookup during dependency resolving

		// first we need to figure out all the subgroups indicated by InGroupName
		TArray<FString> AllGroupNames;
		CreateSubGroupNames(InGroupName, AllGroupNames);

		// now drill down into the successive levels of the free and find or create the necessary nodes. 
		FNode* CurrentNode = &GroupRootNode;
		for (int GroupNameIndex = 0; GroupNameIndex < AllGroupNames.Num(); ++GroupNameIndex)
		{
			const int32 NodeIndex = CurrentNode->FindOrAddGroupNodeIndex(AllGroupNames[GroupNameIndex]);
			CurrentNode->Indices.Add(ProcName, NodeIndex);
			// we make every node aware of all the subgroups it contains
			for (int SubgroupIndex = GroupNameIndex + 1; SubgroupIndex < AllGroupNames.Num(); ++SubgroupIndex)
			{
				CurrentNode->Indices.Add(*AllGroupNames[SubgroupIndex], NodeIndex);
			}

			CurrentNode = &CurrentNode->SubNodes[NodeIndex];
		}

		// at this point CurrentNode points at the leaf group where we need to add the processor itself
		if (CurrentNode->Indices.Find(ProcName) == nullptr)
		{
			const int32 ProcNodeIndex = CurrentNode->SubNodes.Add({ProcName, &Processor});
			CurrentNode->Indices.Add(ProcName, ProcNodeIndex);
		}
		else
		{
			UE_LOG(LogMass, Warning, TEXT("%s Processor %s already registered. Duplicates are not supported.")
				, ANSI_TO_TCHAR(__FUNCTION__), *ProcName.ToString());
		}
	}
}

void FProcessorDependencySolver::BuildDependencies(FNode& RootNode)
{
	// The main idea here is that for every subnode we convert its dependencies (expressed via ExecuteBefore and ExecuteAfter)
	// into a single dependency list by reversing the order of ExecuteBefore relationship - so if A.ExecuteBefore contains B then
	// we turn it into B.ExecuteAfter += A. The dependency names get converted to node indices. If given dependency name 
	// is unknown at the RootNode's level (i.e. it's not present in its subtree) we make that name a dependency of RootNode. 
	// Handling of this dependency will then be taken over by RootNode's parent.

	for (int32 NodeIndex = 0; NodeIndex < RootNode.SubNodes.Num(); ++NodeIndex)
	{
		FNode& Node = RootNode.SubNodes[NodeIndex];
		TConstArrayView<FName> ExecuteBefore;
		TConstArrayView<FName> ExecuteAfter;

		if (Node.Processor)
		{
			ExecuteBefore = Node.Processor->GetExecutionOrder().ExecuteBefore;
			ExecuteAfter = Node.Processor->GetExecutionOrder().ExecuteAfter;
			Node.Processor->ExportRequirements(Node.Requirements);
			RootNode.Requirements += Node.Requirements;
		}
		else
		{
			BuildDependencies(Node);
			RootNode.Requirements += Node.Requirements;
			ExecuteBefore = Node.ExecuteBefore;
			ExecuteAfter = Node.ExecuteAfter;
		}
		
		for (const FName& DependencyName : ExecuteBefore)
		{
			const int32 DependencyIndex = RootNode.FindNodeIndex(DependencyName);
			// RootNode doesn't know about DependencyName then add it to RootNode's dependencies
			if (DependencyIndex == INDEX_NONE)
			{
				RootNode.ExecuteBefore.AddUnique(DependencyName);
			}
			else
			{
				if (NodeIndex != DependencyIndex)
				{
					// just make DependencyIndex depend on Node
					RootNode.SubNodes[DependencyIndex].OriginalDependencies.AddUnique(NodeIndex);
				}
				else
				{
					UE_LOG(LogMass, Warning, TEXT("%s a node (%s) tried to add a circular dependency on itself")
						, ANSI_TO_TCHAR(__FUNCTION__), *Node.Name.ToString());
				}
			}
		}
		for (const FName& DependencyName : ExecuteAfter)
		{
			const int32 DependencyIndex = RootNode.FindNodeIndex(DependencyName);
			// RootNode doesn't know about DependencyName then add it to RootNode's dependencies
			if (DependencyIndex == INDEX_NONE)
			{
				RootNode.ExecuteAfter.AddUnique(DependencyName);
			}
			else 
			{
				if (NodeIndex != DependencyIndex)
				{
					// just make Node depend on DependencyIndex
					Node.OriginalDependencies.AddUnique(DependencyIndex);
				}
				else
				{
					UE_LOG(LogMass, Warning, TEXT("%s a node (%s) tried to add a circular dependency on itself")
						, ANSI_TO_TCHAR(__FUNCTION__), *Node.Name.ToString());
				}
			}
		}
	}
}

void FProcessorDependencySolver::LogNode(const FNode& RootNode, const FNode* ParentNode, int Indent)
{
	FString Dependencies;
	for (const int32 DependencyIndex : RootNode.OriginalDependencies)
	{
		if (ensure(ParentNode && ParentNode->SubNodes.IsValidIndex(DependencyIndex)))
		{
			Dependencies += ParentNode->SubNodes[DependencyIndex].Name.ToString() + TEXT(", ");
		}
	}

	UE_LOG(LogMass, Log, TEXT("%*s%s %s%s"), Indent, TEXT(""), *RootNode.Name.ToString()
		, Dependencies.Len() > 0 ? TEXT("after: ") : TEXT(""), *Dependencies);

	if (RootNode.Processor == nullptr)
	{
		UE_LOG(LogMass, Log, TEXT("%*s%s before:%s after:%s"), Indent, TEXT(""), *RootNode.Name.ToString()
			, *NameViewToString(RootNode.ExecuteBefore)
			, *NameViewToString(RootNode.ExecuteAfter));

		for (const FNode& Node : RootNode.SubNodes)
		{
			LogNode(Node, &RootNode, Indent + 4);
		}
	}
	else
	{
		UE_LOG(LogMass, Log, TEXT("%*s%s before:%s after:%s"), Indent, TEXT(""), *RootNode.Name.ToString()
			, *NameViewToString(RootNode.Processor->GetExecutionOrder().ExecuteBefore)
			, *NameViewToString(RootNode.Processor->GetExecutionOrder().ExecuteAfter));
	}
}

struct FDumpGraphDependencyUtils
{
	static void DumpGraphNode(FArchive& LogFile, const FProcessorDependencySolver::FNode& Node, int Indent, TSet<const FProcessorDependencySolver::FNode*>& AllNodes, const bool bRoot)
	{
		const FString NodeName = Node.Name.ToString();
		if (!Node.Processor)
		{
			const FString ClusterNodeName = NodeName.Replace(TEXT("."), TEXT("_"));
			const FString GraphNodeName = NodeName.Replace(TEXT("."), TEXT(" "));

			int32 Index = -1;
			Node.Name.ToString().FindLastChar(TEXT('.'), Index);
			const FString GroupName = NodeName.Mid(Index+1);

			LogFile.Logf(TEXT("%*ssubgraph cluster_%s"), Indent, TEXT(""), *ClusterNodeName);
			LogFile.Logf(TEXT("%*s{"), Indent, TEXT(""));
			LogFile.Logf(TEXT("%*slabel =\"%s\";"), Indent + 4, TEXT(""), *GroupName);
			LogFile.Logf(TEXT("%*s\"%s Start\"%s;"), Indent + 4, TEXT(""), *GraphNodeName, bRoot ? TEXT("") : TEXT("[shape=point style=invis]"));
			LogFile.Logf(TEXT("%*s\"%s End\"%s;"), Indent + 4, TEXT(""), *GraphNodeName, bRoot ? TEXT("") : TEXT("[shape=point style=invis]"));
			for (const FProcessorDependencySolver::FNode& SubNode : Node.SubNodes)
			{
				DumpGraphNode(LogFile, SubNode, Indent + 4, AllNodes, false);
			}
			LogFile.Logf(TEXT("%*s}"), Indent, TEXT(""));
		}
		else
		{
			LogFile.Logf(TEXT("%*s\"%s\""), Indent, TEXT(""), *NodeName);
			AllNodes.Add(&Node);
		}
	}

	static const FProcessorDependencySolver::FNode* FindDepNodeInParents(const TArray<const FProcessorDependencySolver::FNode*>& Parents, FName DependencyName)
	{
		for (int32 i = Parents.Num() - 1; i >= 0; --i)
		{
			const FProcessorDependencySolver::FNode* CurNode = Parents[i];
			int32 DependencyIndex = CurNode->FindNodeIndex(DependencyName);
			while(DependencyIndex != INDEX_NONE)
			{
				if(CurNode->SubNodes[DependencyIndex].Name == DependencyName)
				{
					return &CurNode->SubNodes[DependencyIndex];
				}
				else
				{
					// Dig down the chain
					CurNode = &CurNode->SubNodes[DependencyIndex];
					DependencyIndex = CurNode->FindNodeIndex(DependencyName);
				}
			}
		}
		return nullptr;
	};

	static bool DoAllSubNodeHasDependency(const FProcessorDependencySolver::FNode& Node, const FName DependencyName, bool bBeforeDep)
	{
		bool bDepExistInAllSibling = true;
		for (const FProcessorDependencySolver::FNode& SiblingSubNode : Node.SubNodes)
		{
			if (SiblingSubNode.Processor)
			{
				const TArray<FName>& ExecuteDep = bBeforeDep ? SiblingSubNode.Processor->GetExecutionOrder().ExecuteBefore : SiblingSubNode.Processor->GetExecutionOrder().ExecuteAfter;
				if (ExecuteDep.Find(DependencyName) == INDEX_NONE)
				{
					bDepExistInAllSibling = false;
					break;
				}
			}
			else if (!DoAllSubNodeHasDependency(SiblingSubNode, DependencyName, bBeforeDep))
			{
				bDepExistInAllSibling = false;
				break;
			}
		}
		return bDepExistInAllSibling;
	}

	static bool DoesDependencyExistIndirectly(const TArray<const FProcessorDependencySolver::FNode*>& Parents, const FProcessorDependencySolver::FNode& Node, const FName DepNameToFind, bool bBeforeDep)
	{
		check(Node.Processor);
		const TArray<FName>& ExecuteDep = bBeforeDep ? Node.Processor->GetExecutionOrder().ExecuteBefore : Node.Processor->GetExecutionOrder().ExecuteAfter;
		for (const FName& DependencyName : ExecuteDep)
		{
			if (DependencyName == DepNameToFind)
			{
				continue;
			}
			if (const FProcessorDependencySolver::FNode* DepNode = FindDepNodeInParents(Parents, DependencyName))
			{
				if(DepNode->Processor)
				{
					const TArray<FName>& ExecuteDepDep = bBeforeDep ? DepNode->Processor->GetExecutionOrder().ExecuteBefore : DepNode->Processor->GetExecutionOrder().ExecuteAfter;
					if(ExecuteDepDep.Find(DepNameToFind) != INDEX_NONE)
					{
						return true;
					}
				}
				else if(DoAllSubNodeHasDependency(*DepNode, DepNameToFind, bBeforeDep))
				{
					return true;
				}
			}
		}
		return false;
	}

	static void RemoveAllProcessorFromSet(const FProcessorDependencySolver::FNode& DepNode, TSet<const FProcessorDependencySolver::FNode*>& Set)
	{
		if (DepNode.Processor)
		{
			Set.Remove(&DepNode);
		}
		else
		{
			for (const FProcessorDependencySolver::FNode& SubNode : DepNode.SubNodes)
			{
				RemoveAllProcessorFromSet(SubNode, Set);
			}
		}
	}

	static bool AreAllProcessorInSet(const FProcessorDependencySolver::FNode& DepNode, TSet<const FProcessorDependencySolver::FNode*>& Set)
	{
		if (DepNode.Processor)
		{
			if (!Set.Contains(&DepNode))
			{
				return false;
			}
		}
		else
		{
			for (const FProcessorDependencySolver::FNode& SubNode : DepNode.SubNodes)
			{
				if (!AreAllProcessorInSet(SubNode, Set))
				{
					return false;
				}
			}
		}
		return true;
	}

	static void DumpGraphDependencies(FArchive& LogFile, const FProcessorDependencySolver::FNode& Node, TArray<const FProcessorDependencySolver::FNode*> Parents, TArray<const FProcessorDependencySolver::FNode*> CommonBeforeDep, TArray<const FProcessorDependencySolver::FNode*> CommonAfterDep, TSet<const FProcessorDependencySolver::FNode*>& DependsOnStart, TSet<const FProcessorDependencySolver::FNode*>& LinkToEnd)
	{
		const FString NodeName = Node.Name.ToString();
		const FProcessorDependencySolver::FNode* ParentNode = Parents.Num() > 0 ? Parents.Last() : nullptr;
		const FString ParentNodeName = ParentNode ? ParentNode->Name.ToString() : TEXT("");
		const FString ParentGraphNodeName = ParentNodeName.Replace(TEXT("."), TEXT(" "));
		const FString ParentClusterNodeName = ParentNodeName.Replace(TEXT("."), TEXT("_"));
		if (Node.Processor)
		{
			for (const FName& DependencyName : Node.Processor->GetExecutionOrder().ExecuteBefore)
			{
				if (const FProcessorDependencySolver::FNode* DepNode = FindDepNodeInParents(Parents, DependencyName))
				{
					LinkToEnd.Remove(&Node);
					RemoveAllProcessorFromSet(*DepNode, DependsOnStart);

					if (CommonBeforeDep.Find(DepNode) != INDEX_NONE)
					{
						continue;
					}
					if (DoesDependencyExistIndirectly(Parents, Node, DependencyName, true))
					{
						continue;
					}

					const FString DepNodeName = DepNode->Name.ToString();
					if (DepNode->Processor)
					{
						LogFile.Logf(TEXT("    \"%s\" -> \"%s\";"), *NodeName, *DepNodeName);
					}
					else
					{
						const FString DepClusterNodeName = DepNodeName.Replace(TEXT("."), TEXT("_"));
						const FString DepGraphNodeName = DepNodeName.Replace(TEXT("."), TEXT(" "));
						LogFile.Logf(TEXT("    \"%s\" -> \"%s Start\"[lhead=cluster_%s];"), *NodeName, *DepGraphNodeName, *DepClusterNodeName);
					}

				}
			}
			for (const FName& DependencyName : Node.Processor->GetExecutionOrder().ExecuteAfter)
			{
				if (const FProcessorDependencySolver::FNode* DepNode = FindDepNodeInParents(Parents, DependencyName))
				{
					DependsOnStart.Remove(&Node);
					RemoveAllProcessorFromSet(*DepNode, LinkToEnd);

					if (CommonAfterDep.Find(DepNode) != INDEX_NONE)
					{
						continue;
					}
					if (DoesDependencyExistIndirectly(Parents, Node, DependencyName, false))
					{
						continue;
					}

					const FString DepNodeName = DepNode->Name.ToString();
					if (DepNode->Processor)
					{
						LogFile.Logf(TEXT("    \"%s\" -> \"%s\";"), *DepNodeName, *NodeName);
					}
					else
					{
						const FString DepClusterNodeName = DepNodeName.Replace(TEXT("."), TEXT("_"));
						const FString DepGraphNodeName = DepNodeName.Replace(TEXT("."), TEXT(" "));
						LogFile.Logf(TEXT("    \"%s End\" -> \"%s\"[ltail=cluster_%s];"), *DepGraphNodeName, *NodeName, *DepClusterNodeName);
					}
				}
			}

			// Layouting
			LogFile.Logf(TEXT("    \"%s Start\" -> \"%s\" -> \"%s End\"[style=invis];"), *ParentGraphNodeName, *NodeName, *ParentGraphNodeName);
		}
		else if (ParentNode)
		{

			const FString ClusterNodeName = NodeName.Replace(TEXT("."), TEXT("_"));
			const FString GraphNodeName = NodeName.Replace(TEXT("."), TEXT(" "));

			// Find common dependency through out all sub nodes
 			TSet<FName> BeforeDepOuputed;
 			TSet<FName> AfterDepOutputed;
			for (const FProcessorDependencySolver::FNode& SubNode : Node.SubNodes)
			{
				if(SubNode.Processor)
				{
					for (const FName& DependencyName : SubNode.Processor->GetExecutionOrder().ExecuteBefore)
					{
						if (const FProcessorDependencySolver::FNode* DepNode = FindDepNodeInParents(Parents, DependencyName))
						{
							if (DoAllSubNodeHasDependency(Node, DependencyName, true /*bBeforeDep*/))
							{
								CommonBeforeDep.Add(DepNode);

								if (!DoesDependencyExistIndirectly(Parents, SubNode, DependencyName, true))
								{
									if (BeforeDepOuputed.Contains(DependencyName))
									{
										continue;
									}
									BeforeDepOuputed.Add(DependencyName);

									const FString DepNodeName = DepNode->Name.ToString();
									if (DepNode->Processor)
									{
										LogFile.Logf(TEXT("    \"%s End\" -> \"%s\"[ltail=cluster_%s];"), *GraphNodeName, *DepNodeName, *ClusterNodeName);
									}
									else
									{
										const FString DepClusterNodeName = DepNodeName.Replace(TEXT("."), TEXT("_"));
										const FString DepGraphNodeName = DepNodeName.Replace(TEXT("."), TEXT(" "));
										LogFile.Logf(TEXT("    \"%s End\" -> \"%s Start\"[ltail=cluster_%s, lhead=cluster_%s];"), *GraphNodeName, *DepGraphNodeName, *ClusterNodeName, *DepClusterNodeName);
									}
								}
							}
						}
					}

					for (const FName& DependencyName : SubNode.Processor->GetExecutionOrder().ExecuteAfter)
					{
						if (const FProcessorDependencySolver::FNode* DepNode = FindDepNodeInParents(Parents, DependencyName))
						{
							if (DoAllSubNodeHasDependency(Node, DependencyName, false /*bBeforeDep*/))
							{
								CommonAfterDep.Add(DepNode);

								if (!DoesDependencyExistIndirectly(Parents, SubNode, DependencyName, false))
								{
									if (AfterDepOutputed.Contains(DependencyName))
									{
										continue;
									}
									AfterDepOutputed.Add(DependencyName);

									const FString DepNodeName = DepNode->Name.ToString();
									if (DepNode->Processor)
									{
										LogFile.Logf(TEXT("    \"%s\" -> \"%s Start\"[lhead=cluster_%s];"), *DepNodeName, *GraphNodeName, *ClusterNodeName);
									}
									else
									{
										const FString DepClusterNodeName = DepNodeName.Replace(TEXT("."), TEXT("_"));
										const FString DepGraphNodeName = DepNodeName.Replace(TEXT("."), TEXT(" "));
										LogFile.Logf(TEXT("    \"%s End\" -> \"%s Start\"[ltail=cluster_%s, lhead=cluster_%s];"), *DepGraphNodeName,*GraphNodeName, *DepClusterNodeName, *ClusterNodeName);
									}
								}
							}
						}
					}
				}
			}

 			// Layouting
			LogFile.Logf(TEXT("    \"%s Start\" -> \"%s Start\" -> \"%s End\" -> \"%s End\"[style=invis];"), *ParentGraphNodeName, *GraphNodeName, *GraphNodeName, *ParentGraphNodeName);
		}

		Parents.Push(&Node);
		for (const FProcessorDependencySolver::FNode& SubNode : Node.SubNodes)
		{
			DumpGraphDependencies(LogFile, SubNode, Parents, CommonBeforeDep, CommonAfterDep, DependsOnStart, LinkToEnd);
		}
	}

	static void PromoteStartAndEndDependency(FArchive& LogFile, const FString& GraphName, const FProcessorDependencySolver::FNode& Node, TSet<const FProcessorDependencySolver::FNode*>& DependsOnStart, TSet<const FProcessorDependencySolver::FNode*>& LinkToEnd)
	{
		if (Node.Processor)
		{
			return;
		}

		bool bAllDependsOnStart = true;
		bool bAllLinkedToEnd = true;
		for (const FProcessorDependencySolver::FNode& SubNode : Node.SubNodes)
		{
			if (!AreAllProcessorInSet(SubNode, DependsOnStart))
			{
				bAllDependsOnStart = false;
			}

			if (!AreAllProcessorInSet(SubNode, LinkToEnd))
			{
				bAllLinkedToEnd = false;
			}

			if (!bAllDependsOnStart && !bAllLinkedToEnd)
			{
				break;
			}
		}

		const FString NodeName = Node.Name.ToString();
		const FString ClusterNodeName = NodeName.Replace(TEXT("."), TEXT("_"));
		const FString GraphNodeName = NodeName.Replace(TEXT("."), TEXT(" "));
		if (bAllDependsOnStart)
		{
			RemoveAllProcessorFromSet(Node, DependsOnStart);
			LogFile.Logf(TEXT("    \"%s Start\" -> \"%s Start\"[lhead=cluster_%s, weight=0];"), *GraphName, *GraphNodeName, *ClusterNodeName);
		}

		if (bAllLinkedToEnd)
		{
			RemoveAllProcessorFromSet(Node, LinkToEnd);
			LogFile.Logf(TEXT("    \"%s End\" -> \"%s End\"[ltail=cluster_%s, weight=0];"), *GraphNodeName, *GraphName, *ClusterNodeName);
		}

		if(!bAllDependsOnStart || !bAllLinkedToEnd)
		{
			for (const FProcessorDependencySolver::FNode& SubNode : Node.SubNodes)
			{
				PromoteStartAndEndDependency(LogFile, GraphName, SubNode, DependsOnStart, LinkToEnd);
			}
		}
	}

	static void DumpAllGraphDependencies(FArchive& LogFile, const FProcessorDependencySolver::FNode& GroupRootNode, const TSet<const FProcessorDependencySolver::FNode*>& AllNodes)
	{
		TSet<const FProcessorDependencySolver::FNode*> DependsOnStart = AllNodes;
		TSet<const FProcessorDependencySolver::FNode*> LinkToEnd = AllNodes;
		const FString GraphName = GroupRootNode.Name.ToString();
		FDumpGraphDependencyUtils::DumpGraphDependencies(LogFile, GroupRootNode, TArray<const FProcessorDependencySolver::FNode*>(), TArray<const FProcessorDependencySolver::FNode*>(), TArray<const FProcessorDependencySolver::FNode*>(), DependsOnStart, LinkToEnd);
		FDumpGraphDependencyUtils::PromoteStartAndEndDependency(LogFile, GraphName, GroupRootNode, DependsOnStart, LinkToEnd);
		for (const FProcessorDependencySolver::FNode* Node : DependsOnStart)
		{
			LogFile.Logf(TEXT("    \"%s Start\" -> \"%s\"[weight=0];"), *GraphName, *Node->Name.ToString());
		}
		for (const FProcessorDependencySolver::FNode* Node : LinkToEnd)
		{
			LogFile.Logf(TEXT("    \"%s\" -> \"%s End\"[weight=0];"), *Node->Name.ToString(), *GraphName);
		}
	}
};

void FProcessorDependencySolver::DumpGraph(FArchive& LogFile) const
{
	TSet<const FNode*> AllNodes;
	LogFile.Logf(TEXT("digraph MassProcessorGraph"));
	LogFile.Logf(TEXT("{"));
	LogFile.Logf(TEXT("    compound = true;"));
	LogFile.Logf(TEXT("    newrank = true;"));
	FDumpGraphDependencyUtils::DumpGraphNode(LogFile, GroupRootNode, 4/* Indent */, AllNodes, true/* bRoot */);
	FDumpGraphDependencyUtils::DumpAllGraphDependencies(LogFile, GroupRootNode, AllNodes);
	LogFile.Logf(TEXT("}"));
}

void FProcessorDependencySolver::Solve(FNode& RootNode, TConstArrayView<const FName> PriorityNodes, TArray<FProcessorDependencySolver::FOrderInfo>& OutResult, int LoggingIndent)
{
	if (RootNode.SubNodes.Num() == 0)
	{
		return;
	}

	for (FNode& SubNode : RootNode.SubNodes)
	{
		SubNode.TransientDependencies = SubNode.OriginalDependencies;
	}

	TArray<int32> IndicesRemaining;
	// populate with priority nodes and their dependencies first
	
	if (PriorityNodes.Num())
	{
		IndicesRemaining.Reserve(RootNode.SubNodes.Num());
		for (const FName PriorityNodeName : PriorityNodes)
		{
			// @todo complex names
			const int32* PriorityNodeIndex = RootNode.Indices.Find(PriorityNodeName);
			if (PriorityNodeIndex && RootNode.SubNodes.IsValidIndex(*PriorityNodeIndex)
				// remember that RootNode.Indices points to a whole group's index for elements from a given group
				&& RootNode.SubNodes[*PriorityNodeIndex].Name == PriorityNodeName)
			{ 
				const int32 StartIndex = IndicesRemaining.Add(*PriorityNodeIndex);
				// in this loop we'll be potentially adding elements to IndicesRemaining and the idea is to process
				// the values freshly added as well, so that we populate IndicesRemaining with all super-dependencies of 
				// the priority node currently processed
				for (int i = StartIndex; i < IndicesRemaining.Num(); ++i)
				{
					const int32 SubNodeIndex = IndicesRemaining[i];
					for (const int32 DependencyIndex : RootNode.SubNodes[SubNodeIndex].OriginalDependencies)
					{
						// some dependencies may have already been added to IndicesRemaining by higher-priority nodes
						// thus we're adding only unique items (IndicesRemaining is a 1-1 mapping of all RootNode's child nodes).
						IndicesRemaining.AddUnique(DependencyIndex);
					}
				}
			}
		}
	}

	if (IndicesRemaining.Num())
	{
		// some node indices already added, fill up with missing ones
		for (int32 i = 0; i < RootNode.SubNodes.Num(); ++i)
		{
			IndicesRemaining.AddUnique(i);
		}
	}
	else
	{
		IndicesRemaining.AddZeroed(RootNode.SubNodes.Num());
		// no priority nodes, just use add all indices in sequence
		// start from 1 since IndicesRemaining[0] = 0 already
		for (int32 i = 1; i < RootNode.SubNodes.Num(); ++i)
		{
			IndicesRemaining[i] = i;
		}
	}
	
	FResourceUsage ResourceUsage;
	
	TArray<int32> SortedNodeIndices;
	SortedNodeIndices.Reserve(RootNode.SubNodes.Num());

	while (IndicesRemaining.Num())
	{
		const bool bStepSuccessful = PerformSolverStep(ResourceUsage, RootNode, IndicesRemaining, SortedNodeIndices);

		if (bStepSuccessful == false)
		{
			bAnyCyclesDetected = true;

			UE_LOG(LogMass, Error, TEXT("Detected processing dependency cycle:"));
			for (const int32 Index : IndicesRemaining)
			{
				UMassProcessor* Processor = RootNode.SubNodes[Index].Processor;
				if (Processor)
				{
					UE_LOG(LogMass, Error, TEXT("\t%s, group: %s, before: %s, after %s")
						, *Processor->GetName()
						, *Processor->GetExecutionOrder().ExecuteInGroup.ToString()
						, *NameViewToString(Processor->GetExecutionOrder().ExecuteBefore)
						, *NameViewToString(Processor->GetExecutionOrder().ExecuteAfter));
				}
				else
				{
					// group
					UE_LOG(LogMass, Error, TEXT("\tGroup %s"), *RootNode.SubNodes[Index].Name.ToString());
				}
			}
			UE_LOG(LogMass, Error, TEXT("Cutting the chain at an arbitrary location."));

			// remove first dependency
			// note that if we're in a cycle handling scenario every node does have some dependencies left
			RootNode.SubNodes[IndicesRemaining[0]].TransientDependencies.Pop(/*bAllowShrinking=*/false);
		}
	}

	// now we have the desired order in SortedNodeIndices. We have to traverse it recursively to add to OutResult
	for (int i = 0; i < SortedNodeIndices.Num(); ++i)
	{
		const int32 NodeIndex = SortedNodeIndices[i];

		TArray<FName> DependencyNames;
		for (const int32 DependencyIndex : RootNode.SubNodes[NodeIndex].OriginalDependencies)
		{
			DependencyNames.AddUnique(RootNode.SubNodes[DependencyIndex].Name);
		}

		if (RootNode.SubNodes[NodeIndex].Processor != nullptr)
		{
			OutResult.Add({ RootNode.SubNodes[NodeIndex].Name, RootNode.SubNodes[NodeIndex].Processor, EDependencyNodeType::Processor, DependencyNames });
		}
		else if (RootNode.SubNodes[NodeIndex].SubNodes.Num() > 0)
		{
			OutResult.Add({ RootNode.SubNodes[NodeIndex].Name, RootNode.SubNodes[NodeIndex].Processor, EDependencyNodeType::GroupStart, DependencyNames });
			Solve(RootNode.SubNodes[NodeIndex], PriorityNodes, OutResult, LoggingIndent);
			DependencyNames.Reset(RootNode.SubNodes[NodeIndex].SubNodes.Num());
			for (const FNode& ChildNode : RootNode.SubNodes[NodeIndex].SubNodes)
			{
				DependencyNames.Add(ChildNode.Name);
			}
			OutResult.Add({ RootNode.SubNodes[NodeIndex].Name, RootNode.SubNodes[NodeIndex].Processor, EDependencyNodeType::GroupEnd, DependencyNames });
		}
	}
}

void FProcessorDependencySolver::ResolveDependencies(TArray<FProcessorDependencySolver::FOrderInfo>& OutResult, TConstArrayView<const FName> PriorityNodes)
{
	FScopedCategoryAndVerbosityOverride LogOverride(TEXT("LogMass"), ELogVerbosity::Log);

	bAnyCyclesDetected = false;

	UE_LOG(LogMass, Log, TEXT("Gathering dependencies data:"));

	// gather the processors information first
	for (UMassProcessor* Processor : Processors)
	{
		if (Processor == nullptr)
		{
			UE_LOG(LogMass, Warning, TEXT("%s nullptr found in Processors collection being processed"), ANSI_TO_TCHAR(__FUNCTION__));
			continue;
		}
		AddNode(Processor->GetExecutionOrder().ExecuteInGroup, *Processor);
	}

	BuildDependencies(GroupRootNode);
	// @todo anything in GroupRootNode.Dependencies is an undefined symbol

	// Any dependencies that are promoted to the root node means they are unresolved.
    const bool bAnyUnresolvedDependencies = GroupRootNode.HasDependencies();
	if (bAnyUnresolvedDependencies)
	{
		const UWorld* World = Processors.IsEmpty() ? nullptr : Processors[0]->GetWorld();
	    for (const FName& DependencyName : GroupRootNode.ExecuteBefore)
	    {
		    UE_LOG(LogMass, Log, TEXT("(%s) %s found an unresolved execute before dependency (%s)"),
		    	World != nullptr ? *ToString(World->GetNetMode()) : TEXT("unknown"), ANSI_TO_TCHAR(__FUNCTION__), *DependencyName.ToString());
	    }
    
	    for (const FName& DependencyName : GroupRootNode.ExecuteAfter)
	    {
		    UE_LOG(LogMass, Log, TEXT("(%s) %s found an unresolved execute after dependency (%s)"),
		    	World != nullptr ? *ToString(World->GetNetMode()) : TEXT("unknown"), ANSI_TO_TCHAR(__FUNCTION__), *DependencyName.ToString());
	    }
	}

	LogNode(GroupRootNode);
	
	if (!DependencyGraphFileName.IsEmpty())
	{
		const FString FileName = FString::Printf(TEXT("%s%s-%s.dot"), *FPaths::ProjectLogDir(), *DependencyGraphFileName, *FDateTime::Now().ToString());
		if (FArchive* LogFile = IFileManager::Get().CreateFileWriter(*FileName))
		{
			DumpGraph(*LogFile);

			LogFile->Close();
			delete LogFile;
		}
		else
		{
			UE_LOG(LogMass, Error, TEXT("%s Unable to dump dependency graph into filename %s"), ANSI_TO_TCHAR(__FUNCTION__), *DependencyGraphFileName);
		}
	}

	GroupRootNode.TransientDependencies = GroupRootNode.OriginalDependencies;
	Solve(GroupRootNode, PriorityNodes, OutResult);

#if WITH_UNREAL_DEVELOPER_TOOLS
	if (bAnyCyclesDetected || bAnyUnresolvedDependencies)
	{
		FMessageLog EditorErrors("LogMass");
		if(bAnyCyclesDetected)
		{
		EditorErrors.Error(LOCTEXT("ProcessorDependenciesCycle", "Processor dependencies cycle found!"));
		    EditorErrors.Notify(LOCTEXT("ProcessorDependenciesCycle", "Processor dependencies cycle found!"));
		}
		if(bAnyUnresolvedDependencies)
		{
		    EditorErrors.Info(LOCTEXT("ProcessorUnresolvedDependencies", "Unresolved processor dependencies found!"));
		    //EditorErrors.Notify(LOCTEXT("ProcessorUnresolvedDependencies", "Unresolved processor dependencies found!"));
		}
		EditorErrors.Info(FText::FromString(TEXT("See the log for details")));
	}
#endif // WITH_UNREAL_DEVELOPER_TOOLS

	UE_LOG(LogMass, Log, TEXT("Dependency order:"));
	for (const FProcessorDependencySolver::FOrderInfo& Info : OutResult)
	{
		UE_LOG(LogMass, Log, TEXT("\t%s"), *Info.Name.ToString());
	}
}

#undef LOCTEXT_NAMESPACE 
