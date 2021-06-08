// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimBlueprintCompiler.h"
#include "UObject/UObjectHash.h"
#include "Animation/AnimInstance.h"
#include "EdGraphUtilities.h"
#include "K2Node_CallFunction.h"
#include "K2Node_StructMemberGet.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Knot.h"
#include "K2Node_StructMemberSet.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_GetArrayItem.h"

#include "AnimationGraphSchema.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetReinstanceUtilities.h"
#include "AnimGraphNode_Root.h"
#include "Animation/AnimNode_CustomProperty.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"
#include "AnimGraphNode_LinkedAnimGraph.h"
#include "AnimationEditorUtils.h"
#include "AnimationGraph.h"
#include "AnimBlueprintPostCompileValidation.h" 
#include "AnimGraphNode_LinkedInputPose.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "String/ParseTokens.h"
#include "Algo/Transform.h"
#include "Algo/Accumulate.h"
#include "IClassVariableCreator.h"
#include "AnimBlueprintGeneratedClassCompiledData.h"
#include "AnimBlueprintCompilationContext.h"
#include "AnimBlueprintVariableCreationContext.h"

#define LOCTEXT_NAMESPACE "AnimBlueprintCompiler"

//////////////////////////////////////////////////////////////////////////
// FAnimBlueprintCompiler

