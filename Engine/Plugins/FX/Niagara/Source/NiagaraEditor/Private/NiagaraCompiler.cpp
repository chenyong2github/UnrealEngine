// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraCompiler.h"
#include "NiagaraHlslTranslator.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraEditorModule.h"
#include "NiagaraComponent.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IShaderFormat.h"
#include "ShaderFormatVectorVM.h"
#include "NiagaraConstants.h"
#include "NiagaraSystem.h"
#include "NiagaraNodeEmitter.h"
#include "NiagaraNodeInput.h"
#include "NiagaraScriptSource.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceStaticMesh.h"
#include "NiagaraDataInterfaceCurlNoise.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeParameterMapSet.h"
#include "INiagaraEditorTypeUtilities.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraNodeEmitter.h"
#include "NiagaraNodeOutput.h"
#include "ShaderCore.h"
#include "EdGraphSchema_Niagara.h"
#include "Misc/FileHelper.h"
#include "ShaderCompiler.h"
#include "NiagaraShader.h"
#include "NiagaraScript.h"
#include "Serialization/MemoryReader.h"
#include "HAL/ThreadSafeBool.h"

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

static FCriticalSection TranslationCritSec;

void DumpHLSLText(const FString& SourceCode, const FString& DebugName)
{
	FScopeLock Lock(&TranslationCritSec);
	FNiagaraUtilities::DumpHLSLText(SourceCode, DebugName);
}

void FNiagaraCompileRequestData::VisitReferencedGraphs(UNiagaraGraph* InSrcGraph, UNiagaraGraph* InDupeGraph, ENiagaraScriptUsage InUsage, FCompileConstantResolver ConstantResolver)
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
	FNiagaraEditorUtilities::ResolveNumerics(InDupeGraph, bStandaloneScript, ChangedFromNumericVars);
	ClonedGraphs.AddUnique(InDupeGraph);

	VisitReferencedGraphsRecursive(InDupeGraph, ConstantResolver);
}

