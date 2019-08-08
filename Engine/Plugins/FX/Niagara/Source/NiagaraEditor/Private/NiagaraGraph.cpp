// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraGraph.h"
#include "Modules/ModuleManager.h"
#include "NiagaraCommon.h"
#include "NiagaraEditorModule.h"
#include "NiagaraScript.h"
#include "NiagaraComponent.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "ComponentReregisterContext.h"
#include "NiagaraConstants.h"
#include "NiagaraSystem.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeWriteDataSet.h"
#include "NiagaraNodeReadDataSet.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraDataInterface.h"
#include "GraphEditAction.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraNodeParameterMapBase.h"
#include "NiagaraNodeParameterMapGet.h"
#include "INiagaraEditorTypeUtilities.h"
#include "NiagaraConstants.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraNode.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraNodeStaticSwitch.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraHlslTranslator.h"
#include "NiagaraNodeFunctionCall.h"

DECLARE_CYCLE_STAT(TEXT("NiagaraEditor - Graph - FindInputNodes"), STAT_NiagaraEditor_Graph_FindInputNodes, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("NiagaraEditor - Graph - FindInputNodes_NotFilterUsage"), STAT_NiagaraEditor_Graph_FindInputNodes_NotFilterUsage, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("NiagaraEditor - Graph - FindInputNodes_FilterUsage"), STAT_NiagaraEditor_Graph_FindInputNodes_FilterUsage, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("NiagaraEditor - Graph - FindInputNodes_FilterDupes"), STAT_NiagaraEditor_Graph_FindInputNodes_FilterDupes, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("NiagaraEditor - Graph - FindInputNodes_FindInputNodes_Sort"), STAT_NiagaraEditor_Graph_FindInputNodes_Sort, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("NiagaraEditor - Graph - FindOutputNode"), STAT_NiagaraEditor_Graph_FindOutputNode, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("NiagaraEditor - Graph - BuildTraversalHelper"), STAT_NiagaraEditor_Graph_BuildTraversalHelper, STATGROUP_NiagaraEditor);

bool bWriteToLog = false;

#define LOCTEXT_NAMESPACE "NiagaraGraph"

FNiagaraGraphParameterReferenceCollection::FNiagaraGraphParameterReferenceCollection(const bool bInCreated)
	: Graph(nullptr), bCreated(bInCreated)
{
}

bool FNiagaraGraphParameterReferenceCollection::WasCreated() const
{
	return bCreated;
}

FNiagaraGraphScriptUsageInfo::FNiagaraGraphScriptUsageInfo() : UsageType(ENiagaraScriptUsage::Function)
{
}

void FNiagaraGraphScriptUsageInfo::PostLoad(UObject* Owner)
{
	const int32 NiagaraVer = Owner->GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);
	if (NiagaraVer < FNiagaraCustomVersion::UseHashesToIdentifyCompileStateOfTopLevelScripts)
	{
		// When loading old data use the last generated compile id as the base id to prevent recompiles on load for existing scripts.
		BaseId = GeneratedCompileId;

		if (CompileHash.IsValid() == false && DataHash_DEPRECATED.Num() == FNiagaraCompileHash::HashSize)
		{
			CompileHash = FNiagaraCompileHash(DataHash_DEPRECATED);
		}
	}
}

UNiagaraGraph::UNiagaraGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bNeedNumericCacheRebuilt(true)
	, bIsRenamingParameter(false)
	, bParameterReferenceRefreshPending(true)
	, bUnreferencedMetaDataPurgePending(true)
{
	Schema = UEdGraphSchema_Niagara::StaticClass();
	ChangeId = FGuid::NewGuid();
}

FDelegateHandle UNiagaraGraph::AddOnGraphNeedsRecompileHandler(const FOnGraphChanged::FDelegate& InHandler)
{
	return OnGraphNeedsRecompile.Add(InHandler);
}

void UNiagaraGraph::RemoveOnGraphNeedsRecompileHandler(FDelegateHandle Handle)
{
	OnGraphNeedsRecompile.Remove(Handle);
}

void UNiagaraGraph::NotifyGraphChanged(const FEdGraphEditAction& InAction)
{
	InvalidateCachedParameterData();
	if ((InAction.Action & GRAPHACTION_AddNode) != 0 || (InAction.Action & GRAPHACTION_RemoveNode) != 0 ||
		(InAction.Action & GRAPHACTION_GenericNeedsRecompile) != 0)
	{
		MarkGraphRequiresSynchronization(TEXT("Graph Changed"));
	}
	if ((InAction.Action & GRAPHACTION_GenericNeedsRecompile) != 0)
	{
		OnGraphNeedsRecompile.Broadcast(InAction);
		return;
	}
	Super::NotifyGraphChanged(InAction);
}

void UNiagaraGraph::NotifyGraphChanged()
{
	Super::NotifyGraphChanged();
	InvalidateCachedParameterData();
	InvalidateNumericCache();
}

void UNiagaraGraph::PostLoad()
{
	Super::PostLoad();

	for (FNiagaraGraphScriptUsageInfo& CachedUsageInfoItem : CachedUsageInfo)
	{
		CachedUsageInfoItem.PostLoad(this);
	}

	// In the past, we didn't bother setting the CallSortPriority and just used lexicographic ordering.
	// In the event that we have multiple non-matching nodes with a zero call sort priority, this will
	// give every node a unique order value.
	TArray<UNiagaraNodeInput*> InputNodes;
	GetNodesOfClass(InputNodes);
	bool bAllZeroes = true;
	TArray<FName> UniqueNames;
	for (UNiagaraNodeInput* InputNode : InputNodes)
	{
		if (InputNode->CallSortPriority != 0)
		{
			bAllZeroes = false;
		}

		if (InputNode->Usage == ENiagaraInputNodeUsage::Parameter)
		{
			UniqueNames.AddUnique(InputNode->Input.GetName());
		}

		if (InputNode->Usage == ENiagaraInputNodeUsage::SystemConstant)
		{
			InputNode->Input = FNiagaraConstants::UpdateEngineConstant(InputNode->Input);
		}
	}



	if (bAllZeroes && UniqueNames.Num() > 1)
	{
		// Just do the lexicographic sort and assign the call order to their ordered index value.
		UniqueNames.Sort(FNameLexicalLess());
		for (UNiagaraNodeInput* InputNode : InputNodes)
		{
			if (InputNode->Usage == ENiagaraInputNodeUsage::Parameter)
			{
				int32 FoundIndex = UniqueNames.Find(InputNode->Input.GetName());
				check(FoundIndex != -1);
				InputNode->CallSortPriority = FoundIndex;
			}
		}
	}

	// If this is from a prior version, enforce a valid Change Id!
	if (ChangeId.IsValid() == false)
	{
		MarkGraphRequiresSynchronization(TEXT("Graph change id was invalid"));
	}

	// Assume that all externally referenced assets have changed, so update to match. They will return true if they have changed.
	TArray<UNiagaraNode*> NiagaraNodes;
	GetNodesOfClass<UNiagaraNode>(NiagaraNodes);
	bool bAnyExternalChanges = false;
	for (UNiagaraNode* NiagaraNode : NiagaraNodes)
	{
		UObject* ReferencedAsset = NiagaraNode->GetReferencedAsset();
		if (ReferencedAsset != nullptr)
		{
			ReferencedAsset->ConditionalPostLoad();
			NiagaraNode->ConditionalPostLoad();
			if (NiagaraNode->RefreshFromExternalChanges())
			{
				bAnyExternalChanges = true;
				
			}
		}
		else
		{
			NiagaraNode->ConditionalPostLoad();
		}
	}

	RebuildCachedCompileIds();

	if (GIsEditor)
	{
		SetFlags(RF_Transactional);
	}

	// Migrate input condition metadata
	const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);

	if (NiagaraVer < FNiagaraCustomVersion::MetaDataAndParametersUpdate)
	{
		// If the version of the asset is older than FNiagaraCustomVersion::MetaDataAndParametersUpdate 
		// we need to migrate the old metadata by looping through VariableToMetaData_DEPRECATED
		// and create new entries in VariableToScriptVariable
		for (auto It = VariableToMetaData_DEPRECATED.CreateConstIterator(); It; ++It)
		{
			SetMetaData(It.Key(), It.Value());

			FString PathName = GetPathName();
			int ColonPos;
			if (PathName.FindChar(TCHAR('.'), ColonPos))
			{
				// GetPathName() returns something similar to "/Path/To/ScriptName.ScriptName:NiagaraScriptSource_N.NiagaraGraph_N"
				// so this will extract "/Path/To/ScriptName"
				PathName = PathName.Left(ColonPos);
			}
		}
		VariableToMetaData_DEPRECATED.Empty();
	}
	if (NiagaraVer < FNiagaraCustomVersion::MoveCommonInputMetadataToProperties)
	{
		auto MigrateInputCondition = [](TMap<FName, FString>& PropertyMetaData, const FName& InputConditionKey, FNiagaraInputConditionMetadata& InOutInputCondition)
		{
			FString* InputCondition = PropertyMetaData.Find(InputConditionKey);
			if (InputCondition != nullptr)
			{
				FString InputName;
				FString TargetValue;
				int32 EqualsIndex = InputCondition->Find("=");
				if (EqualsIndex == INDEX_NONE)
				{
					InOutInputCondition.InputName = **InputCondition;
				}
				else
				{
					InOutInputCondition.InputName = *InputCondition->Left(EqualsIndex);
					InOutInputCondition.TargetValues.Add(InputCondition->RightChop(EqualsIndex + 1));
				}
				PropertyMetaData.Remove(InputConditionKey);
			}
		};

		for (auto& VariableToScriptVariableItem : VariableToScriptVariable)
		{
			UNiagaraScriptVariable*& MetaData = VariableToScriptVariableItem.Value;

			// Migrate advanced display.
			if (MetaData->Metadata.PropertyMetaData.Contains("AdvancedDisplay"))
			{
				MetaData->Metadata.bAdvancedDisplay = true;
				MetaData->Metadata.PropertyMetaData.Remove("AdvancedDisplay");
			}

			// Migrate inline edit condition toggle
			if (MetaData->Metadata.PropertyMetaData.Contains("InlineEditConditionToggle"))
			{
				MetaData->Metadata.bInlineEditConditionToggle = true;
				MetaData->Metadata.PropertyMetaData.Remove("InlineEditConditionToggle");
			}

			// Migrate edit and visible conditions
			MigrateInputCondition(MetaData->Metadata.PropertyMetaData, TEXT("EditCondition"), MetaData->Metadata.EditCondition);
			MigrateInputCondition(MetaData->Metadata.PropertyMetaData, TEXT("VisibleCondition"), MetaData->Metadata.VisibleCondition);
		}
	}

	InvalidateCachedParameterData();
}