FAnimBlueprintCompilerContext::FAnimBlueprintCompilerContext(UAnimBlueprint* SourceSketch, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions)
	: FKismetCompilerContext(SourceSketch, InMessageLog, InCompileOptions)
	, AnimBlueprint(SourceSketch)
	, bIsDerivedAnimBlueprint(false)
{
	AnimBlueprintCompilerHandlerCollection.Initialize(this);

	// Add the animation graph schema to skip default function processing on them
	KnownGraphSchemas.AddUnique(UAnimationGraphSchema::StaticClass());

	// Make sure the skeleton has finished preloading
	if (AnimBlueprint->TargetSkeleton != nullptr)
	{
		if (FLinkerLoad* Linker = AnimBlueprint->TargetSkeleton->GetLinker())
		{
			Linker->Preload(AnimBlueprint->TargetSkeleton);
		}
	}

	if (AnimBlueprint->HasAnyFlags(RF_NeedPostLoad))
	{
		//Compilation during loading .. need to verify node guids as some anim blueprints have duplicated guids

		TArray<UEdGraph*> ChildGraphs;
		ChildGraphs.Reserve(20);

		TSet<FGuid> NodeGuids;
		NodeGuids.Reserve(200);

		// Tracking to see if we need to warn for deterministic cooking
		bool bNodeGuidsRegenerated = false;

		auto CheckGraph = [&bNodeGuidsRegenerated, &NodeGuids, &ChildGraphs](UEdGraph* InGraph)
		{
			if (AnimationEditorUtils::IsAnimGraph(InGraph))
			{
				ChildGraphs.Reset();
				AnimationEditorUtils::FindChildGraphsFromNodes(InGraph, ChildGraphs);

				for (int32 Index = 0; Index < ChildGraphs.Num(); ++Index) // Not ranged for as we modify array within the loop
				{
					UEdGraph* ChildGraph = ChildGraphs[Index];

					// Get subgraphs before continuing 
					AnimationEditorUtils::FindChildGraphsFromNodes(ChildGraph, ChildGraphs);

					for (UEdGraphNode* Node : ChildGraph->Nodes)
					{
						if (Node)
						{
							if (NodeGuids.Contains(Node->NodeGuid))
							{
								bNodeGuidsRegenerated = true;
							
								Node->CreateNewGuid(); // GUID is already being used, create a new one.
							}
							else
							{
								NodeGuids.Add(Node->NodeGuid);
							}
						}
					}
				}
			}
		};

		for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
		{
			CheckGraph(Graph);
		}

		for(FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
		{
			for(UEdGraph* Graph : InterfaceDesc.Graphs)
			{
				CheckGraph(Graph);
			}
		}

		if(bNodeGuidsRegenerated)
		{
			UE_LOG(LogAnimation, Warning, TEXT("Animation Blueprint %s has nodes with invalid node guids that have been regenerated. This blueprint will not cook deterministically until it is resaved."), *AnimBlueprint->GetPathName());
		}
	}

	// Determine if there is an anim blueprint in the ancestry of this class
	bIsDerivedAnimBlueprint = UAnimBlueprint::FindRootAnimBlueprint(AnimBlueprint) != NULL;

	// Regenerate temporary stub functions
	// We do this here to catch the standard and 'fast' (compilation manager) compilation paths
	CreateAnimGraphStubFunctions();
}

FAnimBlueprintCompilerContext::~FAnimBlueprintCompilerContext()
{
	DestroyAnimGraphStubFunctions();
}

void FAnimBlueprintCompilerContext::ForAllSubGraphs(UEdGraph* InGraph, TFunctionRef<void(UEdGraph*)> InPerGraphFunction)
{
	TArray<UEdGraph*> AllGraphs;
	AllGraphs.Add(InGraph);
	InGraph->GetAllChildrenGraphs(AllGraphs);

	for(UEdGraph* CurrGraph : AllGraphs)
	{
		InPerGraphFunction(CurrGraph);
	}
};

void FAnimBlueprintCompilerContext::CreateClassVariablesFromBlueprint()
{
	FKismetCompilerContext::CreateClassVariablesFromBlueprint();

	if(!bIsDerivedAnimBlueprint)
	{
		auto ProcessGraph = [this](UEdGraph* InGraph)
		{
			TArray<IClassVariableCreator*> ClassVariableCreators;
			InGraph->GetNodesOfClass(ClassVariableCreators);
			FAnimBlueprintVariableCreationContext CreationContext(this);
			for(IClassVariableCreator* ClassVariableCreator : ClassVariableCreators)
			{
				ClassVariableCreator->CreateClassVariablesFromBlueprint(CreationContext);
			}
		};

		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			ForAllSubGraphs(Graph, ProcessGraph);
		}

		for(FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
		{
			for(UEdGraph* Graph : InterfaceDesc.Graphs)
			{
				ForAllSubGraphs(Graph, ProcessGraph);
			}
		}
	}
}


UEdGraphSchema_K2* FAnimBlueprintCompilerContext::CreateSchema()
{
	AnimSchema = NewObject<UAnimationGraphSchema>();
	return AnimSchema;
}

void FAnimBlueprintCompilerContext::ProcessAnimationNode(UAnimGraphNode_Base* VisualAnimNode)
{
	// Early out if this node has already been processed
	if (AllocatedAnimNodes.Contains(VisualAnimNode))
	{
		return;
	}

	// Make sure the visual node has a runtime node template
	const UScriptStruct* NodeType = VisualAnimNode->GetFNodeType();
	if (NodeType == NULL)
	{
		MessageLog.Error(TEXT("@@ has no animation node member"), VisualAnimNode);
		return;
	}

	// Give the visual node a chance to do validation
	{
		const int32 PreValidationErrorCount = MessageLog.NumErrors;
		VisualAnimNode->ValidateAnimNodeDuringCompilation(AnimBlueprint->TargetSkeleton, MessageLog);
		VisualAnimNode->BakeDataDuringCompilation(MessageLog);
		if (MessageLog.NumErrors != PreValidationErrorCount)
		{
			return;
		}
	}

	// Create a property for the node
	const FString NodeVariableName = ClassScopeNetNameMap.MakeValidName(VisualAnimNode);

	const UAnimationGraphSchema* AnimGraphDefaultSchema = GetDefault<UAnimationGraphSchema>();

	FEdGraphPinType NodeVariableType;
	NodeVariableType.PinCategory = UAnimationGraphSchema::PC_Struct;
	NodeVariableType.PinSubCategoryObject = MakeWeakObjectPtr(const_cast<UScriptStruct*>(NodeType));

	FStructProperty* NewProperty = CastField<FStructProperty>(CreateVariable(FName(*NodeVariableName), NodeVariableType));

	if (NewProperty == NULL)
	{
		MessageLog.Error(TEXT("Failed to create node property for @@"), VisualAnimNode);
	}

	// Register this node with the compile-time data structures
	const int32 AllocatedIndex = AllocateNodeIndexCounter++;
	AllocatedAnimNodes.Add(VisualAnimNode, NewProperty);
	AllocatedNodePropertiesToNodes.Add(NewProperty, VisualAnimNode);
	AllocatedAnimNodeIndices.Add(VisualAnimNode, AllocatedIndex);
	AllocatedPropertiesByIndex.Add(AllocatedIndex, NewProperty);

	UAnimGraphNode_Base* TrueSourceObject = MessageLog.FindSourceObjectTypeChecked<UAnimGraphNode_Base>(VisualAnimNode);
	SourceNodeToProcessedNodeMap.Add(TrueSourceObject, VisualAnimNode);

	// Register the slightly more permanent debug information
	FAnimBlueprintDebugData& NewAnimBlueprintDebugData = NewAnimBlueprintClass->GetAnimBlueprintDebugData();
	NewAnimBlueprintDebugData.NodePropertyToIndexMap.Add(TrueSourceObject, AllocatedIndex);
	NewAnimBlueprintDebugData.NodeGuidToIndexMap.Add(TrueSourceObject->NodeGuid, AllocatedIndex);
	NewAnimBlueprintDebugData.NodePropertyIndexToNodeMap.Add(AllocatedIndex, TrueSourceObject);
	NewAnimBlueprintClass->GetDebugData().RegisterClassPropertyAssociation(TrueSourceObject, NewProperty);

	FAnimBlueprintGeneratedClassCompiledData CompiledData(NewAnimBlueprintClass);
	FAnimBlueprintCompilationContext CompilerContext(this);
	VisualAnimNode->ProcessDuringCompilation(CompilerContext, CompiledData);
}

int32 FAnimBlueprintCompilerContext::GetAllocationIndexOfNode(UAnimGraphNode_Base* VisualAnimNode)
{
	ProcessAnimationNode(VisualAnimNode);
	int32* pResult = AllocatedAnimNodeIndices.Find(VisualAnimNode);
	return (pResult != NULL) ? *pResult : INDEX_NONE;
}

bool FAnimBlueprintCompilerContext::ShouldForceKeepNode(const UEdGraphNode* Node) const
{
	// Force keep anim nodes during the standard pruning step. Isolated ones will then be removed via PruneIsolatedAnimationNodes.
	// Anim graph nodes are always culled at their expansion step anyways.
	return Node->IsA<UAnimGraphNode_Base>();
}

void FAnimBlueprintCompilerContext::PostExpansionStep(const UEdGraph* Graph)
{
	FAnimBlueprintGeneratedClassCompiledData CompiledData(NewAnimBlueprintClass);
	FAnimBlueprintPostExpansionStepContext CompilerContext(this);
	OnPostExpansionStepDelegate.Broadcast(Graph, CompilerContext, CompiledData);
}

void FAnimBlueprintCompilerContext::PruneIsolatedAnimationNodes(const TArray<UAnimGraphNode_Base*>& RootSet, TArray<UAnimGraphNode_Base*>& GraphNodes)
{
	struct FNodeVisitorDownPoseWires
	{
		TSet<UEdGraphNode*> VisitedNodes;
		const UAnimationGraphSchema* Schema;

		FNodeVisitorDownPoseWires()
		{
			Schema = GetDefault<UAnimationGraphSchema>();
		}

		void TraverseNodes(UEdGraphNode* Node)
		{
			VisitedNodes.Add(Node);

			// Follow every exec output pin
			for (int32 i = 0; i < Node->Pins.Num(); ++i)
			{
				UEdGraphPin* MyPin = Node->Pins[i];

				if ((MyPin->Direction == EGPD_Input) && (Schema->IsPosePin(MyPin->PinType)))
				{
					for (int32 j = 0; j < MyPin->LinkedTo.Num(); ++j)
					{
						UEdGraphPin* OtherPin = MyPin->LinkedTo[j];
						UEdGraphNode* OtherNode = OtherPin->GetOwningNode();
						if (!VisitedNodes.Contains(OtherNode))
						{
							TraverseNodes(OtherNode);
						}
					}
				}
			}
		}
	};

	// Prune the nodes that aren't reachable via an animation pose link
	FNodeVisitorDownPoseWires Visitor;

	for (auto RootIt = RootSet.CreateConstIterator(); RootIt; ++RootIt)
	{
		UAnimGraphNode_Base* RootNode = *RootIt;
		Visitor.TraverseNodes(RootNode);
	}

	for (int32 NodeIndex = 0; NodeIndex < GraphNodes.Num(); ++NodeIndex)
	{
		UAnimGraphNode_Base* Node = GraphNodes[NodeIndex];

		// We cant prune linked input poses as even if they are not linked to the root, they are needed for the dynamic link phase at runtime
		if (!Visitor.VisitedNodes.Contains(Node) && !IsNodePure(Node) && !Node->IsA<UAnimGraphNode_LinkedInputPose>())
		{
			Node->BreakAllNodeLinks();
			GraphNodes.RemoveAtSwap(NodeIndex);
			--NodeIndex;
		}
	}
}

void FAnimBlueprintCompilerContext::ProcessAnimationNodes(TArray<UAnimGraphNode_Base*>& AnimNodeList)
{
	// Process the remaining nodes
	for (UAnimGraphNode_Base* AnimNode : AnimNodeList)
	{
		ProcessAnimationNode(AnimNode);
	}
}

void FAnimBlueprintCompilerContext::GetLinkedAnimNodes(UAnimGraphNode_Base* InGraphNode, TArray<UAnimGraphNode_Base*> &LinkedAnimNodes) const
{
	for(UEdGraphPin* Pin : InGraphNode->Pins)
	{
		if(Pin->Direction == EEdGraphPinDirection::EGPD_Input &&
		   Pin->PinType.PinCategory == TEXT("struct"))
		{
			if(UScriptStruct* Struct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get()))
			{
				if(Struct->IsChildOf(FPoseLinkBase::StaticStruct()))
				{
					GetLinkedAnimNodes_TraversePin(Pin, LinkedAnimNodes);
				}
			}
		}
	}
}

