// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCompiler.h"
#include "NiagaraHlslTranslator.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraEditorModule.h"
#include "NiagaraComponent.h"
#include "ShaderFormatVectorVM.h"
#include "NiagaraSystem.h"
#include "NiagaraNodeEmitter.h"
#include "NiagaraNodeInput.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraDataInterface.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeParameterMapSet.h"
#include "INiagaraEditorTypeUtilities.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraNodeOutput.h"
#include "ShaderCore.h"
#include "EdGraphSchema_Niagara.h"
#include "EdGraphUtilities.h"
#include "Misc/FileHelper.h"
#include "ShaderCompiler.h"
#include "NiagaraShader.h"
#include "NiagaraScript.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraTrace.h"
#include "Serialization/MemoryReader.h"
#include "../../Niagara/Private/NiagaraPrecompileContainer.h"

#define LOCTEXT_NAMESPACE "NiagaraCompiler"

DEFINE_LOG_CATEGORY_STATIC(LogNiagaraCompiler, All, All);

DECLARE_CYCLE_STAT(TEXT("Niagara - Module - CompileScript"), STAT_NiagaraEditor_Module_CompileScript, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslCompiler - CompileScript"), STAT_NiagaraEditor_HlslCompiler_CompileScript, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslCompiler - CompileShader_VectorVM"), STAT_NiagaraEditor_HlslCompiler_CompileShader_VectorVM, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - Module - CompileShader_VectorVMSucceeded"), STAT_NiagaraEditor_HlslCompiler_CompileShader_VectorVMSucceeded, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptSource - PreCompile"), STAT_NiagaraEditor_ScriptSource_PreCompile, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslCompiler - TestCompileShader_VectorVM"), STAT_NiagaraEditor_HlslCompiler_TestCompileShader_VectorVM, STATGROUP_NiagaraEditor);

static int32 GbForceNiagaraTranslatorSingleThreaded = 1;
static FAutoConsoleVariableRef CVarForceNiagaraTranslatorSingleThreaded(
	TEXT("fx.ForceNiagaraTranslatorSingleThreaded"),
	GbForceNiagaraTranslatorSingleThreaded,
	TEXT("If > 0 all translation will occur one at a time, useful for debugging. \n"),
	ECVF_Default
);

// Enable this to log out generated HLSL for debugging purposes.
static int32 GbForceNiagaraTranslatorDump = 0;
static FAutoConsoleVariableRef CVarForceNiagaraTranslatorDump(
	TEXT("fx.ForceNiagaraTranslatorDump"),
	GbForceNiagaraTranslatorDump,
	TEXT("If > 0 all translation generated HLSL will be dumped \n"),
	ECVF_Default
);

static int32 GbForceNiagaraVMBinaryDump = 0;
static FAutoConsoleVariableRef CVarForceNiagaraVMBinaryDump(
	TEXT("fx.ForceNiagaraVMBinaryDump"),
	GbForceNiagaraVMBinaryDump,
	TEXT("If > 0 all translation generated binary text will be dumped \n"),
	ECVF_Default
);

static int32 GNiagaraEnablePrecompilerNamespaceFixup = 0;
static FAutoConsoleVariableRef CVarNiagaraEnablePrecompilerNamespaceFixup(
	TEXT("fx.NiagaraEnablePrecompilerNamespaceFixup"),
	GNiagaraEnablePrecompilerNamespaceFixup,
	TEXT("Enable a precompiler stage to discover parameter name matches and convert matched parameter hlsl name tokens to appropriate namespaces. \n"),
	ECVF_Default
);


static FCriticalSection TranslationCritSec;

void DumpHLSLText(const FString& SourceCode, const FString& DebugName)
{
	FScopeLock Lock(&TranslationCritSec);
	FNiagaraUtilities::DumpHLSLText(SourceCode, DebugName);
}

template< class T >
T* PrecompileDuplicateObject(T const* SourceObject, UObject* Outer, const FName Name = NAME_None)
{
	//double StartTime = FPlatformTime::Seconds();
	T* DupeObj = DuplicateObject<T>(SourceObject, Outer, Name);
	//float DeltaTime = (float)(FPlatformTime::Seconds() - StartTime);
	//if (DeltaTime > 0.01f)
	//{
	//	UE_LOG(LogNiagaraEditor, Log, TEXT("\tPrecompile Duplicate %s took %f sec"), *SourceObject->GetPathName(), DeltaTime);
	//}
	return DupeObj;

}

void FNiagaraCompileRequestData::VisitReferencedGraphs(UNiagaraGraph* InSrcGraph, UNiagaraGraph* InDupeGraph, ENiagaraScriptUsage InUsage, FCompileConstantResolver ConstantResolver, bool bNeedsCompilation, TMap<UNiagaraNodeFunctionCall*, ENiagaraScriptUsage> FunctionsWithUsage)
{
	if (!InDupeGraph && !InSrcGraph)
	{
		return;
	}
	FunctionData Data;
	Data.ClonedScript = nullptr;
	Data.ClonedGraph = InDupeGraph;
	Data.Usage = InUsage;
	Data.bHasNumericInputs = false;
	TArray<FunctionData> MadeArray;
	MadeArray.Add(Data);
	PreprocessedFunctions.Add(InSrcGraph, MadeArray);

	bool bStandaloneScript = false;

	TArray<UNiagaraNodeOutput*> OutputNodes;
	InDupeGraph->FindOutputNodes(OutputNodes);
	if (OutputNodes.Num() == 1 && UNiagaraScript::IsStandaloneScript(OutputNodes[0]->GetUsage()))
	{
		bStandaloneScript = true;
	}

	if (bNeedsCompilation)
	{
		FNiagaraEditorUtilities::ResolveNumerics(InDupeGraph, bStandaloneScript, ChangedFromNumericVars);
	}
	ClonedGraphs.AddUnique(InDupeGraph);

	VisitReferencedGraphsRecursive(InDupeGraph, ConstantResolver, bNeedsCompilation, FunctionsWithUsage);
}

void FNiagaraCompileRequestData::VisitReferencedGraphsRecursive(UNiagaraGraph* InGraph, const FCompileConstantResolver& ConstantResolver, bool bNeedsCompilation, TMap<UNiagaraNodeFunctionCall*, ENiagaraScriptUsage> FunctionsWithUsage)
{
	if (!InGraph)
	{
		return;
	}

	TArray<UNiagaraNode*> Nodes;
	InGraph->GetNodesOfClass(Nodes);
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	FPinCollectorArray CallOutputs;
	FPinCollectorArray CallInputs;

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
					UNiagaraDataInterface* Dupe = bNeedsCompilation ? PrecompileDuplicateObject<UNiagaraDataInterface>(DataInterface, GetTransientPackage()) : DataInterface;
					CopiedDataInterfacesByName.Add(DIName, Dupe);
				}
				continue;
			}

			ENiagaraScriptUsage ScriptUsage = ENiagaraScriptUsage::Function;

			UNiagaraNodeFunctionCall* FunctionCallNode = Cast<UNiagaraNodeFunctionCall>(InNode);
			if (FunctionCallNode)
			{
				UNiagaraScript* FunctionScript = FunctionCallNode->FunctionScript;
				ScriptUsage = FunctionCallNode->GetCalledUsage();

				FCompileConstantResolver FunctionConstantResolver = ConstantResolver;
				if (FunctionsWithUsage.Contains(FunctionCallNode))
				{
					FunctionConstantResolver = ConstantResolver.WithUsage(FunctionsWithUsage[FunctionCallNode]);
				}

				if (FunctionScript != nullptr)
				{
					UNiagaraGraph* FunctionGraph = FunctionCallNode->GetCalledGraph();
					{
						bool bHasNumericParams = FunctionGraph->HasNumericParameters();
						bool bHasNumericInputs = false;

						// Any function which is not directly owned by it's outer function call node must be cloned since its graph
						// will be modified in some way with it's internals function calls replaced with cloned references, or with
						// numeric fixup.  Currently the only scripts which don't need cloning are scripts used by UNiagaraNodeAssignment
						// module nodes and UNiagaraNodeCustomHlsl expression dynamic input nodes.
						bool bRequiresClonedScript = FunctionScript->GetOuter()->IsA<UNiagaraNodeFunctionCall>() == false;

						CallOutputs.Reset();
						CallInputs.Reset();
						InNode->GetOutputPins(CallOutputs);
						InNode->GetInputPins(CallInputs);

						TArray<UNiagaraNodeInput*> InputNodes;
						UNiagaraGraph::FFindInputNodeOptions Options;
						Options.bFilterDuplicates = true;
						Options.bIncludeParameters = true;
						Options.bIncludeAttributes = false;
						Options.bIncludeSystemConstants = false;
						Options.bIncludeTranslatorConstants = false;
						FunctionGraph->FindInputNodes(InputNodes, Options);

						for (UNiagaraNodeInput* Input : InputNodes)
						{
							if (Input->Input.GetType() == FNiagaraTypeDefinition::GetGenericNumericDef())
							{
								bHasNumericInputs = true;
							}
						}

						/*UE_LOG(LogNiagaraEditor, Display, TEXT("FunctionGraph = %p SrcScriptName = %s DiffPackage? %s Numerics? %s NumericInputs? %s"), FunctionGraph, *FunctionGraph->GetPathName(),
							bFromDifferentPackage ? TEXT("yes") : TEXT("no"), bHasNumericParams ? TEXT("yes") : TEXT("no"), bHasNumericInputs ? TEXT("yes") : TEXT("no"));*/

						UNiagaraGraph* ProcessedGraph = nullptr;
						// We only need to clone a non-numeric graph once.

						if (!PreprocessedFunctions.Contains(FunctionGraph))
						{
							UNiagaraScript* DupeScript = nullptr;
							if (bRequiresClonedScript == false)
							{
								DupeScript = FunctionScript;
								ProcessedGraph = FunctionGraph;
							}
							else
							{
								DupeScript = bNeedsCompilation ? PrecompileDuplicateObject<UNiagaraScript>(FunctionScript, InNode, FunctionScript->GetFName()) : FunctionScript;
								if (bNeedsCompilation)
								{
									if (DupeScript->GetSource(FunctionCallNode->SelectedScriptVersion)->GetOuter() != DupeScript)
									{
										UNiagaraScriptSource* DupeScriptSource = CastChecked<UNiagaraScriptSource>(DupeScript->GetSource(FunctionCallNode->SelectedScriptVersion));
										DupeScript->SetSource(DuplicateObject<UNiagaraScriptSource>(DupeScriptSource, DupeScript, DupeScript->GetLatestSource()->GetFName()), FunctionCallNode->SelectedScriptVersion);
									}
								}
								ProcessedGraph = Cast<UNiagaraScriptSource>(DupeScript->GetSource(FunctionCallNode->SelectedScriptVersion))->NodeGraph;
								if (bNeedsCompilation)
								{
									FEdGraphUtilities::MergeChildrenGraphsIn(ProcessedGraph, ProcessedGraph, /*bRequireSchemaMatch=*/ true);
									
									FNiagaraEditorUtilities::PreprocessFunctionGraph(Schema, ProcessedGraph, CallInputs, CallOutputs, ScriptUsage, FunctionConstantResolver);
								}
								FunctionCallNode->FunctionScript = DupeScript;
							}

							FunctionData Data;
							Data.ClonedScript = DupeScript;
							Data.ClonedGraph = ProcessedGraph;
							Data.CallInputs = CallInputs;
							Data.CallOutputs = CallOutputs;
							Data.Usage = ScriptUsage;
							Data.bHasNumericInputs = bHasNumericInputs;
							TArray<FunctionData> MadeArray;
							MadeArray.Add(Data);
							PreprocessedFunctions.Add(FunctionGraph, MadeArray);
							VisitReferencedGraphsRecursive(ProcessedGraph, FunctionConstantResolver, bNeedsCompilation, FunctionsWithUsage);
							ClonedGraphs.AddUnique(ProcessedGraph);

						}
						else if (bHasNumericParams)
						{
							UNiagaraScript* DupeScript = bNeedsCompilation ? PrecompileDuplicateObject<UNiagaraScript>(FunctionScript, InNode, FunctionScript->GetFName()) : FunctionScript;
							ProcessedGraph = Cast<UNiagaraScriptSource>(DupeScript->GetSource(FunctionCallNode->SelectedScriptVersion))->NodeGraph;
							if (bNeedsCompilation)
							{
								FEdGraphUtilities::MergeChildrenGraphsIn(ProcessedGraph, ProcessedGraph, /*bRequireSchemaMatch=*/ true);
								FNiagaraEditorUtilities::PreprocessFunctionGraph(Schema, ProcessedGraph, CallInputs, CallOutputs, ScriptUsage, FunctionConstantResolver);
							}
							FunctionCallNode->FunctionScript = DupeScript;

							TArray<FunctionData>* FoundArray = PreprocessedFunctions.Find(FunctionGraph);
							ClonedGraphs.AddUnique(ProcessedGraph);

							FunctionData Data;
							Data.ClonedScript = DupeScript;
							Data.ClonedGraph = ProcessedGraph;
							Data.CallInputs = CallInputs;
							Data.CallOutputs = CallOutputs;
							Data.Usage = ScriptUsage;
							Data.bHasNumericInputs = bHasNumericInputs;

							FoundArray->Add(Data);
							VisitReferencedGraphsRecursive(ProcessedGraph, FunctionConstantResolver, bNeedsCompilation, FunctionsWithUsage);
						}
						else if(bRequiresClonedScript)
						{
							TArray<FunctionData>* FoundArray = PreprocessedFunctions.Find(FunctionGraph);
							check(FoundArray != nullptr && FoundArray->Num() != 0);
							FunctionCallNode->FunctionScript = (*FoundArray)[0].ClonedScript;
						}
					}
				}
			}

			UNiagaraNodeEmitter* EmitterNode = Cast<UNiagaraNodeEmitter>(InNode);
			if (EmitterNode)
			{
				for (TSharedPtr<FNiagaraCompileRequestData, ESPMode::ThreadSafe>& Ptr : EmitterData)
				{
					if (Ptr->EmitterUniqueName == EmitterNode->GetEmitterUniqueName() && bNeedsCompilation)
					{
						EmitterNode->SyncEnabledState(); // Just to be safe, sync here while we likely still have the handle source.
						EmitterNode->SetOwnerSystem(nullptr);
						EmitterNode->SetCachedVariablesForCompilation(*Ptr->EmitterUniqueName, Ptr->NodeGraphDeepCopy, Ptr->Source);
					}
				}
			}
		}
	}
}

const TMap<FName, UNiagaraDataInterface*>& FNiagaraCompileRequestData::GetObjectNameMap()
{
	return CopiedDataInterfacesByName;
}

void FNiagaraCompileRequestData::MergeInEmitterPrecompiledData(FNiagaraCompileRequestDataBase* InEmitterDataBase)
{
	FNiagaraCompileRequestData* InEmitterData = (FNiagaraCompileRequestData*)InEmitterDataBase;
	if (InEmitterData)
	{
		CopiedDataInterfacesByName.Append(InEmitterData->CopiedDataInterfacesByName);
	}
}



FName FNiagaraCompileRequestData::ResolveEmitterAlias(FName VariableName) const
{
	return FNiagaraParameterMapHistory::ResolveEmitterAlias(VariableName, EmitterUniqueName);
}

void FNiagaraCompileRequestData::GetReferencedObjects(TArray<UObject*>& Objects)
{
	Objects.Add(NodeGraphDeepCopy);
	TArray<UNiagaraDataInterface*> DIs;
	CopiedDataInterfacesByName.GenerateValueArray(DIs);
	for (UNiagaraDataInterface* DI : DIs)
	{
		Objects.Add(DI);
	}

	{
		auto Iter = CDOs.CreateIterator();
		while (Iter)
		{
			Objects.Add(Iter.Value());
			++Iter;
		}
	}

	{
		auto Iter = PreprocessedFunctions.CreateIterator();
		while (Iter)
		{
			for (int32 i = 0; i < Iter.Value().Num(); i++)
			{
				Objects.Add(Iter.Value()[i].ClonedScript);
				Objects.Add(Iter.Value()[i].ClonedGraph);
			}
			++Iter;
		}
	}
}

bool FNiagaraCompileRequestData::GatherPreCompiledVariables(const FString& InNamespaceFilter, TArray<FNiagaraVariable>& OutVars)
{
	if (PrecompiledHistories.Num() == 0)
	{
		return false;
	}

	for (const FNiagaraParameterMapHistory& History : PrecompiledHistories)
	{
		for (const FNiagaraVariable& Var : History.Variables)
		{
			if (FNiagaraParameterMapHistory::IsInNamespace(Var, InNamespaceFilter))
			{
				FNiagaraVariable NewVar = Var;
				if (NewVar.IsDataAllocated() == false && !Var.IsDataInterface())
				{
					FNiagaraEditorUtilities::ResetVariableToDefaultValue(NewVar);
				}
				OutVars.AddUnique(NewVar);
			}
		}
	}
	return true;
}

void FNiagaraCompileRequestData::DeepCopyGraphs(UNiagaraScriptSource* ScriptSource, ENiagaraScriptUsage InUsage, FCompileConstantResolver ConstantResolver, bool bNeedsCompilation)
{
	// Clone the source graph so we can modify it as needed; merging in the child graphs
	//NodeGraphDeepCopy = CastChecked<UNiagaraGraph>(FEdGraphUtilities::CloneGraph(ScriptSource->NodeGraph, GetTransientPackage(), 0));
	Source = bNeedsCompilation ? PrecompileDuplicateObject<UNiagaraScriptSource>(ScriptSource, GetTransientPackage()) : ScriptSource;
	NodeGraphDeepCopy = Source->NodeGraph;
	
	//double StartTime = FPlatformTime::Seconds();
	if (bNeedsCompilation)
	{
		FEdGraphUtilities::MergeChildrenGraphsIn(NodeGraphDeepCopy, NodeGraphDeepCopy, /*bRequireSchemaMatch=*/ true);
	}
	VisitReferencedGraphs(ScriptSource->NodeGraph, NodeGraphDeepCopy, InUsage, ConstantResolver, bNeedsCompilation);
	//float DeltaTime = (float)(FPlatformTime::Seconds() - StartTime);
	//if (DeltaTime > 0.01f)
	//{
	//	UE_LOG(LogNiagaraEditor, Log, TEXT("\tPrecompile VisitReferencedGraphs %s took %f sec"), *ScriptSource->GetPathName(), DeltaTime);
	//}
}

void FNiagaraCompileRequestData::DeepCopyGraphs(UNiagaraScriptSource* ScriptSource, UNiagaraEmitter* Emitter, bool bNeedsCompilation)
{
	Source = bNeedsCompilation ? PrecompileDuplicateObject<UNiagaraScriptSource>(ScriptSource, GetTransientPackage()) : ScriptSource;
	NodeGraphDeepCopy = Source->NodeGraph;
	if (bNeedsCompilation)
	{
		FEdGraphUtilities::MergeChildrenGraphsIn(NodeGraphDeepCopy, NodeGraphDeepCopy, /*bRequireSchemaMatch=*/ true);
	}
	TMap<UNiagaraNodeFunctionCall*, ENiagaraScriptUsage> FunctionsWithUsage;
	for (UEdGraphNode* Node : ScriptSource->NodeGraph->Nodes)
	{
		if (UNiagaraNodeFunctionCall* FunctionCall = Cast<UNiagaraNodeFunctionCall>(Node))
		{
			UNiagaraNodeOutput* OutputNode = FNiagaraStackGraphUtilities::GetEmitterOutputNodeForStackNode(*FunctionCall);
			FunctionsWithUsage.Add(FunctionCall, OutputNode ? OutputNode->GetUsage() : ENiagaraScriptUsage::EmitterSpawnScript);
		}
	}
	FCompileConstantResolver ConstantResolver(Emitter, ENiagaraScriptUsage::EmitterSpawnScript);
	VisitReferencedGraphs(ScriptSource->NodeGraph, NodeGraphDeepCopy, ENiagaraScriptUsage::EmitterSpawnScript, ConstantResolver, bNeedsCompilation, FunctionsWithUsage);
}


void FNiagaraCompileRequestData::AddRapidIterationParameters(const FNiagaraParameterStore& InParamStore, FCompileConstantResolver InResolver)
{
	TArray<FNiagaraVariable> StoreParams;
	InParamStore.GetParameters(StoreParams);

	for (int32 i = 0; i < StoreParams.Num(); i++)
	{
		// Only support POD data...
		if (StoreParams[i].IsDataInterface() || StoreParams[i].IsUObject())
		{
			continue;
		}

		if (InResolver.ResolveConstant(StoreParams[i]))
		{
			continue;
		}

		// Check to see if we already have this RI var...
		int32 OurFoundIdx = INDEX_NONE;
		for (int32 OurIdx = 0; OurIdx < RapidIterationParams.Num(); OurIdx++)
		{
			if (RapidIterationParams[OurIdx].GetType() == StoreParams[i].GetType() && RapidIterationParams[OurIdx].GetName() == StoreParams[i].GetName())
			{
				OurFoundIdx = OurIdx;
				break;
			}
		}

		// If we don't already have it, add it with the up-to-date value.
		if (OurFoundIdx == INDEX_NONE)
		{
			// In parameter stores, the data isn't always up-to-date in the variable, so make sure to get the most up-to-date data before passing in.
			const int32* Index = InParamStore.FindParameterOffset(StoreParams[i]);
			if (Index != nullptr)
			{
				StoreParams[i].SetData(InParamStore.GetParameterData(*Index)); // This will memcopy the data in.
				RapidIterationParams.Add(StoreParams[i]);
			}
		}
		else
		{
			FNiagaraVariable ExistingVar = RapidIterationParams[OurFoundIdx];

			const int32* Index = InParamStore.FindParameterOffset(StoreParams[i]);
			if (Index != nullptr)
			{
				StoreParams[i].SetData(InParamStore.GetParameterData(*Index)); // This will memcopy the data in.

				if (StoreParams[i] != ExistingVar)
				{
					UE_LOG(LogNiagaraEditor, Display, TEXT("Mismatch in values for Rapid iteration param: %s vs %s"), *StoreParams[i].ToString(), *ExistingVar.ToString());
				}
			}
		}
	}
}

void FNiagaraCompileRequestData::FinishPrecompile(const TArray<FNiagaraVariable>& EncounterableVariables, ENiagaraScriptUsage InUsage, FCompileConstantResolver ConstantResolver, const TArray<UNiagaraSimulationStageBase*>* SimStages)
{
	//double StartTime = FPlatformTime::Seconds();
	{
		ENiagaraScriptCompileStatusEnum = StaticEnum<ENiagaraScriptCompileStatus>();
		ENiagaraScriptUsageEnum = StaticEnum<ENiagaraScriptUsage>();

		PrecompiledHistories.Empty();

		TArray<UNiagaraNodeOutput*> OutputNodes;
		NodeGraphDeepCopy->FindOutputNodes(OutputNodes);
		PrecompiledHistories.Empty();

		int32 NumSimStageNodes = 0;
		for (UNiagaraNodeOutput* FoundOutputNode : OutputNodes)
		{
			FName SimStageName;
			bool bStageEnabled = true;
			if (FoundOutputNode->GetUsage() == ENiagaraScriptUsage::ParticleSimulationStageScript && SimStages)
			{
				if ( bSimulationStagesEnabled )
				{
					const FGuid& UsageId = FoundOutputNode->GetUsageId();

					// Find the matching simstage to the output node
					for (UNiagaraSimulationStageBase* SimStage : *SimStages)
					{
						if (SimStage && SimStage->Script)
						{
							if (SimStage->Script->GetUsageId() == UsageId)
							{
								bStageEnabled = SimStage->bEnabled;
								UNiagaraSimulationStageGeneric* GenericStage = Cast<UNiagaraSimulationStageGeneric>(SimStage);
								if (GenericStage && SimStage->bEnabled)
								{
									SimStageName = (GenericStage->IterationSource == ENiagaraIterationSource::DataInterface) ? GenericStage->DataInterface.BoundVariable.GetName() : FName();
									break;
								}
							}
						}
					}
				}
				else
				{
					bStageEnabled = false;
				}
			}

			if (bStageEnabled)
			{
				// Map all for this output node
				FNiagaraParameterMapHistoryWithMetaDataBuilder Builder;
				Builder.ConstantResolver = ConstantResolver;
				Builder.AddGraphToCallingGraphContextStack(NodeGraphDeepCopy);
				Builder.RegisterEncounterableVariables(EncounterableVariables);

				FString TranslationName = TEXT("Emitter");
				Builder.BeginTranslation(TranslationName);
				Builder.BeginUsage(FoundOutputNode->GetUsage(), SimStageName);
				Builder.EnableScriptWhitelist(true, FoundOutputNode->GetUsage());
				Builder.BuildParameterMaps(FoundOutputNode, true);
				Builder.EndUsage();

				ensure(Builder.Histories.Num() <= 1);

				for (FNiagaraParameterMapHistory& History : Builder.Histories)
				{
					History.OriginatingScriptUsage = FoundOutputNode->GetUsage();
					for (FNiagaraVariable& Var : History.Variables)
					{
						if (Var.GetType() == FNiagaraTypeDefinition::GetGenericNumericDef())
						{
							UE_LOG(LogNiagaraEditor, Log, TEXT("Invalid numeric parameter found! %s"), *Var.GetName().ToString())
						}
					}
				}

				if (FoundOutputNode->GetUsage() == ENiagaraScriptUsage::ParticleSimulationStageScript)
				{
					NumSimStageNodes++;
				}

				PrecompiledHistories.Append(Builder.Histories);
				Builder.EndTranslation(TranslationName);
			}
			else
			{
				// Add in a blank spot
				PrecompiledHistories.Emplace();
			}
		}

		if (SimStages && NumSimStageNodes)
		{
			SpawnOnlyPerStage.Reserve(NumSimStageNodes);
			PartialParticleUpdatePerStage.Reserve(NumSimStageNodes);
			NumIterationsPerStage.Reserve(NumSimStageNodes);
			IterationSourcePerStage.Reserve(NumSimStageNodes);
			StageGuids.Reserve(NumSimStageNodes);
			StageNames.Reserve(NumSimStageNodes);
			const int32 NumProvidedStages = SimStages->Num();

			for (int32 i=0, ActiveStageCount = 0; ActiveStageCount < NumSimStageNodes && i < NumProvidedStages; ++i)
			{
				UNiagaraSimulationStageBase* SimStage = (*SimStages)[i];
				if (SimStage == nullptr || !SimStage->bEnabled)
				{
					continue;
				}

				if ( UNiagaraSimulationStageGeneric* GenericStage = Cast<UNiagaraSimulationStageGeneric>(SimStage) )
				{
					NumIterationsPerStage.Add(GenericStage->Iterations);
					IterationSourcePerStage.Add(GenericStage->IterationSource == ENiagaraIterationSource::DataInterface ? GenericStage->DataInterface.BoundVariable.GetName() : FName());
					SpawnOnlyPerStage.Add(GenericStage->bSpawnOnly);
					PartialParticleUpdatePerStage.Add(GenericStage->bDisablePartialParticleUpdate == false);
					StageGuids.Add(GenericStage->Script->GetUsageId());
					StageNames.Add(GenericStage->SimulationStageName);

					++ActiveStageCount;
				}
			}
		}


		// Generate CDO's for any referenced data interfaces...
		for (int32 i = 0; i < PrecompiledHistories.Num(); i++)
		{
			for (const FNiagaraVariable& Var : PrecompiledHistories[i].Variables)
			{
				if (Var.IsDataInterface())
				{
					UClass* Class = const_cast<UClass*>(Var.GetType().GetClass());
					UObject* Obj = PrecompileDuplicateObject(Class->GetDefaultObject(true), GetTransientPackage());
					CDOs.Add(Class, Obj);
				}
			}
		}

		// Generate CDO's for data interfaces that are passed in to function or dynamic input scripts compiled standalone as we do not have a history
		if (InUsage == ENiagaraScriptUsage::Function || InUsage == ENiagaraScriptUsage::DynamicInput)
		{
			for (const auto ReferencedGraph : ClonedGraphs)
			{
				TArray<UNiagaraNodeInput*> InputNodes;
				TArray<FNiagaraVariable*> InputVariables;
				ReferencedGraph->FindInputNodes(InputNodes);
				for (const auto InputNode : InputNodes)
				{
					InputVariables.Add(&InputNode->Input);
				}

				for (const auto InputVariable : InputVariables)
				{
					if (InputVariable->IsDataInterface())
					{
						UClass* Class = const_cast<UClass*>(InputVariable->GetType().GetClass());
						UObject* Obj = PrecompileDuplicateObject(Class->GetDefaultObject(true), GetTransientPackage());
						CDOs.Add(Class, Obj);
					}
				}
			}
		}
	}

	//float DeltaTime = (float)(FPlatformTime::Seconds() - StartTime);
	//if (DeltaTime > 0.01f)
	//{
	//	UE_LOG(LogNiagaraEditor, Log, TEXT("\tPrecompile FinishPrecompile %s took %f sec"), *ScriptSource->GetPathName(), DeltaTime);
	//}
}

bool ScriptSourceNeedsCompiling(UNiagaraScriptSource* InSource, TArray<UNiagaraScript*>& InCompilingScripts)
{
	if (!InSource || InCompilingScripts.Num() == 0)
		return false;

	// For now, we are just going to duplicate everything if there are any scripts needing compilation. 
	// We do this to mitigate any complications around cross-graph communication.
	return true;
}

TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> FNiagaraEditorModule::Precompile(UObject* InObj, FGuid Version)
{
	TArray<UNiagaraScript*> InCompilingScripts;
	UNiagaraScript* Script = Cast<UNiagaraScript>(InObj);
	UNiagaraPrecompileContainer* Container = Cast<UNiagaraPrecompileContainer>(InObj);
	UNiagaraSystem* System = Cast<UNiagaraSystem>(InObj);
	UPackage* LogPackage = nullptr;

	if (Container)
	{
		System = Container->System;
		InCompilingScripts = Container->Scripts;
		if (System)
		{
			LogPackage = System->GetOutermost();
		}
	}
	else if (Script)
	{
		InCompilingScripts.Add(Script);
		LogPackage = Script->GetOutermost();
	}

	if (!LogPackage || (!Script && !System))
	{
		TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> InvalidPtr;
		return InvalidPtr;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(NiagaraPrecompile);
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(LogPackage ? *LogPackage->GetName() : *InObj->GetName(), NiagaraChannel);

	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_ScriptSource_PreCompile);
	double StartTime = FPlatformTime::Seconds();

	TSharedPtr<FNiagaraCompileRequestData, ESPMode::ThreadSafe> BasePtr = MakeShared<FNiagaraCompileRequestData, ESPMode::ThreadSafe>();
	TArray<TSharedPtr<FNiagaraCompileRequestData, ESPMode::ThreadSafe>> DependentRequests;
	FCompileConstantResolver EmptyResolver;

	BasePtr->SourceName = InObj->GetName();

	if (Script)
	{
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetSource(Version));
		bool bNeedsCompilation = ScriptSourceNeedsCompiling(Source, InCompilingScripts);
		BasePtr->DeepCopyGraphs(Source, Script->GetUsage(), EmptyResolver, bNeedsCompilation);
		const TArray<FNiagaraVariable> EncounterableVariables;
		BasePtr->FinishPrecompile(EncounterableVariables, Script->GetUsage(), EmptyResolver, nullptr);
	}
	else if (System)
	{
		check(System->GetSystemSpawnScript()->GetLatestSource() == System->GetSystemUpdateScript()->GetLatestSource());
		BasePtr->bUseRapidIterationParams = !System->bBakeOutRapidIteration;

		// Store off the current variables in the exposed parameters list.
		TArray<FNiagaraVariable> OriginalExposedParams;
		System->GetExposedParameters().GetParameters(OriginalExposedParams);

		// Create an array of variables that we might encounter when traversing the graphs (include the originally exposed vars above)
		TArray<FNiagaraVariable> EncounterableVars(OriginalExposedParams);

		// First deep copy all the emitter graphs referenced by the system so that we can later hook up emitter handles in the system traversal.
		BasePtr->EmitterData.Empty();
		for (int32 i = 0; i < System->GetEmitterHandles().Num(); i++)
		{
			const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(i);
			TSharedPtr<FNiagaraCompileRequestData, ESPMode::ThreadSafe> EmitterPtr = MakeShared<FNiagaraCompileRequestData, ESPMode::ThreadSafe>();
			EmitterPtr->EmitterUniqueName = Handle.GetInstance()->GetUniqueEmitterName();
			if (Handle.GetIsEnabled()) // Don't need to copy the graph if we aren't going to use it.
			{
				UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Handle.GetInstance()->GraphSource);
				bool bNeedsCompilation = ScriptSourceNeedsCompiling(Source, InCompilingScripts);
				EmitterPtr->DeepCopyGraphs(Source, Handle.GetInstance(), bNeedsCompilation);
			}
			EmitterPtr->SourceName = BasePtr->SourceName;
			EmitterPtr->bUseRapidIterationParams = BasePtr->bUseRapidIterationParams || (!Handle.GetInstance()->bBakeOutRapidIteration);
			EmitterPtr->bSimulationStagesEnabled = Handle.GetInstance()->bSimulationStagesEnabled;
			BasePtr->EmitterData.Add(EmitterPtr);
		}

		// Now deep copy the system graphs, skipping traversal into any emitter references.
		{
			FCompileConstantResolver ConstantResolver(System, ENiagaraScriptUsage::SystemSpawnScript);
			UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(System->GetSystemSpawnScript()->GetLatestSource());
			bool bNeedsCompilation = ScriptSourceNeedsCompiling(Source, InCompilingScripts);
			BasePtr->DeepCopyGraphs(Source, ENiagaraScriptUsage::SystemSpawnScript, ConstantResolver, bNeedsCompilation);
			BasePtr->FinishPrecompile(EncounterableVars, ENiagaraScriptUsage::SystemSpawnScript, ConstantResolver, nullptr);
		}

		// Add the User and System variables that we did encounter to the list that emitters might also encounter.
		BasePtr->GatherPreCompiledVariables(TEXT("User"), EncounterableVars);
		BasePtr->GatherPreCompiledVariables(TEXT("System"), EncounterableVars);

		// now that the scripts have been precompiled we can prepare the rapid iteration parameters, which we need to do before we
		// actually generate the hlsl in the case of baking out the parameters
		TArray<UNiagaraScript*> Scripts;
		TMap<UNiagaraScript*, const UNiagaraEmitter*> ScriptToEmitterMap;

		// Now we can finish off the emitters.
		for (int32 i = 0; i < System->GetEmitterHandles().Num(); i++)
		{
			const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(i);
			FCompileConstantResolver ConstantResolver(Handle.GetInstance(), ENiagaraScriptUsage::EmitterSpawnScript);
			if (Handle.GetIsEnabled()) // Don't pull in the emitter if it isn't going to be used.
			{
				TArray<UNiagaraScript*> EmitterScripts;
				Handle.GetInstance()->GetScripts(EmitterScripts, false, true);

				for (int32 ScriptIdx = 0; ScriptIdx < EmitterScripts.Num(); ScriptIdx++)
				{
					if (EmitterScripts[ScriptIdx])
					{
						Scripts.AddUnique(EmitterScripts[ScriptIdx]);
						ScriptToEmitterMap.Add(EmitterScripts[ScriptIdx], Handle.GetInstance());
					}
				}
				BasePtr->EmitterData[i]->FinishPrecompile(EncounterableVars, ENiagaraScriptUsage::EmitterSpawnScript, ConstantResolver, &Handle.GetInstance()->GetSimulationStages());
				BasePtr->MergeInEmitterPrecompiledData(BasePtr->EmitterData[i].Get());

				for (UNiagaraRendererProperties* RendererProperty : Handle.GetInstance()->GetRenderers())
				{
					for (const FNiagaraVariable& BoundAttribute : RendererProperty->GetBoundAttributes())
					{
						BasePtr->EmitterData[i]->RequiredRendererVariables.AddUnique(BoundAttribute);
					}
				}
			}
		}

		{
			TMap<UNiagaraScript*, UNiagaraScript*> ScriptDependencyMap;

			// Prepare rapid iteration parameters for execution.
			for (auto ScriptEmitterPair = ScriptToEmitterMap.CreateIterator(); ScriptEmitterPair; ++ScriptEmitterPair)
			{
				UNiagaraScript* CompiledScript = ScriptEmitterPair->Key;
				const UNiagaraEmitter* Emitter = ScriptEmitterPair->Value;

				if (UNiagaraScript::IsEquivalentUsage(CompiledScript->GetUsage(), ENiagaraScriptUsage::EmitterSpawnScript))
				{
					UNiagaraScript* SystemSpawnScript = System->GetSystemSpawnScript();
					Scripts.AddUnique(SystemSpawnScript);
					ScriptDependencyMap.Add(CompiledScript, SystemSpawnScript);
					ScriptToEmitterMap.Add(SystemSpawnScript, nullptr);
				}

				if (UNiagaraScript::IsEquivalentUsage(CompiledScript->GetUsage(), ENiagaraScriptUsage::EmitterUpdateScript))
				{
					UNiagaraScript* SystemUpdateScript = System->GetSystemUpdateScript();
					Scripts.AddUnique(SystemUpdateScript);
					ScriptDependencyMap.Add(CompiledScript, SystemUpdateScript);
					ScriptToEmitterMap.Add(SystemUpdateScript, nullptr);
				}

				if (UNiagaraScript::IsEquivalentUsage(CompiledScript->GetUsage(), ENiagaraScriptUsage::ParticleSpawnScript))
				{
					if (Emitter && Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
					{
						UNiagaraScript* ComputeScript = const_cast<UNiagaraScript*>(Emitter->GetGPUComputeScript());

						Scripts.AddUnique(ComputeScript);
						ScriptDependencyMap.Add(CompiledScript, ComputeScript);
						ScriptToEmitterMap.Add(ComputeScript, Emitter);
					}
				}

				if (UNiagaraScript::IsEquivalentUsage(CompiledScript->GetUsage(), ENiagaraScriptUsage::ParticleUpdateScript))
				{
					if (Emitter && Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
					{
						UNiagaraScript* ComputeScript = const_cast<UNiagaraScript*>(Emitter->GetGPUComputeScript());

						Scripts.AddUnique(ComputeScript);
						ScriptDependencyMap.Add(CompiledScript, ComputeScript);
						ScriptToEmitterMap.Add(ComputeScript, Emitter);
					}
					else if (Emitter && Emitter->bInterpolatedSpawning)
					{
						Scripts.AddUnique(Emitter->SpawnScriptProps.Script);
						ScriptDependencyMap.Add(CompiledScript, Emitter->SpawnScriptProps.Script);
						ScriptToEmitterMap.Add(Emitter->SpawnScriptProps.Script, Emitter);
					}
				}
			}

			FNiagaraUtilities::PrepareRapidIterationParameters(Scripts, ScriptDependencyMap, ScriptToEmitterMap);

			BasePtr->AddRapidIterationParameters(System->GetSystemSpawnScript()->RapidIterationParameters, EmptyResolver);
			BasePtr->AddRapidIterationParameters(System->GetSystemUpdateScript()->RapidIterationParameters, EmptyResolver);

			// Now we can finish off the emitters.
			for (int32 i = 0; i < System->GetEmitterHandles().Num(); i++)
			{
				const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(i);
				FCompileConstantResolver ConstantResolver(Handle.GetInstance(), ENiagaraScriptUsage::EmitterSpawnScript);
				if (Handle.GetIsEnabled()) // Don't pull in the emitter if it isn't going to be used.
				{
					TArray<UNiagaraScript*> EmitterScripts;
					Handle.GetInstance()->GetScripts(EmitterScripts, false);

					for (int32 ScriptIdx = 0; ScriptIdx < EmitterScripts.Num(); ScriptIdx++)
					{
						if (EmitterScripts[ScriptIdx])
						{
							BasePtr->EmitterData[i]->AddRapidIterationParameters(EmitterScripts[ScriptIdx]->RapidIterationParameters, ConstantResolver);
						}
					}
				}
			}
		}
	}

	UE_LOG(LogNiagaraEditor, Verbose, TEXT("'%s' Precompile took %f sec."), *LogPackage->GetName(),
		(float)(FPlatformTime::Seconds() - StartTime));

	return BasePtr;
}

int32 FNiagaraEditorModule::CompileScript(const FNiagaraCompileRequestDataBase* InCompileRequest, const FNiagaraCompileOptions& InCompileOptions)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_CompileScript);

	check(InCompileRequest != NULL);
	const FNiagaraCompileRequestData* CompileRequest = (const FNiagaraCompileRequestData*)InCompileRequest;
	TArray<FNiagaraVariable> CookedRapidIterationParams = CompileRequest->GetUseRapidIterationParams() ? TArray<FNiagaraVariable>() : CompileRequest->RapidIterationParams;

	UE_LOG(LogNiagaraEditor, Verbose, TEXT("Compiling System %s ..................................................................."), *InCompileOptions.FullName);

	FNiagaraCompileResults Results;
	TSharedPtr<FHlslNiagaraCompiler> Compiler = MakeShared<FHlslNiagaraCompiler>();
	FNiagaraTranslateResults TranslateResults;
	FHlslNiagaraTranslator Translator;

	FHlslNiagaraTranslatorOptions TranslateOptions;

	if (InCompileOptions.TargetUsage == ENiagaraScriptUsage::ParticleGPUComputeScript)
	{
		TranslateOptions.SimTarget = ENiagaraSimTarget::GPUComputeSim;
	}
	else
	{
		TranslateOptions.SimTarget = ENiagaraSimTarget::CPUSim;
	}
	TranslateOptions.OverrideModuleConstants = CookedRapidIterationParams;
	TranslateOptions.bParameterRapidIteration = InCompileRequest->GetUseRapidIterationParams();


	double TranslationStartTime = FPlatformTime::Seconds();
	if (GbForceNiagaraTranslatorSingleThreaded > 0)
	{
		FScopeLock Lock(&TranslationCritSec);
		TranslateResults = Translator.Translate(CompileRequest, InCompileOptions, TranslateOptions);
	}
	else
	{
		TranslateResults = Translator.Translate(CompileRequest, InCompileOptions, TranslateOptions);
	}
	UE_LOG(LogNiagaraEditor, Verbose, TEXT("Translating System %s took %f sec."), *InCompileOptions.FullName, (float)(FPlatformTime::Seconds() - TranslationStartTime));

	if (GbForceNiagaraTranslatorDump != 0)
	{
		DumpHLSLText(Translator.GetTranslatedHLSL(), InCompileOptions.FullName);
		if (GbForceNiagaraVMBinaryDump != 0 && Results.Data.IsValid())
		{
			DumpHLSLText(Results.Data->LastAssemblyTranslation, InCompileOptions.FullName);
		}
	}

	int32 JobID = Compiler->CompileScript(CompileRequest, InCompileOptions, TranslateResults, &Translator.GetTranslateOutput(), Translator.GetTranslatedHLSL());
	ActiveCompilations.Add(JobID, Compiler);
	return JobID;
}

TSharedPtr<FNiagaraVMExecutableData> FNiagaraEditorModule::GetCompilationResult(int32 JobID, bool bWait)
{
	TSharedPtr<FHlslNiagaraCompiler>* MapEntry = ActiveCompilations.Find(JobID);
	check(MapEntry && *MapEntry);

	TSharedPtr<FHlslNiagaraCompiler> Compiler = *MapEntry;
	TOptional<FNiagaraCompileResults> CompileResult = Compiler->GetCompileResult(JobID, bWait);
	if (!CompileResult)
	{
		return TSharedPtr<FNiagaraVMExecutableData>();
	}
	ActiveCompilations.Remove(JobID);
	FNiagaraCompileResults& Results = CompileResult.GetValue();

	FString OutGraphLevelErrorMessages;
	for (const FNiagaraCompileEvent& Message : Results.CompileEvents)
	{
		#if defined(NIAGARA_SCRIPT_COMPILE_LOGGING_MEDIUM)
			UE_LOG(LogNiagaraCompiler, Log, TEXT("%s"), *Message.Message);
		#endif
		if (Message.Severity == FNiagaraCompileEventSeverity::Error)
		{
			// Write the error messages to the string as well so that they can be echoed up the chain.
			if (OutGraphLevelErrorMessages.Len() > 0)
			{
				OutGraphLevelErrorMessages += "\n";
			}
			OutGraphLevelErrorMessages += Message.Message;
		}
	}
	
	Results.Data->ErrorMsg = OutGraphLevelErrorMessages;
	Results.Data->LastCompileStatus = (FNiagaraCompileResults::CompileResultsToSummary(&Results));
	if (Results.Data->LastCompileStatus != ENiagaraScriptCompileStatus::NCS_Error)
	{
		// When there are no errors the compile events get emptied, so add them back here.
		Results.Data->LastCompileEvents.Append(Results.CompileEvents);
	}
	return CompileResult->Data;
}

void FNiagaraEditorModule::TestCompileScriptFromConsole(const TArray<FString>& Arguments)
{
	if (Arguments.Num() == 1)
	{
		FString TranslatedHLSL;
		FFileHelper::LoadFileToString(TranslatedHLSL, *Arguments[0]);
		if (TranslatedHLSL.Len() != 0)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslCompiler_TestCompileShader_VectorVM);
			FShaderCompilerInput Input;
			Input.Target = FShaderTarget(SF_Compute, SP_PCD3D_SM5);
			Input.VirtualSourceFilePath = TEXT("/Plugin/FX/Niagara/Private/NiagaraEmitterInstanceShader.usf");
			Input.EntryPointName = TEXT("SimulateMain");
			Input.Environment.SetDefine(TEXT("VM_SIMULATION"), 1);
			Input.Environment.SetDefine(TEXT("COMPUTESHADER"), 1);
			Input.Environment.SetDefine(TEXT("PIXELSHADER"), 0);
			Input.Environment.SetDefine(TEXT("DOMAINSHADER"), 0);
			Input.Environment.SetDefine(TEXT("HULLSHADER"), 0);
			Input.Environment.SetDefine(TEXT("VERTEXSHADER"), 0);
			Input.Environment.SetDefine(TEXT("GEOMETRYSHADER"), 0);
			Input.Environment.IncludeVirtualPathToContentsMap.Add(TEXT("/Engine/Generated/NiagaraEmitterInstance.ush"), TranslatedHLSL);

			FShaderCompilerOutput Output;
			FVectorVMCompilationOutput CompilationOutput;
			double StartTime = FPlatformTime::Seconds();
			bool bSucceeded = CompileShader_VectorVM(Input, Output, FString(FPlatformProcess::ShaderDir()), 0, CompilationOutput, GNiagaraSkipVectorVMBackendOptimizations != 0);
			float DeltaTime = (float)(FPlatformTime::Seconds() - StartTime);

			if (bSucceeded)
			{
				UE_LOG(LogNiagaraCompiler, Log, TEXT("Test compile of %s took %f seconds and succeeded."), *Arguments[0], DeltaTime);
			}
			else
			{
				UE_LOG(LogNiagaraCompiler, Error, TEXT("Test compile of %s took %f seconds and failed.  Errors: %s"), *Arguments[0], DeltaTime, *CompilationOutput.Errors);
			}
		}
		else
		{
			UE_LOG(LogNiagaraCompiler, Error, TEXT("Test compile of %s failed, the file could not be loaded or it was empty."), *Arguments[0]);
		}
	}
	else
	{
		UE_LOG(LogNiagaraCompiler, Error, TEXT("Test compile failed, file name argument was missing."));
	}
}


ENiagaraScriptCompileStatus FNiagaraCompileResults::CompileResultsToSummary(const FNiagaraCompileResults* CompileResults)
{
	ENiagaraScriptCompileStatus SummaryStatus = ENiagaraScriptCompileStatus::NCS_Unknown;
	if (CompileResults != nullptr)
	{
		if (CompileResults->NumErrors > 0)
		{
			SummaryStatus = ENiagaraScriptCompileStatus::NCS_Error;
		}
		else
		{
			if (CompileResults->bVMSucceeded)
			{
				if (CompileResults->NumWarnings)
				{
					SummaryStatus = ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings;
				}
				else
				{
					SummaryStatus = ENiagaraScriptCompileStatus::NCS_UpToDate;
				}
			}

			if (CompileResults->bComputeSucceeded)
			{
				if (CompileResults->NumWarnings)
				{
					SummaryStatus = ENiagaraScriptCompileStatus::NCS_ComputeUpToDateWithWarnings;
				}
				else
				{
					SummaryStatus = ENiagaraScriptCompileStatus::NCS_UpToDate;
				}
			}
		}
	}
	return SummaryStatus;
}