void FNiagaraCompileRequestData::VisitReferencedGraphsRecursive(UNiagaraGraph* InGraph, const FCompileConstantResolver& ConstantResolver)
{
	if (!InGraph)
	{
		return;
	}
	UPackage* OwningPackage = InGraph->GetOutermost();

	TArray<UNiagaraNode*> Nodes;
	InGraph->GetNodesOfClass(Nodes);
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();

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
					FName DIName = InputNode->Input.GetName();
					UNiagaraDataInterface* Dupe = DuplicateObject<UNiagaraDataInterface>(DataInterface, GetTransientPackage());
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

				if (FunctionScript != nullptr)
				{
					UNiagaraGraph* FunctionGraph = FunctionCallNode->GetCalledGraph();
					{
						bool bHasNumericParams = FunctionGraph->HasNumericParameters();
						bool bHasNumericInputs = false;

						UPackage* FunctionPackage = FunctionGraph->GetOutermost();
						bool bFromDifferentPackage = OwningPackage != FunctionPackage;

						TArray<UEdGraphPin*> CallOutputs;
						TArray<UEdGraphPin*> CallInputs;
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
							if (!bFromDifferentPackage && !bHasNumericInputs && !bHasNumericParams)
							{
								DupeScript = FunctionScript;
								ProcessedGraph = FunctionGraph;
							}
							else
							{
								DupeScript = DuplicateObject<UNiagaraScript>(FunctionScript, InNode, FunctionScript->GetFName());
								ProcessedGraph = Cast<UNiagaraScriptSource>(DupeScript->GetSource())->NodeGraph;
								FEdGraphUtilities::MergeChildrenGraphsIn(ProcessedGraph, ProcessedGraph, /*bRequireSchemaMatch=*/ true);
								FNiagaraEditorUtilities::PreprocessFunctionGraph(Schema, ProcessedGraph, CallInputs, CallOutputs, ScriptUsage, ConstantResolver);
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
							VisitReferencedGraphsRecursive(ProcessedGraph, ConstantResolver);		
							ClonedGraphs.AddUnique(ProcessedGraph);

						}
						else if (bHasNumericParams)
						{
							UNiagaraScript* DupeScript = DuplicateObject<UNiagaraScript>(FunctionScript, InNode, FunctionScript->GetFName());
							ProcessedGraph = Cast<UNiagaraScriptSource>(DupeScript->GetSource())->NodeGraph;
							FEdGraphUtilities::MergeChildrenGraphsIn(ProcessedGraph, ProcessedGraph, /*bRequireSchemaMatch=*/ true);
							FNiagaraEditorUtilities::PreprocessFunctionGraph(Schema, ProcessedGraph, CallInputs, CallOutputs, ScriptUsage, ConstantResolver);
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
							VisitReferencedGraphsRecursive(ProcessedGraph, ConstantResolver);
						}
						else if (bFromDifferentPackage)
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
					if (Ptr->EmitterUniqueName == EmitterNode->GetEmitterUniqueName())
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
//	check(EmitterData.Contains(InEmitterDataBase));

	FNiagaraCompileRequestData* InEmitterData = (FNiagaraCompileRequestData*)InEmitterDataBase;
	if (InEmitterData)
	{
		{
			auto It = InEmitterData->CopiedDataInterfacesByName.CreateIterator();
			while (It)
			{
				FName Name = It.Key();
				Name = FNiagaraParameterMapHistory::ResolveEmitterAlias(Name, InEmitterData->GetUniqueEmitterName());
				CopiedDataInterfacesByName.Add(Name, It.Value());
				++It;
			}
		}
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

void FNiagaraCompileRequestData::DeepCopyGraphs(UNiagaraScriptSource* ScriptSource, ENiagaraScriptUsage InUsage, FCompileConstantResolver ConstantResolver)
{
	// Clone the source graph so we can modify it as needed; merging in the child graphs
	//NodeGraphDeepCopy = CastChecked<UNiagaraGraph>(FEdGraphUtilities::CloneGraph(ScriptSource->NodeGraph, GetTransientPackage(), 0));
	Source = DuplicateObject<UNiagaraScriptSource>(ScriptSource, GetTransientPackage());
	NodeGraphDeepCopy = Source->NodeGraph;
	FEdGraphUtilities::MergeChildrenGraphsIn(NodeGraphDeepCopy, NodeGraphDeepCopy, /*bRequireSchemaMatch=*/ true);
	VisitReferencedGraphs(ScriptSource->NodeGraph, NodeGraphDeepCopy, InUsage, ConstantResolver);
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

void FNiagaraCompileRequestData::FinishPrecompile(UNiagaraScriptSource* ScriptSource, const TArray<FNiagaraVariable>& EncounterableVariables, ENiagaraScriptUsage InUsage, FCompileConstantResolver ConstantResolver)
{
	{
		ENiagaraScriptCompileStatusEnum = StaticEnum<ENiagaraScriptCompileStatus>();
		ENiagaraScriptUsageEnum = StaticEnum<ENiagaraScriptUsage>();

		PrecompiledHistories.Empty();

		TArray<UNiagaraNodeOutput*> OutputNodes;
		NodeGraphDeepCopy->FindOutputNodes(OutputNodes);
		PrecompiledHistories.Empty();

		for (UNiagaraNodeOutput* FoundOutputNode : OutputNodes)
		{
			// Map all for this output node
			FNiagaraParameterMapHistoryBuilder Builder;
			Builder.ConstantResolver = ConstantResolver;
			Builder.RegisterEncounterableVariables(EncounterableVariables);

			FString TranslationName = TEXT("Emitter");
			Builder.BeginTranslation(TranslationName);
			Builder.EnableScriptWhitelist(true, FoundOutputNode->GetUsage());
			Builder.BuildParameterMaps(FoundOutputNode, true);
			
			TArray<FNiagaraParameterMapHistory> Histories = Builder.Histories;
			ensure(Histories.Num() <= 1);

			for (FNiagaraParameterMapHistory& History : Histories)
			{
				for (FNiagaraVariable& Var : History.Variables)
				{
					check(Var.GetType() != FNiagaraTypeDefinition::GetGenericNumericDef());
				}
			}

			PrecompiledHistories.Append(Histories);
			Builder.EndTranslation(TranslationName);
		}


		// Generate CDO's for any referenced data interfaces...
		for (int32 i = 0; i < PrecompiledHistories.Num(); i++)
		{
			for (const FNiagaraVariable& Var : PrecompiledHistories[i].Variables)
			{
				if (Var.IsDataInterface())
				{
					UClass* Class = const_cast<UClass*>(Var.GetType().GetClass());
					UObject* Obj = DuplicateObject(Class->GetDefaultObject(true), GetTransientPackage());
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
						UObject* Obj = DuplicateObject(Class->GetDefaultObject(true), GetTransientPackage());
						CDOs.Add(Class, Obj);
					}
				}
			}
		}

	}
}

TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> FNiagaraEditorModule::Precompile(UObject* InObj)
{
	UNiagaraScript* Script = Cast<UNiagaraScript>(InObj);
	UNiagaraSystem* System = Cast<UNiagaraSystem>(InObj);

	if (!Script && !System)
	{
		TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> InvalidPtr;
		return InvalidPtr;
	}

	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_ScriptSource_PreCompile);
	double StartTime = FPlatformTime::Seconds();

	TSharedPtr<FNiagaraCompileRequestData, ESPMode::ThreadSafe> BasePtr = MakeShared<FNiagaraCompileRequestData, ESPMode::ThreadSafe>();
	TArray<TSharedPtr<FNiagaraCompileRequestData, ESPMode::ThreadSafe>> DependentRequests;
	FCompileConstantResolver EmptyResolver;

	BasePtr->SourceName = InObj->GetName();

	if (Script)
	{
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetSource());
		BasePtr->DeepCopyGraphs(Source, Script->GetUsage(), EmptyResolver);
		const TArray<FNiagaraVariable> EncounterableVariables;
		BasePtr->FinishPrecompile(Source, EncounterableVariables, Script->GetUsage(), EmptyResolver);
	}
	else if (System)
	{
		BasePtr->bUseRapidIterationParams = !System->bBakeOutRapidIteration;

		// Store off the current variables in the exposed parameters list.
		TArray<FNiagaraVariable> OriginalExposedParams;
		System->GetExposedParameters().GetParameters(OriginalExposedParams);

		// Create an array of variables that we might encounter when traversing the graphs (include the originally exposed vars above)
		TArray<FNiagaraVariable> EncounterableVars(OriginalExposedParams);

		// Create an array of variables that we *did* encounter when traversing the graphs.
		TArray<FNiagaraVariable> EncounteredExposedVars;
		check(System->GetSystemSpawnScript()->GetSource() == System->GetSystemUpdateScript()->GetSource());
		uint32 DetailLevelMask = 0xFFFFFFFF;

		// First deep copy all the emitter graphs referenced by the system so that we can later hook up emitter handles in the system traversal.
		BasePtr->EmitterData.Empty();
		for (int32 i = 0; i < System->GetEmitterHandles().Num(); i++)
		{
			const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(i);
			FCompileConstantResolver ConstantResolver(Handle.GetInstance());
			TSharedPtr<FNiagaraCompileRequestData, ESPMode::ThreadSafe> EmitterPtr = MakeShared<FNiagaraCompileRequestData, ESPMode::ThreadSafe>();
			if (Handle.GetIsEnabled()) // Don't need to copy the graph if we aren't going to use it.
			{
				EmitterPtr->DeepCopyGraphs(Cast<UNiagaraScriptSource>(Handle.GetInstance()->GraphSource), ENiagaraScriptUsage::EmitterSpawnScript, ConstantResolver);
			}
			EmitterPtr->EmitterUniqueName = Handle.GetInstance()->GetUniqueEmitterName();
			EmitterPtr->SourceName = BasePtr->SourceName;
			EmitterPtr->bUseRapidIterationParams = BasePtr->bUseRapidIterationParams || (!Handle.GetInstance()->bBakeOutRapidIteration);
			BasePtr->EmitterData.Add(EmitterPtr);
		}

		// Now deep copy the system graphs, skipping traversal into any emitter references.
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(System->GetSystemSpawnScript()->GetSource());
		BasePtr->DeepCopyGraphs(Source, ENiagaraScriptUsage::SystemSpawnScript, FCompileConstantResolver());
		BasePtr->AddRapidIterationParameters(System->GetSystemSpawnScript()->RapidIterationParameters, FCompileConstantResolver());
		BasePtr->AddRapidIterationParameters(System->GetSystemUpdateScript()->RapidIterationParameters, FCompileConstantResolver());
		BasePtr->FinishPrecompile(Source, EncounterableVars, ENiagaraScriptUsage::SystemSpawnScript, EmptyResolver);

		// Add the User and System variables that we did encounter to the list that emitters might also encounter.
		BasePtr->GatherPreCompiledVariables(TEXT("User"), EncounterableVars);
		BasePtr->GatherPreCompiledVariables(TEXT("System"), EncounterableVars);

		// Now we can finish off the emitters.
		for (int32 i = 0; i < System->GetEmitterHandles().Num(); i++)
		{
			const FNiagaraEmitterHandle& Handle = System->GetEmitterHandle(i);
			FCompileConstantResolver ConstantResolver(Handle.GetInstance());
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
				BasePtr->EmitterData[i]->FinishPrecompile(Cast<UNiagaraScriptSource>(Handle.GetInstance()->GraphSource), EncounterableVars, ENiagaraScriptUsage::EmitterSpawnScript, ConstantResolver);
				BasePtr->MergeInEmitterPrecompiledData(BasePtr->EmitterData[i].Get());
			}
		}

	}

	UE_LOG(LogNiagaraEditor, Verbose, TEXT("'%s' Precompile took %f sec."), *InObj->GetOutermost()->GetName(),
		(float)(FPlatformTime::Seconds() - StartTime));

	return BasePtr;
}

int32 FNiagaraEditorModule::CompileScript(const FNiagaraCompileRequestDataBase* InCompileRequest, const FNiagaraCompileOptions& InCompileOptions)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_CompileScript);

	double StartTime = FPlatformTime::Seconds();

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

	int32 JobID = Compiler->CompileScript(CompileRequest, InCompileOptions, &Translator.GetTranslateOutput(), Translator.GetTranslatedHLSL());
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