void FAnimBlueprintCompilerContext::GetLinkedAnimNodes_TraversePin(UEdGraphPin* InPin, TArray<UAnimGraphNode_Base*>& LinkedAnimNodes) const
{
	if(!InPin)
	{
		return;
	}

	for(UEdGraphPin* LinkedPin : InPin->LinkedTo)
	{
		if(!LinkedPin)
		{
			continue;
		}
		
		UEdGraphNode* OwningNode = LinkedPin->GetOwningNode();

		if(UK2Node_Knot* InnerKnot = Cast<UK2Node_Knot>(OwningNode))
		{
			GetLinkedAnimNodes_TraversePin(InnerKnot->GetInputPin(), LinkedAnimNodes);
		}
		else if(UAnimGraphNode_Base* AnimNode = Cast<UAnimGraphNode_Base>(OwningNode))
		{
			GetLinkedAnimNodes_ProcessAnimNode(AnimNode, LinkedAnimNodes);
		}
	}
}

void FAnimBlueprintCompilerContext::GetLinkedAnimNodes_ProcessAnimNode(UAnimGraphNode_Base* AnimNode, TArray<UAnimGraphNode_Base *>& LinkedAnimNodes) const
{
	if(!AllocatedAnimNodes.Contains(AnimNode))
	{
		UAnimGraphNode_Base* TrueSourceNode = MessageLog.FindSourceObjectTypeChecked<UAnimGraphNode_Base>(AnimNode);

		if(UAnimGraphNode_Base*const* AllocatedNode = SourceNodeToProcessedNodeMap.Find(TrueSourceNode))
		{
			LinkedAnimNodes.Add(*AllocatedNode);
		}
		else
		{
			FString ErrorString = FText::Format(LOCTEXT("MissingLinkFmt", "Missing allocated node for {0} while searching for node links - likely due to the node having outstanding errors."), FText::FromString(AnimNode->GetName())).ToString();
			MessageLog.Error(*ErrorString);
		}
	}
	else
	{
		LinkedAnimNodes.Add(AnimNode);
	}
}

void FAnimBlueprintCompilerContext::ProcessAllAnimationNodes()
{
	// Validate that we have a skeleton
	if ((AnimBlueprint->TargetSkeleton == nullptr) && !AnimBlueprint->bIsNewlyCreated)
	{
		MessageLog.Error(*LOCTEXT("NoSkeleton", "@@ - The skeleton asset for this animation Blueprint is missing, so it cannot be compiled!").ToString(), AnimBlueprint);
		return;
	}

	// Build the raw node lists
	TArray<UAnimGraphNode_Base*> RootAnimNodeList;
	ConsolidatedEventGraph->GetNodesOfClass<UAnimGraphNode_Base>(RootAnimNodeList);

	// We recursively build the node lists for pre- and post-processing phases to make sure
	// we catch any handler-relevant nodes in sub-graphs
	TArray<UAnimGraphNode_Base*> AllSubGraphsAnimNodeList;
	ForAllSubGraphs(ConsolidatedEventGraph, [&AllSubGraphsAnimNodeList](UEdGraph* InGraph)
	{
		InGraph->GetNodesOfClass<UAnimGraphNode_Base>(AllSubGraphsAnimNodeList);
	});

	// Find the root nodes
	TArray<UAnimGraphNode_Base*> RootSet;

	AllocateNodeIndexCounter = 0;

	for (UAnimGraphNode_Base* SourceNode : RootAnimNodeList)
	{
		UAnimGraphNode_Base* TrueNode = MessageLog.FindSourceObjectTypeChecked<UAnimGraphNode_Base>(SourceNode);
		TrueNode->BlueprintUsage = EBlueprintUsage::NoProperties;

		if(SourceNode->IsNodeRootSet())
		{
			RootSet.Add(SourceNode);
		}
	}

	if (RootAnimNodeList.Num() > 0)
	{
		// Prune any anim nodes (they will be skipped by PruneIsolatedNodes above)
		PruneIsolatedAnimationNodes(RootSet, RootAnimNodeList);

		// Validate the graph
		ValidateGraphIsWellFormed(ConsolidatedEventGraph);

		FAnimBlueprintGeneratedClassCompiledData CompiledData(NewAnimBlueprintClass);
		FAnimBlueprintCompilationContext CompilerContext(this);
		OnPreProcessAnimationNodesDelegate.Broadcast(AllSubGraphsAnimNodeList, CompilerContext, CompiledData);

		// Process the animation nodes
		ProcessAnimationNodes(RootAnimNodeList);

		OnPostProcessAnimationNodesDelegate.Broadcast(AllSubGraphsAnimNodeList, CompilerContext, CompiledData);
	}
	else
	{
		MessageLog.Error(*LOCTEXT("ExpectedAFunctionEntry_Error", "Expected at least one animation node, but did not find any").ToString());
	}
}