int32 FHlslNiagaraCompiler::CompileScript(const FNiagaraCompileRequestData* InCompileRequest, const FNiagaraCompileOptions& InOptions, const FNiagaraTranslateResults& InTranslateResults, FNiagaraTranslatorOutput *TranslatorOutput, FString &TranslatedHLSL)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslCompiler_CompileScript);

	CompileResults.Data = MakeShared<FNiagaraVMExecutableData>();

	CompileResults.AppendCompileEvents(MakeArrayView(InTranslateResults.CompileEvents));
	CompileResults.Data->LastCompileEvents.Append(InTranslateResults.CompileEvents);
	CompileResults.Data->ExternalDependencies = InTranslateResults.CompileDependencies;
	CompileResults.Data->CompileTags = InTranslateResults.CompileTags;

	//TODO: This should probably be done via the same route that other shaders take through the shader compiler etc.
	//But that adds the complexity of a new shader type, new shader class and a new shader map to contain them etc.
	//Can do things simply for now.
	
	CompileResults.Data->LastHlslTranslation = TEXT("");

	FShaderCompilerInput Input;
	Input.Target = FShaderTarget(SF_Compute, SP_PCD3D_SM5);
	Input.VirtualSourceFilePath = TEXT("/Plugin/FX/Niagara/Private/NiagaraEmitterInstanceShader.usf");
	Input.EntryPointName = TEXT("SimulateMain");
	Input.Environment.SetDefine(TEXT("VM_SIMULATION"), 1);
	Input.Environment.SetDefine(TEXT("COMPUTESHADER"), 1);
	Input.Environment.SetDefine(TEXT("PIXELSHADER"), 0);
	Input.Environment.SetDefine(TEXT("DOMAINSHADER"), 0);
	Input.Environment.SetDefine(TEXT("HULLSHADER"), 0);
	Input.Environment.SetDefine(TEXT("VERTEXSHADER"), 0);
	Input.Environment.SetDefine(TEXT("GEOMETRYSHADER"), 0);
	Input.Environment.IncludeVirtualPathToContentsMap.Add(TEXT("/Engine/Generated/NiagaraEmitterInstance.ush"), TranslatedHLSL);
	Input.bGenerateDirectCompileFile = false;
	Input.DumpDebugInfoRootPath = GShaderCompilingManager->GetAbsoluteShaderDebugInfoDirectory() / TEXT("VM");
	FString UsageIdStr = !InOptions.TargetUsageId.IsValid() ? TEXT("") : (TEXT("_") + InOptions.TargetUsageId.ToString());
	Input.DebugGroupName = InCompileRequest->SourceName / InCompileRequest->EmitterUniqueName / InCompileRequest->ENiagaraScriptUsageEnum->GetNameStringByValue((int64)InOptions.TargetUsage) + UsageIdStr;
	Input.DebugExtension.Empty();
	Input.DumpDebugInfoPath.Empty();

	if (GShaderCompilingManager->GetDumpShaderDebugInfo() == FShaderCompilingManager::EDumpShaderDebugInfo::Always)
	{
		Input.DumpDebugInfoPath = GShaderCompilingManager->CreateShaderDebugInfoPath(Input);
	}
	CompileResults.DumpDebugInfoPath = Input.DumpDebugInfoPath;

	uint32 JobID = FShaderCommonCompileJob::GetNextJobId();
	CompilationJob = MakeUnique<FNiagaraCompilerJob>();
	CompilationJob->TranslatorOutput = TranslatorOutput ? *TranslatorOutput : FNiagaraTranslatorOutput();

	CompileResults.bVMSucceeded = (CompilationJob->TranslatorOutput.Errors.Len() == 0) && (TranslatedHLSL.Len() > 0) && !InTranslateResults.NumErrors;

	// only issue jobs for VM compilation if we're going to be using the resulting byte code.  This excludes particle scripts when we're using
	// a GPU simulation
	if (InOptions.IsGpuScript() && UNiagaraScript::IsParticleScript(InOptions.TargetUsage))
	{
		CompileResults.bComputeSucceeded = false;
		if (CompileResults.bVMSucceeded)
		{
			*(CompileResults.Data) = CompilationJob->TranslatorOutput.ScriptData;
			CompileResults.Data->ByteCode.Empty();
			CompileResults.bComputeSucceeded = true;
		}
		CompileResults.Data->LastHlslTranslationGPU = TranslatedHLSL;
		CompileResults.Data->CompileTags = InTranslateResults.CompileTags;
		DumpDebugInfo(CompileResults, Input, true);
		CompilationJob->CompileResults = CompileResults;
		return JobID;
	}
	CompilationJob->TranslatorOutput.ScriptData.LastHlslTranslation = TranslatedHLSL;
	CompilationJob->TranslatorOutput.ScriptData.ExternalDependencies = InTranslateResults.CompileDependencies;
	CompilationJob->TranslatorOutput.ScriptData.CompileTags = InTranslateResults.CompileTags;

	bool bJobScheduled = false;
	if (CompileResults.bVMSucceeded)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslCompiler_CompileShader_VectorVM);
		CompilationJob->StartTime = FPlatformTime::Seconds();

		FShaderType* NiagaraShaderType = nullptr;
		for (TLinkedList<FShaderType*>::TIterator ShaderTypeIt(FShaderType::GetTypeList()); ShaderTypeIt; ShaderTypeIt.Next())
		{
			if (FNiagaraShaderType* ShaderType = ShaderTypeIt->GetNiagaraShaderType())
			{
				NiagaraShaderType = ShaderType;
				break;
			}
		}
		if (NiagaraShaderType)
		{
			TRefCountPtr<FShaderCompileJob> Job = GShaderCompilingManager->PrepareShaderCompileJob(JobID, FShaderCompileJobKey(NiagaraShaderType), EShaderCompileJobPriority::Normal);
			if (Job)
			{
				TArray<FShaderCommonCompileJobPtr> NewJobs;
				CompilationJob->ShaderCompileJob = Job;
				Input.ShaderFormat = FName(TEXT("VVM_1_0"));
				if (GNiagaraSkipVectorVMBackendOptimizations != 0)
				{
					Input.Environment.CompilerFlags.Add(CFLAG_SkipOptimizations);
				}
				Job->Input = Input;
				NewJobs.Add(FShaderCommonCompileJobPtr(Job));

				GShaderCompilingManager->SubmitJobs(NewJobs, FString(), FString());
			}
			bJobScheduled = true;
		}
	}
	CompileResults.Data->LastHlslTranslation = TranslatedHLSL;

	if (!bJobScheduled)
	{

		CompileResults.Data->ByteCode.Empty();
		CompileResults.Data->Attributes.Empty();
		CompileResults.Data->Parameters.Empty();
		CompileResults.Data->InternalParameters.Empty();
		CompileResults.Data->DataInterfaceInfo.Empty();

	}
	CompilationJob->CompileResults = CompileResults;

	return JobID;
}

void FHlslNiagaraCompiler::FixupVMAssembly(FString& Asm)
{
	for (int32 OpCode = 0; OpCode < VectorVM::GetNumOpCodes(); ++OpCode)
	{
		//TODO: reduce string ops here by moving these out to a static list.
		FString ToReplace = TEXT("__OP__") + LexToString(OpCode) + TEXT("(");
		FString Replacement = VectorVM::GetOpName(EVectorVMOp(OpCode)) + TEXT("(");
		Asm.ReplaceInline(*ToReplace, *Replacement);
	}
}

//TODO: Map Lines of HLSL to their source Nodes and flag those nodes with errors associated with their lines.
void FHlslNiagaraCompiler::DumpDebugInfo(const FNiagaraCompileResults& CompileResult, const FShaderCompilerInput& Input, bool bGPUScript)
{
	if (CompileResults.Data.IsValid())
	{
		// Support dumping debug info only on failure or warnings
		FString DumpDebugInfoPath = CompileResult.DumpDebugInfoPath;
		if (DumpDebugInfoPath.IsEmpty())
		{
			const FShaderCompilingManager::EDumpShaderDebugInfo DumpShaderDebugInfo = GShaderCompilingManager->GetDumpShaderDebugInfo();
			bool bDumpDebugInfo = false;
			if (DumpShaderDebugInfo == FShaderCompilingManager::EDumpShaderDebugInfo::OnError)
			{
				bDumpDebugInfo = !CompileResult.bVMSucceeded;
			}
			else if (DumpShaderDebugInfo == FShaderCompilingManager::EDumpShaderDebugInfo::OnErrorOrWarning)
			{
				bDumpDebugInfo = !CompileResult.bVMSucceeded || (CompileResult.NumErrors + CompileResult.NumWarnings) > 0;
			}

			if (bDumpDebugInfo)
			{
				DumpDebugInfoPath = GShaderCompilingManager->CreateShaderDebugInfoPath(Input);
			}
		}

		if (!DumpDebugInfoPath.IsEmpty())
		{
			FString ExportText = CompileResults.Data->LastHlslTranslation;
			FString ExportTextAsm = CompileResults.Data->LastAssemblyTranslation;
			if (bGPUScript)
			{
				ExportText = CompileResults.Data->LastHlslTranslationGPU;
				ExportTextAsm = "";
			}
			FString ExportTextParams;
			for (const FNiagaraVariable& Var : CompileResults.Data->Parameters.Parameters)
			{
				ExportTextParams += Var.ToString();
				ExportTextParams += "\n";
			}

			FNiagaraEditorUtilities::WriteTextFileToDisk(DumpDebugInfoPath, TEXT("NiagaraEmitterInstance.ush"), ExportText, true);
			FNiagaraEditorUtilities::WriteTextFileToDisk(DumpDebugInfoPath, TEXT("NiagaraEmitterInstance.asm"), ExportTextAsm, true);
			FNiagaraEditorUtilities::WriteTextFileToDisk(DumpDebugInfoPath, TEXT("NiagaraEmitterInstance.params"), ExportTextParams, true);
		}
	}
}