void UNiagaraGraph::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	NotifyGraphChanged();
}

class UNiagaraScriptSource* UNiagaraGraph::GetSource() const
{
	return CastChecked<UNiagaraScriptSource>(GetOuter());
}

FGuid UNiagaraGraph::ComputeCompileID(ENiagaraScriptUsage InUsage, const FGuid& InUsageId)
{
	RebuildCachedCompileIds();

	for (int32 j = 0; j < CachedUsageInfo.Num(); j++)
	{
		if (UNiagaraScript::IsEquivalentUsage(CachedUsageInfo[j].UsageType, InUsage) && CachedUsageInfo[j].UsageId == InUsageId)
		{
			return CachedUsageInfo[j].GeneratedCompileId;
		}
	}

	return FGuid();

}

FNiagaraCompileHash UNiagaraGraph::GetCompileDataHash(ENiagaraScriptUsage InUsage, const FGuid& InUsageId) const
{
	for (int32 i = 0; i < CachedUsageInfo.Num(); i++)
	{
		if (UNiagaraScript::IsEquivalentUsage(CachedUsageInfo[i].UsageType, InUsage) && CachedUsageInfo[i].UsageId == InUsageId)
		{
			return CachedUsageInfo[i].CompileHash;
		}
	}
	return FNiagaraCompileHash();
}

FGuid UNiagaraGraph::GetBaseId(ENiagaraScriptUsage InUsage, const FGuid& InUsageId) const
{
	for (int32 i = 0; i < CachedUsageInfo.Num(); i++)
	{
		if (UNiagaraScript::IsEquivalentUsage(CachedUsageInfo[i].UsageType, InUsage) && CachedUsageInfo[i].UsageId == InUsageId)
		{
			return CachedUsageInfo[i].BaseId;
		}
	}
	return FGuid();
}

void UNiagaraGraph::ForceBaseId(ENiagaraScriptUsage InUsage, const FGuid& InUsageId, const FGuid InForcedBaseId)
{
	FNiagaraGraphScriptUsageInfo* MatchingCachedUsageInfo = CachedUsageInfo.FindByPredicate([InUsage, InUsageId](const FNiagaraGraphScriptUsageInfo& CachedUsageInfoItem)
	{ 
		return CachedUsageInfoItem.UsageType == InUsage && CachedUsageInfoItem.UsageId == InUsageId; 
	});

	if (MatchingCachedUsageInfo == nullptr)
	{
		MatchingCachedUsageInfo = &CachedUsageInfo.AddDefaulted_GetRef();
		MatchingCachedUsageInfo->UsageType = InUsage;
		MatchingCachedUsageInfo->UsageId = InUsageId;
	}
	MatchingCachedUsageInfo->BaseId = InForcedBaseId;
}

UEdGraphPin* UNiagaraGraph::FindParameterMapDefaultValuePin(const FName VariableName, ENiagaraScriptUsage InUsage, ENiagaraScriptUsage InParentUsage) const
{
	TArray<UEdGraphPin*> MatchingDefaultPins;
	
	TArray<UNiagaraNode*> NodesTraversed;
	BuildTraversal(NodesTraversed, InUsage, FGuid());

	UEdGraphPin* DefaultInputPin = nullptr;
	for (UNiagaraNode* Node : NodesTraversed)
	{
		UNiagaraNodeParameterMapGet* GetNode = Cast<UNiagaraNodeParameterMapGet>(Node);
	
		if (GetNode)
		{
			TArray<UEdGraphPin*> OutputPins;
			GetNode->GetOutputPins(OutputPins);
			for (UEdGraphPin* OutputPin : OutputPins)
			{
				if (VariableName == OutputPin->PinName)
				{
					UEdGraphPin* Pin = GetNode->GetDefaultPin(OutputPin);
					if (Pin)
					{
						DefaultInputPin = Pin;
						break;
					}
				}
			}
		}

		if (DefaultInputPin != nullptr)
		{
			break;
		}
	}


	// There are some pins 
	if (DefaultInputPin && DefaultInputPin->LinkedTo.Num() != 0 && DefaultInputPin->LinkedTo[0] != nullptr)
	{
		UNiagaraNode* Owner = Cast<UNiagaraNode>(DefaultInputPin->LinkedTo[0]->GetOwningNode());
		UEdGraphPin* PreviousInput = DefaultInputPin;
		int32 NumIters = 0;
		while (Owner)
		{
			// Check to see if there are any reroute or choose by usage nodes involved in this..
			UEdGraphPin* InputPin = Owner->GetPassThroughPin(PreviousInput->LinkedTo[0], InParentUsage);
			if (InputPin == nullptr)
			{
				return PreviousInput;
			}
			else if (InputPin->LinkedTo.Num() == 0)
			{
				return InputPin;
			}

			check(InputPin->LinkedTo[0] != nullptr);
			Owner = Cast<UNiagaraNode>(InputPin->LinkedTo[0]->GetOwningNode());
			PreviousInput = InputPin;
			++NumIters;
			check(NumIters < Nodes.Num()); // If you hit this assert then we have a cycle in our graph somewhere.
		}
	}
	else
	{
		return DefaultInputPin;
	}

	return nullptr;
}

void UNiagaraGraph::FindOutputNodes(TArray<UNiagaraNodeOutput*>& OutputNodes) const
{
	for (UEdGraphNode* Node : Nodes)
	{
		if (UNiagaraNodeOutput* OutNode = Cast<UNiagaraNodeOutput>(Node))
		{
			OutputNodes.Add(OutNode);
		}
	}
}