void FAnimBlueprintCompilerContext::CopyTermDefaultsToDefaultObject(UObject* DefaultObject)
{
	Super::CopyTermDefaultsToDefaultObject(DefaultObject);

	UAnimInstance* DefaultAnimInstance = Cast<UAnimInstance>(DefaultObject);

	if (bIsDerivedAnimBlueprint && DefaultAnimInstance)
	{
		// If we are a derived animation graph; apply any stored overrides.
		// Restore values from the root class to catch values where the override has been removed.
		UAnimBlueprintGeneratedClass* RootAnimClass = NewAnimBlueprintClass;
		while (UAnimBlueprintGeneratedClass* NextClass = Cast<UAnimBlueprintGeneratedClass>(RootAnimClass->GetSuperClass()))
		{
			RootAnimClass = NextClass;
		}
		UObject* RootDefaultObject = RootAnimClass->GetDefaultObject();

		for (TFieldIterator<FProperty> It(RootAnimClass); It; ++It)
		{
			FProperty* RootProp = *It;

			if (FStructProperty* RootStructProp = CastField<FStructProperty>(RootProp))
			{
				if (RootStructProp->Struct->IsChildOf(FAnimNode_Base::StaticStruct()))
				{
					FStructProperty* ChildStructProp = FindFProperty<FStructProperty>(NewAnimBlueprintClass, *RootStructProp->GetName());
					check(ChildStructProp);
					uint8* SourcePtr = RootStructProp->ContainerPtrToValuePtr<uint8>(RootDefaultObject);
					uint8* DestPtr = ChildStructProp->ContainerPtrToValuePtr<uint8>(DefaultAnimInstance);
					check(SourcePtr && DestPtr);
					RootStructProp->CopyCompleteValue(DestPtr, SourcePtr);
				}
			}
		}
	}

	// Give game-specific logic a chance to replace animations
	if(DefaultAnimInstance)
	{
		DefaultAnimInstance->ApplyAnimOverridesToCDO(MessageLog);
	}

	if (bIsDerivedAnimBlueprint && DefaultAnimInstance)
	{
		// Patch the overridden values into the CDO
		TArray<FAnimParentNodeAssetOverride*> AssetOverrides;
		AnimBlueprint->GetAssetOverrides(AssetOverrides);
		for (FAnimParentNodeAssetOverride* Override : AssetOverrides)
		{
			if (Override->NewAsset)
			{
				FAnimNode_Base* BaseNode = NewAnimBlueprintClass->GetPropertyInstance<FAnimNode_Base>(DefaultAnimInstance, Override->ParentNodeGuid, EPropertySearchMode::Hierarchy);
				if (BaseNode)
				{
					BaseNode->OverrideAsset(Override->NewAsset);
				}
			}
		}

		return;
	}

	if(DefaultAnimInstance)
	{
		int32 LinkIndexCount = 0;
		TMap<UAnimGraphNode_Base*, int32> LinkIndexMap;
		TMap<UAnimGraphNode_Base*, uint8*> NodeBaseAddresses;

		// Initialize animation nodes from their templates
		for (TFieldIterator<FProperty> It(DefaultAnimInstance->GetClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			FProperty* TargetProperty = *It;

			if (UAnimGraphNode_Base* VisualAnimNode = AllocatedNodePropertiesToNodes.FindRef(TargetProperty))
			{
				const FStructProperty* SourceNodeProperty = VisualAnimNode->GetFNodeProperty();
				check(SourceNodeProperty != NULL);
				check(CastFieldChecked<FStructProperty>(TargetProperty)->Struct == SourceNodeProperty->Struct);

				uint8* DestinationPtr = TargetProperty->ContainerPtrToValuePtr<uint8>(DefaultAnimInstance);
				uint8* SourcePtr = SourceNodeProperty->ContainerPtrToValuePtr<uint8>(VisualAnimNode);

				if(UAnimGraphNode_Root* RootNode = ExactCast<UAnimGraphNode_Root>(VisualAnimNode))
				{
					// patch graph name into root nodes
					FAnimNode_Root NewRoot = *reinterpret_cast<FAnimNode_Root*>(SourcePtr);
					NewRoot.Name = Cast<UAnimGraphNode_Root>(MessageLog.FindSourceObject(RootNode))->GetGraph()->GetFName();
					TargetProperty->CopyCompleteValue(DestinationPtr, &NewRoot);
				}
				else if(UAnimGraphNode_LinkedInputPose* LinkedInputPoseNode = ExactCast<UAnimGraphNode_LinkedInputPose>(VisualAnimNode))
				{
					// patch graph name into linked input pose nodes
					FAnimNode_LinkedInputPose NewLinkedInputPose = *reinterpret_cast<FAnimNode_LinkedInputPose*>(SourcePtr);
					NewLinkedInputPose.Graph = Cast<UAnimGraphNode_LinkedInputPose>(MessageLog.FindSourceObject(LinkedInputPoseNode))->GetGraph()->GetFName();
					TargetProperty->CopyCompleteValue(DestinationPtr, &NewLinkedInputPose);
				}
				else if(UAnimGraphNode_LinkedAnimGraph* LinkedAnimGraphNode = ExactCast<UAnimGraphNode_LinkedAnimGraph>(VisualAnimNode))
				{
					// patch node index into linked anim graph nodes
					FAnimNode_LinkedAnimGraph NewLinkedAnimGraph = *reinterpret_cast<FAnimNode_LinkedAnimGraph*>(SourcePtr);
					NewLinkedAnimGraph.NodeIndex = LinkIndexCount;
					TargetProperty->CopyCompleteValue(DestinationPtr, &NewLinkedAnimGraph);
				}
				else if(UAnimGraphNode_LinkedAnimLayer* LinkedAnimLayerNode = ExactCast<UAnimGraphNode_LinkedAnimLayer>(VisualAnimNode))
				{
					// patch node index into linked anim layer nodes
					FAnimNode_LinkedAnimLayer NewLinkedAnimLayer = *reinterpret_cast<FAnimNode_LinkedAnimLayer*>(SourcePtr);
					NewLinkedAnimLayer.NodeIndex = LinkIndexCount;
					TargetProperty->CopyCompleteValue(DestinationPtr, &NewLinkedAnimLayer);
				}
				else
				{
					TargetProperty->CopyCompleteValue(DestinationPtr, SourcePtr);
				}

				LinkIndexMap.Add(VisualAnimNode, LinkIndexCount);
				NodeBaseAddresses.Add(VisualAnimNode, DestinationPtr);
				++LinkIndexCount;
			}
		}

		// And wire up node links
		for (auto PoseLinkIt = ValidPoseLinkList.CreateIterator(); PoseLinkIt; ++PoseLinkIt)
		{
			FPoseLinkMappingRecord& Record = *PoseLinkIt;

			UAnimGraphNode_Base* LinkingNode = Record.GetLinkingNode();
			UAnimGraphNode_Base* LinkedNode = Record.GetLinkedNode();
		
			// @TODO this is quick solution for crash - if there were previous errors and some nodes were not added, they could still end here -
			// this check avoids that and since there are already errors, compilation won't be successful.
			// but I'd prefer stopping compilation earlier to avoid getting here in first place
			if (LinkIndexMap.Contains(LinkingNode) && LinkIndexMap.Contains(LinkedNode))
			{
				const int32 SourceNodeIndex = LinkIndexMap.FindChecked(LinkingNode);
				const int32 LinkedNodeIndex = LinkIndexMap.FindChecked(LinkedNode);
				uint8* DestinationPtr = NodeBaseAddresses.FindChecked(LinkingNode);

				Record.PatchLinkIndex(DestinationPtr, LinkedNodeIndex, SourceNodeIndex);
			}
		}   

		FAnimBlueprintGeneratedClassCompiledData CompiledData(NewAnimBlueprintClass);
		FAnimBlueprintCopyTermDefaultsContext CompilerContext(this);
		OnCopyTermDefaultsToDefaultObjectDelegate.Broadcast(DefaultObject, CompilerContext, CompiledData);

		UAnimBlueprintGeneratedClass* AnimBlueprintGeneratedClass = CastChecked<UAnimBlueprintGeneratedClass>(NewClass);

		// copy threaded update flag to CDO
		DefaultAnimInstance->bUseMultiThreadedAnimationUpdate = AnimBlueprint->bUseMultiThreadedAnimationUpdate;

		// Verify thread-safety
		if(GetDefault<UEngine>()->bAllowMultiThreadedAnimationUpdate && DefaultAnimInstance->bUseMultiThreadedAnimationUpdate)
		{
			// If we are a child anim BP, check parent classes & their CDOs
			if (UAnimBlueprintGeneratedClass* ParentClass = Cast<UAnimBlueprintGeneratedClass>(AnimBlueprintGeneratedClass->GetSuperClass()))
			{
				UAnimBlueprint* ParentAnimBlueprint = Cast<UAnimBlueprint>(ParentClass->ClassGeneratedBy);
				if (ParentAnimBlueprint && !ParentAnimBlueprint->bUseMultiThreadedAnimationUpdate)
				{
					DefaultAnimInstance->bUseMultiThreadedAnimationUpdate = false;
				}

				UAnimInstance* ParentDefaultObject = Cast<UAnimInstance>(ParentClass->GetDefaultObject(false));
				if (ParentDefaultObject && !ParentDefaultObject->bUseMultiThreadedAnimationUpdate)
				{
					DefaultAnimInstance->bUseMultiThreadedAnimationUpdate = false;
				}
			}

			// iterate all properties to determine validity
			for (FStructProperty* Property : TFieldRange<FStructProperty>(AnimBlueprintGeneratedClass, EFieldIteratorFlags::IncludeSuper))
			{
				if(Property->Struct->IsChildOf(FAnimNode_Base::StaticStruct()))
				{
					FAnimNode_Base* AnimNode = Property->ContainerPtrToValuePtr<FAnimNode_Base>(DefaultAnimInstance);
					if(!AnimNode->CanUpdateInWorkerThread())
					{
						MessageLog.Warning(*FText::Format(LOCTEXT("HasIncompatibleNode", "Found incompatible node \"{0}\" in blend graph. Disable threaded update or use member variable access."), FText::FromName(Property->Struct->GetFName())).ToString())
							->AddToken(FDocumentationToken::Create(TEXT("Engine/Animation/AnimBlueprints/AnimGraph")));;

						DefaultAnimInstance->bUseMultiThreadedAnimationUpdate = false;
					}
				}
			}

			if (FunctionList.Num() > 0)
			{
				// find the ubergraph in the function list
				FKismetFunctionContext* UbergraphFunctionContext = nullptr;
				for (FKismetFunctionContext& FunctionContext : FunctionList)
				{
					if (FunctionList[0].Function->GetName().StartsWith(TEXT("ExecuteUbergraph")))
					{
						UbergraphFunctionContext = &FunctionContext;
						break;
					}
				}

				if (UbergraphFunctionContext)
				{
					// run through the per-node compiled statements looking for struct-sets used by anim nodes
					for (auto& StatementPair : UbergraphFunctionContext->StatementsPerNode)
					{
						if (UK2Node_StructMemberSet* StructMemberSetNode = Cast<UK2Node_StructMemberSet>(StatementPair.Key))
						{
							UObject* SourceNode = MessageLog.FindSourceObject(StructMemberSetNode);

							if (SourceNode && StructMemberSetNode->StructType->IsChildOf(FAnimNode_Base::StaticStruct()))
							{
								for (FBlueprintCompiledStatement* Statement : StatementPair.Value)
								{
									if (Statement->Type == KCST_CallFunction && Statement->FunctionToCall)
									{
										// pure function?
										const bool bPureFunctionCall = Statement->FunctionToCall->HasAnyFunctionFlags(FUNC_BlueprintPure);

										// function called on something other than function library or anim instance?
										UClass* FunctionClass = CastChecked<UClass>(Statement->FunctionToCall->GetOuter());
										const bool bFunctionLibraryCall = FunctionClass->IsChildOf<UBlueprintFunctionLibrary>();
										const bool bAnimInstanceCall = FunctionClass->IsChildOf<UAnimInstance>();

										// Whitelisted/blacklisted? Some functions are not really 'pure', so we give people the opportunity to mark them up.
										// Mark up the class if it is generally thread safe, then unsafe functions can be marked up individually. We assume
										// that classes are unsafe by default, as well as if they are marked up NotBlueprintThreadSafe.
										const bool bClassThreadSafe = FunctionClass->HasMetaData(TEXT("BlueprintThreadSafe"));
										const bool bClassNotThreadSafe = FunctionClass->HasMetaData(TEXT("NotBlueprintThreadSafe")) || !FunctionClass->HasMetaData(TEXT("BlueprintThreadSafe"));
										const bool bFunctionThreadSafe = Statement->FunctionToCall->HasMetaData(TEXT("BlueprintThreadSafe"));
										const bool bFunctionNotThreadSafe = Statement->FunctionToCall->HasMetaData(TEXT("NotBlueprintThreadSafe"));

										const bool bThreadSafe = (bClassThreadSafe && !bFunctionNotThreadSafe) || (bClassNotThreadSafe && bFunctionThreadSafe);

										const bool bValidForUsage = bPureFunctionCall && bThreadSafe && (bFunctionLibraryCall || bAnimInstanceCall);

										if (!bValidForUsage)
										{
											UEdGraphNode* FunctionNode = nullptr;
											if (Statement->FunctionContext && Statement->FunctionContext->SourcePin)
											{
												FunctionNode = Statement->FunctionContext->SourcePin->GetOwningNode();
											}
											else if (Statement->LHS && Statement->LHS->SourcePin)
											{
												FunctionNode = Statement->LHS->SourcePin->GetOwningNode();
											}

											if (FunctionNode)
											{
												MessageLog.Warning(*LOCTEXT("NotThreadSafeWarningNodeContext", "Node @@ uses potentially thread-unsafe call @@. Disable threaded update or use a thread-safe call. Function may need BlueprintThreadSafe metadata adding.").ToString(), SourceNode, FunctionNode)
													->AddToken(FDocumentationToken::Create(TEXT("Engine/Animation/AnimBlueprints/AnimGraph")));
											}
											else if (Statement->FunctionToCall)
											{
												MessageLog.Warning(*FText::Format(LOCTEXT("NotThreadSafeWarningFunctionContext", "Node @@ uses potentially thread-unsafe call {0}. Disable threaded update or use a thread-safe call. Function may need BlueprintThreadSafe metadata adding."), Statement->FunctionToCall->GetDisplayNameText()).ToString(), SourceNode)
													->AddToken(FDocumentationToken::Create(TEXT("Engine/Animation/AnimBlueprints/AnimGraph")));
											}
											else
											{
												MessageLog.Warning(*LOCTEXT("NotThreadSafeWarningUnknownContext", "Node @@ uses potentially thread-unsafe call. Disable threaded update or use a thread-safe call.").ToString(), SourceNode)
													->AddToken(FDocumentationToken::Create(TEXT("Engine/Animation/AnimBlueprints/AnimGraph")));
											}

											DefaultAnimInstance->bUseMultiThreadedAnimationUpdate = false;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
}

void FAnimBlueprintCompilerContext::ExpandSplitPins(UEdGraph* InGraph)
{
	for (TArray<UEdGraphNode*>::TIterator NodeIt(InGraph->Nodes); NodeIt; ++NodeIt)
	{
		UK2Node* K2Node = Cast<UK2Node>(*NodeIt);
		if (K2Node != nullptr)
		{
			K2Node->ExpandSplitPins(*this, InGraph);
		}
	}
}

// Merges in any all ubergraph pages into the gathering ubergraph
void FAnimBlueprintCompilerContext::MergeUbergraphPagesIn(UEdGraph* Ubergraph)
{
	Super::MergeUbergraphPagesIn(Ubergraph);

	if (bIsDerivedAnimBlueprint)
	{
		// Skip any work related to an anim graph, it's all done by the parent class
	}
	else
	{
		// Move all animation graph nodes and associated pure logic chains into the consolidated event graph
		auto MoveGraph = [this](UEdGraph* InGraph)
		{
			if (InGraph->Schema->IsChildOf(UAnimationGraphSchema::StaticClass()))
			{
				// Merge all the animation nodes, contents, etc... into the ubergraph
				UEdGraph* ClonedGraph = FEdGraphUtilities::CloneGraph(InGraph, NULL, &MessageLog, true);

				// Prune the graph up-front
				const bool bIncludePotentialRootNodes = false;
				PruneIsolatedNodes(ClonedGraph, bIncludePotentialRootNodes);

				const bool bIsLoading = Blueprint->bIsRegeneratingOnLoad || IsAsyncLoading();
				const bool bIsCompiling = Blueprint->bBeingCompiled;
				ClonedGraph->MoveNodesToAnotherGraph(ConsolidatedEventGraph, bIsLoading, bIsCompiling);

				// Move subgraphs too
				ConsolidatedEventGraph->SubGraphs.Append(ClonedGraph->SubGraphs);
			}
		};

		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			MoveGraph(Graph);
		}

		for(FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
		{
			for(UEdGraph* Graph : InterfaceDesc.Graphs)
			{
				MoveGraph(Graph);
			}
		}

		// Make sure we expand any split pins here before we process animation nodes.
		ForAllSubGraphs(ConsolidatedEventGraph, [this](UEdGraph* InGraph)
		{
			ExpandSplitPins(InGraph);
		});

		// Compile the animation graph
		ProcessAllAnimationNodes();
	}
}

void FAnimBlueprintCompilerContext::ProcessOneFunctionGraph(UEdGraph* SourceGraph, bool bInternalFunction)
{
	if(!KnownGraphSchemas.FindByPredicate([SourceGraph](const TSubclassOf<UEdGraphSchema>& InSchemaClass)
	{
		return SourceGraph->Schema->IsChildOf(InSchemaClass.Get());
	}))
	{
		// Not known as a schema that this compiler looks at, pass to the default
		Super::ProcessOneFunctionGraph(SourceGraph, bInternalFunction);
	}
}

void FAnimBlueprintCompilerContext::EnsureProperGeneratedClass(UClass*& TargetUClass)
{
	if( TargetUClass && !((UObject*)TargetUClass)->IsA(UAnimBlueprintGeneratedClass::StaticClass()) )
	{
		FKismetCompilerUtilities::ConsignToOblivion(TargetUClass, Blueprint->bIsRegeneratingOnLoad);
		TargetUClass = NULL;
	}
}

void FAnimBlueprintCompilerContext::SpawnNewClass(const FString& NewClassName)
{
	NewAnimBlueprintClass = FindObject<UAnimBlueprintGeneratedClass>(Blueprint->GetOutermost(), *NewClassName);

	if (NewAnimBlueprintClass == NULL)
	{
		NewAnimBlueprintClass = NewObject<UAnimBlueprintGeneratedClass>(Blueprint->GetOutermost(), FName(*NewClassName), RF_Public | RF_Transactional);
	}
	else
	{
		// Already existed, but wasn't linked in the Blueprint yet due to load ordering issues
		FBlueprintCompileReinstancer::Create(NewAnimBlueprintClass);
	}
	NewClass = NewAnimBlueprintClass;

	FAnimBlueprintGeneratedClassCompiledData CompiledData(NewAnimBlueprintClass);
	FAnimBlueprintCompilationBracketContext CompilerContext(this);
	OnStartCompilingClassDelegate.Broadcast(NewAnimBlueprintClass, CompilerContext, CompiledData);
}

void FAnimBlueprintCompilerContext::OnPostCDOCompiled()
{
	for (UAnimBlueprintGeneratedClass* ClassWithInputHandlers = NewAnimBlueprintClass; ClassWithInputHandlers != nullptr; ClassWithInputHandlers = Cast<UAnimBlueprintGeneratedClass>(ClassWithInputHandlers->GetSuperClass()))
	{
		FExposedValueHandler::ClassInitialization(ClassWithInputHandlers->EvaluateGraphExposedInputs, NewAnimBlueprintClass->ClassDefaultObject);

		ClassWithInputHandlers->LinkFunctionsToDefaultObjectNodes(NewAnimBlueprintClass->ClassDefaultObject);
	}
}

void FAnimBlueprintCompilerContext::OnNewClassSet(UBlueprintGeneratedClass* ClassToUse)
{
	NewAnimBlueprintClass = CastChecked<UAnimBlueprintGeneratedClass>(ClassToUse);
}

void FAnimBlueprintCompilerContext::CleanAndSanitizeClass(UBlueprintGeneratedClass* ClassToClean, UObject*& InOldCDO)
{
	Super::CleanAndSanitizeClass(ClassToClean, InOldCDO);

	// Make sure our typed pointer is set
	check(ClassToClean == NewClass && NewAnimBlueprintClass == NewClass);

	NewAnimBlueprintClass->AnimBlueprintDebugData = FAnimBlueprintDebugData();

	// Reset the baked data
	//@TODO: Move this into PurgeClass
	NewAnimBlueprintClass->BakedStateMachines.Empty();
	NewAnimBlueprintClass->AnimNotifies.Empty();
	NewAnimBlueprintClass->AnimBlueprintFunctions.Empty();
	NewAnimBlueprintClass->OrderedSavedPoseIndicesMap.Empty();
	NewAnimBlueprintClass->AnimNodeProperties.Empty();
	NewAnimBlueprintClass->LinkedAnimGraphNodeProperties.Empty();
	NewAnimBlueprintClass->LinkedAnimLayerNodeProperties.Empty();
	NewAnimBlueprintClass->PreUpdateNodeProperties.Empty();
	NewAnimBlueprintClass->DynamicResetNodeProperties.Empty();
	NewAnimBlueprintClass->StateMachineNodeProperties.Empty();
	NewAnimBlueprintClass->InitializationNodeProperties.Empty();
	NewAnimBlueprintClass->EvaluateGraphExposedInputs.Empty();
	NewAnimBlueprintClass->GraphAssetPlayerInformation.Empty();
	NewAnimBlueprintClass->GraphBlendOptions.Empty();

	// Copy over runtime data from the blueprint to the class
	NewAnimBlueprintClass->TargetSkeleton = AnimBlueprint->TargetSkeleton;

	UAnimBlueprint* RootAnimBP = UAnimBlueprint::FindRootAnimBlueprint(AnimBlueprint);
	bIsDerivedAnimBlueprint = RootAnimBP != NULL;

	FAnimBlueprintGeneratedClassCompiledData CompiledData(NewAnimBlueprintClass);
	FAnimBlueprintCompilationBracketContext CompilerContext(this);
	OnStartCompilingClassDelegate.Broadcast(NewAnimBlueprintClass, CompilerContext, CompiledData);
}

void FAnimBlueprintCompilerContext::FinishCompilingClass(UClass* Class)
{
	const UAnimBlueprint* PossibleRoot = UAnimBlueprint::FindRootAnimBlueprint(AnimBlueprint);
	const UAnimBlueprint* Src = PossibleRoot ? PossibleRoot : AnimBlueprint;

	UAnimBlueprintGeneratedClass* AnimBlueprintGeneratedClass = CastChecked<UAnimBlueprintGeneratedClass>(Class);
	AnimBlueprintGeneratedClass->SyncGroupNames.Reset();
	AnimBlueprintGeneratedClass->SyncGroupNames.Reserve(Src->Groups.Num());
	for (const FAnimGroupInfo& GroupInfo : Src->Groups)
	{
		AnimBlueprintGeneratedClass->SyncGroupNames.Add(GroupInfo.Name);
	}

	// Add graph blend options to class if blend values were actually customized
	auto AddBlendOptions = [AnimBlueprintGeneratedClass](UEdGraph* Graph)
	{
		UAnimationGraph* AnimGraph = Cast<UAnimationGraph>(Graph);
		if (AnimGraph && (AnimGraph->BlendOptions.BlendInTime >= 0.0f || AnimGraph->BlendOptions.BlendOutTime >= 0.0f))
		{
			AnimBlueprintGeneratedClass->GraphBlendOptions.Add(AnimGraph->GetFName(), AnimGraph->BlendOptions);
		}
	};


	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		AddBlendOptions(Graph);
	}

	for (FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		if (InterfaceDesc.Interface->IsChildOf<UAnimLayerInterface>())
		{
			for (UEdGraph* Graph : InterfaceDesc.Graphs)
			{
				AddBlendOptions(Graph);
			}
		}
	}

	FAnimBlueprintGeneratedClassCompiledData CompiledData(NewAnimBlueprintClass);
	FAnimBlueprintCompilationBracketContext CompilerContext(this);
	OnFinishCompilingClassDelegate.Broadcast(AnimBlueprintGeneratedClass, CompilerContext, CompiledData);

	Super::FinishCompilingClass(Class);
}

void FAnimBlueprintCompilerContext::PostCompile()
{
	Super::PostCompile();

	for (UPoseWatch* PoseWatch : AnimBlueprint->PoseWatches)
	{
		AnimationEditorUtils::SetPoseWatch(PoseWatch, AnimBlueprint);
	}

	UAnimBlueprintGeneratedClass* AnimBlueprintGeneratedClass = CastChecked<UAnimBlueprintGeneratedClass>(NewClass);
	if(UAnimInstance* DefaultAnimInstance = Cast<UAnimInstance>(AnimBlueprintGeneratedClass->GetDefaultObject()))
	{
		// iterate all anim node and call PostCompile
		const USkeleton* CurrentSkeleton = AnimBlueprint->TargetSkeleton;
		for (FStructProperty* Property : TFieldRange<FStructProperty>(AnimBlueprintGeneratedClass, EFieldIteratorFlags::IncludeSuper))
		{
			if (Property->Struct->IsChildOf(FAnimNode_Base::StaticStruct()))
			{
				FAnimNode_Base* AnimNode = Property->ContainerPtrToValuePtr<FAnimNode_Base>(DefaultAnimInstance);
				AnimNode->PostCompile(CurrentSkeleton);
			}
		}
	}
}

void FAnimBlueprintCompilerContext::PostCompileDiagnostics()
{
	FKismetCompilerContext::PostCompileDiagnostics();

#if WITH_EDITORONLY_DATA // ANIMINST_PostCompileValidation
	// See if AnimInstance implements a PostCompileValidation Class. 
	// If so, instantiate it, and let it perform Validation of our newly compiled AnimBlueprint.
	if (const UAnimInstance* const DefaultAnimInstance = Cast<UAnimInstance>(NewAnimBlueprintClass->GetDefaultObject()))
	{
		if (DefaultAnimInstance->PostCompileValidationClassName.IsValid())
		{
			UClass* PostCompileValidationClass = LoadClass<UObject>(nullptr, *DefaultAnimInstance->PostCompileValidationClassName.ToString());
			if (PostCompileValidationClass)
			{
				UAnimBlueprintPostCompileValidation* PostCompileValidation = NewObject<UAnimBlueprintPostCompileValidation>(GetTransientPackage(), PostCompileValidationClass);
				if (PostCompileValidation)
				{
					FAnimBPCompileValidationParams PCV_Params(DefaultAnimInstance, NewAnimBlueprintClass, MessageLog, AllocatedNodePropertiesToNodes);
					PostCompileValidation->DoPostCompileValidation(PCV_Params);
				}
			}
		}
	}
#endif // WITH_EDITORONLY_DATA

	if (!bIsDerivedAnimBlueprint)
	{
		bool bUsingCopyPoseFromMesh = false;

		// Run thru all nodes and make sure they like the final results
		for (auto NodeIt = AllocatedAnimNodeIndices.CreateConstIterator(); NodeIt; ++NodeIt)
		{
			if (UAnimGraphNode_Base* Node = NodeIt.Key())
			{
				Node->ValidateAnimNodePostCompile(MessageLog, NewAnimBlueprintClass, NodeIt.Value());
				bUsingCopyPoseFromMesh = bUsingCopyPoseFromMesh || Node->UsingCopyPoseFromMesh();
			}
		}

		// Update CDO
		if (UAnimInstance* const DefaultAnimInstance = Cast<UAnimInstance>(NewAnimBlueprintClass->GetDefaultObject()))
		{
			DefaultAnimInstance->bUsingCopyPoseFromMesh = bUsingCopyPoseFromMesh;
		}
	}
}

void FAnimBlueprintCompilerContext::CreateAnimGraphStubFunctions()
{
	TArray<UEdGraph*> NewGraphs;

	auto CreateStubForGraph = [this, &NewGraphs](UEdGraph* InGraph)
	{
		if(InGraph->Schema->IsChildOf(UAnimationGraphSchema::StaticClass()))
		{
			// Check to see if we are implementing an interface, and if so, use the signature from that graph instead
			// as we may not have yet been conformed to it (it happens later in compilation)
			UEdGraph* GraphToUseforSignature = InGraph;
			for(const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
			{
				UClass* InterfaceClass = InterfaceDesc.Interface;
				if(InterfaceClass)
				{
					if(UAnimBlueprint* InterfaceAnimBlueprint = Cast<UAnimBlueprint>(InterfaceClass->ClassGeneratedBy))
					{
						TArray<UEdGraph*> AllGraphs;
						InterfaceAnimBlueprint->GetAllGraphs(AllGraphs);
						UEdGraph** FoundSourceGraph = AllGraphs.FindByPredicate([InGraph](UEdGraph* InGraphToCheck){ return InGraphToCheck->GetFName() == InGraph->GetFName(); });
						if(FoundSourceGraph)
						{
							GraphToUseforSignature = *FoundSourceGraph;
							break;
						}
					}
				}
			}

			// Find the root and linked input pose nodes
			TArray<UAnimGraphNode_Root*> Roots;
			GraphToUseforSignature->GetNodesOfClass(Roots);

			TArray<UAnimGraphNode_LinkedInputPose*> LinkedInputPoseNodes;
			GraphToUseforSignature->GetNodesOfClass(LinkedInputPoseNodes);

			if(Roots.Num() > 0)
			{
				UAnimGraphNode_Root* RootNode = Roots[0];

				// Make sure there was only one root node
				for (int32 RootIndex = 1; RootIndex < Roots.Num(); ++RootIndex)
				{
					MessageLog.Error(
						*LOCTEXT("ExpectedOneRoot_Error", "Expected only one root node in graph @@, but found both @@ and @@").ToString(),
						InGraph,
						RootNode,
						Roots[RootIndex]
					);
				}

				// Verify no duplicate inputs
				for(UAnimGraphNode_LinkedInputPose* LinkedInputPoseNode0 : LinkedInputPoseNodes)
				{
					for(UAnimGraphNode_LinkedInputPose* LinkedInputPoseNode1 : LinkedInputPoseNodes)
					{
						if(LinkedInputPoseNode0 != LinkedInputPoseNode1)
						{
							if(LinkedInputPoseNode0->Node.Name == LinkedInputPoseNode1->Node.Name)
							{
								MessageLog.Error(
									*LOCTEXT("DuplicateInputNode_Error", "Found duplicate input node @@ in graph @@").ToString(),
									LinkedInputPoseNode1,
									InGraph
								);
							}
						}
					}
				}

				// Create a simple generated graph for our anim 'function'. Decorate it to avoid naming conflicts with the original graph.
				FName NewGraphName(*(GraphToUseforSignature->GetName() + ANIM_FUNC_DECORATOR));

				UEdGraph* StubGraph = NewObject<UEdGraph>(Blueprint, NewGraphName);
				NewGraphs.Add(StubGraph);
				StubGraph->Schema = UEdGraphSchema_K2::StaticClass();
				StubGraph->SetFlags(RF_Transient);

				// Add an entry node
				UK2Node_FunctionEntry* EntryNode = SpawnIntermediateNode<UK2Node_FunctionEntry>(RootNode, StubGraph);
				EntryNode->NodePosX = -200;
				EntryNode->CustomGeneratedFunctionName = GraphToUseforSignature->GetFName();	// Note that the function generated from this temporary graph is undecorated
				EntryNode->MetaData.Category = RootNode->Node.Group == NAME_None ? FText::GetEmpty() : FText::FromName(RootNode->Node.Group);

				// Add linked input poses as parameters
				for(UAnimGraphNode_LinkedInputPose* LinkedInputPoseNode : LinkedInputPoseNodes)
				{
					// Add user defined pins for each linked input pose
					TSharedPtr<FUserPinInfo> PosePinInfo = MakeShared<FUserPinInfo>();
					PosePinInfo->PinType = UAnimationGraphSchema::MakeLocalSpacePosePin();
					PosePinInfo->PinName = LinkedInputPoseNode->Node.Name;
					PosePinInfo->DesiredPinDirection = EGPD_Output;
					EntryNode->UserDefinedPins.Add(PosePinInfo);

					// Add user defined pins for each linked input pose parameter
					for(UEdGraphPin* LinkedInputPoseNodePin : LinkedInputPoseNode->Pins)
					{
						if(!LinkedInputPoseNodePin->bOrphanedPin && LinkedInputPoseNodePin->Direction == EGPD_Output && !UAnimationGraphSchema::IsPosePin(LinkedInputPoseNodePin->PinType))
						{
							TSharedPtr<FUserPinInfo> ParameterPinInfo = MakeShared<FUserPinInfo>();
							ParameterPinInfo->PinType = LinkedInputPoseNodePin->PinType;
							ParameterPinInfo->PinName = LinkedInputPoseNodePin->PinName;
							ParameterPinInfo->DesiredPinDirection = EGPD_Output;
							EntryNode->UserDefinedPins.Add(ParameterPinInfo);
						}
					}
				}
				EntryNode->AllocateDefaultPins();

				UEdGraphPin* EntryExecPin = EntryNode->FindPinChecked(UEdGraphSchema_K2::PN_Then, EGPD_Output);

				UK2Node_FunctionResult* ResultNode = SpawnIntermediateNode<UK2Node_FunctionResult>(RootNode, StubGraph);
				ResultNode->NodePosX = 200;

				// Add root as the 'return value'
				TSharedPtr<FUserPinInfo> PinInfo = MakeShared<FUserPinInfo>();
				PinInfo->PinType = UAnimationGraphSchema::MakeLocalSpacePosePin();
				PinInfo->PinName = GraphToUseforSignature->GetFName();
				PinInfo->DesiredPinDirection = EGPD_Input;
				ResultNode->UserDefinedPins.Add(PinInfo);
	
				ResultNode->AllocateDefaultPins();

				UEdGraphPin* ResultExecPin = ResultNode->FindPinChecked(UEdGraphSchema_K2::PN_Execute, EGPD_Input);

				// Link up entry to exit
				EntryExecPin->MakeLinkTo(ResultExecPin);
			}
			else
			{
				MessageLog.Error(*LOCTEXT("NoRootNodeFound_Error", "Could not find a root node for the graph @@").ToString(), InGraph);
			}
		}	
	};

	for(UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		CreateStubForGraph(Graph);
	}

	for(FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		for(UEdGraph* Graph : InterfaceDesc.Graphs)
		{
			CreateStubForGraph(Graph);
		}
	}

	Blueprint->FunctionGraphs.Append(NewGraphs);
	GeneratedStubGraphs.Append(NewGraphs);
}

void FAnimBlueprintCompilerContext::DestroyAnimGraphStubFunctions()
{
	Blueprint->FunctionGraphs.RemoveAll([this](UEdGraph* InGraph)
	{
		return GeneratedStubGraphs.Contains(InGraph);
	});

	GeneratedStubGraphs.Empty();
}

void FAnimBlueprintCompilerContext::PrecompileFunction(FKismetFunctionContext& Context, EInternalCompilerFlags InternalFlags)
{
	Super::PrecompileFunction(Context, InternalFlags);

	if(Context.Function)
	{
		auto CompareEntryPointName =
		[Function = Context.Function](UEdGraph* InGraph)
		{
			if(InGraph)
			{
				TArray<UK2Node_FunctionEntry*> EntryPoints;
				InGraph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryPoints);
				if(EntryPoints.Num() == 1 && EntryPoints[0])
				{
					return EntryPoints[0]->CustomGeneratedFunctionName == Function->GetFName(); 
				}
			}
			return true;
		};

		if(GeneratedStubGraphs.ContainsByPredicate(CompareEntryPointName))
		{
			Context.Function->SetMetaData(FBlueprintMetadata::MD_BlueprintInternalUseOnly, TEXT("true"));
			Context.Function->SetMetaData(FBlueprintMetadata::MD_AnimBlueprintFunction, TEXT("true"));
		}
	}
}

void FAnimBlueprintCompilerContext::SetCalculatedMetaDataAndFlags(UFunction* Function, UK2Node_FunctionEntry* EntryNode, const UEdGraphSchema_K2* K2Schema)
{
	Super::SetCalculatedMetaDataAndFlags(Function, EntryNode, K2Schema);

	if(Function)
	{
		auto CompareEntryPointName =
		[Function](UEdGraph* InGraph)
		{
			if(InGraph)
			{
				TArray<UK2Node_FunctionEntry*> EntryPoints;
				InGraph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryPoints);
				if(EntryPoints.Num() == 1 && EntryPoints[0])
				{
					return EntryPoints[0]->CustomGeneratedFunctionName == Function->GetFName(); 
				}
			}
			return true;
		};

		// Match by name to generated graph's entry points
		if(GeneratedStubGraphs.ContainsByPredicate(CompareEntryPointName))
		{
			Function->SetMetaData(FBlueprintMetadata::MD_BlueprintInternalUseOnly, TEXT("true"));
			Function->SetMetaData(FBlueprintMetadata::MD_AnimBlueprintFunction, TEXT("true"));
		}
	}
}

FProperty* FAnimBlueprintCompilerContext::CreateUniqueVariable(UObject* InForObject, const FEdGraphPinType& Type)
{
	const FString VariableName = ClassScopeNetNameMap.MakeValidName(InForObject);
	FProperty* Variable = CreateVariable(*VariableName, Type);
	Variable->SetMetaData(FBlueprintMetadata::MD_Private, TEXT("true"));
	return Variable;
}


//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE