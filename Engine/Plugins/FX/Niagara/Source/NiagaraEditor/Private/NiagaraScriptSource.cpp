// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptSource.h"
#include "EdGraphSchema_Niagara.h"
#include "EdGraphUtilities.h"
#include "GraphEditAction.h"
#include "INiagaraEditorTypeUtilities.h"
#include "NiagaraCommon.h"
#include "NiagaraComponent.h"
#include "NiagaraConstants.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraDataInterface.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeParameterMapSet.h"
#include "NiagaraScript.h"
#include "Logging/LogMacros.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptSource - Compile"), STAT_NiagaraEditor_ScriptSource_Compile, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptSource - InitializeNewRapidIterationParameters"), STAT_NiagaraEditor_ScriptSource_InitializeNewRapidIterationParameters, STATGROUP_NiagaraEditor);

UNiagaraScriptSource::UNiagaraScriptSource(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UNiagaraScriptSource::ComputeVMCompilationId(FNiagaraVMExecutableDataId& Id, ENiagaraScriptUsage InUsage, const FGuid& InUsageId, bool bForceRebuild) const
{
	if (!AllowShaderCompiling())
	{
		return;
	}

	Id.ScriptUsageType = InUsage;
	Id.ScriptUsageTypeID = InUsageId;
	Id.CompilerVersionID = FNiagaraCustomVersion::LatestScriptCompileVersion;
	if (NodeGraph)
	{
		NodeGraph->RebuildCachedCompileIds(bForceRebuild);
		Id.BaseScriptCompileHash = FNiagaraCompileHash(NodeGraph->GetCompileDataHash(InUsage, InUsageId));
		NodeGraph->GatherExternalDependencyData(InUsage, InUsageId, Id.ReferencedCompileHashes, Id.DebugReferencedObjects);
	}

	// Add in any referenced HLSL files.
	FSHAHash Hash = GetShaderFileHash((TEXT("/Plugin/FX/Niagara/Private/NiagaraEmitterInstanceShader.usf")), EShaderPlatform::SP_PCD3D_SM5);
	Id.ReferencedCompileHashes.AddUnique(FNiagaraCompileHash(Hash.Hash, sizeof(Hash.Hash)/sizeof(uint8)));
	Id.DebugReferencedObjects.Emplace(TEXT("/Plugin/FX/Niagara/Private/NiagaraEmitterInstanceShader.usf"));
	Hash = GetShaderFileHash((TEXT("/Plugin/FX/Niagara/Private/NiagaraShaderVersion.ush")), EShaderPlatform::SP_PCD3D_SM5);
	Id.ReferencedCompileHashes.AddUnique(FNiagaraCompileHash(Hash.Hash, sizeof(Hash.Hash)/sizeof(uint8)));
	Id.DebugReferencedObjects.Emplace(TEXT("/Plugin/FX/Niagara/Private/NiagaraShaderVersion.ush"));
	Hash = GetShaderFileHash((TEXT("/Engine/Public/ShaderVersion.ush")), EShaderPlatform::SP_PCD3D_SM5);
	Id.ReferencedCompileHashes.AddUnique(FNiagaraCompileHash(Hash.Hash, sizeof(Hash.Hash)/sizeof(uint8)));
	Id.DebugReferencedObjects.Emplace(TEXT("/Engine/Public/ShaderVersion.ush"));
}

FGuid UNiagaraScriptSource::GetCompileBaseId(ENiagaraScriptUsage InUsage, const FGuid& InUsageId) const
{
	return NodeGraph->GetBaseId(InUsage, InUsageId);
}

FNiagaraCompileHash UNiagaraScriptSource::GetCompileHash(ENiagaraScriptUsage InUsage, const FGuid& InUsageId) const
{
	return NodeGraph->GetCompileDataHash(InUsage, InUsageId);
}

void UNiagaraScriptSource::ForceGraphToRecompileOnNextCheck()
{
	NodeGraph->ForceGraphToRecompileOnNextCheck();
}

void UNiagaraScriptSource::RefreshFromExternalChanges()
{
	if (NodeGraph)
	{
		for (UEdGraphNode* Node : NodeGraph->Nodes)
		{
			UNiagaraNode* NiagaraNode = Cast<UNiagaraNode>(Node);
			if (NiagaraNode)
			{
				NiagaraNode->RefreshFromExternalChanges();
			}
		}
	}
}


void UNiagaraScriptSource::PostLoad()
{
	Super::PostLoad();

	if (NodeGraph)
	{
		// We need to make sure that the node-graph is already resolved b/c we may be asked IsSyncrhonized later...
		NodeGraph->ConditionalPostLoad();

		// Hook up event handlers so the on changed handler can be called correctly.
		NodeGraph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateUObject(this, &UNiagaraScriptSource::OnGraphChanged));
		NodeGraph->AddOnGraphNeedsRecompileHandler(FOnGraphChanged::FDelegate::CreateUObject(this, &UNiagaraScriptSource::OnGraphChanged));
		NodeGraph->OnDataInterfaceChanged().AddUObject(this, &UNiagaraScriptSource::OnGraphDataInterfaceChanged);
	}
}

UNiagaraScriptSource* UNiagaraScriptSource::CreateCompilationCopy(const TArray<ENiagaraScriptUsage>& CompileUsages)
{
	UNiagaraScriptSource* Result = NewObject<UNiagaraScriptSource>();
	Result->NodeGraph = NodeGraph->CreateCompilationCopy(CompileUsages);
	Result->bIsCompilationCopy = true;
	Result->AddToRoot();
	return Result;
}

void UNiagaraScriptSource::ReleaseCompilationCopy()
{
	if (bIsCompilationCopy && !bIsReleased)
	{
		bIsReleased = true;
		NodeGraph = nullptr;
		RemoveFromRoot();
		MarkAsGarbage();
	}
}

bool UNiagaraScriptSource::IsSynchronized(const FGuid& InChangeId)
{
	if (NodeGraph)
	{
		return NodeGraph->IsOtherSynchronized(InChangeId);
	}
	else
	{
		return false;
	}
}

void UNiagaraScriptSource::MarkNotSynchronized(FString Reason)
{
	if (NodeGraph)
	{
		NodeGraph->MarkGraphRequiresSynchronization(Reason);
	}
}

void UNiagaraScriptSource::PostLoadFromEmitter(UNiagaraEmitter& OwningEmitter)
{
	const int32 NiagaraCustomVersion = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);
	if (NiagaraCustomVersion < FNiagaraCustomVersion::ScriptsNowUseAGuidForIdentificationInsteadOfAnIndex)
	{
		TArray<UNiagaraNodeOutput*> OutputNodes;
		NodeGraph->GetNodesOfClass<UNiagaraNodeOutput>(OutputNodes);
		for (int32 i = 0; i < OwningEmitter.GetEventHandlers().Num(); i++)
		{
			const FNiagaraEventScriptProperties& EventScriptProperties = OwningEmitter.GetEventHandlers()[i];
			EventScriptProperties.Script->SetUsageId(FGuid::NewGuid());

			auto FindOutputNodeByUsageIndex = [=](UNiagaraNodeOutput* OutputNode) 
			{ 
				return OutputNode->GetUsage() == ENiagaraScriptUsage::ParticleEventScript && OutputNode->ScriptTypeIndex_DEPRECATED == EventScriptProperties.Script->UsageIndex_DEPRECATED; 
			};
			UNiagaraNodeOutput** MatchingOutputNodePtr = OutputNodes.FindByPredicate(FindOutputNodeByUsageIndex);
			if (MatchingOutputNodePtr != nullptr)
			{
				UNiagaraNodeOutput* MatchingOutputNode = *MatchingOutputNodePtr;
				MatchingOutputNode->SetUsageId(EventScriptProperties.Script->GetUsageId());
			}
		}
		NodeGraph->MarkGraphRequiresSynchronization("Modified while handling a change to the niagara custom version.");
	}
}

void FindObjectNamesRecursive(UNiagaraGraph* InGraph, ENiagaraScriptUsage Usage, FGuid UsageId, FString EmitterUniqueName, TSet<UNiagaraGraph*>& VisitedGraphs, TMap<FName, UNiagaraDataInterface*>& Result)
{
	if (!InGraph)
	{
		return;
	}

	TArray<UNiagaraNode*> Nodes;
	UNiagaraNodeOutput* OutputNode = InGraph->FindEquivalentOutputNode(Usage, UsageId);
	InGraph->BuildTraversal(Nodes, OutputNode);
	for (UEdGraphNode* Node : Nodes)
	{
		if (UNiagaraNode* InNode = Cast<UNiagaraNode>(Node))
		{
			UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(InNode);
			if (InputNode)
			{
				if (InputNode->Input.IsDataInterface())
				{
					UNiagaraDataInterface* DataInterface = InputNode->GetDataInterface();
					bool bIsParameterMapDataInterface = false;
					FName DIName = FHlslNiagaraTranslator::GetDataInterfaceName(InputNode->Input.GetName(), EmitterUniqueName, bIsParameterMapDataInterface);
					if (Result.Contains(DIName) && Result[DIName] != DataInterface)
					{
						UE_LOG(LogNiagaraEditor, Verbose, TEXT("Duplicate data interface name %s in graph %s. One of the data interfaces will override the other when resolving the name."), *DIName.ToString(), *InGraph->GetPathName());
					}
					Result.Add(DIName, DataInterface);
				}
				continue;
			}

			UNiagaraNodeFunctionCall* FunctionCallNode = Cast<UNiagaraNodeFunctionCall>(InNode);
			if (FunctionCallNode)
			{
				UNiagaraGraph* FunctionGraph = FunctionCallNode->GetCalledGraph();
				if(VisitedGraphs.Contains(FunctionGraph) == false)
				{
					VisitedGraphs.Add(FunctionGraph);
					FindObjectNamesRecursive(FunctionGraph, FunctionCallNode->GetCalledUsage(), FGuid(), EmitterUniqueName, VisitedGraphs, Result);
				}
			}
		}
	}
}

TMap<FName, UNiagaraDataInterface*> UNiagaraScriptSource::ComputeObjectNameMap(UNiagaraSystem& System, ENiagaraScriptUsage Usage, FGuid UsageId, FString EmitterUniqueName) const
{
	TMap<FName, UNiagaraDataInterface*> Result;
	if (!NodeGraph)
	{
		return Result;
	}

	TSet<UNiagaraGraph*> VisitedGraphs;
	if (Usage == ENiagaraScriptUsage::ParticleGPUComputeScript)
	{
		// GPU scripts need to include spawn, update, and sim stage script's data interfaces.
		TArray<UNiagaraNodeOutput*> OutputNodes;
		NodeGraph->GetNodesOfClass(OutputNodes);
		for (UNiagaraNodeOutput* OutputNode : OutputNodes)
		{
			if (OutputNode->GetUsage() == ENiagaraScriptUsage::ParticleSpawnScript ||
				OutputNode->GetUsage() == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated ||
				OutputNode->GetUsage() == ENiagaraScriptUsage::ParticleUpdateScript ||
				OutputNode->GetUsage() == ENiagaraScriptUsage::ParticleSimulationStageScript)
			{
				FindObjectNamesRecursive(NodeGraph, OutputNode->GetUsage(), OutputNode->GetUsageId(), EmitterUniqueName, VisitedGraphs, Result);
			}
		}
	}
	else
	{
		// Collect the data interfaces for the current script.
		FindObjectNamesRecursive(NodeGraph, Usage, UsageId, EmitterUniqueName, VisitedGraphs, Result);

		if (Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated)
		{
			// The interpolated spawn script needs to include the particle update script's data interfaces as well.
			FindObjectNamesRecursive(NodeGraph, ENiagaraScriptUsage::ParticleUpdateScript, FGuid(), EmitterUniqueName, VisitedGraphs, Result);
		}
		else if (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript)
		{
			// System scripts need to include data interfaces from the corresponding emitter scripts.
			ENiagaraScriptUsage EmitterUsage = Usage == ENiagaraScriptUsage::SystemSpawnScript
				? ENiagaraScriptUsage::EmitterSpawnScript 
				: ENiagaraScriptUsage::EmitterUpdateScript;

			for (const FNiagaraEmitterHandle& EmitterHandle : System.GetEmitterHandles())
			{
				if (EmitterHandle.GetIsEnabled() && EmitterHandle.GetInstance() != nullptr)
				{
					UNiagaraScriptSource* EmitterSource = Cast<UNiagaraScriptSource>(EmitterHandle.GetInstance()->GraphSource);
					if (EmitterSource != nullptr)
					{
						FindObjectNamesRecursive(EmitterSource->NodeGraph, EmitterUsage, FGuid(), EmitterHandle.GetUniqueInstanceName(), VisitedGraphs, Result);
					}
				}
			}
		}
	}
	return Result;
}

bool UNiagaraScriptSource::AddModuleIfMissing(FString ModulePath, ENiagaraScriptUsage Usage, bool& bOutFoundModule)
{
	FSoftObjectPath SystemUpdateScriptRef(ModulePath);
	FAssetData ModuleScriptAsset;
	ModuleScriptAsset.ObjectPath = SystemUpdateScriptRef.GetAssetPathName();
	bOutFoundModule = false;

	if (ModuleScriptAsset.IsValid())
	{
		bOutFoundModule = true;
		if (UNiagaraNodeOutput* OutputNode = NodeGraph->FindOutputNode(Usage))
		{
			TArray<UNiagaraNodeFunctionCall*> FoundCalls;
			if (!FNiagaraStackGraphUtilities::FindScriptModulesInStack(ModuleScriptAsset, *OutputNode, FoundCalls))
			{
				FNiagaraStackGraphUtilities::AddScriptModuleToStack(ModuleScriptAsset, *OutputNode);
				return true;
			}
		}
	}

	return false;
}

void UNiagaraScriptSource::FixupRenamedParameters(UNiagaraNode* Node, FNiagaraParameterStore& RapidIterationParameters, const TArray<FNiagaraVariable>& OldRapidIterationVariables, const UNiagaraEmitter* Emitter, ENiagaraScriptUsage ScriptUsage) const
{
	UNiagaraNodeFunctionCall* FunctionCallNode = Cast<UNiagaraNodeFunctionCall>(Node);
	if (FunctionCallNode == nullptr || FunctionCallNode->FunctionScript == nullptr)
	{
		return;
	}

	// find out which inputs the module offers
	TSet<const UEdGraphPin*> HiddenModulePins;
	TArray<const UEdGraphPin*> ModuleInputPins;
	FCompileConstantResolver ConstantResolver;
	GetStackFunctionInputPins(*FunctionCallNode, ModuleInputPins, HiddenModulePins, ConstantResolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly);

	// the rapid iteration parameters and the function input pins use different variable naming schemes, so most of this is just used to convert one name to the other 
	UNiagaraGraph* Graph = FunctionCallNode->GetCalledGraph();
	const UEdGraphSchema_Niagara* NiagaraSchema = Graph->GetNiagaraSchema();
	const FString UniqueEmitterName = Emitter ? Emitter->GetUniqueEmitterName() : FString();

	// go through the existing rapid iteration params to see if they are either still valid or were renamed
	for (FNiagaraVariable OldRapidIterationVar : OldRapidIterationVariables)
	{
		if (FGuid* BoundGuid = RapidIterationParameters.ParameterGuidMapping.Find(OldRapidIterationVar))
		{
			for (const UEdGraphPin* ModulePin : ModuleInputPins)
			{
				FNiagaraVariable InputVar = NiagaraSchema->PinToNiagaraVariable(ModulePin);
				TOptional<FNiagaraVariableMetaData> VariableMetaData = Graph->GetMetaData(InputVar);

				FNiagaraParameterHandle AliasedFunctionInputHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(ModulePin->PinName), FunctionCallNode);
				const FString ConstantName = FNiagaraUtilities::CreateRapidIterationConstantName(AliasedFunctionInputHandle.GetParameterHandleString(), *UniqueEmitterName, ScriptUsage);
				bool bNameMatches = OldRapidIterationVar.GetName() == *ConstantName;
				
				// move on if the namespaces don't match
				int32 NameSpaceEnd;
				if (ConstantName.FindLastChar(TEXT('.'), NameSpaceEnd))
				{
					if (!OldRapidIterationVar.IsInNameSpace(ConstantName.Left(NameSpaceEnd)))
					{
						continue;
					}
				}
				
				if (VariableMetaData.IsSet() && VariableMetaData->GetVariableGuid() == *BoundGuid)
				{
					// if the names match we have nothing to do
					if (bNameMatches)
					{
						continue;
					}

					// if the guid matches but the names differ then the parameter was renamed, so lets update the rapid iteration parameter
					FNiagaraTypeDefinition InputType = NiagaraSchema->PinToTypeDefinition(ModulePin);
					FNiagaraVariable NewRapidIterationVar = FNiagaraStackGraphUtilities::CreateRapidIterationParameter(UniqueEmitterName, ScriptUsage, AliasedFunctionInputHandle.GetParameterHandleString(), InputType);
					RapidIterationParameters.RenameParameter(OldRapidIterationVar, NewRapidIterationVar.GetName());
					break;
				}

				// check if maybe the variable type changed from vector to position and update the rapid iteration param accordingly
				if (OldRapidIterationVar.GetType() == FNiagaraTypeDefinition::GetVec3Def() && InputVar.GetType() == FNiagaraTypeDefinition::GetPositionDef())
				{
					// the names have to match here, as we don't rename anything but change the type
					if (!bNameMatches)
					{
						continue;
					}

					// if the guid matches but the types differ then the parameter was changed from vector to position, so lets update the existing rapid iteration parameter
					if (RapidIterationParameters.IndexOf(OldRapidIterationVar) != INDEX_NONE)
					{
						FNiagaraVariable RapidIterationAlias(FNiagaraTypeDefinition::GetPositionDef(), OldRapidIterationVar.GetName());
						RapidIterationParameters.RemoveParameter(RapidIterationAlias);
						RapidIterationParameters.ChangeParameterType(OldRapidIterationVar, FNiagaraTypeDefinition::GetPositionDef());
					}
				}
			}
		}
	}
}

void InitializeNewRapidIterationParametersForNode(const UEdGraphSchema_Niagara* Schema, UNiagaraNode* Node, const UNiagaraEmitter* Emitter, ENiagaraScriptUsage ScriptUsage, FNiagaraParameterStore& RapidIterationParameters, TSet<FName>& ValidRapidIterationParameterNames)
{
	UNiagaraNodeFunctionCall* FunctionCallNode = Cast<UNiagaraNodeFunctionCall>(Node);
	if (FunctionCallNode == nullptr)
	{
		return;
	}
	TSet<const UEdGraphPin*> HiddenPins;
	FCompileConstantResolver Resolver(Emitter, ScriptUsage);
	TArray<const UEdGraphPin*> FunctionInputPins;

	const FString UniqueEmitterName = Emitter ? Emitter->GetUniqueEmitterName() : FString();

	FNiagaraStackGraphUtilities::GetStackFunctionInputPins(*FunctionCallNode, FunctionInputPins, HiddenPins, Resolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly, false);
	for (const UEdGraphPin* FunctionInputPin : FunctionInputPins)
	{
		FNiagaraTypeDefinition InputType = Schema->PinToTypeDefinition(FunctionInputPin);
		if (InputType.IsValid() == false)
		{
			UE_LOG(LogNiagaraEditor, Error, TEXT("Invalid input type found while attempting initialize new rapid iteration parameters. Function Node: %s %s Input Name: %s"),
				*FunctionCallNode->GetPathName(), *FunctionCallNode->GetFunctionName(), *FunctionInputPin->GetName());
			continue;
		}

		if (FNiagaraStackGraphUtilities::IsRapidIterationType(InputType))
		{
			FNiagaraParameterHandle AliasedFunctionInputHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(FunctionInputPin->PinName), FunctionCallNode);
			FNiagaraVariable RapidIterationParameter = FNiagaraStackGraphUtilities::CreateRapidIterationParameter(UniqueEmitterName, ScriptUsage, AliasedFunctionInputHandle.GetParameterHandleString(), InputType);
			ValidRapidIterationParameterNames.Add(RapidIterationParameter.GetName());
			int32 ParameterIndex = RapidIterationParameters.IndexOf(RapidIterationParameter);

			// Only set a value for the parameter if it's not already set.
			if (ParameterIndex == INDEX_NONE)
			{
				FCompileConstantResolver ConstantResolver(Emitter, ScriptUsage);
				UEdGraphPin* DefaultPin = FunctionCallNode->FindParameterMapDefaultValuePin(FunctionInputPin->PinName, ScriptUsage, ConstantResolver);
				// Only set values for inputs which don't have a default wired in the script graph, since inputs with wired defaults can't currently use rapid iteration parameters.
				if (DefaultPin != nullptr && DefaultPin->LinkedTo.Num() == 0)
				{
					// Only set values for inputs without override pins, since and override pin means it's being read from a different value.
					UEdGraphPin* OverridePin = FNiagaraStackGraphUtilities::GetStackFunctionInputOverridePin(*FunctionCallNode, AliasedFunctionInputHandle);
					if (OverridePin == nullptr)
					{
						FNiagaraVariable DefaultVariable = Schema->PinToNiagaraVariable(DefaultPin, true);
						check(DefaultVariable.GetData() != nullptr);
						bool bAddParameterIfMissing = true;
						RapidIterationParameters.SetParameterData(DefaultVariable.GetData(), RapidIterationParameter, bAddParameterIfMissing);
					}
				}
			}
		}
	}
}

void UNiagaraScriptSource::CleanUpOldAndInitializeNewRapidIterationParameters(const UNiagaraEmitter* Emitter, ENiagaraScriptUsage ScriptUsage, FGuid ScriptUsageId, FNiagaraParameterStore& RapidIterationParameters) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_ScriptSource_InitializeNewRapidIterationParameters);
	TArray<UNiagaraNodeOutput*> OutputNodes;
	if (ScriptUsage == ENiagaraScriptUsage::ParticleGPUComputeScript)
	{
		TArray<UNiagaraNodeOutput*> TempOutputNodes;
		NodeGraph->FindOutputNodes(TempOutputNodes);
		for (UNiagaraNodeOutput* OutputNode : TempOutputNodes)
		{
			if (UNiagaraScript::IsParticleScript(OutputNode->GetUsage()))
			{
				OutputNodes.AddUnique(OutputNode);
			}
		}
	}
	else
	{
		UNiagaraNodeOutput* OutputNode = NodeGraph->FindEquivalentOutputNode(ScriptUsage, ScriptUsageId);
		OutputNodes.Add(OutputNode);
	}
	
	TArray<FNiagaraVariable> OldRapidIterationVariables;
	RapidIterationParameters.GetParameters(OldRapidIterationVariables);

	TSet<FName> ValidRapidIterationParameterNames;
	for (UNiagaraNodeOutput* OutputNode : OutputNodes)
	{
		if (OutputNode != nullptr)
		{
			TArray<UNiagaraNode*> Nodes;
			NodeGraph->BuildTraversal(Nodes, OutputNode);
			const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();

			for(UNiagaraNode* Node : Nodes)
			{
				FixupRenamedParameters(Node, RapidIterationParameters, OldRapidIterationVariables, Emitter, ScriptUsage);
				InitializeNewRapidIterationParametersForNode(Schema, Node, Emitter, OutputNode->GetUsage(), RapidIterationParameters, ValidRapidIterationParameterNames);
			}
		}
	}

	// Clean up old rapid iteration parameters.
	TArray<FNiagaraVariable> CurrentRapidIterationVariables;
	RapidIterationParameters.GetParameters(CurrentRapidIterationVariables);
	for (const FNiagaraVariable& CurrentRapidIterationVariable : CurrentRapidIterationVariables)
	{
		if (ValidRapidIterationParameterNames.Contains(CurrentRapidIterationVariable.GetName()) == false)
		{
			RapidIterationParameters.RemoveParameter(CurrentRapidIterationVariable);
		}
	}
}