int32 FHlslNiagaraCompiler::CompileScript(const FNiagaraCompileRequestData* InCompileRequest, const FNiagaraCompileOptions& InOptions, FNiagaraTranslatorOutput *TranslatorOutput, FString &TranslatedHLSL)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslCompiler_CompileScript);

	CompileResults.Data = MakeShared<FNiagaraVMExecutableData>();

	//TODO: This should probably be done via the same route that other shaders take through the shader compiler etc.
	//But that adds the complexity of a new shader type, new shader class and a new shader map to contain them etc.
	//Can do things simply for now.
	
	CompileResults.Data->LastHlslTranslation = TEXT("");

	FShaderCompilerInput Input;
	Input.Target = FShaderTarget(SF_Compute, SP_PCD3D_SM5);
	Input.VirtualSourceFilePath = TEXT("/Plugin/FX/Niagara/Private/NiagaraEmitterInstanceShader.usf");
	Input.EntryPointName = TEXT("SimulateMain");
	Input.Environment.SetDefine(TEXT("VM_SIMULATION"), 1);
	Input.Environment.IncludeVirtualPathToContentsMap.Add(TEXT("/Engine/Generated/NiagaraEmitterInstance.ush"), TranslatedHLSL);
	Input.bGenerateDirectCompileFile = false;
	Input.DumpDebugInfoRootPath = GShaderCompilingManager->GetAbsoluteShaderDebugInfoDirectory() / TEXT("VM");
	FString UsageIdStr = !InOptions.TargetUsageId.IsValid() ? TEXT("") : (TEXT("_") + InOptions.TargetUsageId.ToString());
	Input.DebugGroupName = InCompileRequest->SourceName / InCompileRequest->EmitterUniqueName / InCompileRequest->ENiagaraScriptUsageEnum->GetNameStringByValue((int64)InOptions.TargetUsage) + UsageIdStr;
	Input.DumpDebugInfoPath = Input.DumpDebugInfoRootPath / Input.DebugGroupName;

	if (GShaderCompilingManager->GetDumpShaderDebugInfo())
	{
		// Sanitize the name to be used as a path
		// List mostly comes from set of characters not allowed by windows in a path.  Just try to rename a file and type one of these for the list.
		Input.DumpDebugInfoPath.ReplaceInline(TEXT("<"), TEXT("("));
		Input.DumpDebugInfoPath.ReplaceInline(TEXT(">"), TEXT(")"));
		Input.DumpDebugInfoPath.ReplaceInline(TEXT("::"), TEXT("=="));
		Input.DumpDebugInfoPath.ReplaceInline(TEXT("|"), TEXT("_"));
		Input.DumpDebugInfoPath.ReplaceInline(TEXT("*"), TEXT("-"));
		Input.DumpDebugInfoPath.ReplaceInline(TEXT("?"), TEXT("!"));
		Input.DumpDebugInfoPath.ReplaceInline(TEXT("\""), TEXT("\'"));

		if (!IFileManager::Get().DirectoryExists(*Input.DumpDebugInfoPath))
		{
			verifyf(IFileManager::Get().MakeDirectory(*Input.DumpDebugInfoPath, true), TEXT("Failed to create directory for shader debug info '%s'"), *Input.DumpDebugInfoPath);
		}
	}
	CompileResults.DumpDebugInfoPath = Input.DumpDebugInfoPath;

	int32 JobID = FShaderCommonCompileJob::GetNextJobId();
	CompilationJob = MakeUnique<FNiagaraCompilerJob>();
	CompilationJob->TranslatorOutput = TranslatorOutput ? *TranslatorOutput : FNiagaraTranslatorOutput();

	bool bGPUScript = InOptions.TargetUsage == ENiagaraScriptUsage::ParticleGPUComputeScript;
	if (bGPUScript)
	{
		CompileResults.bComputeSucceeded = false;
		if (TranslatorOutput != nullptr && TranslatorOutput->Errors.Len() > 0)
		{
			Error(FText::Format(LOCTEXT("HlslTranslateErrorMessageFormat", "The HLSL Translator failed.  Errors:\n{0}"), FText::FromString(TranslatorOutput->Errors)));
			CompileResults.bVMSucceeded = false;
		}
		else if (TranslatedHLSL.Len() == 0)
		{
			CompileResults.NumErrors++;
			CompileResults.bVMSucceeded = false;
		}
		else
		{
			*(CompileResults.Data) = CompilationJob->TranslatorOutput.ScriptData;
			CompileResults.Data->ByteCode.Empty();
			CompileResults.bComputeSucceeded = true;
		}
		CompileResults.Data->LastHlslTranslationGPU = TranslatedHLSL;
		DumpDebugInfo(CompileResults, true);
		CompilationJob->CompileResults = CompileResults;
		return JobID;
	}
	CompilationJob->TranslatorOutput.ScriptData.LastHlslTranslation = TranslatedHLSL;

	bool bJobScheduled = false;
	if (TranslatorOutput != nullptr && TranslatorOutput->Errors.Len() > 0)
	{
		//TODO: Map Lines of HLSL to their source Nodes and flag those nodes with errors associated with their lines.
		Error(FText::Format(LOCTEXT("HlslTranslateErrorMessageFormat", "The HLSL Translator failed.  Errors:\n{0}"), FText::FromString(TranslatorOutput->Errors)));
		CompileResults.bVMSucceeded = false;
	}
	else if (TranslatedHLSL.Len() == 0)
	{
		CompileResults.NumErrors++;
		CompileResults.bVMSucceeded = false;
	}
	else
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
			TArray<FShaderCommonCompileJob*> NewJobs;
			CompilationJob->ShaderCompileJob = TRefCountPtr<FShaderCompileJob>(new FShaderCompileJob(JobID, nullptr, NiagaraShaderType, 0));
			Input.ShaderFormat = FName(TEXT("VVM_1_0"));
			if (GNiagaraSkipVectorVMBackendOptimizations != 0)
			{
				Input.Environment.CompilerFlags.Add(CFLAG_SkipOptimizations);
			}
			CompilationJob->ShaderCompileJob->Input = Input;
			NewJobs.Add(CompilationJob->ShaderCompileJob);

			GShaderCompilingManager->AddJobs(NewJobs, true, false, FString(), FString(), true);
			bJobScheduled = true;
		}
	}
	CompileResults.Data->LastHlslTranslation = TranslatedHLSL;

	if (!bJobScheduled)
	{
		//Some error. Clear script and exit.
		CompileResults.Data->ByteCode.Empty();
		CompileResults.Data->Attributes.Empty();
		CompileResults.Data->Parameters.Empty();
		CompileResults.Data->InternalParameters.Empty();
		CompileResults.Data->DataInterfaceInfo.Empty();
		//Script->NumUserPtrs = 0;
	}
	CompilationJob->CompileResults = CompileResults;

	return JobID;
}

void FHlslNiagaraCompiler::DumpDebugInfo(FNiagaraCompileResults& CompileResult, bool bGPUScript)
{
	if (GShaderCompilingManager->GetDumpShaderDebugInfo() && CompileResults.Data.IsValid())
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

		FNiagaraEditorUtilities::WriteTextFileToDisk(CompileResult.DumpDebugInfoPath, TEXT("NiagaraEmitterInstance.ush"), ExportText, true);
		FNiagaraEditorUtilities::WriteTextFileToDisk(CompileResult.DumpDebugInfoPath, TEXT("NiagaraEmitterInstance.asm"), ExportTextAsm, true);
		FNiagaraEditorUtilities::WriteTextFileToDisk(CompileResult.DumpDebugInfoPath, TEXT("NiagaraEmitterInstance.params"), ExportTextParams, true);
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
	if (bWait && !CompilationJob->ShaderCompileJob->bFinalized)
	{
		GShaderCompilingManager->FinishCompilation(NULL, ShaderMapIDs);
		check(CompilationJob->ShaderCompileJob->bFinalized);
	}

	if (!CompilationJob->ShaderCompileJob->bFinalized)
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
	}

	if (!Results.bVMSucceeded)
	{
		//Some error. Clear script and exit.
		Results.Data->ByteCode.Empty();
		Results.Data->Attributes.Empty();
		Results.Data->Parameters.Empty();
		Results.Data->InternalParameters.Empty();
		Results.Data->DataInterfaceInfo.Empty();
		//		Script->NumUserPtrs = 0;
	}
	else
	{
		// Copy the shader code over into the script.
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslCompiler_CompileShader_VectorVMSucceeded);
		*Results.Data = CompilationJob->TranslatorOutput.ScriptData;
		Results.Data->ByteCode = CompilationOutput.ByteCode;
		Results.Data->NumTempRegisters = CompilationOutput.MaxTempRegistersUsed + 1;
		Results.Data->LastAssemblyTranslation = CompilationOutput.AssemblyAsString;
		Results.Data->LastOpCount = CompilationOutput.NumOps;
		//Build internal parameters
		Results.Data->InternalParameters.Empty();
		for (int32 i = 0; i < CompilationOutput.InternalConstantOffsets.Num(); ++i)
		{
			EVectorVMBaseTypes Type = CompilationOutput.InternalConstantTypes[i];
			int32 Offset = CompilationOutput.InternalConstantOffsets[i];
			switch (Type)
			{
			case EVectorVMBaseTypes::Float:
			{
				float Val = *(float*)(CompilationOutput.InternalConstantData.GetData() + Offset);
				Results.Data->InternalParameters.SetOrAdd(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), *LexToString(Val)))->SetValue(Val);
			}
			break;
			case EVectorVMBaseTypes::Int:
			{
				int32 Val = *(int32*)(CompilationOutput.InternalConstantData.GetData() + Offset);
				Results.Data->InternalParameters.SetOrAdd(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), *LexToString(Val)))->SetValue(Val);
			}
			break;
			case EVectorVMBaseTypes::Bool:
			{
				int32 Val = *(int32*)(CompilationOutput.InternalConstantData.GetData() + Offset);
				Results.Data->InternalParameters.SetOrAdd(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), Val == 0 ? TEXT("FALSE") : TEXT("TRUE")))->SetValue(Val);
			}
			break;
			}
		}
		Results.CompileTime = (float)(FPlatformTime::Seconds() - CompilationJob->StartTime);
		Results.Data->CompileTime = Results.CompileTime;

		//Extract the external function call table binding info.
		Results.Data->CalledVMExternalFunctions.Empty(CompilationOutput.CalledVMFunctionTable.Num());
		for (FCalledVMFunction& FuncInfo : CompilationOutput.CalledVMFunctionTable)
		{
			//Find the interface corresponding to this call.
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
				Error(FText::Format(LOCTEXT("VectorVMExternalFunctionBindingError", "Failed to bind the exernal function call:  {0}"), FText::FromString(FuncInfo.Name)));
				Results.bVMSucceeded = false;
			}
		}
	}
	DumpDebugInfo(CompileResults, false);

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