TOptional<FNiagaraCompileResults> FHlslNiagaraCompiler::GetCompileResult(int32 JobID, bool bWait /*= false*/)
{
	check(IsInGameThread());

	if (!CompilationJob)
	{
		return TOptional<FNiagaraCompileResults>();
	}
	if (!CompilationJob->ShaderCompileJob)
	{
		// In case we did not schedule any compile jobs but have a static result (e.g. in case of previous translator errors)
		FNiagaraCompileResults Results = CompilationJob->CompileResults;
		CompilationJob.Reset();
		return Results;
	}

	TArray<int32> ShaderMapIDs;
	ShaderMapIDs.Add(JobID);
	if (bWait && !CompilationJob->ShaderCompileJob->bReleased)
	{
		GShaderCompilingManager->FinishCompilation(NULL, ShaderMapIDs);
		check(CompilationJob->ShaderCompileJob->bReleased);
	}

	if (!CompilationJob->ShaderCompileJob->bReleased)
	{
		return TOptional<FNiagaraCompileResults>();
	}
	else
	{
		// We do this because otherwise the shader compiling manager might still reference the deleted job at the end of this method.
		// The finalization flag is set by another thread, so the manager might not have had a change to process the result.
		GShaderCompilingManager->FinishCompilation(NULL, ShaderMapIDs);
	}

	FNiagaraCompileResults Results = CompilationJob->CompileResults;
	Results.bVMSucceeded = false;
	FVectorVMCompilationOutput CompilationOutput;
	if (CompilationJob->ShaderCompileJob->bSucceeded)
	{
		const TArray<uint8>& Code = CompilationJob->ShaderCompileJob->Output.ShaderCode.GetReadAccess();
		FShaderCodeReader ShaderCode(Code);
		FMemoryReader Ar(Code, true);
		Ar.SetLimitSize(ShaderCode.GetActualShaderCodeSize());
		Ar << CompilationOutput;

		Results.bVMSucceeded = true;
	}
	else if (CompilationJob->ShaderCompileJob->Output.Errors.Num() > 0)
	{
		FString Errors;
		for (FShaderCompilerError ShaderError : CompilationJob->ShaderCompileJob->Output.Errors)
		{
			Errors += ShaderError.StrippedErrorMessage + "\n";
		}
		Error(FText::Format(LOCTEXT("VectorVMCompileErrorMessageFormat", "The Vector VM compile failed.  Errors:\n{0}"), FText::FromString(Errors)));
		DumpHLSLText(Results.Data->LastHlslTranslation, CompilationJob->CompileResults.DumpDebugInfoPath);
	}

	if (!Results.bVMSucceeded)
	{
		//For now we just copy the shader code over into the script. 
		Results.Data->ByteCode.Empty();
		Results.Data->Attributes.Empty();
		Results.Data->Parameters.Empty();
		Results.Data->InternalParameters.Empty();
		Results.Data->DataInterfaceInfo.Empty();
		//Eventually Niagara will have all the shader plumbing and do things like materials.
	}
	else
	{
			//Build internal parameters
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslCompiler_CompileShader_VectorVMSucceeded);
		*Results.Data = CompilationJob->TranslatorOutput.ScriptData;
		Results.Data->ByteCode = CompilationOutput.ByteCode;
		Results.Data->NumTempRegisters = CompilationOutput.MaxTempRegistersUsed + 1;
		Results.Data->LastAssemblyTranslation = CompilationOutput.AssemblyAsString;
		FixupVMAssembly(Results.Data->LastAssemblyTranslation);
		Results.Data->LastOpCount = CompilationOutput.NumOps;

		if (GbForceNiagaraVMBinaryDump != 0 && Results.Data.IsValid())
		{
			DumpHLSLText(Results.Data->LastAssemblyTranslation, CompilationJob->CompileResults.DumpDebugInfoPath);
		}

		Results.Data->InternalParameters.Empty();
		for (int32 i = 0; i < CompilationOutput.InternalConstantOffsets.Num(); ++i)
		{
			const FName ConstantName(TEXT("InternalConstant"), i);
			EVectorVMBaseTypes Type = CompilationOutput.InternalConstantTypes[i];
			int32 Offset = CompilationOutput.InternalConstantOffsets[i];
			switch (Type)
			{
			case EVectorVMBaseTypes::Float:
			{
				float Val = *(float*)(CompilationOutput.InternalConstantData.GetData() + Offset);
				Results.Data->InternalParameters.SetOrAdd(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), ConstantName))->SetValue(Val);
			}
			break;
			case EVectorVMBaseTypes::Int:
			{
				int32 Val = *(int32*)(CompilationOutput.InternalConstantData.GetData() + Offset);
				Results.Data->InternalParameters.SetOrAdd(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), ConstantName))->SetValue(Val);
			}
			break;
			case EVectorVMBaseTypes::Bool:
			{
				int32 Val = *(int32*)(CompilationOutput.InternalConstantData.GetData() + Offset);
				Results.Data->InternalParameters.SetOrAdd(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), ConstantName))->SetValue(Val);
			}
			break;
			}
		}
		Results.CompileTime = (float)(FPlatformTime::Seconds() - CompilationJob->StartTime);
		Results.Data->CompileTime = Results.CompileTime;


		Results.Data->CalledVMExternalFunctions.Empty(CompilationOutput.CalledVMFunctionTable.Num());
		for (FCalledVMFunction& FuncInfo : CompilationOutput.CalledVMFunctionTable)
		{
			//Extract the external function call table binding info.
			const FNiagaraFunctionSignature* Sig = nullptr;
			for (FNiagaraScriptDataInterfaceCompileInfo& NDIInfo : CompilationJob->TranslatorOutput.ScriptData.DataInterfaceInfo)
			{
				Sig = NDIInfo.RegisteredFunctions.FindByPredicate([&](const FNiagaraFunctionSignature& CheckSig)
				{
					FString SigSymbol = FHlslNiagaraTranslator::GetFunctionSignatureSymbol(CheckSig);
					return SigSymbol == FuncInfo.Name;
				});
				if (Sig)
				{
					break;
				}
			}

			// Look in function library
			if (Sig == nullptr)
			{
				Sig = UNiagaraFunctionLibrary::GetVectorVMFastPathOps(true).FindByPredicate(
					[&](const FNiagaraFunctionSignature& CheckSig)
				{
					FString SigSymbol = FHlslNiagaraTranslator::GetFunctionSignatureSymbol(CheckSig);
					return SigSymbol == FuncInfo.Name;
				}
				);
			}

			if (Sig)
			{
				int32 NewBindingIdx = Results.Data->CalledVMExternalFunctions.AddDefaulted();
				Results.Data->CalledVMExternalFunctions[NewBindingIdx].Name = *Sig->GetName();
				Results.Data->CalledVMExternalFunctions[NewBindingIdx].OwnerName = Sig->OwnerName;
				Results.Data->CalledVMExternalFunctions[NewBindingIdx].InputParamLocations = FuncInfo.InputParamLocations;
				Results.Data->CalledVMExternalFunctions[NewBindingIdx].NumOutputs = FuncInfo.NumOutputs;
				for (auto it = Sig->FunctionSpecifiers.CreateConstIterator(); it; ++it)
				{
					// we convert the map into an array here to save runtime memory
					Results.Data->CalledVMExternalFunctions[NewBindingIdx].FunctionSpecifiers.Emplace(it->Key, it->Value);
				}
			}
			else
			{
				Error(FText::Format(LOCTEXT("VectorVMExternalFunctionBindingError", "Failed to bind the external function call:  {0}"), FText::FromString(FuncInfo.Name)));
				Results.bVMSucceeded = false;
			}
		}
	}
	DumpDebugInfo(CompileResults, CompilationJob->ShaderCompileJob->Input, false);

	//Seems like Results is a bit of a cobbled together mess at this point.
	//Ideally we can tidy this up in future.
	//Doing this as a minimal risk free fix for not having errors passed through into the compile results.
	Results.NumErrors = CompileResults.NumErrors;
	Results.CompileEvents = CompileResults.CompileEvents;
	Results.Data->CompileTags = CompileResults.Data->CompileTags;

	CompilationJob.Reset();
	return Results;
}

FHlslNiagaraCompiler::FHlslNiagaraCompiler()
	: CompileResults()
{
}



void FHlslNiagaraCompiler::Error(FText ErrorText)
{
	FString ErrorString = FString::Printf(TEXT("%s"), *ErrorText.ToString());
	CompileResults.Data->LastCompileEvents.Add(FNiagaraCompileEvent(FNiagaraCompileEventSeverity::Error, ErrorString));
	CompileResults.CompileEvents.Add(FNiagaraCompileEvent(FNiagaraCompileEventSeverity::Error, ErrorString));
	CompileResults.NumErrors++;
}

void FHlslNiagaraCompiler::Warning(FText WarningText)
{
	FString WarnString = FString::Printf(TEXT("%s"), *WarningText.ToString());
	CompileResults.Data->LastCompileEvents.Add(FNiagaraCompileEvent(FNiagaraCompileEventSeverity::Warning, WarnString));
	CompileResults.CompileEvents.Add(FNiagaraCompileEvent(FNiagaraCompileEventSeverity::Warning, WarnString));
	CompileResults.NumWarnings++;
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