void UNiagaraGraph::FindOutputNodes(ENiagaraScriptUsage TargetUsageType, TArray<UNiagaraNodeOutput*>& OutputNodes) const
{
	TArray<UNiagaraNodeOutput*> NodesFound;
	for (UEdGraphNode* Node : Nodes)
	{
		UNiagaraNodeOutput* OutNode = Cast<UNiagaraNodeOutput>(Node);
		if (OutNode && OutNode->GetUsage() == TargetUsageType)
		{
			NodesFound.Add(OutNode);
		}
	}

	OutputNodes = NodesFound;
}

void UNiagaraGraph::FindEquivalentOutputNodes(ENiagaraScriptUsage TargetUsageType, TArray<UNiagaraNodeOutput*>& OutputNodes) const
{
	TArray<UNiagaraNodeOutput*> NodesFound;
	for (UEdGraphNode* Node : Nodes)
	{
		UNiagaraNodeOutput* OutNode = Cast<UNiagaraNodeOutput>(Node);
		if (OutNode && UNiagaraScript::IsEquivalentUsage(OutNode->GetUsage(), TargetUsageType))
		{
			NodesFound.Add(OutNode);
		}
	}

	OutputNodes = NodesFound;
}

UNiagaraNodeOutput* UNiagaraGraph::FindOutputNode(ENiagaraScriptUsage TargetUsageType, FGuid TargetUsageId) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Graph_FindOutputNode);
	for (UEdGraphNode* Node : Nodes)
	{
		if (UNiagaraNodeOutput* OutNode = Cast<UNiagaraNodeOutput>(Node))
		{
			if (OutNode->GetUsage() == TargetUsageType && OutNode->GetUsageId() == TargetUsageId)
			{
				return OutNode;
			}
		}
	}
	return nullptr;
}

UNiagaraNodeOutput* UNiagaraGraph::FindEquivalentOutputNode(ENiagaraScriptUsage TargetUsageType, FGuid TargetUsageId) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Graph_FindOutputNode);
	for (UEdGraphNode* Node : Nodes)
	{
		if (UNiagaraNodeOutput* OutNode = Cast<UNiagaraNodeOutput>(Node))
		{
			if (UNiagaraScript::IsEquivalentUsage(OutNode->GetUsage(), TargetUsageType) && OutNode->GetUsageId() == TargetUsageId)
			{
				return OutNode;
			}
		}
	}
	return nullptr;
}


void BuildTraversalHelper(TArray<class UNiagaraNode*>& OutNodesTraversed, UNiagaraNode* CurrentNode)
{
	if (CurrentNode == nullptr)
	{
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Graph_BuildTraversalHelper);

	TArray<UEdGraphPin*> Pins = CurrentNode->GetAllPins();
	for (int32 i = 0; i < Pins.Num(); i++)
	{
		if (Pins[i]->Direction == EEdGraphPinDirection::EGPD_Input && Pins[i]->LinkedTo.Num() == 1)
		{
			UNiagaraNode* Node = Cast<UNiagaraNode>(Pins[i]->LinkedTo[0]->GetOwningNode());
			if (OutNodesTraversed.Contains(Node))
			{
				continue;
			}
			BuildTraversalHelper(OutNodesTraversed, Node);
		}
	}

	OutNodesTraversed.Add(CurrentNode);
}

void UNiagaraGraph::BuildTraversal(TArray<class UNiagaraNode*>& OutNodesTraversed, ENiagaraScriptUsage TargetUsage, FGuid TargetUsageId) const
{
	UNiagaraNodeOutput* Output = FindOutputNode(TargetUsage, TargetUsageId);
	if (Output)
	{
		BuildTraversalHelper(OutNodesTraversed, Output);
	}
}

void UNiagaraGraph::BuildTraversal(TArray<class UNiagaraNode*>& OutNodesTraversed, UNiagaraNode* FinalNode) 
{
	if (FinalNode)
	{
		BuildTraversalHelper(OutNodesTraversed, FinalNode);
	}
}


void UNiagaraGraph::FindInputNodes(TArray<UNiagaraNodeInput*>& OutInputNodes, UNiagaraGraph::FFindInputNodeOptions Options) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Graph_FindInputNodes);
	TArray<UNiagaraNodeInput*> InputNodes;

	if (!Options.bFilterByScriptUsage)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Graph_FindInputNodes_NotFilterUsage);

		for (UEdGraphNode* Node : Nodes)
		{
			UNiagaraNodeInput* NiagaraInputNode = Cast<UNiagaraNodeInput>(Node);
			if (NiagaraInputNode != nullptr &&
				((NiagaraInputNode->Usage == ENiagaraInputNodeUsage::Parameter && Options.bIncludeParameters) ||
				(NiagaraInputNode->Usage == ENiagaraInputNodeUsage::Attribute && Options.bIncludeAttributes) ||
				(NiagaraInputNode->Usage == ENiagaraInputNodeUsage::SystemConstant && Options.bIncludeSystemConstants) || 
				(NiagaraInputNode->Usage == ENiagaraInputNodeUsage::TranslatorConstant && Options.bIncludeTranslatorConstants)))
			{
				InputNodes.Add(NiagaraInputNode);
			}
		}
	}
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Graph_FindInputNodes_FilterUsage);

		TArray<class UNiagaraNode*> Traversal;
		BuildTraversal(Traversal, Options.TargetScriptUsage, Options.TargetScriptUsageId);
		for (UNiagaraNode* Node : Traversal)
		{
			UNiagaraNodeInput* NiagaraInputNode = Cast<UNiagaraNodeInput>(Node);
			if (NiagaraInputNode != nullptr &&
				((NiagaraInputNode->Usage == ENiagaraInputNodeUsage::Parameter && Options.bIncludeParameters) ||
					(NiagaraInputNode->Usage == ENiagaraInputNodeUsage::Attribute && Options.bIncludeAttributes) ||
					(NiagaraInputNode->Usage == ENiagaraInputNodeUsage::SystemConstant && Options.bIncludeSystemConstants)))
			{
				InputNodes.Add(NiagaraInputNode);
			}
		}
	}

	if (Options.bFilterDuplicates)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Graph_FindInputNodes_FilterDupes);

		for (UNiagaraNodeInput* InputNode : InputNodes)
		{
			auto NodeMatches = [=](UNiagaraNodeInput* UniqueInputNode)
			{
				if (InputNode->Usage == ENiagaraInputNodeUsage::Parameter)
				{
					return UniqueInputNode->Input.IsEquivalent(InputNode->Input, false);
				}
				else
				{
					return UniqueInputNode->Input.IsEquivalent(InputNode->Input);
				}
			};

			if (OutInputNodes.ContainsByPredicate(NodeMatches) == false)
			{
				OutInputNodes.Add(InputNode);
			}
		}
	}
	else
	{
		OutInputNodes.Append(InputNodes);
	}

	if (Options.bSort)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Graph_FindInputNodes_Sort);

		UNiagaraNodeInput::SortNodes(OutInputNodes);
	}
}

TArray<FNiagaraVariable> UNiagaraGraph::FindStaticSwitchInputs(bool bReachableOnly) const
{
	TArray<UEdGraphNode*> NodesToProcess = bReachableOnly ? FindReachbleNodes() : Nodes;

	TArray<FNiagaraVariable> Result;
	for (UEdGraphNode* Node : NodesToProcess)
	{
		UNiagaraNodeStaticSwitch* SwitchNode = Cast<UNiagaraNodeStaticSwitch>(Node);
		if (SwitchNode && !SwitchNode->IsSetByCompiler())
		{
			FNiagaraVariable Variable(SwitchNode->GetInputType(), SwitchNode->InputParameterName);
			Result.AddUnique(Variable);
		}

		if (UNiagaraNodeFunctionCall* FunctionNode = Cast<UNiagaraNodeFunctionCall>(Node))
		{
			for (const FNiagaraPropagatedVariable& Propagated : FunctionNode->PropagatedStaticSwitchParameters)
			{
				Result.AddUnique(Propagated.ToVariable());
			}			
		}
	}
	Result.Sort([](const FNiagaraVariable& Left, const FNiagaraVariable& Right)
	{
		return Left.GetName().LexicalLess(Right.GetName());
	});
	return Result;
}

TArray<UEdGraphNode*> UNiagaraGraph::FindReachbleNodes() const
{
	TArray<UEdGraphNode*> ResultNodes;
	TArray<UNiagaraNodeOutput*> OutNodes;
	FindOutputNodes(OutNodes);
	ResultNodes.Append(OutNodes);

	for (int i = 0; i < ResultNodes.Num(); i++)
	{
		UEdGraphNode* Node = ResultNodes[i];
		if (Node == nullptr)
		{
			continue;
		}
		
		UNiagaraNodeStaticSwitch* SwitchNode = Cast<UNiagaraNodeStaticSwitch>(Node);
		if (SwitchNode)
		{
			TArray<UEdGraphPin*> OutPins;
			SwitchNode->GetOutputPins(OutPins);
			for (UEdGraphPin* Pin : OutPins)
			{
				UEdGraphPin* TracedPin = SwitchNode->GetTracedOutputPin(Pin, false);
				if (TracedPin && TracedPin != Pin)
				{
					ResultNodes.AddUnique(TracedPin->GetOwningNode());
				}
			}
		}
		else
		{
			TArray<UEdGraphPin*> InputPins;
			for (UEdGraphPin* Pin : Node->GetAllPins())
			{
				if (!Pin || Pin->Direction != EEdGraphPinDirection::EGPD_Input)
				{
					continue;
				}
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin)
					{
						continue;
					}
					ResultNodes.AddUnique(LinkedPin->GetOwningNode());
				}
			}
		}
	}
	return ResultNodes;
}

void UNiagaraGraph::GetParameters(TArray<FNiagaraVariable>& Inputs, TArray<FNiagaraVariable>& Outputs)const
{
	Inputs.Empty();
	Outputs.Empty();

	TArray<UNiagaraNodeInput*> InputsNodes;
	FFindInputNodeOptions Options;
	Options.bSort = true;
	FindInputNodes(InputsNodes, Options);
	for (UNiagaraNodeInput* Input : InputsNodes)
	{
		Inputs.Add(Input->Input);
	}

	TArray<UNiagaraNodeOutput*> OutputNodes;
	FindOutputNodes(OutputNodes);
	for (UNiagaraNodeOutput* OutputNode : OutputNodes)
	{
		for (FNiagaraVariable& Var : OutputNode->Outputs)
		{
			Outputs.AddUnique(Var);
		}
	}

	//Do we need to sort outputs?
	//Should leave them as they're defined in the output node?
// 	auto SortVars = [](const FNiagaraVariable& A, const FNiagaraVariable& B)
// 	{
// 		//Case insensitive lexicographical comparisons of names for sorting.
// 		return A.GetName().ToString() < B.GetName().ToString();
// 	};
// 	Outputs.Sort(SortVars);
}

const TMap<FNiagaraVariable, UNiagaraScriptVariable*>& UNiagaraGraph::GetAllMetaData() const
{
	if (bUnreferencedMetaDataPurgePending)
	{
		PurgeUnreferencedMetaData();
	}
	return VariableToScriptVariable;
}

TMap<FNiagaraVariable, UNiagaraScriptVariable*>& UNiagaraGraph::GetAllMetaData()
{
	if (bUnreferencedMetaDataPurgePending)
	{
		PurgeUnreferencedMetaData();
	}
	return VariableToScriptVariable;
}

const TMap<FNiagaraVariable, FNiagaraGraphParameterReferenceCollection>& UNiagaraGraph::GetParameterReferenceMap() const
{
	if (bParameterReferenceRefreshPending)
	{
		RefreshParameterReferences();
	}
	return ParameterToReferencesMap;
}

void UNiagaraGraph::AddParameter(const FNiagaraVariable& Parameter)
{
	FNiagaraGraphParameterReferenceCollection* FoundParameterReferenceCollection = ParameterToReferencesMap.Find(Parameter);
	if (!FoundParameterReferenceCollection)
	{
		FNiagaraGraphParameterReferenceCollection NewReferenceCollection = FNiagaraGraphParameterReferenceCollection(true /*bCreated*/);
		NewReferenceCollection.Graph = this;
		ParameterToReferencesMap.Add(Parameter, NewReferenceCollection);
	}

	UNiagaraScriptVariable** FoundScriptVariable = VariableToScriptVariable.Find(Parameter);
	if (!FoundScriptVariable)
	{
		UNiagaraScriptVariable* NewScriptVariable = NewObject<UNiagaraScriptVariable>(this);
		NewScriptVariable->Variable = Parameter;
		VariableToScriptVariable.Add(Parameter, NewScriptVariable);
	}
}

void UNiagaraGraph::RemoveParameter(const FNiagaraVariable& Parameter)
{
	FNiagaraGraphParameterReferenceCollection* ReferenceCollection = ParameterToReferencesMap.Find(Parameter);
	if (ReferenceCollection)
	{
		for (int32 Index = 0; Index < ReferenceCollection->ParameterReferences.Num(); Index++)
		{
			const FNiagaraGraphParameterReference& Reference = ReferenceCollection->ParameterReferences[Index];
			UNiagaraNode* Node = Reference.Value.Get();
			if (Node && Node->GetGraph() == this)
			{
				UEdGraphPin* Pin = Node->GetPinByPersistentGuid(Reference.Key);
				if (Pin)
				{
					Node->RemovePin(Pin);
				}
			}
		}

		// Remove it from the reference collection directly because it might have been user added and
		// these aren't removed when the cached data is rebuilt.
		ParameterToReferencesMap.Remove(Parameter);
		NotifyGraphChanged();
	}
}

bool UNiagaraGraph::RenameParameter(const FNiagaraVariable& Parameter, FName NewName)
{
	// Block rename when already renaming. This prevents recursion when CommitEditablePinName is called on referenced nodes. 
	if (bIsRenamingParameter)
	{
		return false;
	}
	bIsRenamingParameter = true;

	// Create the new parameter
	FNiagaraVariable NewParameter = Parameter;
	NewParameter.SetName(NewName);

	UNiagaraScriptVariable** OldScriptVariable = VariableToScriptVariable.Find(Parameter);
	FNiagaraVariableMetaData OldMetaData;
	if (OldScriptVariable)
	{
		OldMetaData = (*OldScriptVariable)->Metadata;
	}
		

	FNiagaraGraphParameterReferenceCollection* ReferenceCollection = ParameterToReferencesMap.Find(Parameter);
	if (ReferenceCollection)
	{
		const FText NewNameText = FText::FromName(NewName);
		FNiagaraGraphParameterReferenceCollection NewReferences = *ReferenceCollection;
		for (FNiagaraGraphParameterReference& Reference : NewReferences.ParameterReferences)
		{
			UNiagaraNode* Node = Reference.Value.Get();
			if (Node && Node->GetGraph() == this)
			{
				UEdGraphPin* Pin = Node->GetPinByPersistentGuid(Reference.Key);
				if (Pin)
				{
					Node->CommitEditablePinName(NewNameText, Pin);
				}
			}
		}

		ParameterToReferencesMap.Remove(Parameter);
		ParameterToReferencesMap.Add(NewParameter, NewReferences);
	}

	// Swap metadata to the new parameter
	if (UNiagaraScriptVariable** Metadata = VariableToScriptVariable.Find(Parameter))
	{
		VariableToScriptVariable.Remove(Parameter);
	}
	SetMetaData(NewParameter, OldMetaData);

	bIsRenamingParameter = false;

	NotifyGraphChanged();
	return true;
}

int32 UNiagaraGraph::GetOutputNodeVariableIndex(const FNiagaraVariable& Variable)const
{
	TArray<FNiagaraVariable> Variables;
	GetOutputNodeVariables(Variables);
	return Variables.Find(Variable);
}