/*
ENiagaraScriptCompileStatus UNiagaraScriptSource::Compile(UNiagaraScript* ScriptOwner, FString& OutGraphLevelErrorMessages)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_ScriptSource_Compile);
	bool bDoPostCompile = false;
	if (!bIsPrecompiled)
	{
		PreCompile(nullptr, TArray<FNiagaraVariable>());
		bDoPostCompile = true;
	}

	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::Get().LoadModuleChecked<FNiagaraEditorModule>(TEXT("NiagaraEditor"));
	ENiagaraScriptCompileStatus Status = NiagaraEditorModule.CompileScript(ScriptOwner, OutGraphLevelErrorMessages);
	check(ScriptOwner != nullptr && IsSynchronized(ScriptOwner->GetChangeID()));
	
	if (bDoPostCompile)
	{
		PostCompile();
	}
	return Status;

// 	FNiagaraConstants& ExternalConsts = ScriptOwner->ConstantData.GetExternalConstants();
// 
// 	//Build the constant list. 
// 	//This is mainly just jumping through some hoops for the custom UI. Should be removed and have the UI just read directly from the constants stored in the UScript.
// 	const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(NodeGraph->GetSchema());
// 	ExposedVectorConstants.Empty();
// 	for (int32 ConstIdx = 0; ConstIdx < ExternalConsts.GetNumVectorConstants(); ConstIdx++)
// 	{
// 		FNiagaraVariableInfo Info;
// 		FVector4 Value;
// 		ExternalConsts.GetVectorConstant(ConstIdx, Value, Info);
// 		if (Schema->IsSystemConstant(Info))
// 		{
// 			continue;//System constants are "external" but should not be exposed to the editor.
// 		}
// 			
// 		EditorExposedVectorConstant *Const = new EditorExposedVectorConstant();
// 		Const->ConstName = Info.Name;
// 		Const->Value = Value;
// 		ExposedVectorConstants.Add(MakeShareable(Const));
// 	}

}
*/

void UNiagaraScriptSource::OnGraphChanged(const FEdGraphEditAction &Action)
{
	OnChangedDelegate.Broadcast();
}

void UNiagaraScriptSource::OnGraphDataInterfaceChanged()
{
	OnChangedDelegate.Broadcast();
}

FGuid UNiagaraScriptSource::GetChangeID() 
{ 
	return NodeGraph->GetChangeID(); 
}

void UNiagaraScriptSource::CollectDataInterfaces(TArray<const UNiagaraDataInterfaceBase*>& DataInterfaces) const
{
	if (NodeGraph)
	{
		TArray<UNiagaraNodeOutput*> OutputNodes;
		NodeGraph->FindOutputNodes(OutputNodes);

		for (UNiagaraNodeOutput* OutputNode : OutputNodes)
		{
			TArray<UNiagaraNode*> TraversalNodes;
			NodeGraph->BuildTraversal(TraversalNodes, OutputNode, true);

			for (const UNiagaraNode* TraversalNode : TraversalNodes)
			{
				if (const UNiagaraNodeInput* NodeInput = Cast<const UNiagaraNodeInput>(TraversalNode))
				{
					if (NodeInput->Input.IsDataInterface())
					{
						DataInterfaces.Add(NodeInput->GetDataInterface());
					}
				}
			}
		}
	}
}

void UNiagaraScriptSource::SynchronizeGraphParametersWithParameterDefinitions(
	const TArray<UNiagaraParameterDefinitionsBase*> TargetDefinitions,
	const TArray<UNiagaraParameterDefinitionsBase*> AllDefinitions,
	const TSet<FGuid>& AllDefinitionsParameterIds,
	INiagaraParameterDefinitionsSubscriber* Subscriber,
	FSynchronizeWithParameterDefinitionsArgs Args)
{
	TArray<UNiagaraParameterDefinitions*> QualifiedTargetDefinitions = FNiagaraEditorUtilities::DowncastParameterDefinitionsBaseArray(TargetDefinitions);
	TArray<UNiagaraParameterDefinitions*> QualifiedAllDefinitions = FNiagaraEditorUtilities::DowncastParameterDefinitionsBaseArray(AllDefinitions);
	NodeGraph->SynchronizeParametersWithParameterDefinitions(QualifiedTargetDefinitions, QualifiedAllDefinitions, AllDefinitionsParameterIds, Subscriber, Args);
}

void UNiagaraScriptSource::RenameGraphAssignmentAndSetNodePins(const FName OldName, const FName NewName)
{
	NodeGraph->RenameAssignmentAndSetNodePins(OldName, NewName);
}

void UNiagaraScriptSource::GetLinkedPositionTypeInputs(const TArray<FNiagaraVariable>& ParametersToCheck, TSet<FNiagaraVariable>& OutLinkedParameters)
{
	if (!NodeGraph)
	{
		return;
	}
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	TArray<UNiagaraNodeOutput*> OutputNodes;
	NodeGraph->FindOutputNodes(OutputNodes);
	
	for (UNiagaraNodeOutput* OutputNode : OutputNodes)
	{
		TArray<UNiagaraNode*> TraversalNodes;
		NodeGraph->BuildTraversal(TraversalNodes, OutputNode, true);

		for (UNiagaraNode* TraversalNode : TraversalNodes)
		{
			UNiagaraNodeFunctionCall* FunctionCallNode = Cast<UNiagaraNodeFunctionCall>(TraversalNode);
			if (!FunctionCallNode)
			{
				continue;
			}
			UNiagaraNodeParameterMapSet* OverrideNode = FNiagaraStackGraphUtilities::GetStackFunctionOverrideNode(*FunctionCallNode);
			if (!OverrideNode)
			{
				continue;
			}
			
			// find inputs that are overriden
			TArray<UEdGraphPin*> OverrideInputPins;
			TArray<UEdGraphPin*> LinkedModuleOutputs;
			OverrideNode->GetInputPins(OverrideInputPins);
			for (UEdGraphPin* OverridePin : OverrideInputPins)
			{
				if (OverridePin->Direction != EGPD_Input || !OverridePin->PinName.ToString().StartsWith(FunctionCallNode->GetFunctionName()))
				{
					continue;
				}

				// Gather linked value inputs
				if (UEdGraphPin* LinkedValuePin = FNiagaraStackGraphUtilities::GetLinkedValueHandleForFunctionInput(*OverridePin))
				{
					for (const FNiagaraVariable& UserVar : ParametersToCheck)
					{
						if (LinkedValuePin->PinName == UserVar.GetName())
						{
							FNiagaraVariable LinkedInputVar = Schema->PinToNiagaraVariable(OverridePin);
							if (LinkedInputVar.GetType() == FNiagaraTypeDefinition::GetPositionDef())
							{
								OutLinkedParameters.Add(UserVar);
							}
						}
					}
				}
			}
		}
	}
}

void UNiagaraScriptSource::ChangedLinkedInputTypes(const FNiagaraVariable& ParametersToChange, const FNiagaraTypeDefinition& NewType)
{
	if (!NodeGraph)
	{
		return;
	}
	NodeGraph->ChangeParameterType({ParametersToChange}, NewType);
}
