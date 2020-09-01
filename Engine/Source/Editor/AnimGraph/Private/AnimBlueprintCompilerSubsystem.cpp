// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintCompilerSubsystem.h"
#include "AnimBlueprintCompiler.h"
#include "AnimBlueprintCompilerSubsystemCollection.h"

void UAnimBlueprintCompilerSubsystem::Initialize(FSubsystemCollectionBase& InCollection)
{
	FAnimBlueprintCompilerSubsystemCollection& AnimBlueprintCompilerSubsystemCollection = *static_cast<FAnimBlueprintCompilerSubsystemCollection*>(&InCollection);
	CompilerContext = AnimBlueprintCompilerSubsystemCollection.CompilerContext;
}

UBlueprint* UAnimBlueprintCompilerSubsystem::GetBlueprint() const
{
	return CompilerContext->Blueprint;
}

UAnimBlueprint* UAnimBlueprintCompilerSubsystem::GetAnimBlueprint() const
{
	return CompilerContext->AnimBlueprint;
}

UAnimBlueprintGeneratedClass* UAnimBlueprintCompilerSubsystem::GetNewAnimBlueprintClass() const 
{ 
	return CompilerContext->NewAnimBlueprintClass; 
}

FCompilerResultsLog& UAnimBlueprintCompilerSubsystem::GetMessageLog() const
{ 
	return CompilerContext->MessageLog; 
}

UEdGraph* UAnimBlueprintCompilerSubsystem::GetConsolidatedEventGraph() const
{
	return CompilerContext->ConsolidatedEventGraph;
}

bool UAnimBlueprintCompilerSubsystem::ValidateGraphIsWellFormed(UEdGraph* Graph) const
{
	return CompilerContext->ValidateGraphIsWellFormed(Graph);
}

int32 UAnimBlueprintCompilerSubsystem::GetAllocationIndexOfNode(UAnimGraphNode_Base* VisualAnimNode)
{
	return CompilerContext->GetAllocationIndexOfNode(VisualAnimNode);
}

void UAnimBlueprintCompilerSubsystem::AddPoseLinkMappingRecord(const FPoseLinkMappingRecord& InRecord)
{
	CompilerContext->ValidPoseLinkList.Add(InRecord);
}

void UAnimBlueprintCompilerSubsystem::GetLinkedAnimNodes(UAnimGraphNode_Base* InGraphNode, TArray<UAnimGraphNode_Base*> &LinkedAnimNodes)
{
	CompilerContext->GetLinkedAnimNodes(InGraphNode, LinkedAnimNodes);
}

const TMap<UAnimGraphNode_Base*, int32>& UAnimBlueprintCompilerSubsystem::GetAllocatedAnimNodeIndices() const
{
	return CompilerContext->AllocatedAnimNodeIndices;
}

const TMap<UAnimGraphNode_Base*, UAnimGraphNode_Base*>& UAnimBlueprintCompilerSubsystem::GetSourceNodeToProcessedNodeMap() const
{
	return CompilerContext->SourceNodeToProcessedNodeMap;
}

const TMap<int32, FProperty*>& UAnimBlueprintCompilerSubsystem::GetAllocatedPropertiesByIndex() const
{
	return CompilerContext->AllocatedPropertiesByIndex;
}

const TMap<UAnimGraphNode_Base*, FProperty*>& UAnimBlueprintCompilerSubsystem::GetAllocatedPropertiesByNode() const
{
	return CompilerContext->AllocatedAnimNodes;
}

void UAnimBlueprintCompilerSubsystem::ExpandSplitPins(UEdGraph* InGraph)
{
	CompilerContext->ExpandSplitPins(InGraph);
}

void UAnimBlueprintCompilerSubsystem::PruneIsolatedAnimationNodes(const TArray<UAnimGraphNode_Base*>& RootSet, TArray<UAnimGraphNode_Base*>& GraphNodes)
{
	CompilerContext->PruneIsolatedAnimationNodes(RootSet, GraphNodes);
}

void UAnimBlueprintCompilerSubsystem::ExpansionStep(UEdGraph* Graph, bool bAllowUbergraphExpansions)
{
	CompilerContext->ExpansionStep(Graph, bAllowUbergraphExpansions);
}

void UAnimBlueprintCompilerSubsystem::ProcessAnimationNodes(TArray<UAnimGraphNode_Base*>& AnimNodeList)
{
	CompilerContext->ProcessAnimationNodes(AnimNodeList);
}

FKismetCompilerContext* UAnimBlueprintCompilerSubsystem::GetKismetCompiler() const
{
	return static_cast<FKismetCompilerContext*>(CompilerContext);
}

UAnimBlueprintCompilerSubsystem* UAnimBlueprintCompilerSubsystem::GetSubsystemInternal(const FKismetCompilerContext* InCompilerContext, TSubclassOf<UAnimBlueprintCompilerSubsystem> InClass)
{
	return static_cast<const FAnimBlueprintCompilerContext*>(InCompilerContext)->AnimBlueprintCompilerSubsystemCollection.GetSubsystem(InClass);
}

UAnimBlueprintCompilerSubsystem* UAnimBlueprintCompilerSubsystem::FindSubsystemWithInterfaceInternal(const FKismetCompilerContext* InCompilerContext, TSubclassOf<UInterface> InInterfaceClass)
{
	return static_cast<const FAnimBlueprintCompilerContext*>(InCompilerContext)->AnimBlueprintCompilerSubsystemCollection.FindSubsystemWithInterface<UAnimBlueprintCompilerSubsystem>(InInterfaceClass);
}

const FKismetCompilerOptions& UAnimBlueprintCompilerSubsystem::GetCompileOptions() const
{
	return CompilerContext->CompileOptions;
}