void UNiagaraGraph::GetOutputNodeVariables(TArray< FNiagaraVariable >& OutVariables)const
{
	TArray<UNiagaraNodeOutput*> OutputNodes;
	FindOutputNodes(OutputNodes);
	for (UNiagaraNodeOutput* OutputNode : OutputNodes)
	{
		for (FNiagaraVariable& Var : OutputNode->Outputs)
		{
			OutVariables.AddUnique(Var);
		}
	}
}

void UNiagaraGraph::GetOutputNodeVariables(ENiagaraScriptUsage InScriptUsage, TArray< FNiagaraVariable >& OutVariables)const
{
	TArray<UNiagaraNodeOutput*> OutputNodes;
	FindOutputNodes(InScriptUsage, OutputNodes);
	for (UNiagaraNodeOutput* OutputNode : OutputNodes)
	{
		for (FNiagaraVariable& Var : OutputNode->Outputs)
		{
			OutVariables.AddUnique(Var);
		}
	}
}

bool UNiagaraGraph::HasParameterMapParameters()const
{
	TArray<FNiagaraVariable> Inputs;
	TArray<FNiagaraVariable> Outputs;

	GetParameters(Inputs, Outputs);

	for (FNiagaraVariable& Var : Inputs)
	{
		if (Var.GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
		{
			return true;
		}
	}
	for (FNiagaraVariable& Var : Outputs)
	{
		if (Var.GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
		{
			return true;
		}
	}

	return false;
}

bool UNiagaraGraph::HasNumericParameters()const
{
	TArray<FNiagaraVariable> Inputs;
	TArray<FNiagaraVariable> Outputs;
	
	GetParameters(Inputs, Outputs);
	
	for (FNiagaraVariable& Var : Inputs)
	{
		if (Var.GetType() == FNiagaraTypeDefinition::GetGenericNumericDef())
		{
			return true;
		}
	}
	for (FNiagaraVariable& Var : Outputs)
	{
		if (Var.GetType() == FNiagaraTypeDefinition::GetGenericNumericDef())
		{
			return true;
		}
	}

	return false;
}

void UNiagaraGraph::NotifyGraphNeedsRecompile()
{
	FEdGraphEditAction Action;
	Action.Action = (EEdGraphActionType)GRAPHACTION_GenericNeedsRecompile;
	NotifyGraphChanged(Action);
}


void UNiagaraGraph::NotifyGraphDataInterfaceChanged()
{
	OnDataInterfaceChangedDelegate.Broadcast();
}

void UNiagaraGraph::SubsumeExternalDependencies(TMap<const UObject*, UObject*>& ExistingConversions)
{
	TArray<UNiagaraNode*> NiagaraNodes;
	GetNodesOfClass<UNiagaraNode>(NiagaraNodes);
	for (UNiagaraNode* NiagaraNode : NiagaraNodes)
	{
		NiagaraNode->SubsumeExternalDependencies(ExistingConversions);
	}
}

FNiagaraTypeDefinition UNiagaraGraph::GetCachedNumericConversion(class UEdGraphPin* InPin)
{
	if (bNeedNumericCacheRebuilt)
	{
		RebuildNumericCache();
	}

	FNiagaraTypeDefinition ReturnDef;
	if (InPin && InPin->PinId.IsValid())
	{
		FNiagaraTypeDefinition* FoundDef = CachedNumericConversions.Find(TPair<FGuid, UEdGraphNode*>(InPin->PinId, InPin->GetOwningNode()));
		if (FoundDef)
		{
			ReturnDef = *FoundDef;
		}
	}
	return ReturnDef;
}

void UNiagaraGraph::RebuildCachedCompileIds(bool bForce)
{
	// If the graph hasn't changed since last rebuild, then do nothing.
	if (!bForce && ChangeId == LastBuiltTraversalDataChangeId && LastBuiltTraversalDataChangeId.IsValid())
	{
		return;
	}

	// First find all the output nodes
	TArray<UNiagaraNodeOutput*> NiagaraOutputNodes;
	GetNodesOfClass<UNiagaraNodeOutput>(NiagaraOutputNodes);

	// Now build the new cache..
	TArray<FNiagaraGraphScriptUsageInfo> NewUsageCache;
	NewUsageCache.AddDefaulted(NiagaraOutputNodes.Num());

	UEnum* FoundEnum = nullptr;
	bool bNeedsAnyNewCompileIds = false;

	FNiagaraGraphScriptUsageInfo* ParticleSpawnUsageInfo = nullptr;
	FNiagaraGraphScriptUsageInfo* ParticleUpdateUsageInfo = nullptr;

	for (int32 i = 0; i < NiagaraOutputNodes.Num(); i++)
	{
		UNiagaraNodeOutput* OutputNode = NiagaraOutputNodes[i];
		NewUsageCache[i].UsageType = OutputNode->GetUsage();
		NewUsageCache[i].UsageId = OutputNode->GetUsageId();

		BuildTraversal(NewUsageCache[i].Traversal, OutputNode);

		int32 FoundMatchIdx = INDEX_NONE;
		for (int32 j = 0; j < CachedUsageInfo.Num(); j++)
		{
			if (UNiagaraScript::IsEquivalentUsage(CachedUsageInfo[j].UsageType, NewUsageCache[i].UsageType) && CachedUsageInfo[j].UsageId == NewUsageCache[i].UsageId)
			{
				FoundMatchIdx = j;
				break;
			}
		}

		if (FoundMatchIdx == INDEX_NONE || CachedUsageInfo[FoundMatchIdx].BaseId.IsValid() == false)
		{
			NewUsageCache[i].BaseId = FGuid::NewGuid();
		}
		else
		{
			//Copy the old base id if available and valid.
			NewUsageCache[i].BaseId = CachedUsageInfo[FoundMatchIdx].BaseId;
		}

		// Now compare the change id's of all the nodes in the traversal by hashing them up and comparing the hash
		// now with the hash from previous runs.
		FSHA1 HashState;
		for (UNiagaraNode* Node : NewUsageCache[i].Traversal)
		{
			Node->UpdateCompileHashForNode(HashState);
		}
		HashState.Final();

		// We can't store in a FShaHash struct directly because you can't UProperty it. Using a standin of the same size.
		TArray<uint8> DataHash;
		DataHash.AddUninitialized(20);
		HashState.GetHash(DataHash.GetData());
		NewUsageCache[i].CompileHash = FNiagaraCompileHash(DataHash);

		bool bNeedsNewCompileId = true;

		// Now compare the hashed data. If it is the same as before, then leave the compile ID as-is. If it is different, generate a new guid.
		if (FoundMatchIdx != INDEX_NONE)
		{
			if (NewUsageCache[i].CompileHash == CachedUsageInfo[FoundMatchIdx].CompileHash)
			{
				NewUsageCache[i].GeneratedCompileId = CachedUsageInfo[FoundMatchIdx].GeneratedCompileId;
				bNeedsNewCompileId = false;
			}
		}

		if (bNeedsNewCompileId)
		{
			NewUsageCache[i].GeneratedCompileId = FGuid::NewGuid();
			bNeedsAnyNewCompileIds = true;
		}

		// TODO sckime debug logic... should be disabled or put under a cvar in the future
		{

			if (FoundEnum == nullptr)
			{
				FoundEnum = StaticEnum<ENiagaraScriptUsage>();
			}

			FString ResultsEnum = TEXT("??");
			if (FoundEnum)
			{
				ResultsEnum = FoundEnum->GetNameStringByValue((int64)NewUsageCache[i].UsageType);
			}

			if (bNeedsNewCompileId)
			{
				//UE_LOG(LogNiagaraEditor, Log, TEXT("'%s' changes detected in %s .. new guid: %s"), *GetFullName(), *ResultsEnum, *NewUsageCache[i].GeneratedCompileId.ToString());
			}
			else
			{
				//UE_LOG(LogNiagaraEditor, Log, TEXT("'%s' changes NOT detected in %s .. keeping guid: %s"), *GetFullName(), *ResultsEnum, *NewUsageCache[i].GeneratedCompileId.ToString());
			}
		}

		if (UNiagaraScript::IsEquivalentUsage(NewUsageCache[i].UsageType, ENiagaraScriptUsage::ParticleSpawnScript) && NewUsageCache[i].UsageId == FGuid())
		{
			ParticleSpawnUsageInfo = &NewUsageCache[i];
		}

		if (UNiagaraScript::IsEquivalentUsage(NewUsageCache[i].UsageType, ENiagaraScriptUsage::ParticleUpdateScript) && NewUsageCache[i].UsageId == FGuid())
		{
			ParticleUpdateUsageInfo = &NewUsageCache[i];
		}
	}

	if (ParticleSpawnUsageInfo != nullptr && ParticleUpdateUsageInfo != nullptr)
	{
		// If we have info for both spawn and update generate the gpu version too.
		FNiagaraGraphScriptUsageInfo GpuUsageInfo;
		GpuUsageInfo.UsageType = ENiagaraScriptUsage::ParticleGPUComputeScript;
		GpuUsageInfo.UsageId = FGuid();

		FNiagaraGraphScriptUsageInfo* OldGpuInfo = CachedUsageInfo.FindByPredicate(
			[](const FNiagaraGraphScriptUsageInfo& OldInfo) { return OldInfo.UsageType == ENiagaraScriptUsage::ParticleGPUComputeScript && OldInfo.UsageId == FGuid(); });
		if (OldGpuInfo == nullptr || OldGpuInfo->BaseId.IsValid() == false)
		{
			GpuUsageInfo.BaseId = FGuid::NewGuid();
		}
		else
		{
			// Copy the old base id if available
			GpuUsageInfo.BaseId = OldGpuInfo->BaseId;
		}

		GpuUsageInfo.Traversal.Append(ParticleSpawnUsageInfo->Traversal);
		GpuUsageInfo.Traversal.Append(ParticleUpdateUsageInfo->Traversal);

		FSHA1 HashState;
		for (UNiagaraNode* Node : GpuUsageInfo.Traversal)
		{
			Node->UpdateCompileHashForNode(HashState);
		}
		HashState.Final();

		TArray<uint8> DataHash;
		DataHash.AddUninitialized(20);
		HashState.GetHash(DataHash.GetData());
		GpuUsageInfo.CompileHash = FNiagaraCompileHash(DataHash);

		FNiagaraGraphScriptUsageInfo* OldGpuUsageInfo = CachedUsageInfo.FindByPredicate([](const FNiagaraGraphScriptUsageInfo& UsageInfo) { return UsageInfo.UsageType == ENiagaraScriptUsage::ParticleGPUComputeScript && UsageInfo.UsageId == FGuid(); });
		if (OldGpuUsageInfo != nullptr && OldGpuUsageInfo->CompileHash == GpuUsageInfo.CompileHash)
		{
			GpuUsageInfo.GeneratedCompileId = OldGpuUsageInfo->GeneratedCompileId;
		}
		else
		{
			GpuUsageInfo.GeneratedCompileId = FGuid::NewGuid();
		}

		NewUsageCache.Add(GpuUsageInfo);
	}

	// Debug logic, usually disabled at top of file.
	if (bNeedsAnyNewCompileIds && bWriteToLog)
	{
		TMap<FGuid, FGuid> ComputeChangeIds;
		FNiagaraEditorUtilities::GatherChangeIds(*this, ComputeChangeIds, GetName());
	}

	// Now update the cache with the newly computed results.
	CachedUsageInfo = NewUsageCache;
	LastBuiltTraversalDataChangeId = ChangeId;

	RebuildNumericCache();
}

void UNiagaraGraph::CopyCachedReferencesMap(UNiagaraGraph* TargetGraph)
{
	TargetGraph->ParameterToReferencesMap = ParameterToReferencesMap;
}

const class UEdGraphSchema_Niagara* UNiagaraGraph::GetNiagaraSchema() const
{
	return Cast<UEdGraphSchema_Niagara>(GetSchema());
}

void UNiagaraGraph::RebuildNumericCache()
{
	CachedNumericConversions.Empty();
	TMap<UNiagaraNode*, bool> VisitedNodes;
	for (UEdGraphNode* Node : Nodes)
	{
		ResolveNumerics(VisitedNodes, Node);
	}
	bNeedNumericCacheRebuilt = false;
}

void UNiagaraGraph::InvalidateNumericCache()
{
	bNeedNumericCacheRebuilt = true; 
	CachedNumericConversions.Empty();
}

FString UNiagaraGraph::GetFunctionAliasByContext(const FNiagaraGraphFunctionAliasContext& FunctionAliasContext)
{
	FString FunctionAlias;
	for (UEdGraphNode* Node : Nodes)
	{
		UNiagaraNode* NiagaraNode = Cast<UNiagaraNode>(Node);
		if (NiagaraNode != nullptr)
		{
			NiagaraNode->AppendFunctionAliasForContext(FunctionAliasContext, FunctionAlias);
		}
	}

	for (UEdGraphPin* Pin : FunctionAliasContext.StaticSwitchValues)
	{
		FunctionAlias += TEXT("_") + FHlslNiagaraTranslator::GetSanitizedFunctionNameSuffix(Pin->GetName()) 
			+ TEXT("_") + FHlslNiagaraTranslator::GetSanitizedFunctionNameSuffix(Pin->DefaultValue);
	}
	return FunctionAlias;
}

void UNiagaraGraph::ResolveNumerics(TMap<UNiagaraNode*, bool>& VisitedNodes, UEdGraphNode* Node)
{
	UNiagaraNode* NiagaraNode = Cast<UNiagaraNode>(Node);
	if (NiagaraNode)
	{
		TArray<UEdGraphPin*> InputPins;
		NiagaraNode->GetInputPins(InputPins);
		for (int32 i = 0; i < InputPins.Num(); i++)
		{
			if (InputPins[i])
			{
				for (int32 j = 0; j < InputPins[i]->LinkedTo.Num(); j++)
				{
					UNiagaraNode* FoundNode = Cast<UNiagaraNode>(InputPins[i]->LinkedTo[j]->GetOwningNode());
					if (!FoundNode || VisitedNodes.Contains(FoundNode))
					{
						continue;
					}
					VisitedNodes.Add(FoundNode, true);
					ResolveNumerics(VisitedNodes, FoundNode);
				}
			}
		}

		NiagaraNode->ResolveNumerics(GetNiagaraSchema(), false, &CachedNumericConversions);
		
	}
}


void UNiagaraGraph::SynchronizeInternalCacheWithGraph(UNiagaraGraph* Other)
{
	// Force us to rebuild the cache, note that this builds traversals and everything else, keeping it in sync if nothing changed from the current version.
	RebuildCachedCompileIds(true);
	
	UEnum* FoundEnum = nullptr;

	// Now go through all of the other graph's usage info. If we find a match for its usage and our data hashes match, use the generated compile id from
	// the other graph.
	for (int32 i = 0; i < CachedUsageInfo.Num(); i++)
	{
		int32 FoundMatchIdx = INDEX_NONE;
		for (int32 j = 0; j < Other->CachedUsageInfo.Num(); j++)
		{
			if (UNiagaraScript::IsEquivalentUsage(Other->CachedUsageInfo[j].UsageType, CachedUsageInfo[i].UsageType) && Other->CachedUsageInfo[j].UsageId == CachedUsageInfo[i].UsageId)
			{
				FoundMatchIdx = j;
				break;
			}
		}

		if (FoundMatchIdx != INDEX_NONE)
		{
			if (CachedUsageInfo[i].CompileHash == Other->CachedUsageInfo[FoundMatchIdx].CompileHash)
			{
				CachedUsageInfo[i].GeneratedCompileId = Other->CachedUsageInfo[FoundMatchIdx].GeneratedCompileId;		

				// TODO sckime debug logic... should be disabled or put under a cvar in the future
				{
					if (FoundEnum == nullptr)
					{
						FoundEnum = StaticEnum<ENiagaraScriptUsage>();
					}

					FString ResultsEnum = TEXT("??");
					if (FoundEnum)
					{
						ResultsEnum = FoundEnum->GetNameStringByValue((int64)CachedUsageInfo[i].UsageType);
					}
					if (GEnableVerboseNiagaraChangeIdLogging)
					{
						UE_LOG(LogNiagaraEditor, Log, TEXT("'%s' changes synchronized with master script in %s .. synced guid: %s"), *GetFullName(), *ResultsEnum, *CachedUsageInfo[i].GeneratedCompileId.ToString());
					}
				}
			}
		}
	}

	if (bWriteToLog)
	{
		TMap<FGuid, FGuid> ComputeChangeIds;
		FNiagaraEditorUtilities::GatherChangeIds(*this, ComputeChangeIds, GetName() + TEXT(".Synced"));
	}
}


void UNiagaraGraph::InvalidateCachedCompileIds()
{
	Modify();
	CachedUsageInfo.Empty();
	MarkGraphRequiresSynchronization(__FUNCTION__);
}

void UNiagaraGraph::GatherExternalDependencyIDs(ENiagaraScriptUsage InUsage, const FGuid& InUsageId, TArray<FNiagaraCompileHash>& InReferencedCompileHashes, TArray<FGuid>& InReferencedIDs, TArray<UObject*>& InReferencedObjs)
{
	RebuildCachedCompileIds();
	
	// Particle compute scripts get all particle scripts baked into their dependency chain. 
	if (InUsage == ENiagaraScriptUsage::ParticleGPUComputeScript)
	{
		for (int32 i = 0; i < CachedUsageInfo.Num(); i++)
		{
			// Add all chains that we depend on.
			if (UNiagaraScript::IsUsageDependentOn(InUsage, CachedUsageInfo[i].UsageType)) 
			{
				InReferencedCompileHashes.Add(CachedUsageInfo[i].CompileHash);
				InReferencedObjs.Add(CachedUsageInfo[i].Traversal.Last());

				for (UNiagaraNode* Node : CachedUsageInfo[i].Traversal)
				{
					Node->GatherExternalDependencyIDs(InUsage, InUsageId, InReferencedCompileHashes, InReferencedIDs, InReferencedObjs);
				}
			}
		}
	}
	// Otherwise, just add downstream dependencies for the specific usage type we're on.
	else
	{
		for (int32 i = 0; i < CachedUsageInfo.Num(); i++)
		{
			// First add our direct dependency chain...
			if (UNiagaraScript::IsEquivalentUsage(CachedUsageInfo[i].UsageType, InUsage) && CachedUsageInfo[i].UsageId == InUsageId)
			{
				// Skip adding to list because we already did it in GetCompileId above.
				for (UNiagaraNode* Node : CachedUsageInfo[i].Traversal)
				{
					Node->GatherExternalDependencyIDs(InUsage, InUsageId, InReferencedCompileHashes, InReferencedIDs, InReferencedObjs);
				}
			}
			// Now add any other dependency chains that we might have...
			else if (UNiagaraScript::IsUsageDependentOn(InUsage, CachedUsageInfo[i].UsageType))
			{
				InReferencedCompileHashes.Add(CachedUsageInfo[i].CompileHash);
				InReferencedObjs.Add(CachedUsageInfo[i].Traversal.Last());

				for (UNiagaraNode* Node : CachedUsageInfo[i].Traversal)
				{
					Node->GatherExternalDependencyIDs(InUsage, InUsageId, InReferencedCompileHashes, InReferencedIDs, InReferencedObjs);
				}
			}
		}
	}
}


void UNiagaraGraph::GetAllReferencedGraphs(TArray<const UNiagaraGraph*>& Graphs) const
{
	Graphs.AddUnique(this);
	for (UEdGraphNode* Node : Nodes)
	{
		if (UNiagaraNode* InNode = Cast<UNiagaraNode>(Node))
		{
			UObject* AssetRef = InNode->GetReferencedAsset();
			if (AssetRef != nullptr && AssetRef->IsA(UNiagaraScript::StaticClass()))
			{
				if (UNiagaraScript* FunctionScript = Cast<UNiagaraScript>(AssetRef))
				{
					if (FunctionScript->GetSource())
					{
						UNiagaraScriptSource* Source = CastChecked<UNiagaraScriptSource>(FunctionScript->GetSource());
						if (Source != nullptr)
						{
							UNiagaraGraph* FunctionGraph = CastChecked<UNiagaraGraph>(Source->NodeGraph);
							if (FunctionGraph != nullptr)
							{
								if (!Graphs.Contains(FunctionGraph))
								{
									FunctionGraph->GetAllReferencedGraphs(Graphs);
								}
							}
						}
					}
				}
				else if (UNiagaraGraph* FunctionGraph = Cast<UNiagaraGraph>(AssetRef))
				{
					if (!Graphs.Contains(FunctionGraph))
					{
						FunctionGraph->GetAllReferencedGraphs(Graphs);
					}
				}
			}
		}
	}
}

/** Determine if another item has been synchronized with this graph.*/
bool UNiagaraGraph::IsOtherSynchronized(const FGuid& InChangeId) const
{
	if (ChangeId.IsValid() && ChangeId == InChangeId)
	{
		return true;
	}
	return false;
}

/** Identify that this graph has undergone changes that will require synchronization with a compiled script.*/
void UNiagaraGraph::MarkGraphRequiresSynchronization(FString Reason)
{
	Modify();
	ChangeId = FGuid::NewGuid();
	if (GEnableVerboseNiagaraChangeIdLogging)
	{
		UE_LOG(LogNiagaraEditor, Verbose, TEXT("Graph %s was marked requires synchronization.  Reason: %s"), *GetPathName(), *Reason);
	}
}

TOptional<FNiagaraVariableMetaData> UNiagaraGraph::GetMetaData(const FNiagaraVariable& InVar) const
{
	if (bUnreferencedMetaDataPurgePending)
	{
		PurgeUnreferencedMetaData();
	}
	
	if (UNiagaraScriptVariable** MetaData = VariableToScriptVariable.Find(InVar))
	{
		if (*MetaData)
		{
			return (*MetaData)->Metadata;
		}
	}
	return TOptional<FNiagaraVariableMetaData>();
}

void UNiagaraGraph::SetMetaData(const FNiagaraVariable& InVar, const FNiagaraVariableMetaData& InMetaData)
{
	ensure(FNiagaraConstants::IsNiagaraConstant(InVar) == false);

	if (UNiagaraScriptVariable** FoundMetaData = VariableToScriptVariable.Find(InVar))
	{
		if (*FoundMetaData)
		{
			// Replace the old metadata..
			(*FoundMetaData)->Metadata = InMetaData;
		} 
	}
	else 
	{
		UNiagaraScriptVariable*& NewScriptVariable = VariableToScriptVariable.Add(InVar, NewObject<UNiagaraScriptVariable>(this));
		NewScriptVariable->Variable = InVar;
		NewScriptVariable->Metadata = InMetaData;
	}
}

void UNiagaraGraph::PurgeUnreferencedMetaData() const
{
	TSet<FNiagaraVariable> ReferencedParameters;
	ReferencedParameters.Append(FindStaticSwitchInputs());
	const UEdGraphSchema_Niagara* NiagaraSchema = Cast<UEdGraphSchema_Niagara>(Schema);
	for (UEdGraphNode* Node : Nodes)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->PinType.PinSubCategory == UNiagaraNodeParameterMapBase::ParameterPinSubCategory)
			{
				const FNiagaraVariable Parameter = NiagaraSchema->PinToNiagaraVariable(Pin, false);
				const FNiagaraParameterHandle Handle = FNiagaraParameterHandle(Parameter.GetName());
				if (Handle.IsModuleHandle() && !FNiagaraConstants::IsNiagaraConstant(Parameter))
				{
					ReferencedParameters.Add(Parameter);
				}
			}
		}
	}

	TArray<FNiagaraVariable> VarsToRemove;
	for (auto It = VariableToScriptVariable.CreateConstIterator(); It; ++It)
	{
		if (!ReferencedParameters.Contains(It.Key()))
		{
			VarsToRemove.Add(It.Key());
		}
	}

	for (FNiagaraVariable& Var : VarsToRemove)
	{
		VariableToScriptVariable.Remove(Var);
	}

	bUnreferencedMetaDataPurgePending = false;
}

UNiagaraGraph::FOnDataInterfaceChanged& UNiagaraGraph::OnDataInterfaceChanged()
{
	return OnDataInterfaceChangedDelegate;
}

void UNiagaraGraph::RefreshParameterReferences() const 
{
	// A set of variables to track which parameters are used so that unused parameters can be removed after the reference tracking.
	TSet<FNiagaraVariable> CandidateUnreferencedParametersToRemove;

	// The set of pins which has already been handled by add parameters.
	TSet<const UEdGraphPin*> HandledPins;

	// Purge existing parameter references and collect candidate unreferenced parameters.
	for (auto& ParameterToReferences : ParameterToReferencesMap)
	{
		ParameterToReferences.Value.ParameterReferences.Empty();
		if (ParameterToReferences.Value.WasCreated() == false)
		{
			// Collect all parameters not created for the user so that they can be removed later if no references are found for them.
			CandidateUnreferencedParametersToRemove.Add(ParameterToReferences.Key);
		}
	}

	auto AddParameterReference = [&](const FNiagaraVariable& Parameter, const UEdGraphPin* Pin)
	{
		if (Pin->PinType.PinSubCategory == UNiagaraNodeParameterMapBase::ParameterPinSubCategory)
		{
			FNiagaraGraphParameterReferenceCollection* ReferenceCollection = ParameterToReferencesMap.Find(Parameter);
			if (ReferenceCollection == nullptr)
			{
				FNiagaraGraphParameterReferenceCollection& NewReferenceCollection = ParameterToReferencesMap.Add(Parameter);
				NewReferenceCollection.Graph = this;
				ReferenceCollection = &NewReferenceCollection;

				// When a variable is created or added from the graph it won't call AddParameter, 
				// but instead call this method
				UNiagaraScriptVariable** FoundScriptVariable = VariableToScriptVariable.Find(Parameter);
				if (!FoundScriptVariable)
				{
					// Warning! This method (RefreshParameterReferences) isn't const at all!
					// Need to cast away the const to be able to create a serializable UObject here...
					UNiagaraScriptVariable* NewScriptVariable = NewObject<UNiagaraScriptVariable>(const_cast<UNiagaraGraph*>(this));
					NewScriptVariable->Variable = Parameter;
					VariableToScriptVariable.Add(Parameter, NewScriptVariable);
				}
			}
			ReferenceCollection->ParameterReferences.AddUnique(FNiagaraGraphParameterReference(Pin->PersistentGuid, Cast<UNiagaraNode>(Pin->GetOwningNode())));

			// If we're adding a parameter reference then it needs to be removed from the list of candidate variables to remove since it's been referenced.
			CandidateUnreferencedParametersToRemove.Remove(Parameter);
		}

		HandledPins.Add(Pin);
	};

	auto AddStaticParameterReference = [&](const FNiagaraVariable& Variable, UNiagaraNode* Node)
	{
		FNiagaraGraphParameterReferenceCollection* ReferenceCollection = ParameterToReferencesMap.Find(Variable);
		if (ReferenceCollection == nullptr)
		{
			FNiagaraGraphParameterReferenceCollection NewReferenceCollection(true);
			NewReferenceCollection.Graph = this;
			ReferenceCollection = &ParameterToReferencesMap.Add(Variable, NewReferenceCollection);
		}
		UNiagaraScriptVariable** FoundScriptVariable = VariableToScriptVariable.Find(Variable);
		if (!FoundScriptVariable)
		{
			UNiagaraScriptVariable* NewScriptVariable = NewObject<UNiagaraScriptVariable>(const_cast<UNiagaraGraph*>(this));
			NewScriptVariable->Variable = Variable;
			NewScriptVariable->Metadata.bIsStaticSwitch = true;
			VariableToScriptVariable.Add(Variable, NewScriptVariable);
		}
		ReferenceCollection->ParameterReferences.AddUnique(FNiagaraGraphParameterReference(Node->NodeGuid, Node));
		CandidateUnreferencedParametersToRemove.Remove(Variable);
	};

	// Add parameter references from parameter map traversals.
	const TArray<FNiagaraParameterMapHistory> Histories = UNiagaraNodeParameterMapBase::GetParameterMaps(this);
	for (const FNiagaraParameterMapHistory& History : Histories)
	{
		for (int32 Index = 0; Index < History.VariablesWithOriginalAliasesIntact.Num(); Index++)
		{
			const FNiagaraVariable& Parameter = History.VariablesWithOriginalAliasesIntact[Index];
			for (const UEdGraphPin* WritePin : History.PerVariableWriteHistory[Index])
			{
				AddParameterReference(Parameter, WritePin);
			}

			for (const TTuple<const UEdGraphPin*, const UEdGraphPin*>& ReadPinTuple : History.PerVariableReadHistory[Index])
			{
				AddParameterReference(Parameter, ReadPinTuple.Key);
			}
		}
	}

	// Check all pins on all nodes in the graph to find parameter pins which may have been missed in the parameter map traversal.  This
	// can happen for nodes which are not fully connected and therefore don't show up in the traversal.
	const UEdGraphSchema_Niagara* NiagaraSchema = Cast<UEdGraphSchema_Niagara>(Schema);
	for (UEdGraphNode* Node : Nodes)
	{
		UNiagaraNodeStaticSwitch* SwitchNode = Cast<UNiagaraNodeStaticSwitch>(Node);
		if (SwitchNode && !SwitchNode->IsSetByCompiler())
		{
			FNiagaraVariable Variable(SwitchNode->GetInputType(), SwitchNode->InputParameterName);
			AddStaticParameterReference(Variable, SwitchNode);
		}
		else if (UNiagaraNodeFunctionCall* FunctionNode = Cast<UNiagaraNodeFunctionCall>(Node))
		{
			for (const FNiagaraPropagatedVariable& Propagated : FunctionNode->PropagatedStaticSwitchParameters)
			{
				AddStaticParameterReference(Propagated.ToVariable(), FunctionNode);
			}
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (HandledPins.Contains(Pin) == false)
			{
				const FNiagaraVariable Parameter = NiagaraSchema->PinToNiagaraVariable(Pin, false);
				AddParameterReference(Parameter, Pin);
			}
		}
	}

	// If there were any previous parameters which didn't have any references added, remove them here.
	for (const FNiagaraVariable& UnreferencedParameterToRemove : CandidateUnreferencedParametersToRemove)
	{
		ParameterToReferencesMap.Remove(UnreferencedParameterToRemove);
	}
	
	static const auto UseShaderStagesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.UseShaderStages"));
	if (UseShaderStagesCVar->GetInt() == 1)
	{
		// Add the array indices to the parameters
		// When a particle attribute is created we need access the corresponding RegisterIdx if we want to 
		// query this attribute at a different location inside the InputData buffer. This index must be 
		// available as well inside the UI if we want to pass it to nodes. It is why we are adding them automatically 
		// to the ParameterToReferencesMap.
		TArray<FString> RegisterNames;
		for (auto& ParameterEntry : ParameterToReferencesMap)
		{
			const FNiagaraVariable& NiagaraVariable = ParameterEntry.Key;
			if (FNiagaraParameterMapHistory::IsAttribute(NiagaraVariable))
			{
				const FString VariableName = FHlslNiagaraTranslator::GetSanitizedSymbolName(NiagaraVariable.GetName().ToString());
				RegisterNames.Add(VariableName.Replace(PARAM_MAP_ATTRIBUTE_STR, PARAM_MAP_INDICES_STR));
			}
		}
		for (const FString& RegisterName : RegisterNames)
		{
			FNiagaraVariable Parameter = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), *RegisterName);
			if (ParameterToReferencesMap.Find(Parameter) == nullptr)
			{
				ParameterToReferencesMap.Add(Parameter, false);
			}
		}
	}

	bParameterReferenceRefreshPending = false;
}

void UNiagaraGraph::InvalidateCachedParameterData()
{
	bParameterReferenceRefreshPending = true;
	bUnreferencedMetaDataPurgePending = true;
}

#undef LOCTEXT_NAMESPACE