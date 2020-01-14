// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraHlslTranslator.h"
#include "NiagaraComponent.h"
#include "NiagaraGraph.h"
#include "NiagaraScriptSource.h"
#include "EdGraphUtilities.h"
#include "UObject/UObjectHash.h"
#include "NiagaraNode.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeIf.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeReadDataSet.h"
#include "NiagaraNodeWriteDataSet.h"
#include "NiagaraNodeParameterMapGet.h"
#include "NiagaraNodeParameterMapSet.h"
#include "NiagaraNodeParameterMapFor.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeOp.h"
#include "NiagaraNodeConvert.h"
#include "EdGraphSchema_Niagara.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/IShaderFormat.h"
#include "ShaderFormatVectorVM.h"
#include "NiagaraConstants.h"
#include "NiagaraSystem.h"
#include "NiagaraNodeEmitter.h"
#include "INiagaraEditorTypeUtilities.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEditorModule.h"
#include "NiagaraNodeReroute.h"

#include "NiagaraFunctionLibrary.h"

#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceCurve.h"
#include "NiagaraDataInterfaceVector2DCurve.h"
#include "NiagaraDataInterfaceVectorCurve.h"
#include "NiagaraDataInterfaceVector4Curve.h"
#include "NiagaraDataInterfaceColorCurve.h"
#include "NiagaraDataInterfaceStaticMesh.h"
#include "NiagaraDataInterfaceCurlNoise.h"

#include "NiagaraParameterCollection.h"
#include "NiagaraEditorTickables.h"
#include "ShaderCore.h"
#include "NiagaraShaderCompilationManager.h"

#include "NiagaraEditorSettings.h"
#include "NiagaraNodeStaticSwitch.h"
#include "NiagaraScriptVariable.h"

#define LOCTEXT_NAMESPACE "NiagaraCompiler"

DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - Translate"), STAT_NiagaraEditor_HlslTranslator_Translate, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - CloneGraphAndPrepareForCompilation"), STAT_NiagaraEditor_HlslTranslator_CloneGraphAndPrepareForCompilation, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - BuildParameterMapHlslDefinitions"), STAT_NiagaraEditor_HlslTranslator_BuildParameterMapHlslDefinitions, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - Emitter"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_Emitter, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - MapGet"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_MapGet, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - FunctionCall"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_FunctionCall, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - FunctionCallCloneGraphNumeric"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_FunctionCallCloneGraphNumeric, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - FunctionCallCloneGraphNonNumeric"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_FunctionCallCloneGraphNonNumeric, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - RegisterFunctionCall"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_RegisterFunctionCall, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - GenerateFunctionCall"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_GenerateFunctionCall, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - CustomHLSL"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_CustomHLSL, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - FuncBody"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_FuncBody, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - Output"), STAT_NiagaraEditor_HlslTranslator_Output, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - MapSet"), STAT_NiagaraEditor_HlslTranslator_MapSet, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - MapForBegin"), STAT_NiagaraEditor_HlslTranslator_MapForBegin, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - MapForEnd"), STAT_NiagaraEditor_HlslTranslator_MapForEnd, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - Operation"), STAT_NiagaraEditor_HlslTranslator_Operation, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - If"), STAT_NiagaraEditor_HlslTranslator_If, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - CompilePin"), STAT_NiagaraEditor_HlslTranslator_CompilePin, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - CompileOutputPin"), STAT_NiagaraEditor_HlslTranslator_CompileOutputPin, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - GetParameter"), STAT_NiagaraEditor_HlslTranslator_GetParameter, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - GenerateFunctionSignature"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_GenerateFunctionSignature, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - RegisterFunctionCall_Source"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_RegisterFunctionCall_Source, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - RegisterFunctionCall_Compile"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_RegisterFunctionCall_Compile, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - RegisterFunctionCall_Signature"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_RegisterFunctionCall_Signature, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - RegisterFunctionCall_FunctionDefStr"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_RegisterFunctionCall_FunctionDefStr, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - GenerateFunctionSignature_UniqueDueToMaps"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_GenerateFunctionSignature_UniqueDueToMaps, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - GenerateFunctionSignature_Outputs"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_GenerateFunctionSignature_Outputs, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - GenerateFunctionSignature_Inputs"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_GenerateFunctionSignature_Inputs, STATGROUP_NiagaraEditor);
DECLARE_CYCLE_STAT(TEXT("Niagara - HlslTranslator - GenerateFunctionSignature_FindInputNodes"), STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_GenerateFunctionSignature_FindInputNodes, STATGROUP_NiagaraEditor);

static FNiagaraShaderQueueTickable NiagaraShaderQueueProcessor;
FNiagaraShaderProcessorTickable NiagaraShaderProcessor;

// not pretty. TODO: refactor
// this will be called via a delegate from UNiagaraScript's cache for cook function,
// because editor tickables aren't ticked during cooking
void FNiagaraShaderQueueTickable::ProcessQueue()
{
	check(IsInGameThread());

	for (FNiagaraCompilationQueue::NiagaraCompilationQueueItem &Item : FNiagaraCompilationQueue::Get()->GetQueue())
	{
		FNiagaraShaderScript* ShaderScript = Item.Script;
		TRefCountPtr<FNiagaraShaderMap>NewShaderMap = Item.ShaderMap;

		if (ShaderScript == nullptr) // This script has been removed from the pending queue post submission... just skip it.
		{
			FNiagaraShaderMap::RemovePendingMap(NewShaderMap);
			NewShaderMap->SetCompiledSuccessfully(false);
			UE_LOG(LogNiagaraEditor, Log, TEXT("GPU shader compile skipped. Id %d"), NewShaderMap->GetCompilingId());
			continue;
		}
		UNiagaraScript* CompilableScript = ShaderScript->GetBaseVMScript();

		// For now System scripts don't generate HLSL and go through a special pass...
		// [OP] thinking they'll likely never run on GPU anyways
		if (CompilableScript->IsValidLowLevel() == false || CompilableScript->CanBeRunOnGpu() == false || !CompilableScript->GetVMExecutableData().IsValid() ||
			CompilableScript->GetVMExecutableData().LastHlslTranslationGPU.Len() == 0)
		{
			NewShaderMap->SetCompiledSuccessfully(false);
			FNiagaraShaderMap::RemovePendingMap(NewShaderMap);
			ShaderScript->RemoveOutstandingCompileId(NewShaderMap->GetCompilingId());
			UE_LOG(LogNiagaraEditor, Log, TEXT("GPU shader compile skipped. Id %d"), NewShaderMap->GetCompilingId());
			continue;
		}

		FNiagaraComputeShaderCompilationOutput NewCompilationOutput;

		ShaderScript->SetDataInterfaceParamInfo(CompilableScript->GetVMExecutableData().DIParamInfo);
		ShaderScript->SourceName = TEXT("NiagaraComputeShader");
		UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(CompilableScript->GetOuter());
		if (Emitter && Emitter->GetUniqueEmitterName().Len() > 0)
		{
			ShaderScript->SourceName = Emitter->GetUniqueEmitterName();
		}
		ShaderScript->HlslOutput = CompilableScript->GetVMExecutableData().LastHlslTranslationGPU;

		{
			// Create a shader compiler environment for the script that will be shared by all jobs from this script
			TRefCountPtr<FShaderCompilerEnvironment> CompilerEnvironment = new FShaderCompilerEnvironment();

			FString ShaderCode = CompilableScript->GetVMExecutableData().LastHlslTranslationGPU;
			// When not running in the editor, the shaders are created in-sync in the postload.
			const bool bSynchronousCompile = !GIsEditor;

			// Compile the shaders for the script.
			NewShaderMap->Compile(ShaderScript, Item.ShaderMapId, CompilerEnvironment, NewCompilationOutput, Item.Platform, bSynchronousCompile, Item.bApply);
		}
	}

	FNiagaraCompilationQueue::Get()->GetQueue().Empty();
}

void FNiagaraShaderQueueTickable::Tick(float DeltaSeconds)
{
	ProcessQueue();
}

ENiagaraScriptCompileStatus FNiagaraTranslateResults::TranslateResultsToSummary(const FNiagaraTranslateResults* TranslateResults)
{
	ENiagaraScriptCompileStatus SummaryStatus = ENiagaraScriptCompileStatus::NCS_Unknown;
	if (TranslateResults != nullptr)
	{
		if (TranslateResults->NumErrors > 0)
		{
			SummaryStatus = ENiagaraScriptCompileStatus::NCS_Error;
		}
		else
		{
			if (TranslateResults->bHLSLGenSucceeded)
			{
				if (TranslateResults->NumWarnings)
				{
					SummaryStatus = ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings;
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


FString FHlslNiagaraTranslator::GetCode(int32 ChunkIdx)
{
	FNiagaraCodeChunk& Chunk = CodeChunks[ChunkIdx];
	return GetCode(Chunk);
}

FString FHlslNiagaraTranslator::GetCode(FNiagaraCodeChunk& Chunk)
{
	TArray<FStringFormatArg> Args;
	for (int32 i = 0; i < Chunk.SourceChunks.Num(); ++i)
	{
		Args.Add(GetCodeAsSource(Chunk.SourceChunks[i]));
	}
	FString DefinitionString = FString::Format(*Chunk.Definition, Args);

	FString FinalString;

	if (Chunk.Mode == ENiagaraCodeChunkMode::Body)
	{
		FinalString += TEXT("\t");
	}

	if (Chunk.SymbolName.IsEmpty())
	{
		check(!DefinitionString.IsEmpty());
		FinalString += DefinitionString + (Chunk.bIsTerminated ? TEXT(";\n") : TEXT("\n"));
	}
	else
	{
		if (DefinitionString.IsEmpty())
		{
			check(Chunk.bDecl);//Otherwise, we're doing nothing here.
			FinalString += GetStructHlslTypeName(Chunk.Type) + TEXT(" ") + Chunk.SymbolName + TEXT(";\n");
		}
		else
		{
			if (Chunk.bDecl)
			{
				FinalString += GetStructHlslTypeName(Chunk.Type) + TEXT(" ") + Chunk.SymbolName + TEXT(" = ") + DefinitionString + TEXT(";\n");
			}
			else
			{
				FinalString += Chunk.SymbolName + TEXT(" = ") + DefinitionString + TEXT(";\n");
			}
		}
	}
	return FinalString;
}

FString FHlslNiagaraTranslator::GetCodeAsSource(int32 ChunkIdx)
{
	if (ChunkIdx >= 0 && ChunkIdx < CodeChunks.Num())
	{
		FNiagaraCodeChunk& Chunk = CodeChunks[ChunkIdx];
		return Chunk.SymbolName + Chunk.ComponentMask;
	}
	return "Undefined";
}

bool FHlslNiagaraTranslator::ValidateTypePins(UNiagaraNode* NodeToValidate)
{
	bool bPinsAreValid = true;
	for (UEdGraphPin* Pin : NodeToValidate->GetAllPins())
	{
		if (Pin->PinType.PinCategory == "")
		{
			Error(LOCTEXT("InvalidPinTypeError", "Node pin has an undefined type."), NodeToValidate, Pin);
			bPinsAreValid = false;
		}
		else if (Pin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryType)
		{
			FNiagaraTypeDefinition Type = Schema->PinToTypeDefinition(Pin);
			if (Type.IsValid() == false)
			{
				Error(LOCTEXT("InvalidPinTypeError", "Node pin has an undefined type."), NodeToValidate, Pin);
				bPinsAreValid = false;
			}
		}
	}
	return bPinsAreValid;
}


void FHlslNiagaraTranslator::GenerateFunctionSignature(ENiagaraScriptUsage ScriptUsage, FString InName, const FString& InFullName, UNiagaraGraph* FuncGraph, TArray<int32>& Inputs,
	bool bHadNumericInputs, bool bHasParameterMapParameters, TArray<UEdGraphPin*> StaticSwitchValues, FNiagaraFunctionSignature& OutSig)const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_GenerateFunctionSignature);

	TArray<FNiagaraVariable> InputVars;
	TArray<UNiagaraNodeInput*> InputsNodes;

	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_GenerateFunctionSignature_FindInputNodes);
		InputsNodes.Reserve(100);
		UNiagaraGraph::FFindInputNodeOptions Options;
		Options.bSort = true;
		Options.bFilterDuplicates = true;
		Options.bIncludeTranslatorConstants = false;
		// If we're compiling the emitter function we need to filter to the correct usage so that we only get inputs associated with the emitter call, but if we're compiling any other kind of function call we need all inputs
		// since the function call nodes themselves will have been generated with pins for all inputs and since we match the input nodes here to the inputs passed in by index, the two collections must match otherwise we fail
		// to compile a graph that would otherwise work correctly.
		Options.bFilterByScriptUsage = ScriptUsage == ENiagaraScriptUsage::EmitterSpawnScript || ScriptUsage == ENiagaraScriptUsage::EmitterUpdateScript;
		Options.TargetScriptUsage = ScriptUsage;
		FuncGraph->FindInputNodes(InputsNodes, Options);

		if (Inputs.Num() != InputsNodes.Num())
		{
			const_cast<FHlslNiagaraTranslator*>(this)->Error(FText::Format(LOCTEXT("GenerateFunctionSignatureFail", "Generating function signature for {0} failed.  The function call is providing a different number of inputs than the function graph supplies."),
				FText::FromString(InFullName)), nullptr, nullptr);
			return;
		}
	}

	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_GenerateFunctionSignature_Inputs);

		InName.Reserve(100 * InputsNodes.Num());
		InputVars.Reserve(InputsNodes.Num());
		TArray<uint32> ConstantInputIndicesToRemove;
		for (int32 i = 0; i < InputsNodes.Num(); ++i)
		{
			//Only add to the signature if the caller has provided it, otherwise we use a local default.
			if (Inputs[i] != INDEX_NONE)
			{
				FNiagaraVariable LiteralConstant = InputsNodes[i]->Input;
				if (GetLiteralConstantVariable(LiteralConstant))
				{
					checkf(LiteralConstant.GetType() == FNiagaraTypeDefinition::GetBoolDef(), TEXT("Only boolean types are currently supported for literal constants."));
					FString LiteralConstantAlias = LiteralConstant.GetName().ToString() + TEXT("_") + (LiteralConstant.GetValue<bool>() ? TEXT("true") : TEXT("false"));
					InName += TEXT("_") + GetSanitizedSymbolName(LiteralConstantAlias.Replace(TEXT("."), TEXT("_")));
					ConstantInputIndicesToRemove.Add(i);
				}
				else
				{
					InputVars.Add(InputsNodes[i]->Input);
					if (bHadNumericInputs)
					{
						InName += TEXT("_In");
						InName += InputsNodes[i]->Input.GetType().GetName();
					}
				}
			}
		}
		
		// Remove the inputs which will be handled by inline constants
		for (int32 i = ConstantInputIndicesToRemove.Num() - 1; i >= 0; i--)
		{
			Inputs.RemoveAt(ConstantInputIndicesToRemove[i]);
		}

		//Now actually remove the missing inputs so they match the signature.
		Inputs.Remove(INDEX_NONE);
	}

	TArray<FNiagaraVariable> OutputVars;
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_GenerateFunctionSignature_Outputs);

		OutputVars.Reserve(10);
		FuncGraph->GetOutputNodeVariables(ScriptUsage, OutputVars);

		for (int32 i = 0; i < OutputVars.Num(); ++i)
		{
			//Only add to the signature if the caller has provided it, otherwise we use a local default.
			if (bHadNumericInputs)
			{
				InName += TEXT("_Out");
				InName += OutputVars[i].GetType().GetName();
			}
		}
	}

	const FString* ModuleAliasStr = ActiveHistoryForFunctionCalls.GetModuleAlias();
	const FString* EmitterAliasStr = ActiveHistoryForFunctionCalls.GetEmitterAlias();
	// For now, we want each module call to be unique due to parameter maps and aliasing causing different variables
	// to be written within each call.
	if ((ScriptUsage == ENiagaraScriptUsage::Module || ScriptUsage == ENiagaraScriptUsage::DynamicInput ||
		ScriptUsage == ENiagaraScriptUsage::EmitterSpawnScript || ScriptUsage == ENiagaraScriptUsage::EmitterUpdateScript
		|| bHasParameterMapParameters)
		&& (ModuleAliasStr != nullptr || EmitterAliasStr != nullptr))
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_GenerateFunctionSignature_UniqueDueToMaps);
		FString SignatureName;
		SignatureName.Reserve(1024);
		if (ModuleAliasStr != nullptr)
		{
			SignatureName = GetSanitizedSymbolName(*ModuleAliasStr);
		}
		if (EmitterAliasStr != nullptr)
		{
			FString Prefix = ModuleAliasStr != nullptr ? TEXT("_") : TEXT("");
			SignatureName += Prefix;
			SignatureName += GetSanitizedSymbolName(*EmitterAliasStr);
		}
		SignatureName.ReplaceInline(TEXT("."), TEXT("_"));
		OutSig = FNiagaraFunctionSignature(*SignatureName, InputVars, OutputVars, *InFullName, true, false);
	}
	else
	{
		FNiagaraGraphFunctionAliasContext FunctionAliasContext;
		FunctionAliasContext.CompileUsage = GetCurrentUsage();
		FunctionAliasContext.StaticSwitchValues = StaticSwitchValues;
		FString SignatureName = InName + FuncGraph->GetFunctionAliasByContext(FunctionAliasContext);
		OutSig = FNiagaraFunctionSignature(*SignatureName, InputVars, OutputVars, *InFullName, true, false);
	}
}

//////////////////////////////////////////////////////////////////////////

FHlslNiagaraTranslator::FHlslNiagaraTranslator()
	: Schema(nullptr)
	, TranslateResults()
	, CurrentBodyChunkMode(ENiagaraCodeChunkMode::Body)
	, ActiveStageIdx(-1)
	, bInitializedDefaults(false)
{
}


FString FHlslNiagaraTranslator::GetFunctionDefinitions()
{
	FString FwdDeclString;
	FString DefinitionsString;

	for (TPair<FNiagaraFunctionSignature, FString> FuncPair : Functions)
	{
		FString Sig = GetFunctionSignature(FuncPair.Key);
		FwdDeclString += Sig + TEXT(";\n");
		if (!FuncPair.Value.IsEmpty())
		{
			DefinitionsString += Sig + TEXT("\n{\n") + FuncPair.Value + TEXT("}\n\n");
		}
		// Don't do anything if the value is empty on the function pair, as this is indicative of 
		// data interface functions that should be defined differently.
	}

	// Check to see if we have interpolated spawn enabled, for the GPU we need to look for the additional defines
	bool bHasInterpolatedSpawn = CompileOptions.TargetUsage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated;
	if ( CompileOptions.TargetUsage == ENiagaraScriptUsage::ParticleGPUComputeScript )
	{
		bHasInterpolatedSpawn = CompileOptions.AdditionalDefines.Contains(TEXT("InterpolatedSpawn"));
	}

	//Add a few hard coded helper functions in.
	FwdDeclString += TEXT("float GetSpawnInterpolation();");
	//Add helper function to get the interpolation factor.
	if ( bHasInterpolatedSpawn )
	{
		DefinitionsString += TEXT("float GetSpawnInterpolation()\n{\n");
		DefinitionsString += TEXT("\treturn HackSpawnInterp;\n");
		DefinitionsString += TEXT("}\n\n");
	}
	else
	{
		DefinitionsString += TEXT("float GetSpawnInterpolation()\n{\n");
		DefinitionsString += TEXT("\treturn 1.0f;");
		DefinitionsString += TEXT("}\n\n");
	}

	return FwdDeclString + TEXT("\n") + DefinitionsString;
}

void FHlslNiagaraTranslator::BuildMissingDefaults()
{
	AddBodyComment(TEXT("// Begin HandleMissingDefaultValues"));

	if (UNiagaraScript::IsSpawnScript(TranslationStages[ActiveStageIdx].ScriptUsage))
	{
		// First go through all the variables that we did not write the defaults for yet. For spawn scripts, this usually
		// means variables that reference other variables but are not themselves used within spawn.
		for (FNiagaraVariable& Var : DeferredVariablesMissingDefault)
		{
			const UEdGraphPin* DefaultPin = UniqueVarToDefaultPin.FindChecked(Var);
			bool bWriteToParamMapEntries = UniqueVarToWriteToParamMap.FindChecked(Var);
			int32 OutputChunkId = INDEX_NONE;

			UNiagaraScriptVariable* ScriptVariable = nullptr;
			if (DefaultPin) 
			{
				if (UNiagaraGraph* DefaultPinGraph = CastChecked<UNiagaraGraph>(DefaultPin->GetOwningNode()->GetGraph())) 
				{
					ScriptVariable = DefaultPinGraph->GetScriptVariable(Var);
				}
			}

			HandleParameterRead(ActiveStageIdx, Var, DefaultPin, DefaultPin != nullptr ? Cast<UNiagaraNode>(DefaultPin->GetOwningNode()) : nullptr, OutputChunkId, ScriptVariable, !bWriteToParamMapEntries);
		}

		DeferredVariablesMissingDefault.Empty();

		// Now go through and initialize any "Particles.Initial." variables
		for (FNiagaraVariable& Var : InitialNamespaceVariablesMissingDefault)
		{
			if (FNiagaraParameterMapHistory::IsInitialValue(Var))
			{
				FNiagaraVariable SourceForInitialValue = FNiagaraParameterMapHistory::GetSourceForInitialValue(Var);
				FString ParameterMapInstanceName = GetParameterMapInstanceName(0);
				FString Value = FString::Printf(TEXT("%s.%s = %s.%s;\n"), *ParameterMapInstanceName, *GetSanitizedSymbolName(Var.GetName().ToString()),
					*ParameterMapInstanceName, *GetSanitizedSymbolName(SourceForInitialValue.GetName().ToString()));
				AddBodyChunk(Value);
				continue;
			}
		}

		InitialNamespaceVariablesMissingDefault.Empty();
	}

	AddBodyComment(TEXT("// End HandleMissingDefaultValues\n\n"));
}

FString FHlslNiagaraTranslator::BuildParameterMapHlslDefinitions(TArray<FNiagaraVariable>& PrimaryDataSetOutputEntries)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslTranslator_BuildParameterMapHlslDefinitions);
	FString HlslOutputString;

	// Determine the unique parameter map structs...
	TArray<const UEdGraphPin*> UniqueParamMapStartingPins;
	for (int32 ParamMapIdx = 0; ParamMapIdx < ParamMapHistories.Num(); ParamMapIdx++)
	{
		const UEdGraphPin* OriginalPin = ParamMapHistories[ParamMapIdx].GetOriginalPin();
		UniqueParamMapStartingPins.AddUnique(OriginalPin);
	}


	TArray<FNiagaraVariable> UniqueVariables;

	// Add in currently defined system vars.
	TArray<FNiagaraVariable> ValueArray;
	ParamMapDefinedSystemToNamespaceVars.GenerateValueArray(ValueArray);
	for (FNiagaraVariable& Var : ValueArray)
	{
		if (Var.GetType().GetClass() != nullptr)
		{
			continue;
		}
		UniqueVariables.AddUnique(Var);
	}

	// Add in currently defined emitter vars.
	ParamMapDefinedEmitterParameterToNamespaceVars.GenerateValueArray(ValueArray);
	for (FNiagaraVariable& Var : ValueArray)
	{
		if (Var.GetType().GetClass() != nullptr)
		{
			continue;
		}

		UniqueVariables.AddUnique(Var);
	}

	// Add in currently defined attribute vars.
	ParamMapDefinedAttributesToNamespaceVars.GenerateValueArray(ValueArray);
	for (FNiagaraVariable& Var : ValueArray)
	{
		if (Var.GetType().GetClass() != nullptr)
		{
			continue;
		}

		UniqueVariables.AddUnique(Var);
	}

	// Add in any bulk usage vars.
	for (FNiagaraVariable& Var : ExternalVariablesForBulkUsage)
	{
		if (Var.GetType().GetClass() != nullptr)
		{
			continue;
		}

		UniqueVariables.AddUnique(Var);
	}

	bool bIsSpawnScript = IsSpawnScript();

	// For now we only care about attributes from the other output parameter map histories.
	for (int32 ParamMapIdx = 0; ParamMapIdx < OtherOutputParamMapHistories.Num(); ParamMapIdx++)
	{
		for (int32 VarIdx = 0; VarIdx < OtherOutputParamMapHistories[ParamMapIdx].Variables.Num(); VarIdx++)
		{
			FNiagaraVariable& Var = OtherOutputParamMapHistories[ParamMapIdx].Variables[VarIdx];
			if (OtherOutputParamMapHistories[ParamMapIdx].IsPrimaryDataSetOutput(Var, CompileOptions.TargetUsage))
			{
				int32 PreviousMax = UniqueVariables.Num();
				if (UniqueVariables.AddUnique(Var) == PreviousMax) // i.e. we didn't find it previously, so we added to the end.
				{
					if (bIsSpawnScript)
					{
						if (!AddStructToDefinitionSet(Var.GetType()))
						{
							Error(FText::Format(LOCTEXT("ParameterMapTypeError", "Cannot handle type {0}! Variable: {1}"), Var.GetType().GetNameText(), FText::FromName(Var.GetName())), nullptr, nullptr);
						}
					}
				}
			}
		}
	}


	// Define all the top-level structs and look for sub-structs as yet undefined..
	for (int32 UniqueParamMapIdx = 0; UniqueParamMapIdx < UniqueParamMapStartingPins.Num(); UniqueParamMapIdx++)
	{
		for (int32 ParamMapIdx = 0; ParamMapIdx < ParamMapHistories.Num(); ParamMapIdx++)
		{
			// We need to unify the variables across all the parameter maps that we've found during compilation. We 
			// define the parameter maps as the "same struct type" if they originate from the same input pin.
			const UEdGraphPin* OriginalPin = ParamMapHistories[ParamMapIdx].GetOriginalPin();
			if (OriginalPin != UniqueParamMapStartingPins[UniqueParamMapIdx])
			{
				continue;
			}

			for (int32 VarIdx = 0; VarIdx < ParamMapHistories[ParamMapIdx].Variables.Num(); VarIdx++)
			{
				const FNiagaraVariable& SrcVariable = ParamMapHistories[ParamMapIdx].Variables[VarIdx];

				if (SrcVariable.GetType().GetClass() != nullptr)
				{
					continue;
				}

				FNiagaraVariable Variable = SrcVariable;
				UniqueVariables.AddUnique(Variable);
			}
		}
	}

	static const auto UseShaderStagesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.UseShaderStages"));
	if (UseShaderStagesCVar->GetInt() == 1)
	{
		// Add the attribute indices to the list of unique variables
		TArray<FString> RegisterNames;
		for (int32 UniqueVarIdx = 0; UniqueVarIdx < UniqueVariables.Num(); UniqueVarIdx++)
		{
			const FNiagaraVariable& NiagaraVariable = UniqueVariables[UniqueVarIdx];
			if (FNiagaraParameterMapHistory::IsAttribute(NiagaraVariable))
			{
				const FString VariableName = GetSanitizedSymbolName(NiagaraVariable.GetName().ToString());
				RegisterNames.Add(VariableName.Replace(PARAM_MAP_ATTRIBUTE_STR, PARAM_MAP_INDICES_STR));
			}
		}
		for (const FString& RegisterName : RegisterNames)
		{
			FNiagaraVariable NiagaraVariable = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), *RegisterName);
			UniqueVariables.AddUnique(NiagaraVariable);
		}
	}


	TMap<FString, TArray<TPair<FString, FString>> > ParamStructNameToMembers;
	TArray<FString> ParamStructNames;

	for (int32 UniqueVarIdx = 0; UniqueVarIdx < UniqueVariables.Num(); UniqueVarIdx++)
	{
		int UniqueParamMapIdx = 0;
		FNiagaraVariable Variable = UniqueVariables[UniqueVarIdx];

		if (!AddStructToDefinitionSet(Variable.GetType()))
		{
			Error(FText::Format(LOCTEXT("ParameterMapTypeError", "Cannot handle type {0}! Variable: {1}"), Variable.GetType().GetNameText(), FText::FromName(Variable.GetName())), nullptr, nullptr);
		}

		// In order 
		for (int32 ParamMapIdx = 0; ParamMapIdx < OtherOutputParamMapHistories.Num(); ParamMapIdx++)
		{
			if (OtherOutputParamMapHistories[ParamMapIdx].IsPrimaryDataSetOutput(Variable, CompileOptions.TargetUsage))
			{
				PrimaryDataSetOutputEntries.AddUnique(Variable);
				break;
			}
		}

		TArray<FString> StructNameArray;
		FString SanitizedVarName = GetSanitizedSymbolName(Variable.GetName().ToString());
		int32 NumFound = SanitizedVarName.ParseIntoArray(StructNameArray, TEXT("."));
		if (NumFound == 1) // Meaning no split above
		{
			Error(FText::Format(LOCTEXT("OnlyOneNamespaceEntry", "Only one namespace entry found for: {0}"), FText::FromString(SanitizedVarName)), nullptr, nullptr);
		}
		else if (NumFound > 1)
		{
			while (StructNameArray.Num())
			{
				FString FinalName = StructNameArray[StructNameArray.Num() - 1];
				StructNameArray.RemoveAt(StructNameArray.Num() - 1);
				FString StructType = FString::Printf(TEXT("FParamMap%d_%s"), UniqueParamMapIdx, *FString::Join(StructNameArray, TEXT("_")));
				if (StructNameArray.Num() == 0)
				{
					StructType = FString::Printf(TEXT("FParamMap%d"), UniqueParamMapIdx);
				}

				FString TypeName = GetStructHlslTypeName(Variable.GetType());
				FString VarName = GetSanitizedSymbolName(*FinalName);
				if (NumFound > StructNameArray.Num() + 1 && StructNameArray.Num() != 0)
				{
					TypeName = FString::Printf(TEXT("FParamMap%d_%s_%s"), UniqueParamMapIdx, *FString::Join(StructNameArray, TEXT("_")), *GetSanitizedSymbolName(*FinalName));
				}
				else if (StructNameArray.Num() == 0)
				{
					TypeName = FString::Printf(TEXT("FParamMap%d_%s"), UniqueParamMapIdx, *GetSanitizedSymbolName(*FinalName));
				}
				TPair<FString, FString> Pair(TypeName, VarName);
				ParamStructNameToMembers.FindOrAdd(StructType).AddUnique(Pair);
				ParamStructNames.AddUnique(StructType);
			}
		}
	}

	// Build up the sub-structs..
	ParamStructNames.Sort();
	FString StructDefString = "";
	for (int32 i = ParamStructNames.Num() - 1; i >= 0; i--)
	{
		const FString& StructName = ParamStructNames[i];
		StructDefString += FString::Printf(TEXT("struct %s\n{\n"), *StructName);
		TArray<TPair<FString, FString>> StructMembers = ParamStructNameToMembers[StructName];
		auto SortMembers = [](const TPair<FString, FString>& A, const TPair<FString, FString>& B)
		{
			return A.Value < B.Value;
		};
		StructMembers.Sort(SortMembers);
		for (const TPair<FString, FString>& Line : StructMembers)
		{
			StructDefString += TEXT("\t") + Line.Key + TEXT(" ") + Line.Value + TEXT(";\n");;
		}
		StructDefString += TEXT("};\n\n");
	}

	HlslOutputString += StructDefString;

	return HlslOutputString;
}

bool FHlslNiagaraTranslator::ShouldConsiderTargetParameterMap(ENiagaraScriptUsage InUsage) const
{
	ENiagaraScriptUsage TargetUsage = GetTargetUsage();
	if (TargetUsage >= ENiagaraScriptUsage::ParticleSpawnScript && TargetUsage <= ENiagaraScriptUsage::ParticleEventScript)
	{
		return InUsage >= ENiagaraScriptUsage::ParticleSpawnScript && InUsage <= ENiagaraScriptUsage::ParticleEventScript;
	}
	else if (TargetUsage == ENiagaraScriptUsage::SystemSpawnScript)
	{
		if (InUsage == ENiagaraScriptUsage::SystemUpdateScript)
		{
			return true;
		}
		else if (TargetUsage == InUsage)
		{
			return true;
		}
	}
	else if (TargetUsage == InUsage)
	{
		return true;
	}

	return false;
}

void FHlslNiagaraTranslator::HandleNamespacedExternalVariablesToDataSetRead(TArray<FNiagaraVariable>& InDataSetVars, FString InNamespaceStr)
{
	for (const FNiagaraVariable& Var : ExternalVariablesForBulkUsage)
	{
		if (FNiagaraParameterMapHistory::IsInNamespace(Var, InNamespaceStr))
		{
			InDataSetVars.Add(Var);
		}
	}
}

bool FHlslNiagaraTranslator::IsVariableInUniformBuffer(const FNiagaraVariable& Variable) const
{
	static FNiagaraVariable ExcludeVariables[] =
	{
		FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter_SpawnInterval")),
		FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter_InterpSpawnStartDt")),
		FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.SpawnInterval")),
		FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Emitter.InterpSpawnStartDt")),
		FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(),   TEXT("Emitter_SpawnGroup")),
		FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(),   TEXT("Emitter.SpawnGroup")),
	};

	if (CompilationTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		for ( const FNiagaraVariable& ExcludeVar : ExcludeVariables )
		{
			if ( Variable == ExcludeVar )
			{
				return false;
			}
		}
	}
	return true;
}

const FNiagaraTranslateResults &FHlslNiagaraTranslator::Translate(const FNiagaraCompileRequestData* InCompileData, const FNiagaraCompileOptions& InCompileOptions, FHlslNiagaraTranslatorOptions InTranslateOptions)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslTranslator_Translate);
	check(InCompileData);

	CompileOptions = InCompileOptions;
	CompileData = InCompileData;
	TranslationOptions = InTranslateOptions;
	CompilationTarget = TranslationOptions.SimTarget;
	TranslateResults.bHLSLGenSucceeded = false;
	TranslateResults.OutputHLSL = "";

	UNiagaraGraph* SourceGraph = CompileData->NodeGraphDeepCopy;

	if (!SourceGraph)
	{
		Error(LOCTEXT("GetGraphFail", "Cannot find graph node!"), nullptr, nullptr);
		return TranslateResults;
	}

	if (SourceGraph->IsEmpty())
	{
		if (UNiagaraScript::IsSystemScript(CompileOptions.TargetUsage))
		{
			Error(LOCTEXT("GetNoNodeSystemFail", "Graph contains no nodes! Please add an emitter."), nullptr, nullptr);
		}
		else
		{
			Error(LOCTEXT("GetNoNodeFail", "Graph contains no nodes! Please add an output node."), nullptr, nullptr);
		}
		return TranslateResults;
	}

	bool bNeedsPersistentIDs = CompileOptions.AdditionalDefines.Contains(TEXT("RequiresPersistentIDs"));

	TranslationStages.Empty();
	ActiveStageIdx = 0;

	bool bHasInterpolatedSpawn = CompileOptions.AdditionalDefines.Contains(TEXT("InterpolatedSpawn"));
	ParamMapHistories.Empty();
	ParamMapSetVariablesToChunks.Empty();

	switch (CompileOptions.TargetUsage)
	{
	case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated:
		TranslationStages.Add(FHlslNiagaraTranslationStage(CompileOptions.TargetUsage, CompileOptions.TargetUsageId));
		TranslationStages.Add(FHlslNiagaraTranslationStage(ENiagaraScriptUsage::ParticleUpdateScript, FGuid()));
		TranslationStages[0].PassNamespace = TEXT("MapSpawn");
		TranslationStages[1].PassNamespace = TEXT("MapUpdate");
		TranslationStages[0].ChunkModeIndex = ENiagaraCodeChunkMode::SpawnBody;
		TranslationStages[1].ChunkModeIndex = ENiagaraCodeChunkMode::UpdateBody;
		TranslationStages[0].OutputNode = SourceGraph->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleSpawnScript, TranslationStages[0].UsageId);
		TranslationStages[1].OutputNode = SourceGraph->FindEquivalentOutputNode(TranslationStages[1].ScriptUsage, TranslationStages[1].UsageId);
		TranslationStages[1].bInterpolatePreviousParams = true;
		ParamMapHistories.AddDefaulted(2);
		ParamMapSetVariablesToChunks.AddDefaulted(2);
		break;
	case ENiagaraScriptUsage::ParticleGPUComputeScript:
		TranslationStages.Add(FHlslNiagaraTranslationStage((bHasInterpolatedSpawn ? ENiagaraScriptUsage::ParticleSpawnScriptInterpolated : ENiagaraScriptUsage::ParticleSpawnScript), FGuid()));
		TranslationStages.Add(FHlslNiagaraTranslationStage(ENiagaraScriptUsage::ParticleUpdateScript, FGuid()));
		TranslationStages[0].PassNamespace = TEXT("MapSpawn");
		TranslationStages[1].PassNamespace = TEXT("MapUpdate");
		TranslationStages[0].ChunkModeIndex = ENiagaraCodeChunkMode::SpawnBody;
		TranslationStages[1].ChunkModeIndex = ENiagaraCodeChunkMode::UpdateBody;
		TranslationStages[0].OutputNode = SourceGraph->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleSpawnScript, TranslationStages[0].UsageId);
		TranslationStages[1].OutputNode = SourceGraph->FindEquivalentOutputNode(TranslationStages[1].ScriptUsage, TranslationStages[1].UsageId);
		TranslationStages[1].bInterpolatePreviousParams = bHasInterpolatedSpawn;
		ParamMapHistories.AddDefaulted(2);
		ParamMapSetVariablesToChunks.AddDefaulted(2);
		break;
	default:
		TranslationStages.Add(FHlslNiagaraTranslationStage(CompileOptions.TargetUsage, CompileOptions.TargetUsageId));
		TranslationStages[0].PassNamespace = TEXT("Map");
		TranslationStages[0].OutputNode = SourceGraph->FindEquivalentOutputNode(TranslationStages[0].ScriptUsage, TranslationStages[0].UsageId);
		TranslationStages[0].ChunkModeIndex = ENiagaraCodeChunkMode::Body;
		ParamMapHistories.AddDefaulted(1);
		ParamMapSetVariablesToChunks.AddDefaulted(1);
		break;
	}

	for (int32 i = 0; i < TranslationStages.Num(); i++)
	{
		if (TranslationStages[i].OutputNode == nullptr)
		{
			Error(FText::Format(LOCTEXT("GetOutputNodeFail", "Cannot find output node of type {0}!"), FText::AsNumber((int32)TranslationStages[i].ScriptUsage)), nullptr, nullptr);
			return TranslateResults;
		}

		ValidateTypePins(TranslationStages[i].OutputNode);
		{
			bool bHasAnyConnections = false;
			for (int32 PinIdx = 0; PinIdx < TranslationStages[i].OutputNode->Pins.Num(); PinIdx++)
			{
				if (TranslationStages[i].OutputNode->Pins[PinIdx]->Direction == EEdGraphPinDirection::EGPD_Input && TranslationStages[i].OutputNode->Pins[PinIdx]->LinkedTo.Num() != 0)
				{
					bHasAnyConnections = true;
				}
			}
			if (!bHasAnyConnections)
			{
				Error(FText::Format(LOCTEXT("GetOutputNodeConnectivityFail", "Cannot find any connections to output node of type {0}!"), FText::AsNumber((int32)TranslationStages[i].ScriptUsage)), nullptr, nullptr);
				return TranslateResults;
			}
		}
	}


	// Get all the parameter map histories traced to this graph from output nodes. We'll revisit this shortly in order to build out just the ones we care about for this translation.
	OtherOutputParamMapHistories = CompileData->GetPrecomputedHistories();

	if (ParamMapHistories.Num() == 1 && OtherOutputParamMapHistories.Num() == 1 && (CompileOptions.TargetUsage == ENiagaraScriptUsage::Function || CompileOptions.TargetUsage == ENiagaraScriptUsage::DynamicInput))
	{
		ParamMapHistories[0] = (OtherOutputParamMapHistories[0]);

		TArray<int32> Entries;
		Entries.AddZeroed(OtherOutputParamMapHistories[0].Variables.Num());
		for (int32 i = 0; i < Entries.Num(); i++)
		{
			Entries[i] = INDEX_NONE;
		}
		ParamMapSetVariablesToChunks[0] = (Entries);
	}
	else
	{
		for (FNiagaraParameterMapHistory& FoundHistory : OtherOutputParamMapHistories)
		{
			const UNiagaraNodeOutput* HistoryOutputNode = FoundHistory.GetFinalOutputNode();
			if (HistoryOutputNode != nullptr && !ShouldConsiderTargetParameterMap(HistoryOutputNode->GetUsage()))
			{
				continue;
			}

			// Now see if we want to use any of these specifically..
			for (int32 ParamMapIdx = 0; ParamMapIdx < TranslationStages.Num(); ParamMapIdx++)
			{
				UNiagaraNodeOutput* TargetOutputNode = TranslationStages[ParamMapIdx].OutputNode;
				if (FoundHistory.GetFinalOutputNode() == TargetOutputNode)
				{
					if (bNeedsPersistentIDs)
					{
						//TODO: Setup alias for current level to decouple from "Particles". Would we ever want emitter or system persistent IDs?
						FNiagaraVariable Var = FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particles.ID"));
						FoundHistory.AddVariable(Var, Var, nullptr);
					}
					{
						// NOTE(mv): This will explicitly expose Particles.UniqueID to the HLSL code regardless of whether it is exposed in a script or not. 
						//           This is necessary as the script needs to know about it even when no scripts reference it. 
						FNiagaraVariable Var = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Particles.UniqueID"));
						FoundHistory.AddVariable(Var, Var, nullptr);
					}

					if (RequiresInterpolation())
					{
						FNiagaraVariable Var = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Interpolation.InterpSpawn_Index"));
						FoundHistory.AddVariable(Var, Var, nullptr);

						Var = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interpolation.InterpSpawn_SpawnTime"));
						FoundHistory.AddVariable(Var, Var, nullptr);

						Var = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interpolation.InterpSpawn_UpdateTime"));
						FoundHistory.AddVariable(Var, Var, nullptr);

						Var = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interpolation.InterpSpawn_InvSpawnTime"));
						FoundHistory.AddVariable(Var, Var, nullptr);

						Var = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interpolation.InterpSpawn_InvUpdateTime"));
						FoundHistory.AddVariable(Var, Var, nullptr);

						Var = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interpolation.SpawnInterp"));
						FoundHistory.AddVariable(Var, Var, nullptr);

						Var = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interpolation.Emitter_SpawnInterval"));
						FoundHistory.AddVariable(Var, Var, nullptr);

						Var = FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Interpolation.Emitter_InterpSpawnStartDt"));
						FoundHistory.AddVariable(Var, Var, nullptr);

						Var = FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Interpolation.Emitter_SpawnGroup"));
						FoundHistory.AddVariable(Var, Var, nullptr);
					}

					ParamMapHistories[ParamMapIdx] = (FoundHistory);

					TArray<int32> Entries;
					Entries.AddZeroed(FoundHistory.Variables.Num());
					for (int32 i = 0; i < Entries.Num(); i++)
					{
						Entries[i] = INDEX_NONE;
					}
					ParamMapSetVariablesToChunks[ParamMapIdx] = (Entries);
				}
			}
		}
	}

	CompilationOutput.ScriptData.ParameterCollectionPaths.Empty();
	for (FNiagaraParameterMapHistory& History : ParamMapHistories)
	{
		for (UNiagaraParameterCollection* Collection : History.ParameterCollections)
		{
			CompilationOutput.ScriptData.ParameterCollectionPaths.AddUnique(FSoftObjectPath(Collection).ToString());
		}
	}
	ENiagaraScriptUsage Usage = CompileOptions.TargetUsage;
	if (Usage != ENiagaraScriptUsage::SystemSpawnScript && Usage != ENiagaraScriptUsage::SystemUpdateScript)
	{
		ValidateParticleIDUsage();
	}

	//Create main scope pin cache.
	PinToCodeChunks.AddDefaulted(1);

	ActiveHistoryForFunctionCalls.BeginTranslation(GetUniqueEmitterName());

	CompilationOutput.ScriptData.StatScopes.Empty();
	EnterStatsScope(FNiagaraStatScope(*CompileOptions.GetFullName(), *CompileOptions.GetFullName()));

	FHlslNiagaraTranslator* ThisTranslator = this;
	TArray<int32> OutputChunks;

	bool bInterpolateParams = false;

	if (TranslationStages.Num() > 1)
	{
		for (int32 i = 0; i < TranslationStages.Num(); i++)
		{
			ActiveStageIdx = i;
			CurrentBodyChunkMode = TranslationStages[i].ChunkModeIndex;
			if (UNiagaraScript::IsParticleSpawnScript(TranslationStages[i].ScriptUsage))
			{
				AddBodyComment(bHasInterpolatedSpawn ? TEXT("//Begin Interpolated Spawn Script!") : TEXT("//Begin Spawn Script!"));
				CurrentParamMapIndices.Empty();
				CurrentParamMapIndices.Add(0);
				TranslationStages[i].OutputNode->Compile(ThisTranslator, OutputChunks);
				InstanceWrite = FDataSetAccessInfo(); // Reset after building the output..
				AddBodyComment(TEXT("//End Spawn Script!\n\n"));
				BuildMissingDefaults();
			}

			if (TranslationStages[i].bInterpolatePreviousParams)
			{
				bInterpolateParams = true;
			}

			if (UNiagaraScript::IsParticleUpdateScript(TranslationStages[i].ScriptUsage))
			{
				AddBodyComment(TEXT("//Begin Update Script!"));
				//Now we compile the update script (with partial dt) and read from the temp values written above.
				CurrentParamMapIndices.Empty();
				CurrentParamMapIndices.Add(1);
				TranslationStages[i].OutputNode->Compile(ThisTranslator, OutputChunks);
				AddBodyComment(TEXT("//End Update Script!\n\n"));
			}
		}
		CurrentBodyChunkMode = ENiagaraCodeChunkMode::Body;
	}
	else if (TranslationStages.Num() == 1)
	{
		CurrentBodyChunkMode = TranslationStages[0].ChunkModeIndex;
		ActiveStageIdx = 0;
		check(CompileOptions.TargetUsage == TranslationStages[0].ScriptUsage);
		CurrentParamMapIndices.Empty();
		CurrentParamMapIndices.Add(0);

		TranslationStages[0].OutputNode->Compile(ThisTranslator, OutputChunks);

		if (IsSpawnScript())
		{
			BuildMissingDefaults();
		}
	}
	else
	{
		Error(LOCTEXT("NoTranslationStages", "Cannot find any translation stages!"), nullptr, nullptr);
		return TranslateResults;
	}

	CurrentParamMapIndices.Empty();
	ExitStatsScope();

	ActiveHistoryForFunctionCalls.EndTranslation(GetUniqueEmitterName());

	TranslateResults.bHLSLGenSucceeded = TranslateResults.NumErrors == 0;

	//If we're compiling a function then we have all we need already, we don't want to actually generate shader/vm code.
	if (FunctionCtx())
		return TranslateResults;

	//Now evaluate all the code chunks to generate the shader code.
	//FString HlslOutput;
	if (TranslateResults.bHLSLGenSucceeded)
	{
		//TODO: Declare all used structures up here too.
		CompilationOutput.ScriptData.ReadDataSets.Empty();
		CompilationOutput.ScriptData.WriteDataSets.Empty();

		//Generate function definitions
		FString FunctionDefinitionString = GetFunctionDefinitions();
		FunctionDefinitionString += TEXT("\n");
		{
			if (TranslationStages.Num() > 1 && RequiresInterpolation())
			{
				int32 OutputIdx = 0;
				//ensure the interpolated spawn constants are part of the parameter set.
				ParameterMapRegisterExternalConstantNamespaceVariable(SYS_PARAM_ENGINE_TIME, nullptr, 0, OutputIdx, nullptr);
				ParameterMapRegisterExternalConstantNamespaceVariable(SYS_PARAM_ENGINE_DELTA_TIME, nullptr, 0, OutputIdx, nullptr);
				ParameterMapRegisterExternalConstantNamespaceVariable(SYS_PARAM_ENGINE_INV_DELTA_TIME, nullptr, 0, OutputIdx, nullptr);
				ParameterMapRegisterExternalConstantNamespaceVariable(SYS_PARAM_ENGINE_EXEC_COUNT, nullptr, 0, OutputIdx, nullptr);
				ParameterMapRegisterExternalConstantNamespaceVariable(SYS_PARAM_EMITTER_SPAWNRATE, nullptr, 0, OutputIdx, nullptr);
				if (CompilationTarget != ENiagaraSimTarget::GPUComputeSim)
				{
					ParameterMapRegisterExternalConstantNamespaceVariable(SYS_PARAM_EMITTER_SPAWN_INTERVAL, nullptr, 0, OutputIdx, nullptr);
					ParameterMapRegisterExternalConstantNamespaceVariable(SYS_PARAM_EMITTER_INTERP_SPAWN_START_DT, nullptr, 0, OutputIdx, nullptr);
					ParameterMapRegisterExternalConstantNamespaceVariable(SYS_PARAM_EMITTER_SPAWN_GROUP, nullptr, 0, OutputIdx, nullptr);
				}
			}

			if (TranslationStages.Num() > 0)
			{
				int32 OutputIdx = 0;
				// NOTE(mv): This will explicitly expose Engine.Emitter.TotalSpawnedParticles to the HLSL code regardless of whether it is exposed in a script or not. 
				ParameterMapRegisterExternalConstantNamespaceVariable(SYS_PARAM_ENGINE_EMITTER_TOTAL_SPAWNED_PARTICLES, nullptr, 0, OutputIdx, nullptr);
				ParameterMapRegisterExternalConstantNamespaceVariable(SYS_PARAM_EMITTER_RANDOM_SEED, nullptr, 0, OutputIdx, nullptr);
				//ParameterMapRegisterExternalConstantNamespaceVariable(SYS_PARAM_EMITTER_DETERMINISM, nullptr, 0, OutputIdx, nullptr);
			}
		}

		// Generate the Parameter Map HLSL definitions. We don't add to the final HLSL output here. We just build up the strings and tables
		// that are needed later.
		TArray<FNiagaraVariable> PrimaryDataSetOutputEntries;
		FString ParameterMapDefinitionStr = BuildParameterMapHlslDefinitions(PrimaryDataSetOutputEntries);

		for (FNiagaraTypeDefinition Type : StructsToDefine)
		{
			FText ErrorMessage;
			HlslOutput += BuildHLSLStructDecl(Type, ErrorMessage);
			if (ErrorMessage.IsEmpty() == false)
			{
				Error(ErrorMessage, nullptr, nullptr);
			}
		}
		//Declare parameters.
		//TODO: Separate Cbuffer for Global, System and Emitter parameters.
		{
			HlslOutput += TEXT("cbuffer FEmitterParameters\n{\n");

			for (int32 i = 0; i < ChunksByMode[(int32)ENiagaraCodeChunkMode::Uniform].Num(); ++i)
			{
				FNiagaraVariable BufferVariable(CodeChunks[ChunksByMode[(int32)ENiagaraCodeChunkMode::Uniform][i]].Type, FName(*CodeChunks[ChunksByMode[(int32)ENiagaraCodeChunkMode::Uniform][i]].SymbolName));
				if ( IsVariableInUniformBuffer(BufferVariable) )
				{
					FString Chunk = GetCode(ChunksByMode[(int32)ENiagaraCodeChunkMode::Uniform][i]);
					HlslOutput += TEXT("\t") + Chunk;
				}
			}

			if (bInterpolateParams)
			{
				//Define the params from the previous frame after the main parameters.
				for (int32 i = 0; i < ChunksByMode[(int32)ENiagaraCodeChunkMode::Uniform].Num(); ++i)
				{
					//Copy the chunk so we can fiddle it's symbol name.
					FNiagaraCodeChunk Chunk = CodeChunks[ChunksByMode[(int32)ENiagaraCodeChunkMode::Uniform][i]];
					Chunk.SymbolName = INTERPOLATED_PARAMETER_PREFIX + Chunk.SymbolName;
					HlslOutput += TEXT("\t") + GetCode(Chunk);
				}
			}

			HlslOutput += TEXT("}\n\n");
		}

		WriteDataSetStructDeclarations(DataSetReadInfo[0], true, HlslOutput);
		WriteDataSetStructDeclarations(DataSetWriteInfo[0], false, HlslOutput);

		//Map of all variables accessed by all datasets.
		TArray<TArray<FNiagaraVariable>> DataSetVariables;

		TMap<FNiagaraDataSetID, int32> DataSetReads;
		TMap<FNiagaraDataSetID, int32> DataSetWrites;

		const FNiagaraDataSetID InstanceDataSetID = GetInstanceDataSetID();

		const int32 InstanceReadVarsIndex = DataSetVariables.AddDefaulted();
		const int32 InstanceWriteVarsIndex = DataSetVariables.AddDefaulted();

		DataSetReads.Add(InstanceDataSetID, InstanceReadVarsIndex);
		DataSetWrites.Add(InstanceDataSetID, InstanceWriteVarsIndex);

		if (IsBulkSystemScript())
		{
			// We have two sets of data that can change independently.. The engine data set are variables
			// that are essentially set once per system. The constants are rapid iteration variables
			// that exist per emitter and change infrequently. Since they are so different, putting
			// them in two distinct read data sets seems warranted.
			const FNiagaraDataSetID SystemEngineDataSetID = GetSystemEngineDataSetID();

			const int32 SystemEngineReadVarsIndex = DataSetVariables.Num();
			DataSetReads.Add(SystemEngineDataSetID, SystemEngineReadVarsIndex);
			TArray<FNiagaraVariable>& SystemEngineReadVars = DataSetVariables.AddDefaulted_GetRef();

			HandleNamespacedExternalVariablesToDataSetRead(SystemEngineReadVars, TEXT("Engine"));
			HandleNamespacedExternalVariablesToDataSetRead(SystemEngineReadVars, TEXT("User"));

			// We sort the variables so that they end up in the same ordering between Spawn & Update...
			Algo::SortBy(SystemEngineReadVars, &FNiagaraVariable::GetName, FNameLexicalLess());

			{
				FNiagaraParameters ExternalParams;
				ExternalParams.Parameters = DataSetVariables[SystemEngineReadVarsIndex];
				CompilationOutput.ScriptData.DataSetToParameters.Add(GetSystemEngineDataSetID().Name, ExternalParams);
			}
		}

		// Now we pull in the HLSL generated above by building the parameter map definitions..
		HlslOutput += ParameterMapDefinitionStr;

		// Gather up all the unique Attribute variables that we generated.
		TArray<FNiagaraVariable> BasicAttributes;
		for (FNiagaraVariable& Var : InstanceRead.Variables)
		{
			if (Var.GetType().GetClass() != nullptr)
			{
				continue;
			}
			BasicAttributes.AddUnique(Var);
		}
		for (FNiagaraVariable& Var : InstanceWrite.Variables)
		{
			if (Var.GetType().GetClass() != nullptr)
			{
				continue;
			}
			else if (Var.GetType() != FNiagaraTypeDefinition::GetParameterMapDef())
			{
				BasicAttributes.AddUnique(Var);
			}
			else
			{
				for (FNiagaraVariable& ParamMapVar : PrimaryDataSetOutputEntries)
				{
					BasicAttributes.AddUnique(ParamMapVar);
				}
			}
		}

		// We sort the variables so that they end up in the same ordering between Spawn & Update...
		Algo::SortBy(BasicAttributes, &FNiagaraVariable::GetName, FNameLexicalLess());

		DataSetVariables[InstanceReadVarsIndex] = BasicAttributes;
		DataSetVariables[InstanceWriteVarsIndex] = BasicAttributes;

		//Define the simulation context. Which is a helper struct containing all the input, result and intermediate data needed for a single simulation.
		//Allows us to reuse the same simulate function but provide different wrappers for final IO between GPU and CPU sims.
		{
			HlslOutput += TEXT("struct FSimulationContext") TEXT("\n{\n");

			// We need to reserve a place in the simulation context for the base Parameter Map.
			if (PrimaryDataSetOutputEntries.Num() != 0 || ParamMapDefinedSystemToNamespaceVars.Num() != 0 || ParamMapDefinedEmitterParameterToNamespaceVars.Num() != 0 || (ParamMapSetVariablesToChunks.Num() != 0 && ParamMapSetVariablesToChunks[0].Num() > 0))
			{
				for (int32 i = 0; i < TranslationStages.Num(); i++)
				{
					HlslOutput += TEXT("\tFParamMap0 ") + TranslationStages[i].PassNamespace + TEXT(";\n");
				}
			}

			WriteDataSetContextVars(DataSetReadInfo[0], true, HlslOutput);
			WriteDataSetContextVars(DataSetWriteInfo[0], false, HlslOutput);


			HlslOutput += TEXT("};\n\n");
		}
		
		HlslOutput += TEXT("static float HackSpawnInterp = 1.0;\n");

		HlslOutput += FunctionDefinitionString;

		TArray<int32> WriteConditionVars;

		// copy the accessed data sets over to the script, so we can grab them during sim
		for (TPair <FNiagaraDataSetID, TMap<int32, FDataSetAccessInfo>> InfoPair : DataSetReadInfo[0])
		{
			CompilationOutput.ScriptData.ReadDataSets.Add(InfoPair.Key);
		}

		for (TPair <FNiagaraDataSetID, TMap<int32, FDataSetAccessInfo>> InfoPair : DataSetWriteInfo[0])
		{
			FNiagaraDataSetProperties SetProps;
			SetProps.ID = InfoPair.Key;
			for (TPair <int32, FDataSetAccessInfo> IndexPair : InfoPair.Value)
			{
				SetProps.Variables = IndexPair.Value.Variables;
			}

			CompilationOutput.ScriptData.WriteDataSets.Add(SetProps);

			int32* ConditionalWriteChunkIdxPtr = DataSetWriteConditionalInfo[0].Find(InfoPair.Key);
			if (ConditionalWriteChunkIdxPtr == nullptr)
			{
				WriteConditionVars.Add(INDEX_NONE);
			}
			else
			{
				WriteConditionVars.Add(*ConditionalWriteChunkIdxPtr);
			}
		}

		DefineInterpolatedParametersFunction(HlslOutput);

		// define functions for reading and writing all secondary data sets
		DefineDataSetReadFunction(HlslOutput, CompilationOutput.ScriptData.ReadDataSets);
		DefineDataSetWriteFunction(HlslOutput, CompilationOutput.ScriptData.WriteDataSets, WriteConditionVars);


		//Define the shared per instance simulation function
		// for interpolated scripts AND GPU sim, define spawn and sim in separate functions
		if (TranslationStages.Num() > 1)
		{
			for (int32 StageIdx = 0; StageIdx < TranslationStages.Num(); StageIdx++)
			{
				HlslOutput += TEXT("void Simulate") + TranslationStages[StageIdx].PassNamespace + TEXT("(inout FSimulationContext Context)\n{\n");
				int32 ChunkMode = (int32)TranslationStages[StageIdx].ChunkModeIndex;
				for (int32 i = 0; i < ChunksByMode[ChunkMode].Num(); ++i)
				{
					HlslOutput += TEXT("\t") + GetCode(ChunksByMode[ChunkMode][i]);
				}
				HlslOutput += TEXT("}\n");
			}
		}
		else
		{
			HlslOutput += TEXT("void Simulate(inout FSimulationContext Context)\n{\n");
			for (int32 i = 0; i < ChunksByMode[(int32)ENiagaraCodeChunkMode::Body].Num(); ++i)
			{
				HlslOutput += GetCode(ChunksByMode[(int32)ENiagaraCodeChunkMode::Body][i]);
			}
			HlslOutput += TEXT("}\n");
		}

		if (TranslationOptions.SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			FString DataInterfaceHLSL;
			DefineDataInterfaceHLSL(DataInterfaceHLSL);
			HlslOutput += DataInterfaceHLSL;

			DefineExternalFunctionsHLSL(HlslOutput);
		}

		//And finally, define the actual main function that handles the reading and writing of data and calls the shared per instance simulate function.
		//TODO: Different wrappers for GPU and CPU sims. 
		if (TranslationOptions.SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			DefineMainGPUFunctions(DataSetVariables, DataSetReads, DataSetWrites);
		}
		else
		{
			DefineMain(HlslOutput, DataSetVariables, DataSetReads, DataSetWrites);
		}

		//Get full list of instance data accessed by the script as the VM binding assumes same for input and output.
		for (FNiagaraVariable& Var : DataSetVariables[InstanceReadVarsIndex])
		{
			if (FNiagaraParameterMapHistory::IsAttribute(Var))
			{
				FNiagaraVariable BasicAttribVar = FNiagaraParameterMapHistory::ResolveAsBasicAttribute(Var, false);
				CompilationOutput.ScriptData.Attributes.AddUnique(BasicAttribVar);
			}
			else
			{
				CompilationOutput.ScriptData.Attributes.AddUnique(Var);
			}
		}

		// We may have created some transient data interfaces. This cleans up the ones that we created.
		CompilationOutput.ScriptData.DIParamInfo = DIParamInfo;
		if (InstanceRead.Variables.Num() == 1 && InstanceRead.Variables[0].GetName() == TEXT("Particles.UniqueID")) {
			// NOTE(mv): Explicitly allow reading from Particles.UniqueID, as it is an engine managed variable and 
			//           is written to before Simulate() in the SpawnScript...
			// TODO(mv): Also allow Particles.ID for the same reasons?
			CompilationOutput.ScriptData.bReadsAttributeData = false;
		} else {
			CompilationOutput.ScriptData.bReadsAttributeData = InstanceRead.Variables.Num() != 0;
		}
		TranslateResults.OutputHLSL = HlslOutput;

	}

	return TranslateResults;
}


void FHlslNiagaraTranslator::GatherVariableForDataSetAccess(const FNiagaraVariable& Var, FString Format, int32& IntCounter, int32 &FloatCounter, int32 DataSetIndex, FString InstanceIdxSymbol, FString &HlslOutputString)
{
	TArray<FString> Components;
	UScriptStruct* Struct = Var.GetType().GetScriptStruct();
	if (!Struct)
	{
		Error(FText::Format(LOCTEXT("BadStructDef", "Variable {0} missing struct definition."), FText::FromName(Var.GetName())), nullptr, nullptr);
		return;
	}

	TArray<ENiagaraBaseTypes> Types;
	GatherComponentsForDataSetAccess(Struct, TEXT(""), false, Components, Types);

	//Add floats and then ints to hlsl
	TArray<FStringFormatArg> FormatArgs;
	FormatArgs.Empty(5);
	FormatArgs.Add(TEXT(""));//We'll set the var name below.
	FormatArgs.Add(TEXT(""));//We'll set the type name below.
	// none for the output op (data set comes from acquireindex op)
	if (DataSetIndex != INDEX_NONE)
	{
		FormatArgs.Add(DataSetIndex);
	}
	int32 RegIdx = FormatArgs.Add(0);
	if (!InstanceIdxSymbol.IsEmpty())
	{
		FormatArgs.Add(InstanceIdxSymbol);
	}
	int32 DefaultIdx = FormatArgs.Add(0);

	check(Components.Num() == Types.Num());
	for (int32 CompIdx = 0; CompIdx < Components.Num(); ++CompIdx)
	{
		if (Types[CompIdx] == NBT_Float)
		{
			FormatArgs[1] = TEXT("Float");
			FormatArgs[DefaultIdx] = TEXT("0.0f");
			FormatArgs[RegIdx] = FloatCounter++;
		}
		else if (Types[CompIdx] == NBT_Int32)
		{
			FormatArgs[1] = TEXT("Int");
			FormatArgs[DefaultIdx] = TEXT("0");
			if (CompilationTarget == ENiagaraSimTarget::GPUComputeSim)
			{
				FormatArgs[RegIdx] = IntCounter++;
			}
			else
			{
				FormatArgs[RegIdx] = FloatCounter++;
			}
		}
		else
		{
			check(Types[CompIdx] == NBT_Bool);
			FormatArgs[1] = TEXT("Bool");
			FormatArgs[DefaultIdx] = TEXT("false");
			FormatArgs[RegIdx] = CompilationTarget == ENiagaraSimTarget::GPUComputeSim ? IntCounter++ : FloatCounter++;
		}
		FormatArgs[0] = Components[CompIdx];
		HlslOutputString += FString::Format(*Format, FormatArgs);
	}
}

void FHlslNiagaraTranslator::GatherComponentsForDataSetAccess(UScriptStruct* Struct, FString VariableSymbol, bool bMatrixRoot, TArray<FString>& Components, TArray<ENiagaraBaseTypes>& Types)
{
	bool bIsVector = IsHlslBuiltinVector(FNiagaraTypeDefinition(Struct));
	bool bIsScalar = FNiagaraTypeDefinition::IsScalarDefinition(Struct);
	bool bIsMatrix = FNiagaraTypeDefinition(Struct) == FNiagaraTypeDefinition::GetMatrix4Def();
	if (bIsMatrix)
	{
		bMatrixRoot = true;
	}

	//Bools are an awkward special case. TODO: make neater.
	if (FNiagaraTypeDefinition(Struct) == FNiagaraTypeDefinition::GetBoolDef())
	{
		Types.Add(ENiagaraBaseTypes::NBT_Bool);
		Components.Add(VariableSymbol);
		return;
	}

	for (TFieldIterator<const FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		const FProperty* Property = *PropertyIt;

		if (const FStructProperty* StructProp = CastField<const FStructProperty>(Property))
		{
			if (bMatrixRoot && FNiagaraTypeDefinition(StructProp->Struct) == FNiagaraTypeDefinition::GetFloatDef())
			{
				GatherComponentsForDataSetAccess(StructProp->Struct, VariableSymbol + ComputeMatrixColumnAccess(Property->GetName()), bMatrixRoot, Components, Types);
			}
			else if (bMatrixRoot &&  FNiagaraTypeDefinition(StructProp->Struct) == FNiagaraTypeDefinition::GetVec4Def())
			{
				GatherComponentsForDataSetAccess(StructProp->Struct, VariableSymbol + ComputeMatrixRowAccess(Property->GetName()), bMatrixRoot, Components, Types);
			}
			else
			{
				GatherComponentsForDataSetAccess(StructProp->Struct, VariableSymbol + TEXT(".") + Property->GetName(), bMatrixRoot, Components, Types);
			}
		}
		else
		{
			FString VarName = VariableSymbol;
			if (bMatrixRoot)
			{
				if (bIsVector && Property->IsA(FFloatProperty::StaticClass())) // Parent is a vector type, we are a float type
				{
					VarName += ComputeMatrixColumnAccess(Property->GetName());
				}
			}
			else if (!bIsScalar)
			{
				VarName += TEXT(".");
				VarName += bIsVector ? Property->GetName().ToLower() : Property->GetName();
			}

			if (Property->IsA(FFloatProperty::StaticClass()))
			{
				Types.Add(ENiagaraBaseTypes::NBT_Float);
				Components.Add(VarName);
			}
			else if (Property->IsA(FIntProperty::StaticClass()))
			{
				Types.Add(ENiagaraBaseTypes::NBT_Int32);
				Components.Add(VarName);
			}
			else if (Property->IsA(FBoolProperty::StaticClass()))
			{
				Types.Add(ENiagaraBaseTypes::NBT_Bool);
				Components.Add(VarName);
			}
		}
	}
}

void FHlslNiagaraTranslator::DefineInterpolatedParametersFunction(FString &HlslOutputString)
{
	for (int32 i = 0; i < TranslationStages.Num(); i++)
	{
		if (!TranslationStages[i].bInterpolatePreviousParams)
		{
			continue;
		}

		FString Emitter_InterpSpawnStartDt = GetSanitizedSymbolName(ActiveHistoryForFunctionCalls.ResolveAliases(SYS_PARAM_EMITTER_INTERP_SPAWN_START_DT).GetName().ToString());
		Emitter_InterpSpawnStartDt = Emitter_InterpSpawnStartDt.Replace(TEXT("."), TEXT("_"));//TODO: This should be rolled into GetSanitisedSymbolName but currently some usages require the '.' intact. Fix those up!.
		FString Emitter_SpawnInterval = GetSanitizedSymbolName(ActiveHistoryForFunctionCalls.ResolveAliases(SYS_PARAM_EMITTER_SPAWN_INTERVAL).GetName().ToString());
		Emitter_SpawnInterval = Emitter_SpawnInterval.Replace(TEXT("."), TEXT("_"));//TODO: This should be rolled into GetSanitisedSymbolName but currently some usages require the '.' intact. Fix those up!.

		HlslOutputString += TEXT("void InterpolateParameters(inout FSimulationContext Context)\n{\n");

		FString PrevMap = TranslationStages[i - 1].PassNamespace;
		FString CurMap = TranslationStages[i].PassNamespace;
		{
			// GPU simulation is slightly different as we run all spawns at once rather than separate invocations so we can not ExecIndex() as it has to be biased into the correct spawn group's index
			if ( CompilationTarget == ENiagaraSimTarget::GPUComputeSim )
			{
				HlslOutputString += TEXT("\tint InterpSpawn_Index = GInterpSpawnIndex;\n");
			}
			else
			{
				HlslOutputString += TEXT("\tint InterpSpawn_Index = ExecIndex();\n");
			}

			HlslOutputString += TEXT("\tfloat InterpSpawn_SpawnTime = ") + Emitter_InterpSpawnStartDt + TEXT(" + (") + Emitter_SpawnInterval + TEXT(" * InterpSpawn_Index);\n");
			HlslOutputString += TEXT("\tfloat InterpSpawn_UpdateTime = Engine_DeltaTime - InterpSpawn_SpawnTime;\n");
			HlslOutputString += TEXT("\tfloat InterpSpawn_InvSpawnTime = 1.0 / InterpSpawn_SpawnTime;\n");
			HlslOutputString += TEXT("\tfloat InterpSpawn_InvUpdateTime = 1.0 / InterpSpawn_UpdateTime;\n");
			HlslOutputString += TEXT("\tfloat SpawnInterp = InterpSpawn_SpawnTime * Engine_InverseDeltaTime ;\n");
			HlslOutputString += TEXT("\tHackSpawnInterp = SpawnInterp;\n");

			HlslOutputString += TEXT("\tContext.") + PrevMap + TEXT(".Interpolation.InterpSpawn_Index = InterpSpawn_Index;\n");
			HlslOutputString += TEXT("\tContext.") + PrevMap + TEXT(".Interpolation.InterpSpawn_SpawnTime = InterpSpawn_SpawnTime;\n");
			HlslOutputString += TEXT("\tContext.") + PrevMap + TEXT(".Interpolation.InterpSpawn_UpdateTime = InterpSpawn_UpdateTime;\n");
			HlslOutputString += TEXT("\tContext.") + PrevMap + TEXT(".Interpolation.InterpSpawn_InvSpawnTime = InterpSpawn_InvSpawnTime;\n");
			HlslOutputString += TEXT("\tContext.") + PrevMap + TEXT(".Interpolation.InterpSpawn_InvUpdateTime = InterpSpawn_InvUpdateTime;\n");
			HlslOutputString += TEXT("\tContext.") + PrevMap + TEXT(".Interpolation.SpawnInterp = SpawnInterp;\n");
			HlslOutputString += TEXT("\tContext.") + PrevMap + TEXT(".Interpolation.Emitter_SpawnInterval = Emitter_SpawnInterval;\n");
			HlslOutputString += TEXT("\tContext.") + PrevMap + TEXT(".Interpolation.Emitter_InterpSpawnStartDt = Emitter_InterpSpawnStartDt;\n");
			HlslOutputString += TEXT("\tContext.") + PrevMap + TEXT(".Interpolation.Emitter_SpawnGroup = Emitter_SpawnGroup;\n");

			for (int32 UniformIdx = 0; UniformIdx < ChunksByMode[(int32)ENiagaraCodeChunkMode::Uniform].Num(); ++UniformIdx)
			{
				int32 ChunkIdx = ChunksByMode[(int32)ENiagaraCodeChunkMode::Uniform][UniformIdx];
				if (UniformIdx != INDEX_NONE)
				{
					FNiagaraVariable* FoundNamespacedVar = nullptr;
					const FName* FoundSystemKey = ParamMapDefinedSystemVarsToUniformChunks.FindKey(UniformIdx);

					// This uniform was either an emitter uniform parameter or a system uniform parameter. Search our maps to find out which one it was 
					// so that we can properly deal with accessors.
					if (FoundSystemKey != nullptr)
					{
						FoundNamespacedVar = ParamMapDefinedSystemToNamespaceVars.Find(*FoundSystemKey);
					}
					/*else
					{
						const FName* FoundEmitterParameterKey = ParamMapDefinedEmitterParameterVarsToUniformChunks.FindKey(UniformIdx);
						if (FoundEmitterParameterKey != nullptr)
						{
							FoundNamespacedVar = ParamMapDefinedEmitterParameterToNamespaceVars.Find(*FoundEmitterParameterKey);
						}
					}*/

					if (FoundNamespacedVar != nullptr)
					{
						FString FoundName = GetSanitizedSymbolName(FoundNamespacedVar->GetName().ToString());
						FNiagaraCodeChunk& Chunk = CodeChunks[ChunkIdx];
						if (ShouldInterpolateParameter(*FoundNamespacedVar))
						{
							HlslOutputString += TEXT("\tContext.") + PrevMap + TEXT(".") + FoundName + TEXT(" = lerp(") + INTERPOLATED_PARAMETER_PREFIX + Chunk.SymbolName + Chunk.ComponentMask + TEXT(", ") + Chunk.SymbolName + Chunk.ComponentMask + TEXT(", ") + TEXT("SpawnInterp);\n");
						}
						else
						{
							// For now, we do nothing for non-floating point variables..
						}
					}
				}
			}
			HlslOutputString += TEXT("\tContext.") + PrevMap + TEXT(".Engine.DeltaTime = 0.0f;\n");
			HlslOutputString += TEXT("\tContext.") + PrevMap + TEXT(".Engine.InverseDeltaTime = 0.0f;\n");
			HlslOutputString += TEXT("\tContext.") + CurMap + TEXT(".Engine.DeltaTime = InterpSpawn_UpdateTime;\n");
			HlslOutputString += TEXT("\tContext.") + CurMap + TEXT(".Engine.InverseDeltaTime = InterpSpawn_InvUpdateTime;\n");
		}

		HlslOutputString += TEXT("}\n\n");
	}
}

void FHlslNiagaraTranslator::DefineDataSetReadFunction(FString &HlslOutputString, TArray<FNiagaraDataSetID> &ReadDataSets)
{
	if (UNiagaraScript::IsParticleEventScript(CompileOptions.TargetUsage) && CompilationTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		HlslOutputString += TEXT("void ReadDataSets(inout FSimulationContext Context, int SetInstanceIndex)\n{\n");
	}
	else
	{
		HlslOutputString += TEXT("void ReadDataSets(inout FSimulationContext Context)\n{\n");
	}

	// We shouldn't read anything in a Spawn Script!
	if (UNiagaraScript::IsParticleSpawnScript(CompileOptions.TargetUsage) || UNiagaraScript::IsGPUScript(CompileOptions.TargetUsage))
	{
		HlslOutputString += TEXT("}\n\n");
		return;
	}

	for (TPair<FNiagaraDataSetID, TMap<int32, FDataSetAccessInfo>>& DataSetInfoPair : DataSetReadInfo[0])
	{
		FNiagaraDataSetID DataSet = DataSetInfoPair.Key;
		int32 OffsetCounterInt = 0;
		int32 OffsetCounterFloat = 0;
		int32 &FloatCounter = OffsetCounterFloat;
		int32 &IntCounter = CompilationTarget == ENiagaraSimTarget::GPUComputeSim ? OffsetCounterInt : OffsetCounterFloat;
		int32 DataSetIndex = 1;
		for (TPair<int32, FDataSetAccessInfo>& IndexInfoPair : DataSetInfoPair.Value)
		{
			FString Symbol = FString("\tContext.") + DataSet.Name.ToString() + "Read.";  // TODO: HACK - need to get the real symbol name here
			FString SetIdx = FString::FromInt(DataSetIndex);
			FString DataSetComponentBufferSize = "DSComponentBufferSizeRead{1}" + SetIdx;
			if (CompilationTarget == ENiagaraSimTarget::GPUComputeSim)
			{
				for (FNiagaraVariable &Var : IndexInfoPair.Value.Variables)
				{
					// TODO: temp = should really generate output functions for each set
					FString Fmt = Symbol + Var.GetName().ToString() + FString(TEXT("{0} = ReadDataSet{1}")) + SetIdx + TEXT("[{2}*") + DataSetComponentBufferSize + " + SetInstanceIndex];\n";
					GatherVariableForDataSetAccess(Var, Fmt, IntCounter, FloatCounter, -1, TEXT(""), HlslOutputString);
				}
			}
			else
			{
				for (FNiagaraVariable &Var : IndexInfoPair.Value.Variables)
				{
					// TODO: currently always emitting a non-advancing read, needs to be changed for some of the use cases
					FString Fmt = TEXT("\tContext.") + DataSet.Name.ToString() + "Read." + Var.GetName().ToString() + TEXT("{0} = InputDataNoadvance{1}({2}, {3});\n");
					GatherVariableForDataSetAccess(Var, Fmt, IntCounter, FloatCounter, DataSetIndex, TEXT(""), HlslOutputString);
				}
			}
		}
	}

	HlslOutputString += TEXT("}\n\n");
}


void FHlslNiagaraTranslator::DefineDataSetWriteFunction(FString &HlslOutputString, TArray<FNiagaraDataSetProperties> &WriteDataSets, TArray<int32>& WriteConditionVarIndices)
{
	HlslOutputString += TEXT("void WriteDataSets(inout FSimulationContext Context)\n{\n");

	int32 DataSetIndex = 1;
	for (TPair<FNiagaraDataSetID, TMap<int32, FDataSetAccessInfo>>& DataSetInfoPair : DataSetWriteInfo[0])
	{
		FNiagaraDataSetID DataSet = DataSetInfoPair.Key;
		int32 OffsetCounter = 0;

		HlslOutputString += "\t{\n";
		HlslOutputString += "\tint TmpWriteIndex;\n";
		int32* ConditionalWriteIdxPtr = DataSetWriteConditionalInfo[0].Find(DataSet);
		if (nullptr == ConditionalWriteIdxPtr || INDEX_NONE == *ConditionalWriteIdxPtr)
		{
			HlslOutputString += "\tbool bValid = true;\n";
		}
		else
		{
			HlslOutputString += "\tbool bValid = " + FString("Context.") + DataSet.Name.ToString() + "Write_Valid;\n";
		}
		int32 WriteOffsetInt = 0;
		int32 WriteOffsetFloat = 0;
		int32 &FloatCounter = WriteOffsetFloat;
		int32 &IntCounter = CompilationTarget == ENiagaraSimTarget::GPUComputeSim ? WriteOffsetInt : WriteOffsetFloat;

		// grab the current ouput index; currently pass true, but should use an arbitrary bool to determine whether write should happen or not

		HlslOutputString += "\tTmpWriteIndex = AcquireIndex(";
		HlslOutputString += FString::FromInt(DataSetIndex);
		HlslOutputString += ", bValid);\n";

		HlslOutputString += CompilationTarget == ENiagaraSimTarget::GPUComputeSim ? "\tif(TmpWriteIndex>=0)\n\t{\n" : "";

		for (TPair<int32, FDataSetAccessInfo>& IndexInfoPair : DataSetInfoPair.Value)
		{
			FString Symbol = FString("Context.") + DataSet.Name.ToString() + "Write";  // TODO: HACK - need to get the real symbol name here
			if (CompilationTarget == ENiagaraSimTarget::GPUComputeSim)
			{
				FString SetIdx = FString::FromInt(DataSetIndex);
				FString DataSetComponentBufferSize = "DSComponentBufferSizeWrite{1}" + SetIdx;
				for (FNiagaraVariable &Var : IndexInfoPair.Value.Variables)
				{
					// TODO: temp = should really generate output functions for each set
					FString Fmt = FString(TEXT("\t\tRWWriteDataSet{1}")) + SetIdx + TEXT("[{2}*") + DataSetComponentBufferSize + TEXT(" + {3}] = ") + Symbol + TEXT(".") + Var.GetName().ToString() + TEXT("{0};\n");
					GatherVariableForDataSetAccess(Var, Fmt, IntCounter, FloatCounter, -1, TEXT("TmpWriteIndex"), HlslOutputString);
				}
			}
			else
			{
				for (FNiagaraVariable &Var : IndexInfoPair.Value.Variables)
				{
					// TODO: data set index is always 1; need to increase each set
					FString Fmt = TEXT("\t\tOutputData{1}(") + FString::FromInt(DataSetIndex) + (", {2}, {3}, ") + Symbol + "." + Var.GetName().ToString() + TEXT("{0});\n");
					GatherVariableForDataSetAccess(Var, Fmt, FloatCounter, FloatCounter, -1, TEXT("TmpWriteIndex"), HlslOutputString);
				}
			}
		}

		HlslOutputString += CompilationTarget == ENiagaraSimTarget::GPUComputeSim ? "\t}\n" : "";
		DataSetIndex++;
		HlslOutputString += "\t}\n";
	}

	HlslOutput += TEXT("}\n\n");
}

void FHlslNiagaraTranslator::DefineDataInterfaceHLSL(FString &InHlslOutput)
{
	FString InterfaceCommonHLSL;
	FString InterfaceUniformHLSL;
	FString InterfaceFunctionHLSL;
	TArray<FString> BufferParamNames;
	TSet<FName> InterfaceClasses;
	for (uint32 i = 0; i < 32; i++)
	{
		BufferParamNames.Add(TEXT("DataInterfaceBuffer_") + FString::FromInt(i));
	}

	uint32 CurBufferIndex = 0;
	for (int32 i = 0; i < CompilationOutput.ScriptData.DataInterfaceInfo.Num(); i++)
	{
		FNiagaraScriptDataInterfaceCompileInfo& Info = CompilationOutput.ScriptData.DataInterfaceInfo[i];

		UObject* const* FoundCDO = CompileData->CDOs.Find(Info.Type.GetClass());
		check(FoundCDO != nullptr);
		UNiagaraDataInterface* CDO = Cast<UNiagaraDataInterface>(*FoundCDO);
		if (CDO && CDO->CanExecuteOnTarget(ENiagaraSimTarget::GPUComputeSim))
		{
			if ( !InterfaceClasses.Contains(Info.Type.GetFName()) )
			{
				CDO->GetCommonHLSL(InterfaceCommonHLSL);
				InterfaceClasses.Add(Info.Type.GetFName());
			}

			FString OwnerIDString = Info.Name.ToString();
			FString SanitizedOwnerIDString = GetSanitizedSymbolName(OwnerIDString, true);

			// grab the buffer definition from the interface
			//
			FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo = DIParamInfo.AddDefaulted_GetRef();
			DIInstanceInfo.DataInterfaceHLSLSymbol = SanitizedOwnerIDString;
			DIInstanceInfo.DIClassName = Info.Type.GetClass()->GetName();

			// Build a list of function instances that will be generated for this DI.
			const TSet<FNiagaraFunctionSignature>* DataInterfaceFunctions = DataInterfaceRegisteredFunctions.Find(Info.Type.GetFName());
			if (DataInterfaceFunctions != nullptr)
			{
				DIInstanceInfo.GeneratedFunctions.Reserve(DataInterfaceFunctions->Num());
				for (const FNiagaraFunctionSignature& OriginalSig : *DataInterfaceFunctions)
				{
					if (!OriginalSig.bSupportsGPU)
					{
						Error(FText::Format(LOCTEXT("GPUDataInterfaceFunctionNotSupported", "DataInterface {0} function {1} cannot run on the GPU."), FText::FromName(Info.Type.GetFName()), FText::FromName(OriginalSig.Name)), nullptr, nullptr);
						continue;
					}

					// make a copy so we can modify the owner id and get the correct hlsl signature
					FNiagaraFunctionSignature Sig = OriginalSig;
					Sig.OwnerName = Info.Name;

					FNiagaraDataInterfaceGeneratedFunction& DIFunc = DIInstanceInfo.GeneratedFunctions.AddDefaulted_GetRef();
					DIFunc.DefinitionName = Sig.Name;
					DIFunc.InstanceName = GetFunctionSignatureSymbol(Sig);
					DIFunc.Specifiers.Empty(Sig.FunctionSpecifiers.Num());
					for (const TTuple<FName, FName>& Specifier : Sig.FunctionSpecifiers)
					{
						DIFunc.Specifiers.Add(Specifier);
					}
				}
			}

			CDO->GetParameterDefinitionHLSL(DIInstanceInfo, InterfaceUniformHLSL);

			// Ask the DI to generate HLSL.
			for(int FunctionInstanceIndex = 0; FunctionInstanceIndex < DIInstanceInfo.GeneratedFunctions.Num(); ++FunctionInstanceIndex)
			{
				const FNiagaraDataInterfaceGeneratedFunction& DIFunc = DIInstanceInfo.GeneratedFunctions[FunctionInstanceIndex];
				const bool HlslOK = CDO->GetFunctionHLSL(DIInstanceInfo, DIFunc, FunctionInstanceIndex, InterfaceFunctionHLSL);
				if (!HlslOK)
				{
					Error(FText::Format(LOCTEXT("GPUDataInterfaceFunctionNotSupported", "DataInterface {0} function {1} is not implemented for GPU."), FText::FromName(Info.Type.GetFName()), FText::FromName(DIFunc.DefinitionName)), nullptr, nullptr);
				}
			}
		}
		else
		{
			Error(FText::Format(LOCTEXT("NonGPUDataInterfaceError", "DataInterface {0} ({1}) cannot run on the GPU."), FText::FromName(Info.Name), FText::FromString(CDO ? CDO->GetClass()->GetName() : TEXT(""))), nullptr, nullptr);
		}
	}
	InHlslOutput += InterfaceCommonHLSL + InterfaceUniformHLSL + InterfaceFunctionHLSL;
}

void FHlslNiagaraTranslator::DefineExternalFunctionsHLSL(FString &InHlslOutput)
{
	for (FNiagaraFunctionSignature& FunctionSig : CompilationOutput.ScriptData.AdditionalExternalFunctions )
	{
		if ( UNiagaraFunctionLibrary::DefineFunctionHLSL(FunctionSig, InHlslOutput) == false )
		{
			Error(FText::Format(LOCTEXT("ExternFunctionMissingHLSL", "ExternalFunction {0} does not have a HLSL implementation for the GPU."), FText::FromName(FunctionSig.Name)), nullptr, nullptr);
		}
	}
}

void FHlslNiagaraTranslator::DefineMainGPUFunctions(
	const TArray<TArray<FNiagaraVariable>>& DataSetVariables,
	const TMap<FNiagaraDataSetID, int32>& DataSetReads,
	const TMap<FNiagaraDataSetID, int32>& DataSetWrites)
{
	static const auto UseShaderStagesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.UseShaderStages"));
	const bool bUseShaderStages = UseShaderStagesCVar->GetInt() == 1;
 
	TArray<FNiagaraDataSetID> ReadDataSetIDs;
	TArray<FNiagaraDataSetID> WriteDataSetIDs;
	
	DataSetReads.GetKeys(ReadDataSetIDs);
	DataSetWrites.GetKeys(WriteDataSetIDs);

	// Whether Alive is used and must be set at each run
	const bool bUsesAlive = [&]
	{
		TArray<FName> DataSetNames;
		for (const FNiagaraDataSetID& ReadId : ReadDataSetIDs)
		{
			DataSetNames.AddUnique(ReadId.Name);
		}
		for (const FNiagaraDataSetID& WriteId : WriteDataSetIDs)
		{
			DataSetNames.AddUnique(WriteId.Name);
		}
		for (int32 i = 0; i < ParamMapHistories.Num(); i++)
		{
			for (FName DataSetName : DataSetNames)
			{
				if (ParamMapHistories[i].FindVariable(*(DataSetName.ToString() + TEXT(".Alive")), FNiagaraTypeDefinition::GetBoolDef()) != INDEX_NONE)
				{
					return true;
				}
			}
		}
		return false;
	}();

	const bool bNeedsPersistentIDs = CompileOptions.AdditionalDefines.Contains(TEXT("RequiresPersistentIDs"));

	// A list of constant to reset after Emitter_SpawnGroup gets modified by GetEmitterSpawnInfoForParticle()
	TArray<FString> EmitterSpawnGroupReinit;

	///////////////////////
	// InitConstants()
	HlslOutput += TEXT("void InitConstants(inout FSimulationContext Context)\n{\n");
	{
		// Fill in the defaults for parameters.
		for (const FString& InitChunk : MainPreSimulateChunks)
		{
			HlslOutput += TEXT("\t") + InitChunk + TEXT("\n");

			if (InitChunk.Contains(TEXT("Emitter_SpawnGroup;")))
			{
				EmitterSpawnGroupReinit.Add(InitChunk);
			}
		}
	}
	HlslOutput += TEXT("}\n\n");

	///////////////////////
	// InitSpawnVariables()
	HlslOutput += TEXT("void InitSpawnVariables(inout FSimulationContext Context)\n{\n");
	{
		// Reset constant that have been modified by GetEmitterSpawnInfoForParticle()
		if (EmitterSpawnGroupReinit.Num())
		{
			for (const FString& ReinitChunk : EmitterSpawnGroupReinit)
			{
				HlslOutput += TEXT("\t") + ReinitChunk + TEXT("\n");
			}
			HlslOutput += TEXT("\n");
		}

		FString ContextName = TEXT("\tContext.Map.");
		if (TranslationStages.Num() > 1) // First context 0 is "MapSpawn"
		{
			ContextName = FString::Printf(TEXT("\tContext.%s."), *TranslationStages[0].PassNamespace);
		}

		//The VM register binding assumes the same inputs as outputs which is obviously not always the case.
		for (int32 DataSetIndex = 0, IntCounter = 0, FloatCounter = 0; DataSetIndex < DataSetReads.Num(); ++DataSetIndex)
		{
			const FNiagaraDataSetID DataSetID = ReadDataSetIDs[DataSetIndex];
			const TArray<FNiagaraVariable>& NiagaraVariables = DataSetVariables[DataSetReads[DataSetID]];
			for (const FNiagaraVariable& Var : NiagaraVariables)
			{
				FString VarFmt = ContextName + GetSanitizedSymbolName(Var.GetName().ToString()) + TEXT("{0} = {4};\n");
				GatherVariableForDataSetAccess(Var, VarFmt, IntCounter, FloatCounter, DataSetIndex, TEXT(""), HlslOutput);
			}
		}

		if (bUsesAlive)
		{
			HlslOutput += TEXT("\n") + ContextName + TEXT("DataInstance.Alive=true;\n");
		}

		if (bNeedsPersistentIDs)
		{
			HlslOutput += TEXT("\n\tint IDIndex, IDAcquireTag;\n\tAcquireID(0, IDIndex, IDAcquireTag);\n");
			HlslOutput += ContextName + TEXT("Particles.ID.Index = IDIndex;\n");
			HlslOutput += ContextName + TEXT("Particles.ID.AcquireTag = IDAcquireTag;\n");
		}
	}
	HlslOutput += TEXT("}\n\n");

	////////////////////////
	// LoadUpdateVariables()
	HlslOutput += TEXT("void LoadUpdateVariables(inout FSimulationContext Context, int InstanceIdx)\n{\n");
	{
		FString ContextName = TEXT("\tContext.Map.");
		if (TranslationStages.Num() > 1) // Last context is "MapUpdate"
		{
			ContextName = FString::Printf(TEXT("\tContext.%s."), *TranslationStages.Last().PassNamespace);
		}

		for (int32 DataSetIndex = 0, IntCounter = 0, FloatCounter = 0; DataSetIndex < DataSetReads.Num(); ++DataSetIndex)
		{
			const FNiagaraDataSetID DataSetID = ReadDataSetIDs[DataSetIndex];
			const TArray<FNiagaraVariable>& NiagaraVariables = DataSetVariables[DataSetReads[DataSetID]];
			for (const FNiagaraVariable& Var : NiagaraVariables)
			{
				const FString VarName = ContextName + GetSanitizedSymbolName(Var.GetName().ToString());
				FString VarFmt;

				// If the NiagaraClearEachFrame value is set on the data set, we don't bother reading it in each frame as we know that it is is invalid. However,
				// this is only used for the base data set. Other reads are potentially from events and are therefore perfectly valid.
				if (DataSetIndex == 0 && Var.GetType().GetScriptStruct() != nullptr && Var.GetType().GetScriptStruct()->GetMetaData(TEXT("NiagaraClearEachFrame")).Equals(TEXT("true"), ESearchCase::IgnoreCase))
				{
					VarFmt = VarName + TEXT("{0} = {4};\n");
				}
				else
				{
					VarFmt = VarName + TEXT("{0} = InputData{1}({2}, {3}, InstanceIdx);\n");

					if (bUseShaderStages)
					{
						if (FNiagaraParameterMapHistory::IsAttribute(Var))
						{
							FString RegisterName = VarName;
							RegisterName.ReplaceInline(PARAM_MAP_ATTRIBUTE_STR, PARAM_MAP_INDICES_STR);
							const int32 RegisterValue = Var.GetType().IsFloatPrimitive() ? FloatCounter : IntCounter;
							HlslOutput += RegisterName + FString::Printf(TEXT(" = %d;\n"), RegisterValue);
						}
					}
				}
				GatherVariableForDataSetAccess(Var, VarFmt, IntCounter, FloatCounter, DataSetIndex, TEXT(""), HlslOutput);
			}
		}
		if (bUsesAlive)
		{
			HlslOutput += TEXT("\n") + ContextName + TEXT("DataInstance.Alive=true;\n");
		}
	}
	HlslOutput += TEXT("}\n\n");

	/////////////////////////////////////
	// ConditionalInterpolateParameters()
	HlslOutput += TEXT("void ConditionalInterpolateParameters(inout FSimulationContext Context)\n{\n");
	{
		if (RequiresInterpolation())
		{
			HlslOutput += TEXT("\tInterpolateParameters(Context);\n"); // Requires ExecIndex, which needs to be in a stage.
		}
	}
	HlslOutput += TEXT("}\n\n");

	//////////////////////
	// TransferAttibutes()
	HlslOutput += TEXT("void TransferAttributes(inout FSimulationContext Context)\n{\n");
	{
		if (TranslationStages.Last().bCopyPreviousParams)
		{
			if (ParamMapDefinedAttributesToNamespaceVars.Num() != 0)
			{
				HlslOutput += TEXT("\tContext.") + TranslationStages.Last().PassNamespace + TEXT(".Particles = Context.") + TranslationStages[0].PassNamespace + TEXT(".Particles;\n");
				if (bUsesAlive)
				{
					HlslOutput += TEXT("\tContext.") + TranslationStages.Last().PassNamespace + TEXT(".DataInstance = Context.") + TranslationStages[0].PassNamespace + TEXT(".DataInstance;\n");
				}
			}
		}
	}
	HlslOutput += TEXT("}\n\n");

	/////////////////////////
	// StoreUpdateVariables()
	HlslOutput += TEXT("void StoreUpdateVariables(in FSimulationContext Context)\n{\n");
	{
		if (bUsesAlive)
		{
			HlslOutput += TEXT("\tconst bool bValid = Context.") + TranslationStages.Last().PassNamespace + TEXT(".DataInstance.Alive;\n");
			HlslOutput += TEXT("\tconst int WriteIndex = OutputIndex(0, false, bValid);\n");
		}
		else
		{
			HlslOutput += TEXT("\tconst bool bValid = GCurrentPhase != -1;\n");
			HlslOutput += TEXT("\tconst int WriteIndex = OutputIndex(0, true, bValid);\n");
		}

		FString ContextName = TEXT("Context.Map.");
		if (TranslationStages.Num() > 1) // Last context is "MapUpdate"
		{
			ContextName = FString::Printf(TEXT("Context.%s."), *TranslationStages.Last().PassNamespace);
		}

		HlslOutput += TEXT("\tif (bValid)\n\t{\n");

		if (bNeedsPersistentIDs)
		{
			HlslOutput += FString::Printf(TEXT("\t\tUpdateID(0, %sParticles.ID.Index, WriteIndex);\n"), *ContextName);
		}

		for (int32 DataSetIndex = 0, IntCounter = 0, FloatCounter = 0; DataSetIndex < DataSetWrites.Num(); ++DataSetIndex)
		{
			const FNiagaraDataSetID DataSetID = ReadDataSetIDs[DataSetIndex];
			const TArray<FNiagaraVariable>& NiagaraVariables = DataSetVariables[DataSetWrites[DataSetID]];
			for (const FNiagaraVariable& Var : NiagaraVariables)
			{
				// If coming from a parameter map, use the one on the context, otherwise use the output.
				FString VarFmt = TEXT("\t\tOutputData{1}(0, {2}, {3}, ") + ContextName + GetSanitizedSymbolName(Var.GetName().ToString()) + TEXT("{0});\n");
				GatherVariableForDataSetAccess(Var, VarFmt, IntCounter, FloatCounter, -1, TEXT("WriteIndex"), HlslOutput);
			}
		}
	}
	HlslOutput += TEXT("\t}\n}\n\n");

	/////////////////////////
	// CopyInstance()
	HlslOutput += TEXT("void CopyInstance(in int InstanceIdx)\n{\n");
	{
#if 0
		HlslOutput += TEXT("\tFSimulationContext Context = (FSimulationContext)0;\n");
		if (UNiagaraScript::IsParticleEventScript(CompileOptions.TargetUsage))
		{
			for (int32 VarArrayIdx = 0; VarArrayIdx < DataSetReads.Num(); VarArrayIdx++)
			{
				const FNiagaraDataSetID DataSetID = ReadDataSetIDs[VarArrayIdx];
				const TArray<FNiagaraVariable>& ArrayRef = DataSetVariables[DataSetReads[DataSetID]];
				DefineDataSetVariableReads(HlslOutput, DataSetID, VarArrayIdx, ArrayRef);
			}

			if (bGpuUsesAlive)
			{
				HlslOutput += TEXT("\tContext.Map.DataInstance.Alive = true;\n");
			}

			for (int32 VarArrayIdx = 0; VarArrayIdx < DataSetWrites.Num(); VarArrayIdx++)
			{
				const FNiagaraDataSetID DataSetID = WriteDataSetIDs[VarArrayIdx];
				const TArray<FNiagaraVariable>& ArrayRef = DataSetVariables[DataSetWrites[DataSetID]];
				DefineDataSetVariableWrites(HlslOutput, DataSetID, VarArrayIdx, ArrayRef);
			}
		}
#else
		HlslOutput += TEXT("\t// TODO!\n");
#endif
	}
	HlslOutput += TEXT("}\n");
}

void FHlslNiagaraTranslator::DefineMain(FString &OutHlslOutput,
	const TArray<TArray<FNiagaraVariable>>& DataSetVariables,
	const TMap<FNiagaraDataSetID, int32>& DataSetReads,
	const TMap<FNiagaraDataSetID, int32>& DataSetWrites)
{
	check(CompilationTarget != ENiagaraSimTarget::GPUComputeSim);

	OutHlslOutput += TEXT("void SimulateMain()\n{\n");
	
	EnterStatsScope(FNiagaraStatScope(*(CompileOptions.GetName() + TEXT("_Main")), TEXT("Main")), OutHlslOutput);

	OutHlslOutput += TEXT("\n\tFSimulationContext Context = (FSimulationContext)0;\n");
	TMap<FName, int32> InputRegisterAllocations;
	TMap<FName, int32> OutputRegisterAllocations;

	ReadIdx = 0;
	WriteIdx = 0;

	//TODO: Grab indices for reading data sets and do the read.
	//read input.

	TArray<FNiagaraDataSetID> ReadDataSetIDs;
	TArray<FNiagaraDataSetID> WriteDataSetIDs;

	DataSetReads.GetKeys(ReadDataSetIDs);
	DataSetWrites.GetKeys(WriteDataSetIDs);

	//The VM register binding assumes the same inputs as outputs which is obviously not always the case.
	for (int32 VarArrayIdx = 0; VarArrayIdx < DataSetReads.Num(); VarArrayIdx++)
	{
		const FNiagaraDataSetID DataSetID = ReadDataSetIDs[VarArrayIdx];
		const TArray<FNiagaraVariable>& ArrayRef = DataSetVariables[DataSetReads[DataSetID]];
		DefineDataSetVariableReads(HlslOutput, DataSetID, VarArrayIdx, ArrayRef);
	}

	bool bNeedsPersistentIDs = CompileOptions.AdditionalDefines.Contains(TEXT("RequiresPersistentIDs"));
	if (bNeedsPersistentIDs && UNiagaraScript::IsSpawnScript(CompileOptions.TargetUsage))
	{
		FString MapName = UNiagaraScript::IsInterpolatedParticleSpawnScript(CompileOptions.TargetUsage) ? TEXT("Context.MapSpawn") : TEXT("Context.Map");
		//Add code to handle persistent IDs.
		OutHlslOutput += TEXT("\tint TempIDIndex;\n\tint TempIDTag;\n");
		OutHlslOutput += TEXT("\tAcquireID(0, TempIDIndex, TempIDTag);\n");
		OutHlslOutput += FString::Printf(TEXT("\t%s.Particles.ID.Index = TempIDIndex;\n\t%s.Particles.ID.AcquireTag = TempIDTag;\n"), *MapName, *MapName);
	}

	{
		// Manually write to Particles.UniqueID on spawn, and deliberately place it at the top of SimulateMain to make sure it's initialized in the right order

		// NOTE(mv): These relies on Particles.UniqueID and Engine.Emitter.TotalSpawnedParticles both being explicitly added to the parameter histories in 
		//           FHlslNiagaraTranslator::Translate.

		// NOTE(mv): This relies on Particles.UniqueID being excluded from being default initialized. 
		//           This happens in FNiagaraParameterMapHistory::ShouldIgnoreVariableDefault
		if (UNiagaraScript::IsParticleSpawnScript(CompileOptions.TargetUsage))
		{
			FString MapName = UNiagaraScript::IsInterpolatedParticleSpawnScript(CompileOptions.TargetUsage) ? TEXT("Context.MapSpawn") : TEXT("Context.Map");
			OutHlslOutput += FString::Printf(TEXT("\t%s.Particles.UniqueID = Engine_Emitter_TotalSpawnedParticles + ExecIndex();\n"), *MapName);
		}
		else if (UNiagaraScript::IsGPUScript(CompileOptions.TargetUsage))
		{
			// NOTE(mv): The GPU script only have one file, so we need to make sure we only apply this in the spawn phase. 
			//           
			OutHlslOutput += TEXT("\tif (Phase == 0) \n\t{\n\t\tContext.MapSpawn.Particles.UniqueID = Engine_Emitter_TotalSpawnedParticles + ExecIndex();\n\t}\n");
		}
	}

	// Fill in the defaults for parameters.
	for (int32 i = 0; i < MainPreSimulateChunks.Num(); ++i)
	{
		OutHlslOutput += TEXT("\t") + MainPreSimulateChunks[i] + TEXT("\n");
	}

	// call the read data set function
	OutHlslOutput += TEXT("\tReadDataSets(Context);\n");
	for (int32 StageIdx = 0; StageIdx < TranslationStages.Num(); StageIdx++)
	{
		if (StageIdx == 0)
		{
			// Either go on to the next phase, or write to the final output context.
			if (RequiresInterpolation())
			{
				OutHlslOutput += TEXT("\tInterpolateParameters(Context);\n"); // Requires ExecIndex, which needs to be in a stage.
			}
		}

		OutHlslOutput += FString::Printf(TEXT("\tSimulate%s(Context);\n"), TranslationStages.Num() > 1 ? *TranslationStages[StageIdx].PassNamespace : TEXT(""));

		if (StageIdx + 1 < TranslationStages.Num() && TranslationStages[StageIdx + 1].bCopyPreviousParams)
		{
			OutHlslOutput += TEXT("\t//Begin Transfer of Attributes!\n");
			if (ParamMapDefinedAttributesToNamespaceVars.Num() != 0)
			{
				FString CopyStr = TEXT("\tContext.") + TranslationStages[StageIdx + 1].PassNamespace + TEXT(".Particles = Context.") + TranslationStages[StageIdx].PassNamespace + TEXT(".Particles;\n");
				OutHlslOutput += CopyStr;
			}
			OutHlslOutput += TEXT("\t//End Transfer of Attributes!\n\n");
		}
	}

	// write secondary data sets
	OutHlslOutput += TEXT("\tWriteDataSets(Context);\n");

	//The VM register binding assumes the same inputs as outputs which is obviously not always the case.
	//We should separate inputs and outputs in the script.
	for (int32 VarArrayIdx = 0; VarArrayIdx < DataSetWrites.Num(); VarArrayIdx++)
	{
		const FNiagaraDataSetID DataSetID = WriteDataSetIDs[VarArrayIdx];
		const TArray<FNiagaraVariable> ArrayRef = DataSetVariables[DataSetWrites[DataSetID]];
		DefineDataSetVariableWrites(HlslOutput, DataSetID, VarArrayIdx, ArrayRef);
	}

	ExitStatsScope(OutHlslOutput);
	OutHlslOutput += TEXT("}\n");
}

void FHlslNiagaraTranslator::DefineDataSetVariableWrites(FString &OutHlslOutput, const FNiagaraDataSetID& Id, int32 DataSetIndex, const TArray<FNiagaraVariable>& WriteVars)
{
	check(CompilationTarget != ENiagaraSimTarget::GPUComputeSim);

	//TODO Grab indices for data set writes (inc output) and do the write. Need to rewrite this for events interleaved..
	OutHlslOutput += "\t{\n";
	bool bUsesAlive = false;
	if (!UNiagaraScript::IsNonParticleScript(CompileOptions.TargetUsage))
	{
		FString DataSetName = Id.Name.ToString();
		bool bHasPerParticleAliveSpawn = false;
		bool bHasPerParticleAliveUpdate = false;
		bool bHasPerParticleAliveEvent = false;
		for (int32 i = 0; i < ParamMapHistories.Num(); i++)
		{
			const UNiagaraNodeOutput* OutputNode = ParamMapHistories[i].GetFinalOutputNode();
			if (!OutputNode)
			{
				continue;
			}

			if (INDEX_NONE == ParamMapHistories[i].FindVariable(*(DataSetName + TEXT(".Alive")), FNiagaraTypeDefinition::GetBoolDef()))
			{
				continue;
			}

			switch (OutputNode->GetUsage())
			{
			case ENiagaraScriptUsage::ParticleSpawnScript:
			case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated:
				bHasPerParticleAliveSpawn = true;
				break;
			case ENiagaraScriptUsage::ParticleUpdateScript:
				bHasPerParticleAliveUpdate = true;
				break;
			case ENiagaraScriptUsage::ParticleEventScript:
				bHasPerParticleAliveEvent = true;
				break;
			}
		}

		if ((bHasPerParticleAliveSpawn || bHasPerParticleAliveUpdate) && TranslationStages.Num() > 1)
		{
			// NOTE: TranslationStages.Num() > 1 for GPU Script or CPU Interpolated Spawn CPU scripts

			// NOTE: Context.MapSpawn is copied to Context.MapUpdate before this point in the script, so we might
			//       as well just keep it simple and check against MapUpdate only instead of redundantly branch.
			OutHlslOutput += TEXT("\tbool bValid = Context.MapUpdate.") + DataSetName + TEXT(".Alive;\n");
			bUsesAlive = true;
		}
		else if ((UNiagaraScript::IsParticleSpawnScript(CompileOptions.TargetUsage) && bHasPerParticleAliveSpawn) 
			|| (UNiagaraScript::IsParticleUpdateScript(CompileOptions.TargetUsage) && bHasPerParticleAliveUpdate)
			|| (UNiagaraScript::IsParticleEventScript(CompileOptions.TargetUsage) && bHasPerParticleAliveEvent))
		{
			// Non-interpolated CPU spawn script
			OutHlslOutput += TEXT("\tbool bValid = Context.Map.") + DataSetName + TEXT(".Alive;\n");
			bUsesAlive = true;
		}
	}

	// grab the current ouput index to write datas 
	if (bUsesAlive)
	{
		OutHlslOutput += "\tint TmpWriteIndex = OutputIndex(0, false, bValid);\n";
	}
	else
	{
		OutHlslOutput += "\tint TmpWriteIndex = OutputIndex(0, true, true);\n";
	}

	bool bNeedsPersistentIDs = CompileOptions.AdditionalDefines.Contains(TEXT("RequiresPersistentIDs"));
	if (bNeedsPersistentIDs && DataSetIndex == 0)
	{
		FString MapName = GetParameterMapInstanceName(0);
		OutHlslOutput += FString::Printf(TEXT("\tUpdateID(0, %s.Particles.ID.Index, TmpWriteIndex);\n"), *MapName);
	}

	int32 WriteOffsetInt = 0;
	int32 WriteOffsetFloat = 0;
	int32 &FloatCounter = WriteOffsetFloat;
	int32 &IntCounter = WriteOffsetFloat;
	for (const FNiagaraVariable &Var : WriteVars)
	{
		// If coming from a parameter map, use the one on the context, otherwise use the output.
		FString Fmt;
		if (TranslationStages.Num() > 1)
		{
			Fmt = TEXT("\tOutputData{1}(0, {2}, {3}, Context.") + TranslationStages[TranslationStages.Num() - 1].PassNamespace + TEXT(".") + GetSanitizedSymbolName(Var.GetName().ToString()) + TEXT("{0});\n");
		}
		else
		{
			Fmt = TEXT("\tOutputData{1}(0, {2}, {3}, Context.Map.") + GetSanitizedSymbolName(Var.GetName().ToString()) + TEXT("{0});\n");
		}
		GatherVariableForDataSetAccess(Var, Fmt, IntCounter, FloatCounter, -1, TEXT("TmpWriteIndex"), OutHlslOutput);
	}
	OutHlslOutput += "\t}\n";
}

void FHlslNiagaraTranslator::DefineDataSetVariableReads(FString &OutHlslOutput, const FNiagaraDataSetID& Id, int32 DataSetIndex, const TArray<FNiagaraVariable>& ReadVars)
{
	check(CompilationTarget != ENiagaraSimTarget::GPUComputeSim);

	int32 ReadOffsetInt = 0;
	int32 ReadOffsetFloat = 0;
	int32 &FloatCounter = ReadOffsetFloat;
	int32 &IntCounter = ReadOffsetFloat;

	FString DataSetName = Id.Name.ToString();
	FString Fmt;

	bool bIsGPUScript = UNiagaraScript::IsGPUScript(CompileOptions.TargetUsage);
	bool bIsSpawnScript =
		UNiagaraScript::IsParticleSpawnScript(CompileOptions.TargetUsage) || UNiagaraScript::IsInterpolatedParticleSpawnScript(CompileOptions.TargetUsage) ||
		UNiagaraScript::IsEmitterSpawnScript(CompileOptions.TargetUsage) || UNiagaraScript::IsSystemSpawnScript(CompileOptions.TargetUsage);
	bool bIsUpdateScript =
		UNiagaraScript::IsParticleUpdateScript(CompileOptions.TargetUsage) || UNiagaraScript::IsEmitterUpdateScript(CompileOptions.TargetUsage) ||
		UNiagaraScript::IsSystemUpdateScript(CompileOptions.TargetUsage);
	bool bIsEventScript = UNiagaraScript::IsParticleEventScript(CompileOptions.TargetUsage);
	bool bIsSystemOrEmitterScript =
		UNiagaraScript::IsEmitterSpawnScript(CompileOptions.TargetUsage) || UNiagaraScript::IsSystemSpawnScript(CompileOptions.TargetUsage) ||
		UNiagaraScript::IsEmitterUpdateScript(CompileOptions.TargetUsage) || UNiagaraScript::IsSystemUpdateScript(CompileOptions.TargetUsage);
	bool bIsPrimaryDataSet = DataSetIndex == 0;

	// This will initialize parameters to 0 for spawning.  For the system and emitter combined spawn script we want to do this on the
	// primary data set which contains the particle data, but we do not want to do this for the secondary data set since it has
	// external user and engine parameters which must be read.
	if (bIsGPUScript || (bIsSpawnScript && (bIsPrimaryDataSet || bIsSystemOrEmitterScript == false)))
	{
		FString ContextName = TEXT("\tContext.Map.");
		if (TranslationStages.Num() > 1)
		{
			ContextName = FString::Printf(TEXT("\tContext.%s."), *TranslationStages[0].PassNamespace);
		}

		FString VarReads;

		for (const FNiagaraVariable &Var : ReadVars)
		{
			Fmt = ContextName + GetSanitizedSymbolName(Var.GetName().ToString()) + TEXT("{0} = {4};\n");
			GatherVariableForDataSetAccess(Var, Fmt, IntCounter, FloatCounter, DataSetIndex, TEXT(""), VarReads);
		}

		OutHlslOutput += VarReads;
	}

	// This will initialize parameters to their correct initial values from constants or data sets for update, and will also initialize parameters
	// for spawn if this is a combined system and emitter spawn script and we're reading from a secondary data set for engine and user parameters.
	if (bIsGPUScript || bIsEventScript || bIsUpdateScript || (bIsSpawnScript && bIsPrimaryDataSet == false && bIsSystemOrEmitterScript))
	{
		FString ContextName = TEXT("\tContext.Map.");
		if (TranslationStages.Num() > 1)
		{
			ContextName = FString::Printf(TEXT("\tContext.%s."), *TranslationStages[TranslationStages.Num() - 1].PassNamespace);
		}

		// if we're a GPU spawn script (meaning a combined spawn/update script), we need to reset register index counter
		if (UNiagaraScript::IsGPUScript(CompileOptions.TargetUsage))
		{
			ReadOffsetInt = 0;
			ReadOffsetFloat = 0;
		}

		FString VarReads;

		for (const FNiagaraVariable &Var : ReadVars)
		{
			const FString VariableName = ContextName + GetSanitizedSymbolName(Var.GetName().ToString());
			// If the NiagaraClearEachFrame value is set on the data set, we don't bother reading it in each frame as we know that it is is invalid. However,
			// this is only used for the base data set. Other reads are potentially from events and are therefore perfectly valid.
			if (DataSetIndex == 0 && Var.GetType().GetScriptStruct() != nullptr && Var.GetType().GetScriptStruct()->GetMetaData(TEXT("NiagaraClearEachFrame")).Equals(TEXT("true"), ESearchCase::IgnoreCase))
			{
				Fmt = VariableName + TEXT("{0} = {4};\n");
			}
			else
			{
				Fmt = VariableName + TEXT("{0} = InputData{1}({2}, {3});\n");

				static const auto UseShaderStagesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.UseShaderStages"));
				if (UseShaderStagesCVar->GetInt() == 1)
				{
					if (FNiagaraParameterMapHistory::IsAttribute(Var))
					{
						FString RegisterName = VariableName;
						RegisterName.ReplaceInline(PARAM_MAP_ATTRIBUTE_STR, PARAM_MAP_INDICES_STR);
						const int32 RegisterValue = Var.GetType().IsFloatPrimitive() ? FloatCounter : IntCounter;
						VarReads += RegisterName + FString::Printf(TEXT(" = %d;\n"), RegisterValue);
					}
				}
			}
			GatherVariableForDataSetAccess(Var, Fmt, IntCounter, FloatCounter, DataSetIndex, TEXT(""), VarReads);
		}

		OutHlslOutput += VarReads;
	}
}

void FHlslNiagaraTranslator::WriteDataSetContextVars(TMap<FNiagaraDataSetID, TMap<int32, FDataSetAccessInfo>>& DataSetAccessInfo, bool bRead, FString& OutHlslOutput)
{
	//Now the intermediate storage for the data set reads and writes.
	uint32 DataSetIndex = 0;
	for (TPair<FNiagaraDataSetID, TMap<int32, FDataSetAccessInfo>>& DataSetInfoPair : DataSetAccessInfo)
	{
		FNiagaraDataSetID DataSet = DataSetInfoPair.Key;

		if (!bRead)
		{
			OutHlslOutput += TEXT("\tbool ") + DataSet.Name.ToString() + TEXT("Write_Valid; \n");
		}

		OutHlslOutput += TEXT("\tF") + DataSet.Name.ToString() + "DataSet " + DataSet.Name.ToString() + (bRead ? TEXT("Read") : TEXT("Write")) + TEXT(";\n");
	}
};


void FHlslNiagaraTranslator::WriteDataSetStructDeclarations(TMap<FNiagaraDataSetID, TMap<int32, FDataSetAccessInfo>>& DataSetAccessInfo, bool bRead, FString& OutHlslOutput)
{
	uint32 DataSetIndex = 1;
	for (TPair<FNiagaraDataSetID, TMap<int32, FDataSetAccessInfo>>& DataSetInfoPair : DataSetAccessInfo)
	{
		FNiagaraDataSetID DataSet = DataSetInfoPair.Key;
		FString StructName = TEXT("F") + DataSet.Name.ToString() + "DataSet";
		OutHlslOutput += TEXT("struct ") + StructName + TEXT("\n{\n");

		for (TPair<int32, FDataSetAccessInfo>& IndexInfoPair : DataSetInfoPair.Value)
		{
			for (FNiagaraVariable Var : IndexInfoPair.Value.Variables)
			{
				OutHlslOutput += TEXT("\t") + GetStructHlslTypeName(Var.GetType()) + TEXT(" ") + Var.GetName().ToString() + ";\n";
			}
		}

		OutHlslOutput += TEXT("};\n");

		// declare buffers for compute shader HLSL only; VM doesn't need htem
		// because its InputData and OutputData functions handle data set management explicitly
		//
		if (CompilationTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			FString IndexString = FString::FromInt(DataSetIndex);
			if (bRead)
			{
				OutHlslOutput += FString(TEXT("Buffer<float> ReadDataSetFloat")) + IndexString + ";\n";
				OutHlslOutput += FString(TEXT("Buffer<int> ReadDataSetInt")) + IndexString + ";\n";
				OutHlslOutput += "int DSComponentBufferSizeReadFloat" + IndexString + ";\n";
				OutHlslOutput += "int DSComponentBufferSizeReadInt" + IndexString + ";\n";
			}
			else
			{
				OutHlslOutput += FString(TEXT("RWBuffer<float> RWWriteDataSetFloat")) + IndexString + ";\n";
				OutHlslOutput += FString(TEXT("RWBuffer<int> RWWriteDataSetInt")) + IndexString + ";\n";
				OutHlslOutput += "int DSComponentBufferSizeWriteFloat" + IndexString + ";\n";
				OutHlslOutput += "int DSComponentBufferSizeWriteInt" + IndexString + ";\n";
			}
		}

		DataSetIndex++;
	}

}


//Decomposes each variable into its constituent register accesses.
void FHlslNiagaraTranslator::DecomposeVariableAccess(UStruct* Struct, bool bRead, FString IndexSymbol, FString HLSLString)
{
	FString AccessStr;

	for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;

		if (FStructProperty* StructProp = CastFieldChecked<FStructProperty>(Property))
		{
			FNiagaraTypeDefinition PropDef(StructProp->Struct);
			if (!IsHlslBuiltinVector(PropDef))
			{
				DecomposeVariableAccess(StructProp->Struct, bRead, IndexSymbol, AccessStr);
				return;
			}
		}

		int32 Index = INDEX_NONE;
		if (bRead)
		{
			Index = ReadIdx++;
			AccessStr = TEXT("ReadInput(");
			AccessStr += FString::FromInt(ReadIdx);
			AccessStr += ");\n";
		}
		else
		{
			Index = WriteIdx++;
			AccessStr = TEXT("WriteOutput(");
			AccessStr += FString::FromInt(WriteIdx);
			AccessStr += ");\n";
		}

		HLSLString += AccessStr;

		FNiagaraTypeDefinition StructDef(Cast<UScriptStruct>(Struct));
		FString TypeName = GetStructHlslTypeName(StructDef);
	}
};

void FHlslNiagaraTranslator::Init()
{
}

FString FHlslNiagaraTranslator::GetSanitizedSymbolName(FString SymbolName, bool bCollapsNamespaces)
{
	if (SymbolName.Len() == 0)
	{
		return SymbolName;
	}

	const UNiagaraEditorSettings* Settings = GetDefault<UNiagaraEditorSettings>();
	check(Settings);
	const TMap<FString, FString>& ReplacementsForInvalid = Settings->GetHLSLKeywordReplacementsMap();

	FString Ret = SymbolName;

	// Split up into individual namespaces...
	TArray<FString> SplitName;
	Ret.ParseIntoArray(SplitName, TEXT("."));

	// Rules for variable namespaces..
	for (int32 i = 0; i < SplitName.Num(); i++)
	{
		SplitName[i].ReplaceInline(TEXT("__"), TEXT("ASC95ASC95")); // Opengl reserves "__" within a name
		if (SplitName[i][0] >= TCHAR('0') && SplitName[i][0] <= TCHAR('9')) // Cannot start with a numeric digit
		{
			SplitName[i] = TEXT("INTEGER_") + SplitName[i];
		}

		const FString* FoundReplacementStr = ReplacementsForInvalid.Find(SplitName[i]); // Look for the string in the keyword protections array.
		if (FoundReplacementStr)
		{
			SplitName[i] = *FoundReplacementStr;
		}

		SplitName[i].ReplaceInline(TEXT("\t"), TEXT(""));
		SplitName[i].ReplaceInline(TEXT(" "), TEXT(""));

		// Handle internationalization of characters..
		SplitName[i] = ConvertToAsciiString(SplitName[i]);
	}

	// Gather back into single string..
	Ret = FString::Join(SplitName, TEXT("."));

	/*
	Ret.ReplaceInline(TEXT("\\"), TEXT("_"));
	Ret.ReplaceInline(TEXT("/"), TEXT("_"));
	Ret.ReplaceInline(TEXT(","), TEXT("_"));
	Ret.ReplaceInline(TEXT("-"), TEXT("_"));
	Ret.ReplaceInline(TEXT(":"), TEXT("_"));
	Ret = Ret.ConvertTabsToSpaces(0);
	*/

	if (bCollapsNamespaces)
	{
		Ret.ReplaceInline(TEXT("."), TEXT("_"));
	}
	return Ret;
}

FString FHlslNiagaraTranslator::GetSanitizedFunctionNameSuffix(FString Name)
{
	if (Name.Len() == 0)
	{
		return Name;
	}
	FString Ret = Name;

	// remove special characters
	Ret.ReplaceInline(TEXT("."), TEXT("_"));
	Ret.ReplaceInline(TEXT("\\"), TEXT("_"));
	Ret.ReplaceInline(TEXT("/"), TEXT("_"));
	Ret.ReplaceInline(TEXT(","), TEXT("_"));
	Ret.ReplaceInline(TEXT("-"), TEXT("_"));
	Ret.ReplaceInline(TEXT(":"), TEXT("_"));
	Ret.ReplaceInline(TEXT("\t"), TEXT(""));
	Ret.ReplaceInline(TEXT(" "), TEXT(""));	
	Ret.ReplaceInline(TEXT("__"), TEXT("ASC95ASC95")); // Opengl reserves "__" within a name

	// Handle internationalization of characters..
	return ConvertToAsciiString(Ret);
}

FString FHlslNiagaraTranslator::ConvertToAsciiString(FString Str)
{
	FString AsciiString;
	AsciiString.Reserve(Str.Len() * 6); // Assign room for every current char to be 'ASCXXX'
	for (int32 j = 0; j < Str.Len(); j++)
	{
		if ((Str[j] >= TCHAR('0') && Str[j] <= TCHAR('9')) ||
			(Str[j] >= TCHAR('A') && Str[j] <= TCHAR('Z')) ||
			(Str[j] >= TCHAR('a') && Str[j] <= TCHAR('z')) ||
			Str[j] == TCHAR('_') || Str[j] == TCHAR(' '))
		{
			// Do nothing.. these are valid chars..
			AsciiString.AppendChar(Str[j]);
		}
		else
		{
			// Need to replace the bad characters..
			AsciiString.Append(TEXT("ASC"));
			AsciiString.AppendInt((int32)Str[j]);
		}
	}
	return AsciiString;
}

FString FHlslNiagaraTranslator::GetUniqueSymbolName(FName BaseName)
{
	FString RetString = GetSanitizedSymbolName(BaseName.ToString());
	FName RetName = *RetString;
	uint32* NameCount = SymbolCounts.Find(RetName);
	if (NameCount == nullptr)
	{
		SymbolCounts.Add(RetName) = 1;
		return RetString;
	}

	if (*NameCount > 0)
	{
		RetString += LexToString(*NameCount);
	}
	++(*NameCount);
	return RetString;
}

void FHlslNiagaraTranslator::EnterFunction(const FString& Name, FNiagaraFunctionSignature& Signature, TArray<int32>& Inputs, const FGuid& InGuid)
{
	FunctionContextStack.Emplace(Name, Signature, Inputs, InGuid);
	//May need some more heavy and scoped symbol tracking?

	//Add new scope for pin reuse.
	PinToCodeChunks.AddDefaulted(1);
}

void FHlslNiagaraTranslator::ExitFunction()
{
	FunctionContextStack.Pop();
	//May need some more heavy and scoped symbol tracking?

	//Pop pin reuse scope.
	PinToCodeChunks.Pop();
}

FString FHlslNiagaraTranslator::GeneratedConstantString(float Constant)
{
	return LexToString(Constant);
}

static int32 GbNiagaraScriptStatTracking = 1;
static FAutoConsoleVariableRef CVarNiagaraScriptStatTracking(
	TEXT("fx.NiagaraScriptStatTracking"),
	GbNiagaraScriptStatTracking,
	TEXT("If > 0 stats tracking operations will be compiled into Niagara Scripts. \n"),
	ECVF_Default
);

void FHlslNiagaraTranslator::EnterStatsScope(FNiagaraStatScope StatScope)
{
	if (GbNiagaraScriptStatTracking)
	{
		int32 ScopeIdx = CompilationOutput.ScriptData.StatScopes.AddUnique(StatScope);
		AddBodyChunk(TEXT(""), FString::Printf(TEXT("EnterStatScope(%d /**%s*/)"), ScopeIdx, *StatScope.FullName.ToString()), FNiagaraTypeDefinition::GetFloatDef(), false);
		StatScopeStack.Push(ScopeIdx);
	}
}

void FHlslNiagaraTranslator::ExitStatsScope()
{
	if (GbNiagaraScriptStatTracking)
	{
		int32 ScopeIdx = StatScopeStack.Pop();
		AddBodyChunk(TEXT(""), FString::Printf(TEXT("ExitStatScope(/**%s*/)"), *CompilationOutput.ScriptData.StatScopes[ScopeIdx].FullName.ToString()), FNiagaraTypeDefinition::GetFloatDef(), false);
	}
}

void FHlslNiagaraTranslator::EnterStatsScope(FNiagaraStatScope StatScope, FString& OutHlsl)
{
	if (GbNiagaraScriptStatTracking)
	{
		int32 ScopeIdx = CompilationOutput.ScriptData.StatScopes.AddUnique(StatScope);
		OutHlsl += FString::Printf(TEXT("EnterStatScope(%d /**%s*/);\n"), ScopeIdx, *StatScope.FullName.ToString());
		StatScopeStack.Push(ScopeIdx);
	}
}

void FHlslNiagaraTranslator::ExitStatsScope(FString& OutHlsl)
{
	if (GbNiagaraScriptStatTracking)
	{
		int32 ScopeIdx = StatScopeStack.Pop();
		OutHlsl += FString::Printf(TEXT("ExitStatScope(/**%s*/);\n"), *CompilationOutput.ScriptData.StatScopes[ScopeIdx].FullName.ToString());
	}
}

FString FHlslNiagaraTranslator::GetCallstack()
{
	FString Callstack = CompileOptions.GetName();

	for (FFunctionContext& Ctx : FunctionContextStack)
	{
		Callstack += TEXT(".") + Ctx.Name;
	}

	return Callstack;
}

TArray<FGuid> FHlslNiagaraTranslator::GetCallstackGuids()
{
	TArray<FGuid> Callstack;
	for (FFunctionContext& Ctx : FunctionContextStack)
	{
		Callstack.Add(Ctx.Id);
	}

	return Callstack;
}

FString FHlslNiagaraTranslator::GeneratedConstantString(FVector4 Constant)
{
	TArray<FStringFormatArg> Args;
	Args.Add(LexToString(Constant.X));
	Args.Add(LexToString(Constant.Y));
	Args.Add(LexToString(Constant.Z));
	Args.Add(LexToString(Constant.W));
	return FString::Format(TEXT("float4({0}, {1}, {2}, {3})"), Args);
}

int32 FHlslNiagaraTranslator::AddUniformChunk(FString SymbolName, const FNiagaraTypeDefinition& Type)
{
	int32 Ret = CodeChunks.IndexOfByPredicate(
		[&](const FNiagaraCodeChunk& Chunk)
	{
		return Chunk.Mode == ENiagaraCodeChunkMode::Uniform && Chunk.SymbolName == SymbolName && Chunk.Type == Type;
	}
	);

	if (Ret == INDEX_NONE)
	{
		Ret = CodeChunks.AddDefaulted();
		FNiagaraCodeChunk& Chunk = CodeChunks[Ret];
		Chunk.SymbolName = GetSanitizedSymbolName(SymbolName);
		Chunk.Type = Type;

		if (UNiagaraScript::IsGPUScript(CompileOptions.TargetUsage))
		{
			if (Type == FNiagaraTypeDefinition::GetVec2Def())
			{
				Chunk.Type = FNiagaraTypeDefinition::GetVec4Def();
				Chunk.ComponentMask = TEXT(".xy");
			}
			else if (Type == FNiagaraTypeDefinition::GetVec3Def())
			{
				Chunk.Type = FNiagaraTypeDefinition::GetVec4Def();
				Chunk.ComponentMask = TEXT(".xyz");
			}
		}

		Chunk.Mode = ENiagaraCodeChunkMode::Uniform;

		ChunksByMode[(int32)ENiagaraCodeChunkMode::Uniform].Add(Ret);
	}
	return Ret;
}

int32 FHlslNiagaraTranslator::AddSourceChunk(FString SymbolName, const FNiagaraTypeDefinition& Type, bool bSanitize)
{
	int32 Ret = CodeChunks.IndexOfByPredicate(
		[&](const FNiagaraCodeChunk& Chunk)
	{
		return Chunk.Mode == ENiagaraCodeChunkMode::Source && Chunk.SymbolName == SymbolName && Chunk.Type == Type;
	}
	);

	if (Ret == INDEX_NONE)
	{
		Ret = CodeChunks.AddDefaulted();
		FNiagaraCodeChunk& Chunk = CodeChunks[Ret];
		Chunk.SymbolName = bSanitize ? GetSanitizedSymbolName(SymbolName) : SymbolName;
		Chunk.Type = Type;
		Chunk.Mode = ENiagaraCodeChunkMode::Source;

		ChunksByMode[(int32)ENiagaraCodeChunkMode::Source].Add(Ret);
	}
	return Ret;
}


int32 FHlslNiagaraTranslator::AddBodyComment(const FString& Comment)
{
	return AddBodyChunk(TEXT(""), Comment, FNiagaraTypeDefinition::GetIntDef(), false, false);
}

int32 FHlslNiagaraTranslator::AddBodyChunk(const FString& Value)
{
	return AddBodyChunk(TEXT(""), Value, FNiagaraTypeDefinition::GetIntDef(), INDEX_NONE, false, false);
}

int32 FHlslNiagaraTranslator::AddBodyChunk(FString SymbolName, FString Definition, const FNiagaraTypeDefinition& Type, TArray<int32>& SourceChunks, bool bDecl, bool bIsTerminated)
{
	check(CurrentBodyChunkMode == ENiagaraCodeChunkMode::Body || CurrentBodyChunkMode == ENiagaraCodeChunkMode::SpawnBody || CurrentBodyChunkMode == ENiagaraCodeChunkMode::UpdateBody);

	int32 Ret = CodeChunks.AddDefaulted();
	FNiagaraCodeChunk& Chunk = CodeChunks[Ret];
	Chunk.SymbolName = GetSanitizedSymbolName(SymbolName);
	Chunk.Definition = Definition;
	Chunk.Type = Type;
	Chunk.bDecl = bDecl;
	Chunk.bIsTerminated = bIsTerminated;
	Chunk.Mode = CurrentBodyChunkMode;
	Chunk.SourceChunks = SourceChunks;

	ChunksByMode[(int32)CurrentBodyChunkMode].Add(Ret);
	return Ret;
}



int32 FHlslNiagaraTranslator::AddBodyChunk(FString SymbolName, FString Definition, const FNiagaraTypeDefinition& Type, int32 SourceChunk, bool bDecl, bool bIsTerminated)
{
	check(CurrentBodyChunkMode == ENiagaraCodeChunkMode::Body || CurrentBodyChunkMode == ENiagaraCodeChunkMode::SpawnBody || CurrentBodyChunkMode == ENiagaraCodeChunkMode::UpdateBody);

	int32 Ret = CodeChunks.AddDefaulted();
	FNiagaraCodeChunk& Chunk = CodeChunks[Ret];
	Chunk.SymbolName = GetSanitizedSymbolName(SymbolName);
	Chunk.Definition = Definition;
	Chunk.Type = Type;
	Chunk.bDecl = bDecl;
	Chunk.bIsTerminated = bIsTerminated;
	Chunk.Mode = CurrentBodyChunkMode;
	Chunk.SourceChunks.Add(SourceChunk);

	ChunksByMode[(int32)CurrentBodyChunkMode].Add(Ret);
	return Ret;
}

int32 FHlslNiagaraTranslator::AddBodyChunk(FString SymbolName, FString Definition, const FNiagaraTypeDefinition& Type, bool bDecl, bool bIsTerminated)
{
	check(CurrentBodyChunkMode == ENiagaraCodeChunkMode::Body || CurrentBodyChunkMode == ENiagaraCodeChunkMode::SpawnBody || CurrentBodyChunkMode == ENiagaraCodeChunkMode::UpdateBody);

	int32 Ret = CodeChunks.AddDefaulted();
	FNiagaraCodeChunk& Chunk = CodeChunks[Ret];
	Chunk.SymbolName = GetSanitizedSymbolName(SymbolName);
	Chunk.Definition = Definition;
	Chunk.Type = Type;
	Chunk.bDecl = bDecl;
	Chunk.bIsTerminated = bIsTerminated;
	Chunk.Mode = CurrentBodyChunkMode;

	ChunksByMode[(int32)CurrentBodyChunkMode].Add(Ret);
	return Ret;
}

bool FHlslNiagaraTranslator::ShouldInterpolateParameter(const FNiagaraVariable& Parameter)
{
	//TODO: Some data driven method of deciding what parameters to interpolate and how to do it.
	//Possibly allow definition of a dynamic input for the interpolation?
	//With defaults for various types. Matrix=none, quat=slerp, everything else = Lerp.

	//We don't want to interpolate matrices. Possibly consider moving to an FTransform like representation rather than matrices which could be interpolated?
	if (Parameter.GetType() == FNiagaraTypeDefinition::GetMatrix4Def())
	{
		return false;
	}

	if (!Parameter.GetType().IsFloatPrimitive())
	{
		return false;
	}

	if (FNiagaraParameterMapHistory::IsRapidIterationParameter(Parameter))
	{
		return false;
	}

	//Skip interpolation for some system constants.
	if (Parameter == SYS_PARAM_ENGINE_DELTA_TIME ||
		Parameter == SYS_PARAM_ENGINE_INV_DELTA_TIME ||
		Parameter == SYS_PARAM_ENGINE_EXEC_COUNT ||
		Parameter == SYS_PARAM_EMITTER_SPAWNRATE ||
		Parameter == SYS_PARAM_EMITTER_SPAWN_INTERVAL ||
		Parameter == SYS_PARAM_EMITTER_INTERP_SPAWN_START_DT ||
		Parameter == SYS_PARAM_ENGINE_EMITTER_TOTAL_SPAWNED_PARTICLES ||
		Parameter == SYS_PARAM_ENGINE_EMITTER_SPAWN_COUNT_SCALE ||
		Parameter == SYS_PARAM_EMITTER_RANDOM_SEED ||
		Parameter == SYS_PARAM_ENGINE_SYSTEM_TICK_COUNT)
	{
		return false;
	}

	return true;
}

void FHlslNiagaraTranslator::UpdateStaticSwitchConstants(UEdGraphNode* Node)
{
	if (UNiagaraNodeStaticSwitch* SwitchNode = Cast<UNiagaraNodeStaticSwitch>(Node))
	{
		TArray<UNiagaraNodeStaticSwitch*> NodesToUpdate;
		NodesToUpdate.Add(SwitchNode);

		for (int i = 0; i < NodesToUpdate.Num(); i++)
		{
			SwitchNode->UpdateCompilerConstantValue(this);
			
			// also check direct upstream static switches, because they are otherwise skipped during the compilation and
			// might be evaluated without their values set correctly.
			TArray<UEdGraphPin*> InPins;
			SwitchNode->GetInputPins(InPins);
			for (UEdGraphPin* Pin : InPins)
			{
				if (UNiagaraNodeStaticSwitch* ConnectedNode = Cast<UNiagaraNodeStaticSwitch>(Pin->GetOwningNode()))
				{
					NodesToUpdate.AddUnique(ConnectedNode);
				}
			}
		}
	}
}

int32 FHlslNiagaraTranslator::GetRapidIterationParameter(const FNiagaraVariable& Parameter)
{
	if (!AddStructToDefinitionSet(Parameter.GetType()))
	{
		Error(FText::Format(LOCTEXT("GetRapidIterationParameterTypeFail_InvalidType", "Cannot handle type {0}! Variable: {1}"), Parameter.GetType().GetNameText(), FText::FromName(Parameter.GetName())), nullptr, nullptr);
		return INDEX_NONE;
	}

	int32 FuncParam = INDEX_NONE;
	if (GetFunctionParameter(Parameter, FuncParam))
	{
		Error(FText::Format(LOCTEXT("GetRapidIterationParameterFuncParamFail", "Variable: {0} cannot be a function parameter because it is a RapidIterationParameter type."), FText::FromName(Parameter.GetName())), nullptr, nullptr);
		return INDEX_NONE;
	}

	bool bIsCandidateForRapidIteration = false;
	if (ActiveHistoryForFunctionCalls.InTopLevelFunctionCall(CompileOptions.TargetUsage))
	{
		if (Parameter.GetType() != FNiagaraTypeDefinition::GetBoolDef() && !Parameter.GetType().IsEnum() && !Parameter.GetType().IsDataInterface())
		{
			bIsCandidateForRapidIteration = true;
		}
		else
		{
			Error(FText::Format(LOCTEXT("GetRapidIterationParameterTypeFail_UnsupportedInput", "Variable: {0} cannot be a RapidIterationParameter input node because it isn't a supported type {1}"), FText::FromName(Parameter.GetName()), Parameter.GetType().GetNameText()), nullptr, nullptr);
			return INDEX_NONE;
		}
	}
	else
	{
		Error(FText::Format(LOCTEXT("GetRapidIterationParameterInTopLevelFail", "Variable: {0} cannot be a RapidIterationParameter input node because it isn't in the top level of an emitter/system/particle graph."), FText::FromName(Parameter.GetName())), nullptr, nullptr);
		return INDEX_NONE;
	}

	FNiagaraVariable RapidIterationConstantVar = Parameter;

	int32 LastSetChunkIdx = INDEX_NONE;
	// Check to see if this is the first time we've encountered this node and it is a viable candidate for rapid iteration
	if (bIsCandidateForRapidIteration && TranslationOptions.bParameterRapidIteration)
	{

		// go ahead and make it into a constant variable..
		int32 OutputChunkId = INDEX_NONE;
		if (ParameterMapRegisterExternalConstantNamespaceVariable(Parameter, nullptr, INDEX_NONE, OutputChunkId, nullptr))
		{
			return OutputChunkId;
		}
	}
	else
	{
		int32 FoundIdx = TranslationOptions.OverrideModuleConstants.Find(RapidIterationConstantVar);
		if (FoundIdx != INDEX_NONE)
		{
			FString DebugConstantStr;
			int32 OutputChunkId = GetConstant(TranslationOptions.OverrideModuleConstants[FoundIdx], &DebugConstantStr);
			//UE_LOG(LogNiagaraEditor, Display, TEXT("Converted parameter %s to constant %s for script %s"), *RapidIterationConstantVar.GetName().ToString(), *DebugConstantStr, *CompileOptions.FullName);
			return OutputChunkId;
		}
	}

	return INDEX_NONE;
}

int32 FHlslNiagaraTranslator::GetParameter(const FNiagaraVariable& Parameter)
{

	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslTranslator_GetParameter);
	if (!AddStructToDefinitionSet(Parameter.GetType()))
	{
		Error(FText::Format(LOCTEXT("GetParameterFail", "Cannot handle type {0}! Variable: {1}"), Parameter.GetType().GetNameText(), FText::FromName(Parameter.GetName())), nullptr, nullptr);
	}

	if (Parameter == TRANSLATOR_PARAM_BEGIN_DEFAULTS)
	{
		if (CurrentDefaultPinTraversal.Num() != 0)
		{
			return ActiveStageIdx;
		}
		else
		{
			Error(FText::Format(LOCTEXT("InitializingDefaults", "Cannot have a {0} node if you are not tracing a default value from a Get node."), FText::FromName(Parameter.GetName())), nullptr, nullptr);
			return INDEX_NONE;
		}
	}

	int32 FuncParam = INDEX_NONE;
	const FNiagaraVariable* FoundKnownVariable = FNiagaraConstants::GetKnownConstant(Parameter.GetName(), false);

	if (FoundKnownVariable == nullptr && GetFunctionParameter(Parameter, FuncParam))
	{
		if (FuncParam != INDEX_NONE)
		{
			if (Parameter.GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				return FuncParam;
			}
			//If this is a valid function parameter, use that.
			FString SymbolName = TEXT("In_") + GetSanitizedSymbolName(Parameter.GetName().ToString());
			return AddSourceChunk(SymbolName, Parameter.GetType());
		}
	}

	if (FoundKnownVariable != nullptr)
	{
		FNiagaraVariable Var = *FoundKnownVariable;
		//Some special variables can be replaced directly with constants which allows for extra optimization in the compiler.
		if (GetLiteralConstantVariable(Var))
		{
			return GetConstant(Var);
		}
	}

	// We don't pass in the input node here (really there could be multiple nodes for the same parameter)
	// so we have to match up the input parameter map variable value through the pre-traversal histories 
	// so that we know which parameter map we are referencing.
	FString SymbolName = GetSanitizedSymbolName(Parameter.GetName().ToString());
	if (Parameter.GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
	{
		if (ParamMapHistories.Num() == 0)
		{
			return INDEX_NONE;
		}

		for (int32 i = 0; i < ParamMapHistories.Num(); i++)
		{
			// Double-check against the current output node we are tracing. Ignore any parameter maps
			// that don't include that node.
			if (CurrentParamMapIndices.Num() != 0 && !CurrentParamMapIndices.Contains(i))
			{
				continue;
			}

			for (int32 PinIdx = 0; PinIdx < ParamMapHistories[i].MapPinHistory.Num(); PinIdx++)
			{
				const UEdGraphPin* Pin = ParamMapHistories[i].MapPinHistory[PinIdx];

				if (Pin != nullptr)
				{
					UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(Pin->GetOwningNode());
					if (InputNode != nullptr && InputNode->Input == Parameter)
					{
						if (CurrentDefaultPinTraversal.Num() == 0)
						{
							if (!bInitializedDefaults)
							{
								InitializeParameterMapDefaults(i);
							}
						}

						return i;
					}
				}
			}
		}
		return INDEX_NONE;
	}

	//Not a in a function or not a valid function parameter so grab from the main uniforms.
	int32 OutputChunkIdx = INDEX_NONE;
	FNiagaraVariable OutputVariable = Parameter;
	if (FNiagaraParameterMapHistory::IsExternalConstantNamespace(OutputVariable, CompileOptions.TargetUsage, CompileOptions.GetTargetUsageBitmask()))
	{
		if (!ParameterMapRegisterExternalConstantNamespaceVariable(OutputVariable, nullptr, 0, OutputChunkIdx, nullptr))
		{
			OutputChunkIdx = INDEX_NONE;
		}
	}
	else
	{
		OutputVariable = FNiagaraParameterMapHistory::MoveToExternalConstantNamespaceVariable(OutputVariable, CompileOptions.TargetUsage);
		if (!ParameterMapRegisterExternalConstantNamespaceVariable(OutputVariable, nullptr, 0, OutputChunkIdx, nullptr))
		{
			OutputChunkIdx = INDEX_NONE;
		}
	}

	if (OutputChunkIdx == INDEX_NONE)
	{
		Error(FText::Format(LOCTEXT("GetParameterFail", "Cannot handle type {0}! Variable: {1}"), Parameter.GetType().GetNameText(), FText::FromName(Parameter.GetName())), nullptr, nullptr);
	}

	return OutputChunkIdx;
}

int32 FHlslNiagaraTranslator::GetConstant(const FNiagaraVariable& Constant, FString* DebugOutputValue)
{
	if (Constant.IsDataInterface())
	{
		return INDEX_NONE;
	}

	FString ConstantStr;
	FNiagaraVariable LiteralConstant = Constant;
	if (GetLiteralConstantVariable(LiteralConstant))
	{
		checkf(LiteralConstant.GetType() == FNiagaraTypeDefinition::GetBoolDef(), TEXT("Only boolean types are currently supported for literal constants."));
		ConstantStr = LiteralConstant.GetValue<bool>() ? TEXT("true") : TEXT("false");
	}
	else
	{
		ConstantStr = GenerateConstantString(Constant);
	}

	if (DebugOutputValue != nullptr)
	{
		*DebugOutputValue = ConstantStr;
	}
	if (ConstantStr.IsEmpty())
	{
		return INDEX_NONE;
	}
	return AddBodyChunk(GetUniqueSymbolName(TEXT("Constant")), ConstantStr, Constant.GetType());
}

int32 FHlslNiagaraTranslator::GetConstantDirect(float InConstantValue)
{
	FNiagaraVariable Constant(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Constant"));
	Constant.SetValue(InConstantValue);

	return GetConstant(Constant);
}

int32 FHlslNiagaraTranslator::GetConstantDirect(bool InConstantValue)
{
	FNiagaraVariable Constant(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Constant"));
	Constant.SetValue(InConstantValue);

	return GetConstant(Constant);
}

FString FHlslNiagaraTranslator::GenerateConstantString(const FNiagaraVariable& Constant)
{
	FNiagaraTypeDefinition Type = Constant.GetType();
	if (!AddStructToDefinitionSet(Type))
	{
		Error(FText::Format(LOCTEXT("GetConstantFail", "Cannot handle type {0}! Variable: {1}"), Type.GetNameText(), FText::FromName(Constant.GetName())), nullptr, nullptr);
	}
	FString ConstantStr = GetHlslDefaultForType(Type);
	if (Constant.IsDataAllocated())
	{
		if (Type == FNiagaraTypeDefinition::GetFloatDef())
		{
			float* ValuePtr = (float*)Constant.GetData();
			ConstantStr = FString::Printf(TEXT("%g"), *ValuePtr);
		}
		else if (Type == FNiagaraTypeDefinition::GetVec2Def())
		{
			float* ValuePtr = (float*)Constant.GetData();
			ConstantStr = FString::Printf(TEXT("float2(%g,%g)"), *ValuePtr, *(ValuePtr + 1));
		}
		else if (Type == FNiagaraTypeDefinition::GetVec3Def())
		{
			float* ValuePtr = (float*)Constant.GetData();
			ConstantStr = FString::Printf(TEXT("float3(%g,%g,%g)"), *ValuePtr, *(ValuePtr + 1), *(ValuePtr + 2));
		}
		else if (Type == FNiagaraTypeDefinition::GetVec4Def())
		{
			float* ValuePtr = (float*)Constant.GetData();
			ConstantStr = FString::Printf(TEXT("float4(%g,%g,%g,%g)"), *ValuePtr, *(ValuePtr + 1), *(ValuePtr + 2), *(ValuePtr + 3));
		}
		else if (Type == FNiagaraTypeDefinition::GetColorDef())
		{
			float* ValuePtr = (float*)Constant.GetData();
			ConstantStr = FString::Printf(TEXT("float4(%g,%g,%g,%g)"), *ValuePtr, *(ValuePtr + 1), *(ValuePtr + 2), *(ValuePtr + 3));
		}
		else if (Type == FNiagaraTypeDefinition::GetQuatDef())
		{
			float* ValuePtr = (float*)Constant.GetData();
			ConstantStr = FString::Printf(TEXT("float4(%g,%g,%g,%g)"), *ValuePtr, *(ValuePtr + 1), *(ValuePtr + 2), *(ValuePtr + 3));
		}
		else if (Type == FNiagaraTypeDefinition::GetIntDef() || Type.GetStruct() == FNiagaraTypeDefinition::GetIntStruct())
		{
			int32* ValuePtr = (int32*)Constant.GetData();
			ConstantStr = FString::Printf(TEXT("%d"), *ValuePtr);
		}
		else if (Type == FNiagaraTypeDefinition::GetBoolDef())
		{
			if (Constant.GetValue<FNiagaraBool>().IsValid() == false)
			{
				Error(FText::Format(LOCTEXT("StructContantsBoolInvalidError", "Boolean constant {0} is not set to explicit True or False. Defaulting to False."), FText::FromName(Constant.GetName())), nullptr, nullptr);
				ConstantStr = TEXT("false");
			}
			else
			{
				bool bValue = Constant.GetValue<FNiagaraBool>().GetValue();
				ConstantStr = bValue ? TEXT("true") : TEXT("false");
			}
		}
		else
		{
			//This is easily doable, just need to keep track of all structs used and define them as well as a ctor function signature with all values decomposed into float1/2/3/4 etc
			//Then call said function here with the same decomposition literal values.

			//For now lets allow this but just ignore the value and take the default ctor.
// 			Error(LOCTEXT("StructContantsUnsupportedError", "Constants of struct types are currently unsupported."), nullptr, nullptr);
// 			return FString();
			return ConstantStr;
		}
	}
	return ConstantStr;
}

void FHlslNiagaraTranslator::InitializeParameterMapDefaults(int32 ParamMapHistoryIdx)
{
	bInitializedDefaults = true;
	AddBodyComment(TEXT("//Begin Initialize Parameter Map Defaults"));
	check(ParamMapHistories.Num() == TranslationStages.Num());

	UniqueVars.Empty();
	UniqueVarToDefaultPin.Empty();
	UniqueVarToWriteToParamMap.Empty();
	UniqueVarToChunk.Empty();

	// First pass just use the current parameter map.
	{
		const FNiagaraParameterMapHistory& History = ParamMapHistories[ParamMapHistoryIdx];
		for (int32 i = 0; i < History.Variables.Num(); i++)
		{
			const FNiagaraVariable& Var = History.Variables[i];
			const FNiagaraVariable& AliasedVar = History.VariablesWithOriginalAliasesIntact[i];
			// Only add primary data set outputs at the top of the script if in a spawn script, otherwise they should be left alone.
			if (UNiagaraScript::IsSpawnScript(TranslationStages[ActiveStageIdx].ScriptUsage))
			{
				if (History.IsPrimaryDataSetOutput(AliasedVar, TranslationStages[ActiveStageIdx].ScriptUsage) &&
					!UniqueVars.Contains(Var))
				{
					UniqueVars.Add(Var);
					const UEdGraphPin* DefaultPin = History.GetDefaultValuePin(i);
					UniqueVarToDefaultPin.Add(Var, DefaultPin);
					UniqueVarToWriteToParamMap.Add(Var, true);
				}
			}
		}
	}

	// Only add primary data set outputs at the top of the script if in a spawn script, otherwise they should be left alone.
	// Above we added all the known from the spawn script, now let's add for all the others.
	if (UNiagaraScript::IsSpawnScript(TranslationStages[ActiveStageIdx].ScriptUsage))
	{
		// Go through all referenced parameter maps and pull in any variables that are 
		// in the primary data set output namespaces.
		for (int32 ParamMapIdx = 0; ParamMapIdx < OtherOutputParamMapHistories.Num(); ParamMapIdx++)
		{
			const FNiagaraParameterMapHistory& History = OtherOutputParamMapHistories[ParamMapIdx];
			for (int32 i = 0; i < History.Variables.Num(); i++)
			{
				const FNiagaraVariable& Var = History.Variables[i];
				const FNiagaraVariable& AliasedVar = History.VariablesWithOriginalAliasesIntact[i];
				if (History.IsPrimaryDataSetOutput(AliasedVar, TranslationStages[ActiveStageIdx].ScriptUsage) &&
					!UniqueVars.Contains(Var))
				{
					UniqueVars.Add(Var);
					const UEdGraphPin* DefaultPin = History.GetDefaultValuePin(i);
					UniqueVarToDefaultPin.Add(Var, DefaultPin);
					UniqueVarToWriteToParamMap.Add(Var, false);
				}
			}
		}

		// Now sort them into buckets: Defined by constants (write immediately), Defined as initial values (delay to end),
		// or defined by linkage or other script (defer to end if not originating from spawn, otherwise insert before first use)
		for (FNiagaraVariable& Var : UniqueVars)
		{
			const UEdGraphPin* DefaultPin = UniqueVarToDefaultPin.FindChecked(Var);
			bool bWriteToParamMapEntries = UniqueVarToWriteToParamMap.FindChecked(Var);
			int32 OutputChunkId = INDEX_NONE;
			
			UNiagaraScriptVariable* ScriptVariable = nullptr;
			if (DefaultPin) 
			{
				if (UNiagaraGraph* DefaultPinGraph = CastChecked<UNiagaraGraph>(DefaultPin->GetOwningNode()->GetGraph())) 
				{
					ScriptVariable = DefaultPinGraph->GetScriptVariable(Var);
				}
			}

			// During the initial pass, only support constants for the default pin and non-bound variables
			if (!FNiagaraParameterMapHistory::IsInitialValue(Var) && (DefaultPin == nullptr || DefaultPin->LinkedTo.Num() == 0) && !(ScriptVariable && ScriptVariable->DefaultMode == ENiagaraDefaultMode::Binding))
			{
				HandleParameterRead(ParamMapHistoryIdx, Var, DefaultPin, DefaultPin != nullptr ? Cast<UNiagaraNode>(DefaultPin->GetOwningNode()) : nullptr, OutputChunkId, nullptr, !bWriteToParamMapEntries);
				UniqueVarToChunk.Add(Var, OutputChunkId);
			}
			else if (FNiagaraParameterMapHistory::IsInitialValue(Var))
			{
				FNiagaraVariable SourceForInitialValue = FNiagaraParameterMapHistory::GetSourceForInitialValue(Var);
				if (!UniqueVars.Contains(SourceForInitialValue))
				{
					Error(FText::Format(LOCTEXT("MissingInitialValueSource", "Variable {0} is used, but its source variable {1} is not set!"), FText::FromName(Var.GetName()), FText::FromName(SourceForInitialValue.GetName())), nullptr, nullptr);
				}
				InitialNamespaceVariablesMissingDefault.Add(Var);
			}
			else
			{
				DeferredVariablesMissingDefault.Add(Var);
			}
		}
	}

	AddBodyComment(TEXT("//End Initialize Parameter Map Defaults"));
}

void FHlslNiagaraTranslator::Output(UNiagaraNodeOutput* OutputNode, const TArray<int32>& ComputedInputs)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslTranslator_Output);

	TArray<FNiagaraVariable> Attributes;
	TArray<int32> Inputs;

	// Build up the attribute list. We don't auto-expand parameter maps here.
	TArray<FNiagaraVariable> Outputs = OutputNode->GetOutputs();
	check(ComputedInputs.Num() == Outputs.Num());
	for (int32 PinIdx = 0; PinIdx < Outputs.Num(); PinIdx++)
	{
		Attributes.Add(Outputs[PinIdx]);
		Inputs.Add(ComputedInputs[PinIdx]);
	}

	if (FunctionCtx())
	{
		for (int32 i = 0; i < Attributes.Num(); ++i)
		{
			if (!AddStructToDefinitionSet(Attributes[i].GetType()))
			{
				Error(FText::Format(LOCTEXT("GetConstantFail", "Cannot handle type {0}! Variable: {1}"), Attributes[i].GetType().GetNameText(), FText::FromName(Attributes[i].GetName())), nullptr, nullptr);
			}

			if (Attributes[i].GetType() != FNiagaraTypeDefinition::GetParameterMapDef())
			{
				FString SymbolName = *GetSanitizedSymbolName((TEXT("Out_") + Attributes[i].GetName().ToString()));
				ENiagaraCodeChunkMode OldMode = CurrentBodyChunkMode;
				CurrentBodyChunkMode = ENiagaraCodeChunkMode::Body;
				AddBodyChunk(SymbolName, TEXT("{0}"), Attributes[i].GetType(), Inputs[i], false);
				CurrentBodyChunkMode = OldMode;
			}
		}
	}
	else
	{

		{
			check(InstanceWrite.CodeChunks.Num() == 0);//Should only hit one output node.

			FString DataSetAccessName = GetDataSetAccessSymbol(GetInstanceDataSetID(), INDEX_NONE, false);
			//First chunk for a write is always the condition pin.
			for (int32 i = 0; i < Attributes.Num(); ++i)
			{
				const FNiagaraVariable& Var = Attributes[i];

				if (!AddStructToDefinitionSet(Var.GetType()))
				{
					Error(FText::Format(LOCTEXT("GetConstantFail", "Cannot handle type {0}! Variable: {1}"), Var.GetType().GetNameText(), FText::FromName(Var.GetName())), nullptr, nullptr);
				}

				//DATASET TODO: add and treat input 0 as the 'valid' input for conditional write
				int32 Input = Inputs[i];


				if (Var.GetType() != FNiagaraTypeDefinition::GetParameterMapDef())
				{
					FNiagaraVariable VarNamespaced = FNiagaraParameterMapHistory::BasicAttributeToNamespacedAttribute(Var);
					FString ParameterMapInstanceName = GetParameterMapInstanceName(0);
					int32 ChunkIdx = AddBodyChunk(ParameterMapInstanceName + TEXT(".") + GetSanitizedSymbolName(VarNamespaced.GetName().ToString()), TEXT("{0}"), VarNamespaced.GetType(), Input, false);

					// Make sure that we end up in the list of Attributes that have been written to by this script.
					if (ParamMapDefinedAttributesToUniformChunks.Find(Var.GetName()) == nullptr)
					{
						ParamMapDefinedAttributesToUniformChunks.Add(Var.GetName(), Input);
						ParamMapDefinedAttributesToNamespaceVars.Add(Var.GetName(), VarNamespaced);
					}

					InstanceWrite.Variables.AddUnique(VarNamespaced);
					InstanceWrite.CodeChunks.Add(ChunkIdx);
				}
				else
				{
					InstanceWrite.Variables.AddUnique(Var);
				}
			}
		}
	}
}

int32 FHlslNiagaraTranslator::GetAttribute(const FNiagaraVariable& Attribute)
{
	if (!AddStructToDefinitionSet(Attribute.GetType()))
	{
		Error(FText::Format(LOCTEXT("GetConstantFail", "Cannot handle type {0}! Variable: {1}"), Attribute.GetType().GetNameText(), FText::FromName(Attribute.GetName())), nullptr, nullptr);
	}

	if (TranslationStages.Num() > 1 && UNiagaraScript::IsParticleSpawnScript(TranslationStages[0].ScriptUsage) && (Attribute.GetName() != TEXT("Particles.UniqueID")))
	{
		if (ActiveStageIdx > 0)
		{
			//This is a special case where we allow the grabbing of attributes in the update section of an interpolated spawn script.
			//But we return the results of the previously ran spawn script.
			FString ParameterMapInstanceName = GetParameterMapInstanceName(0);

			FNiagaraVariable NamespacedVar = Attribute;
			FString SymbolName = *(ParameterMapInstanceName + TEXT(".") + GetSanitizedSymbolName(NamespacedVar.GetName().ToString()));
			return AddSourceChunk(SymbolName, Attribute.GetType());
		}
		else
		{
			Error(LOCTEXT("AttrReadInSpawnError", "Cannot read attribute in a spawn script as it's value is not yet initialized."), nullptr, nullptr);
			return INDEX_NONE;
		}
	}
	else
	{
		// NOTE(mv): Explicitly allow reading from Particles.UniqueID, as it is an engine managed variable and 
		//           is written to before Simulate() in the SpawnScript...
		// TODO(mv): Also allow Particles.ID for the same reasons?
		CompilationOutput.ScriptData.DataUsage.bReadsAttributeData |= (Attribute.GetName() != TEXT("Particles.UniqueID"));

		int32 Chunk = INDEX_NONE;
		if (!ParameterMapRegisterNamespaceAttributeVariable(Attribute, nullptr, 0, Chunk))
		{
			Error(FText::Format(LOCTEXT("AttrReadError", "Cannot read attribute {0} {1}."), Attribute.GetType().GetNameText(), FText::FromString(*Attribute.GetName().ToString())), nullptr, nullptr);
			return INDEX_NONE;
		}
		return Chunk;
	}
}

FString FHlslNiagaraTranslator::GetDataSetAccessSymbol(FNiagaraDataSetID DataSet, int32 IndexChunk, bool bRead)
{
	FString Ret = TEXT("\tContext.") + DataSet.Name.ToString() + (bRead ? TEXT("Read") : TEXT("Write"));
	/*
	FString Ret = TEXT("Context.") + DataSet.Name.ToString();
	Ret += bRead ? TEXT("Read") : TEXT("Write");
	Ret += IndexChunk != INDEX_NONE ? TEXT("_") + CodeChunks[IndexChunk].SymbolName : TEXT("");*/
	return Ret;
}

void FHlslNiagaraTranslator::ParameterMapForBegin(UNiagaraNodeParameterMapFor* ForNode, int32 IterationCount)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslTranslator_MapForBegin);

	AddBodyChunk(TEXT(""), TEXT("for(int index = 0; index < {0}; ++index)\n\t{"), FNiagaraTypeDefinition::GetIntDef(), IterationCount, false, false);
}

void FHlslNiagaraTranslator::ParameterMapForEnd(UNiagaraNodeParameterMapFor* ForNode)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslTranslator_MapForEnd);

	AddBodyChunk(TEXT(""), TEXT("}"), FNiagaraTypeDefinition::GetIntDef(), false, false);
}

void FHlslNiagaraTranslator::ParameterMapSet(UNiagaraNodeParameterMapSet* SetNode, TArray<FCompiledPin>& Inputs, TArray<int32>& Outputs)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslTranslator_MapSet);

	Outputs.SetNum(1);

	FString ParameterMapInstanceName = TEXT("Context.Map");

	// There is only one output pin for a set node, the parameter map must 
	// continue to route through it.
	if (!SetNode->IsNodeEnabled())
	{
		if (Inputs.Num() >= 1)
		{
			Outputs[0] = Inputs[0].CompilationIndex;
		}
		return;
	}

	int32 ParamMapHistoryIdx = INDEX_NONE;
	for (int32 i = 0; i < Inputs.Num(); i++)
	{
		int32 Input = Inputs[i].CompilationIndex;
		if (i == 0) // This is the parameter map
		{
			Outputs[0] = Inputs[0].CompilationIndex;
			ParamMapHistoryIdx = Inputs[0].CompilationIndex;
			ParameterMapInstanceName = GetParameterMapInstanceName(ParamMapHistoryIdx);

			if (ParamMapHistoryIdx == -1)
			{
				Error(LOCTEXT("NoParamMapIdxForInput", "Cannot find parameter map for input!"), SetNode, nullptr);
				Outputs[0] = INDEX_NONE;
					return;
				}
			continue;
		}
		else // These are the pins that we are setting on the parameter map.
		{
			FNiagaraVariable Var = Schema->PinToNiagaraVariable(Inputs[i].Pin, false);

			if (!AddStructToDefinitionSet(Var.GetType()))
			{
				Error(FText::Format(LOCTEXT("ParameterMapSetTypeError", "Cannot handle type {0}! Variable: {1}"), Var.GetType().GetNameText(), FText::FromName(Var.GetName())), nullptr, nullptr);
			}

			FString VarName = Var.GetName().ToString();
			if (FNiagaraParameterMapHistory::IsExternalConstantNamespace(Var, CompileOptions.TargetUsage, CompileOptions.GetTargetUsageBitmask()))
			{
				Error(FText::Format(LOCTEXT("SetSystemConstantFail", "Cannot Set external constant, Type: {0} Variable: {1}"), Var.GetType().GetNameText(), FText::FromName(Var.GetName())), SetNode, nullptr);
				continue;
			}

			Var = ActiveHistoryForFunctionCalls.ResolveAliases(Var);
			const FNiagaraVariable* ConstantVar = FNiagaraConstants::GetKnownConstant(Var.GetName(), false);
			if (ConstantVar != nullptr && ConstantVar->GetType() != Var.GetType())
			{
				Error(FText::Format(LOCTEXT("MismatchedConstantTypes", "Variable {0} is a system constant, but its type is different! {1} != {2}"), FText::FromName(Var.GetName()),
					ConstantVar->GetType().GetNameText(), Var.GetType().GetNameText()), nullptr, nullptr);
			}

			if (FNiagaraConstants::IsEngineManagedAttribute(Var))
			{
				Error(FText::Format(LOCTEXT("SettingSystemAttr", "Variable {0} is an engine managed particle attribute and cannot be set directly."), FText::FromName(Var.GetName())), nullptr, nullptr);
				continue;
			}

			if (ParamMapHistoryIdx < ParamMapHistories.Num())
			{
				int32 VarIdx = ParamMapHistories[ParamMapHistoryIdx].FindVariableByName(Var.GetName());
				if (VarIdx != INDEX_NONE && VarIdx < ParamMapSetVariablesToChunks[ParamMapHistoryIdx].Num())
				{
					ParamMapSetVariablesToChunks[ParamMapHistoryIdx][VarIdx] = Inputs[i].CompilationIndex;
					ParamMapDefinedAttributesToNamespaceVars.FindOrAdd(Var.GetName()) = Var;
				}
			}

			if (Var.IsDataInterface())
			{
				if (CompileOptions.TargetUsage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated && TranslationStages[ActiveStageIdx].ScriptUsage == ENiagaraScriptUsage::ParticleUpdateScript)
				{
					// We don't want to add writes for particle update data interface parameters in both interpolated spawn and update, so skip them when processing the update stage of the 
					// interpolated spawn script.  We don't skip the writes when compiling the particle update script because it's not recompiled when the interpolated spawn flag is changed
					// and this would result in missing data interfaces if interpolated spawn was turned off.
					continue;
				}

				bool bAllowDataInterfaces = true;
				if (ParamMapHistoryIdx < ParamMapHistories.Num() && ParamMapHistories[ParamMapHistoryIdx].IsPrimaryDataSetOutput(Var, CompileOptions.TargetUsage, bAllowDataInterfaces))
				{
					if (Input < 0 || Input >= CompilationOutput.ScriptData.DataInterfaceInfo.Num())
					{
						Error(FText::Format(LOCTEXT("ParameterMapDataInterfaceNotFoundErrorFormat", "Data interface could not be found for parameter map set.  Paramter: {0}"), FText::FromName(Var.GetName())), SetNode, Inputs[i].Pin);
						continue;
					}

					FName UsageName;
					if (FNiagaraParameterMapHistory::IsAliasedEmitterParameter(Var))
					{
						FNiagaraVariable AliasedVar = ActiveHistoryForFunctionCalls.ResolveAliases(Var);
						UsageName = AliasedVar.GetName();
					}
					else
					{
						UsageName = Var.GetName();
					}

					FNiagaraScriptDataInterfaceCompileInfo& Info = CompilationOutput.ScriptData.DataInterfaceInfo[Input];
					if (Info.RegisteredParameterMapWrite == NAME_None)
					{
						Info.RegisteredParameterMapWrite = UsageName;
					}
					else
					{
						Error(FText::Format(LOCTEXT("ExternalDataInterfaceAssignedToMultipleParameters", "The data interface named {0} was added to a parameter map multiple times which isn't supported.  First usage: {1} Invalid usage:{2}"),
							FText::FromName(Info.Name), FText::FromName(Info.RegisteredParameterMapWrite), FText::FromName(UsageName)), SetNode, Inputs[i].Pin);
						continue;
					}
				}
			}
			else
			{
				AddBodyChunk(ParameterMapInstanceName + TEXT(".") + GetSanitizedSymbolName(Var.GetName().ToString()), TEXT("{0}"), Var.GetType(), Input, false);
			}
		}
	}

}

FString FHlslNiagaraTranslator::GetUniqueEmitterName() const
{
	if (CompileOptions.TargetUsage == ENiagaraScriptUsage::SystemSpawnScript || CompileOptions.TargetUsage == ENiagaraScriptUsage::SystemUpdateScript)
	{
		return CompileData->GetUniqueEmitterName();
	}
	else
	{
		return TEXT("Emitter");
	}
}

bool FHlslNiagaraTranslator::IsBulkSystemScript() const
{
	return (CompileOptions.TargetUsage == ENiagaraScriptUsage::SystemSpawnScript || CompileOptions.TargetUsage == ENiagaraScriptUsage::SystemUpdateScript);
}

bool FHlslNiagaraTranslator::IsSpawnScript() const
{
	for (int32 i = 0; i < TranslationStages.Num(); i++)
	{
		if (UNiagaraScript::IsSpawnScript(TranslationStages[i].ScriptUsage))
		{
			return true;
		}
	}
	return false;
}


bool FHlslNiagaraTranslator::RequiresInterpolation() const
{
	for (int32 i = 0; i < TranslationStages.Num(); i++)
	{
		if (TranslationStages[i].bInterpolatePreviousParams)
		{
			return true;
		}
	}
	return false;
}

bool FHlslNiagaraTranslator::GetLiteralConstantVariable(FNiagaraVariable& OutVar) const
{
	if (FNiagaraParameterMapHistory::IsInNamespace(OutVar, PARAM_MAP_EMITTER_STR) || FNiagaraParameterMapHistory::IsInNamespace(OutVar, PARAM_MAP_SYSTEM_STR))
	{
		FNiagaraVariable ResolvedVar = ActiveHistoryForFunctionCalls.ResolveAliases(OutVar);
		if (OutVar == FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Emitter.Localspace")))
		{
			bool bEmitterLocalSpace = CompileOptions.AdditionalDefines.Contains(ResolvedVar.GetName().ToString());
			OutVar.SetValue(bEmitterLocalSpace ? FNiagaraBool(true) : FNiagaraBool(false));
			return true;
		}
		if (OutVar == FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Emitter.Determinism")))
		{
			bool bEmitterDeterminism = CompileOptions.AdditionalDefines.Contains(ResolvedVar.GetName().ToString());
			OutVar.SetValue(bEmitterDeterminism ? FNiagaraBool(true) : FNiagaraBool(false));
			return true;
		}
		if (OutVar == FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Emitter.OverrideGlobalSpawnCountScale")))
		{
			bool bOverrideGlobalSpawnCountScale = CompileOptions.AdditionalDefines.Contains(ResolvedVar.GetName().ToString());
			OutVar.SetValue(bOverrideGlobalSpawnCountScale ? FNiagaraBool(true) : FNiagaraBool(false));
			return true;
		}
		if (OutVar == FNiagaraVariable(FNiagaraTypeDefinition::GetSimulationTargetEnum(), TEXT("Emitter.SimulationTarget")))
		{
			FNiagaraInt32 EnumValue;
			EnumValue.Value = (uint8) CompilationTarget;
			OutVar.SetValue(EnumValue);
			return true;
		}
	}
	else if (OutVar == FNiagaraVariable(FNiagaraTypeDefinition::GetScriptUsageEnum(), TEXT("Script.Usage")))
	{
		FNiagaraInt32 EnumValue;
		EnumValue.Value = (uint8)GetCurrentUsage();
		OutVar.SetValue(EnumValue);
		return true;
	}
	return false;
}

bool FHlslNiagaraTranslator::ParameterMapRegisterExternalConstantNamespaceVariable(FNiagaraVariable InVariable, UNiagaraNode* InNode, int32 InParamMapHistoryIdx, int32& Output, const UEdGraphPin* InDefaultPin)
{
	InVariable = ActiveHistoryForFunctionCalls.ResolveAliases(InVariable);
	FString VarName = InVariable.GetName().ToString();
	FString SymbolName = GetSanitizedSymbolName(VarName);
	FString FlattenedName = SymbolName.Replace(TEXT("."), TEXT("_"));
	FString ParameterMapInstanceName = GetParameterMapInstanceName(InParamMapHistoryIdx);

	Output = INDEX_NONE;
	if (InVariable.IsValid())
	{
		// We don't really want system delta time or inverse system delta time in a spawn script. It leads to trouble.
		if (TranslationStages.Num() > 0 && UNiagaraScript::IsParticleSpawnScript(TranslationStages[ActiveStageIdx].ScriptUsage))
		{
			if (InVariable == SYS_PARAM_ENGINE_DELTA_TIME || InVariable == SYS_PARAM_ENGINE_INV_DELTA_TIME)
			{
				Warning(FText::Format(LOCTEXT("GetParameterInvalidParam", "Cannot call system variable {0} in a spawn script! It is invalid."), FText::FromName(InVariable.GetName())), nullptr, nullptr);
				Output = GetConstantDirect(0.0f);
				return true;
			}
		}

		bool bMissingParameter = false;
		UNiagaraParameterCollection* Collection = nullptr;
		if (InParamMapHistoryIdx >= 0)
		{
			Collection = ParamMapHistories[InParamMapHistoryIdx].IsParameterCollectionParameter(InVariable, bMissingParameter);
			if (Collection && bMissingParameter)
			{
				Error(FText::Format(LOCTEXT("MissingNPCParameterError", "Parameter named {0} of type {1} was not found in Parameter Collection {2}"),
					FText::FromName(InVariable.GetName()), InVariable.GetType().GetNameText(), FText::FromString(Collection->GetFullName())), InNode, InDefaultPin);
				return false;
			}
		}

		bool bIsDataInterface = InVariable.GetType().IsDataInterface();
		const FString* EmitterAlias = ActiveHistoryForFunctionCalls.GetEmitterAlias();
		bool bIsPerInstanceBulkSystemParam = IsBulkSystemScript() && !bIsDataInterface && (FNiagaraParameterMapHistory::IsUserParameter(InVariable) || FNiagaraParameterMapHistory::IsPerInstanceEngineParameter(InVariable, EmitterAlias != nullptr ? *EmitterAlias : TEXT("Emitter")));

		if (!bIsPerInstanceBulkSystemParam)
		{
			int32 UniformIdx = 0;
			int32 UniformChunk = 0;

			if (false == ParamMapDefinedSystemVarsToUniformChunks.Contains(InVariable.GetName()))
			{
				FString SymbolNameDefined = FlattenedName;

				if (InVariable.GetType().IsDataInterface())
				{
					UNiagaraDataInterface* DataInterface = nullptr;
					if (Collection)
					{
						DataInterface = Collection->GetDefaultInstance()->GetParameterStore().GetDataInterface(InVariable);
						if (DataInterface == nullptr)
						{
							Error(FText::Format(LOCTEXT("ParameterCollectionDataInterfaceNotFoundErrorFormat", "Data interface named {0} of type {1} was not found in Parameter Collection {2}"),
								FText::FromName(InVariable.GetName()), InVariable.GetType().GetNameText(), FText::FromString(Collection->GetFullName())), InNode, InDefaultPin);
							return false;
						}
					}
					else
					{
						UObject*const* Obj = CompileData->CDOs.Find(const_cast<UClass*>(InVariable.GetType().GetClass()));
						check(Obj != nullptr);
						DataInterface = CastChecked<UNiagaraDataInterface>(*Obj);
					}
					if (ensure(DataInterface))
					{
						Output = RegisterDataInterface(InVariable, DataInterface, true, true);
						return true;
					}
				}
				if (!InVariable.IsDataAllocated() && !InDefaultPin)
				{
					FNiagaraEditorUtilities::ResetVariableToDefaultValue(InVariable);
				}
				else if (!InVariable.IsDataAllocated())
				{
					FNiagaraVariable Var = Schema->PinToNiagaraVariable(InDefaultPin, true);
					FNiagaraEditorUtilities::ResetVariableToDefaultValue(InVariable);
					if (Var.IsDataAllocated() && Var.GetData() != nullptr)
					{
						InVariable.SetData(Var.GetData());
					}
				}

				if (InVariable.GetAllocatedSizeInBytes() != InVariable.GetSizeInBytes())
				{
					Error(FText::Format(LOCTEXT("GetParameterUnsetParam", "Variable {0} hasn't had its default value set. Required Bytes: {1} vs Allocated Bytes: {2}"), FText::FromName(InVariable.GetName()), FText::AsNumber(InVariable.GetType().GetSize()), FText::AsNumber(InVariable.GetSizeInBytes())), nullptr, nullptr);
				}

				if ( IsVariableInUniformBuffer(InVariable) )
				{
					CompilationOutput.ScriptData.Parameters.SetOrAdd(InVariable);
				}

				UniformIdx = ChunksByMode[(int32)ENiagaraCodeChunkMode::Uniform].Num();

				UniformChunk = AddUniformChunk(SymbolNameDefined, InVariable.GetType());
				ParamMapDefinedSystemVarsToUniformChunks.Add(InVariable.GetName(), UniformIdx);
				ParamMapDefinedSystemToNamespaceVars.Add(InVariable.GetName(), InVariable);
			}
			else
			{
				UniformIdx = ParamMapDefinedSystemVarsToUniformChunks.FindChecked(InVariable.GetName());
				UniformChunk = ChunksByMode[(int32)ENiagaraCodeChunkMode::Uniform][UniformIdx];
			}
			static const auto UseShaderStagesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.UseShaderStages"));
			// Avoid overriding the register indices
			if ((UseShaderStagesCVar->GetInt()==1 && !FNiagaraParameterMapHistory::IsInNamespace(InVariable, PARAM_MAP_INDICES_STR)) || UseShaderStagesCVar->GetInt()==0)
			{
				//Add this separately as the same uniform can appear in the pre sim chunks more than once in different param maps.
				MainPreSimulateChunks.AddUnique(FString::Printf(TEXT("%s.%s = %s;"), *ParameterMapInstanceName, *GetSanitizedSymbolName(VarName), *GetCodeAsSource(UniformChunk)));
			}
		}
		else if (bIsPerInstanceBulkSystemParam && !ExternalVariablesForBulkUsage.Contains(InVariable))
		{
			ExternalVariablesForBulkUsage.Add(InVariable);
		}
		Output = AddSourceChunk(ParameterMapInstanceName + TEXT(".") + SymbolName, InVariable.GetType());
		return true;
	}

	if (Output == INDEX_NONE)
	{
		Error(FText::Format(LOCTEXT("GetSystemConstantFail", "Unknown System constant, Type: {0} Variable: {1}"), InVariable.GetType().GetNameText(), FText::FromName(InVariable.GetName())), InNode, nullptr);
	}
	return false;
}


bool FHlslNiagaraTranslator::ParameterMapRegisterUniformAttributeVariable(const FNiagaraVariable& InVariable, UNiagaraNode* InNode, int32 InParamMapHistoryIdx, int32& Output)
{
	FNiagaraVariable NewVar = FNiagaraParameterMapHistory::BasicAttributeToNamespacedAttribute(InVariable);
	if (NewVar.IsValid())
	{
		return ParameterMapRegisterNamespaceAttributeVariable(NewVar, InNode, InParamMapHistoryIdx, Output);
	}
	return false;
}

void FHlslNiagaraTranslator::ValidateParticleIDUsage()
{
	if (CompileOptions.AdditionalDefines.Contains(TEXT("RequiresPersistentIDs")))
	{
		// persistent IDs are active and can be safely used as inputs
		return;
	}
	FName particleIDName(TEXT("Particles.ID"));
	for (FNiagaraParameterMapHistory& History : ParamMapHistories)
	{
		for (const FNiagaraVariable& Variable : History.Variables)
		{
			if (Variable.GetName() == particleIDName)
			{
				Error(LOCTEXT("PersistentIDActivationFail", "Before the Particles.ID parameter can be used, the 'Requires persistent IDs' option has to be activated in the emitter properties. Note that this comes with additional memory and CPU costs."), nullptr, nullptr);
			}
		}
	}
}

bool FHlslNiagaraTranslator::ParameterMapRegisterNamespaceAttributeVariable(const FNiagaraVariable& InVariable, UNiagaraNode* InNode, int32 InParamMapHistoryIdx, int32& Output)
{
	FString VarName = InVariable.GetName().ToString();
	FString SymbolNameNamespaced = GetSanitizedSymbolName(VarName);
	FString ParameterMapInstanceName = GetParameterMapInstanceName(InParamMapHistoryIdx);
	FNiagaraVariable NamespaceVar = InVariable;

	Output = INDEX_NONE;
	FNiagaraVariable BasicVar = FNiagaraParameterMapHistory::ResolveAsBasicAttribute(InVariable);
	if (BasicVar.IsValid())
	{
		if (false == ParamMapDefinedAttributesToUniformChunks.Contains(BasicVar.GetName()))
		{
			FString SymbolNameDefined = GetSanitizedSymbolName(BasicVar.GetName().ToString());
			int32 UniformChunk = INDEX_NONE;
			int32 Idx = InstanceRead.Variables.Find(NamespaceVar);
			if (Idx != INDEX_NONE)
			{
				UniformChunk = InstanceRead.CodeChunks[Idx];
			}
			else
			{
				UniformChunk = AddSourceChunk(ParameterMapInstanceName + TEXT(".") + SymbolNameNamespaced, NamespaceVar.GetType());
				InstanceRead.CodeChunks.Add(UniformChunk);
				InstanceRead.Variables.Add(NamespaceVar);
			}

			ParamMapDefinedAttributesToUniformChunks.Add(BasicVar.GetName(), UniformChunk);
			ParamMapDefinedAttributesToNamespaceVars.Add(BasicVar.GetName(), NamespaceVar);
		}
		Output = AddSourceChunk(ParameterMapInstanceName + TEXT(".") + SymbolNameNamespaced, NamespaceVar.GetType());
		return true;
	}

	if (Output == INDEX_NONE)
	{
		Error(FText::Format(LOCTEXT("GetEmitterUniformFail", "Unknown Emitter Uniform Variable, Type: {0} Variable: {1}"), InVariable.GetType().GetNameText(), FText::FromName(InVariable.GetName())), InNode, nullptr);
	}
	return false;
}

FString FHlslNiagaraTranslator::GetParameterMapInstanceName(int32 ParamMapHistoryIdx)
{
	FString ParameterMapInstanceName;

	if (TranslationStages.Num() > ActiveStageIdx)
	{
		ParameterMapInstanceName = FString::Printf(TEXT("Context.%s"), *TranslationStages[ActiveStageIdx].PassNamespace);
	}
	return ParameterMapInstanceName;
}

void FHlslNiagaraTranslator::Emitter(class UNiagaraNodeEmitter* EmitterNode, TArray<int32>& Inputs, TArray<int32>& Outputs)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_Emitter);

	// Just pass through the input parameter map pin if the node isn't enabled...
	if (!EmitterNode->IsNodeEnabled())
	{
		TArray<UEdGraphPin*> OutputPins;
		EmitterNode->GetOutputPins(OutputPins);

		Outputs.SetNum(OutputPins.Num());
		for (int32 i = 0; i < OutputPins.Num(); i++)
		{
			Outputs[i] = INDEX_NONE;
		}
		if (Inputs.Num() >= 1)
		{
			Outputs[0] = Inputs[0];
		}
		return;
	}

	FNiagaraFunctionSignature Signature;
	UNiagaraScriptSource* Source = EmitterNode->GetScriptSource();
	if (Source == nullptr)
	{
		Error(LOCTEXT("FunctionCallNonexistantScriptSource", "Emitter call missing ScriptSource"), EmitterNode, nullptr);
		return;
	}

	// We need the generated string to generate the proper signature for now.
	FString EmitterUniqueName = EmitterNode->GetEmitterUniqueName();

	ENiagaraScriptUsage ScriptUsage = EmitterNode->GetUsage();
	FString Name = EmitterNode->GetName();
	FString FullName = EmitterNode->GetFullName();

	FName StatName = *EmitterUniqueName;
	EnterStatsScope(FNiagaraStatScope(StatName, StatName));

	TArray<UEdGraphPin*> CallOutputs;
	TArray<UEdGraphPin*> CallInputs;
	EmitterNode->GetOutputPins(CallOutputs);
	EmitterNode->GetInputPins(CallInputs);


	if (Inputs.Num() == 0 || Schema->PinToNiagaraVariable(CallInputs[0]).GetType() != FNiagaraTypeDefinition::GetParameterMapDef())
	{
		Error(LOCTEXT("EmitterMissingParamMap", "Emitter call missing ParameterMap input pin!"), EmitterNode, nullptr);
		return;
	}


	int32 ParamMapHistoryIdx = Inputs[0];
	if (ParamMapHistoryIdx == INDEX_NONE)
	{
		Error(LOCTEXT("EmitterMissingParamMapIndex", "Emitter call missing valid ParameterMap index!"), EmitterNode, nullptr);
		return;
	}
	ActiveHistoryForFunctionCalls.EnterEmitter(EmitterUniqueName, EmitterNode);

	// Clear out the parameter map writes to emitter module parameters as they should not be shared across emitters.
	if (ParamMapHistoryIdx != -1 && ParamMapHistoryIdx < ParamMapHistories.Num())
	{
		for (int32 i = 0; i < ParamMapHistories[ParamMapHistoryIdx].Variables.Num(); i++)
		{
			check(ParamMapHistories[ParamMapHistoryIdx].VariablesWithOriginalAliasesIntact.Num() > i);
			FNiagaraVariable Var = ParamMapHistories[ParamMapHistoryIdx].VariablesWithOriginalAliasesIntact[i];
			if (FNiagaraParameterMapHistory::IsAliasedModuleParameter(Var))
			{
				ParamMapSetVariablesToChunks[ParamMapHistoryIdx][i] = INDEX_NONE;
			}
		}
	}

	// We act like a function call here as the semantics are identical.
	RegisterFunctionCall(ScriptUsage, Name, FullName, EmitterNode->NodeGuid, Source, Signature, false, FString(), Inputs, CallInputs, CallOutputs, Signature);
	GenerateFunctionCall(ScriptUsage, Signature, Inputs, Outputs);

	// Clear out the parameter map writes to emitter module parameters as they should not be shared across emitters.
	if (ParamMapHistoryIdx != -1 && ParamMapHistoryIdx < ParamMapHistories.Num())
	{
		for (int32 i = 0; i < ParamMapHistories[ParamMapHistoryIdx].Variables.Num(); i++)
		{
			check(ParamMapHistories[ParamMapHistoryIdx].VariablesWithOriginalAliasesIntact.Num() > i);
			FNiagaraVariable Var = ParamMapHistories[ParamMapHistoryIdx].VariablesWithOriginalAliasesIntact[i];
			if (ActiveHistoryForFunctionCalls.IsInEncounteredFunctionNamespace(Var) || FNiagaraParameterMapHistory::IsAliasedModuleParameter(Var))
			{
				ParamMapSetVariablesToChunks[ParamMapHistoryIdx][i] = INDEX_NONE;
			}
		}
	}
	ActiveHistoryForFunctionCalls.ExitEmitter(EmitterUniqueName, EmitterNode);

	ExitStatsScope();
}

void FHlslNiagaraTranslator::ParameterMapGet(UNiagaraNodeParameterMapGet* GetNode, TArray<int32>& Inputs, TArray<int32>& Outputs)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_MapGet);

	TArray<UEdGraphPin*> OutputPins;
	GetNode->GetOutputPins(OutputPins);

	// Push out invalid values for all output pins if the node is disabled.
	if (!GetNode->IsNodeEnabled())
	{
		Outputs.SetNum(OutputPins.Num());
		for (int32 i = 0; i < OutputPins.Num(); i++)
		{
			Outputs[i] = INDEX_NONE;
		}
		return;
	}

	TArray<UEdGraphPin*> InputPins;
	GetNode->GetInputPins(InputPins);

	int32 ParamMapHistoryIdx = Inputs[0];

	Outputs.SetNum(OutputPins.Num());

	if (ParamMapHistoryIdx == -1)
	{
		Error(LOCTEXT("NoParamMapIdxForInput", "Cannot find parameter map for input!"), GetNode, nullptr);
		for (int32 i = 0; i < Outputs.Num(); i++)
		{
			Outputs[i] = INDEX_NONE;
			return;
		}
	}
	else if (ParamMapHistoryIdx >= ParamMapHistories.Num())
	{
		Error(FText::Format(LOCTEXT("InvalidParamMapIdxForInput", "Invalid parameter map index for input {0} of {1}!"), ParamMapHistoryIdx, ParamMapHistories.Num()), GetNode, nullptr);
		for (int32 i = 0; i < Outputs.Num(); i++)
		{
			Outputs[i] = INDEX_NONE;
			return;
		}
	}

	FString ParameterMapInstanceName = GetParameterMapInstanceName(ParamMapHistoryIdx);

	for (int32 i = 0; i < OutputPins.Num(); i++)
	{
		if (GetNode->IsAddPin(OutputPins[i]))
		{
			// Not a real pin.
			Outputs[i] = INDEX_NONE;
			continue;
		}
		else // These are the pins that we are getting off the parameter map.
		{
			FNiagaraTypeDefinition OutputTypeDefinition = Schema->PinToTypeDefinition(OutputPins[i]);
			bool bNeedsValue =
				OutputTypeDefinition != FNiagaraTypeDefinition::GetParameterMapDef() &&
				OutputTypeDefinition.IsDataInterface() == false;
			FNiagaraVariable Var = Schema->PinToNiagaraVariable(OutputPins[i], bNeedsValue);

			UNiagaraScriptVariable* Variable = GetNode->GetNiagaraGraph()->GetScriptVariable(Var);

			// Handle parameter map overrides for bindings as a special case, 
			// effectively copying the bound value to the variable in question prior to invoking a function call.
			// Especially relevant for module parameters that are also bindings
			bool bFoundBinding = false;
			if (Variable && Variable->DefaultMode == ENiagaraDefaultMode::Binding)
			{
				FNiagaraScriptVariableBinding Bind = Variable->DefaultBinding;
				if (Bind.IsValid())
				{
					int LastSetChunkIdx = INDEX_NONE;

					// Check whether we've encountered this variable before, i.e. in BuildMissingDefaults in a spawn script. In this case we'll skip
					for (auto& UniqueVar : UniqueVarToChunk)
					{
						if (UniqueVar.Key.IsEquivalent(FNiagaraVariable(Var.GetType(), Var.GetName())))
						{
							LastSetChunkIdx = UniqueVar.Value;
							break;
						}
					}
					
					if (LastSetChunkIdx == INDEX_NONE)
					{
						// If we haven't encountered this variable before, 
						// search through the param history for a variable matching the bindings and add a source chunk to it
						Var = ActiveHistoryForFunctionCalls.ResolveAliases(Var);
						for (auto HistoryVariable : ParamMapHistories[ParamMapHistoryIdx].Variables)
						{
							if (HistoryVariable.IsEquivalent(FNiagaraVariable(Var.GetType(), Bind.GetName())))
							{
								FString SanitizedName = GetParameterMapInstanceName(ActiveStageIdx) + TEXT(".") + GetSanitizedSymbolName(Var.GetName().ToString());
								LastSetChunkIdx = AddSourceChunk(SanitizedName, Var.GetType());
								break;
							}
						}
						if (LastSetChunkIdx != INDEX_NONE && Var.GetType().GetClass() == nullptr)
						{
							int32 VarIdx = ParamMapHistories[ParamMapHistoryIdx].FindVariableByName(Var.GetName());
							if (VarIdx != INDEX_NONE && VarIdx < ParamMapSetVariablesToChunks[ParamMapHistoryIdx].Num())
							{
								// Record that we wrote to it
								ParamMapSetVariablesToChunks[ParamMapHistoryIdx][VarIdx] = LastSetChunkIdx;
								ParamMapDefinedAttributesToNamespaceVars.FindOrAdd(Var.GetName()) = Var;
								Outputs[i] = LastSetChunkIdx;
								bFoundBinding = true;
							}
						}
					}
				}
			}

			if (!bFoundBinding)
			{
				HandleParameterRead(ParamMapHistoryIdx, Var, GetNode->GetDefaultPin(OutputPins[i]), GetNode, Outputs[i], nullptr);
			}
		}
	}
}

void FHlslNiagaraTranslator::HandleParameterRead(int32 ParamMapHistoryIdx, const FNiagaraVariable& InVar, const UEdGraphPin* DefaultPin, UNiagaraNode* ErrorNode, int32& OutputChunkId, UNiagaraScriptVariable* Variable, bool bTreatAsUnknownParameterMap)
{
	FString ParameterMapInstanceName = GetParameterMapInstanceName(ParamMapHistoryIdx);
	FNiagaraVariable Var = InVar;
	if (!AddStructToDefinitionSet(Var.GetType()))
	{
		Error(FText::Format(LOCTEXT("ParameterMapGetTypeError", "Cannot handle type {0}! Variable: {1}"), Var.GetType().GetNameText(), FText::FromName(Var.GetName())), nullptr, nullptr);
	}

	// If this is a System parameter, just wire in the system appropriate system attribute.
	FString VarName = Var.GetName().ToString();
	FString SymbolName = GetSanitizedSymbolName(VarName);

	bool bIsPerInstanceAttribute = false;
	bool bIsCandidateForRapidIteration = false;
	const UEdGraphPin* InputPin = DefaultPin;

	FString Namespace = FNiagaraParameterMapHistory::GetNamespace(Var);
	if (!ParamMapHistories[ParamMapHistoryIdx].IsValidNamespaceForReading(CompileOptions.TargetUsage, CompileOptions.TargetUsageBitmask, Namespace))
	{
		Error(FText::Format(LOCTEXT("InvalidReadingNamespace", "Variable {0} is in a namespace that isn't valid for reading"), FText::FromName(Var.GetName())), ErrorNode, nullptr);
		return;
	}

	//Some special variables can be replaced directly with constants which allows for extra optimization in the compiler.
	if (GetLiteralConstantVariable(Var))
	{
		OutputChunkId = GetConstant(Var);
		return;
	}

	if (FNiagaraParameterMapHistory::IsExternalConstantNamespace(Var, CompileOptions.TargetUsage, CompileOptions.GetTargetUsageBitmask()))
	{
		if (ParameterMapRegisterExternalConstantNamespaceVariable(Var, ErrorNode, ParamMapHistoryIdx, OutputChunkId, DefaultPin))
		{
			return;
		}
	}
	else if (FNiagaraParameterMapHistory::IsAliasedModuleParameter(Var) && ActiveHistoryForFunctionCalls.InTopLevelFunctionCall(CompileOptions.TargetUsage))
	{
		if (Variable && Variable->DefaultMode == ENiagaraDefaultMode::Binding && Variable->DefaultBinding.IsValid())
		{
			// Skip the case where the below condition is met, but it's overridden by a binding.
			bIsCandidateForRapidIteration = false;
		}
		else if (InputPin != nullptr && InputPin->LinkedTo.Num() == 0 && Var.GetType() != FNiagaraTypeDefinition::GetBoolDef() && !Var.GetType().IsEnum() && !Var.GetType().IsDataInterface())
		{
			bIsCandidateForRapidIteration = true;
		}
	}

	FNiagaraParameterMapHistory& History = ParamMapHistories[ParamMapHistoryIdx];
	bool bWasEmitterAliased = FNiagaraParameterMapHistory::IsAliasedEmitterParameter(Var);
	Var = ActiveHistoryForFunctionCalls.ResolveAliases(Var);

	const FNiagaraVariable* ConstantVar = FNiagaraConstants::GetKnownConstant(Var.GetName(), false);
	if (ConstantVar != nullptr && ConstantVar->GetType() != Var.GetType())
	{
		Error(FText::Format(LOCTEXT("MismatchedConstantTypes", "Variable {0} is a system constant, but its type is different! {1} != {2}"), FText::FromName(Var.GetName()),
			ConstantVar->GetType().GetNameText(), Var.GetType().GetNameText()), ErrorNode, nullptr);
	}

	if (History.IsPrimaryDataSetOutput(Var, GetTargetUsage())) // Note that data interfaces aren't ever in the primary data set even if the namespace matches.
	{
		bIsPerInstanceAttribute = true;
	}

	int32 LastSetChunkIdx = INDEX_NONE;
	if (ParamMapHistoryIdx < ParamMapHistories.Num())
	{
		// See if we've written this variable before, if so we can reuse the index
		int32 VarIdx = ParamMapHistories[ParamMapHistoryIdx].FindVariableByName(Var.GetName());
		if (VarIdx != INDEX_NONE && VarIdx < ParamMapSetVariablesToChunks[ParamMapHistoryIdx].Num())
		{
			LastSetChunkIdx = ParamMapSetVariablesToChunks[ParamMapHistoryIdx][VarIdx];
		}

		// Check to see if this is the first time we've encountered this node and it is a viable candidate for rapid iteration
		if (LastSetChunkIdx == INDEX_NONE && bIsCandidateForRapidIteration)
		{
			FNiagaraVariable OriginalVar = Var;
			bool bVarChanged = false;
			if (!bWasEmitterAliased && ActiveHistoryForFunctionCalls.GetEmitterAlias() != nullptr)
			{
				Var = FNiagaraParameterMapHistory::ConvertVariableToRapidIterationConstantName(Var, *(*ActiveHistoryForFunctionCalls.GetEmitterAlias()), GetTargetUsage());
				bVarChanged = true;
			}
			else if (UNiagaraScript::IsSystemSpawnScript(CompileOptions.TargetUsage) || UNiagaraScript::IsSystemUpdateScript(CompileOptions.TargetUsage))
			{
				Var = FNiagaraParameterMapHistory::ConvertVariableToRapidIterationConstantName(Var, nullptr, GetTargetUsage());
				bVarChanged = true;
			}

			if (TranslationOptions.bParameterRapidIteration)
			{
			// Now try to look up with the new name.. we may have already made this an external variable before..
			if (bVarChanged)
			{
				VarIdx = ParamMapHistories[ParamMapHistoryIdx].FindVariableByName(Var.GetName());
				if (VarIdx != INDEX_NONE && VarIdx < ParamMapSetVariablesToChunks[ParamMapHistoryIdx].Num())
				{
					LastSetChunkIdx = ParamMapSetVariablesToChunks[ParamMapHistoryIdx][VarIdx];
				}
			}

			// If it isn't found yet.. go ahead and make it into a constant variable..
			if (LastSetChunkIdx == INDEX_NONE && ParameterMapRegisterExternalConstantNamespaceVariable(Var, ErrorNode, ParamMapHistoryIdx, OutputChunkId, InputPin))
			{
				LastSetChunkIdx = OutputChunkId;
				if (VarIdx != INDEX_NONE && VarIdx < ParamMapSetVariablesToChunks[ParamMapHistoryIdx].Num())
				{
					// Record that we wrote to it.
					ParamMapSetVariablesToChunks[ParamMapHistoryIdx][VarIdx] = LastSetChunkIdx;
					ParamMapDefinedAttributesToNamespaceVars.FindOrAdd(Var.GetName()) = Var;
				}
				return;
				}				
			}
			else
			{
				int32 FoundIdx = TranslationOptions.OverrideModuleConstants.Find(Var);
				if (FoundIdx == INDEX_NONE)
				{
					if (!bWasEmitterAliased && ActiveHistoryForFunctionCalls.GetEmitterAlias() != nullptr && CompileData != nullptr)
					{
						Var = FNiagaraParameterMapHistory::ConvertVariableToRapidIterationConstantName(OriginalVar, *CompileData->EmitterUniqueName, GetTargetUsage());
						bVarChanged = true;
						FoundIdx = TranslationOptions.OverrideModuleConstants.Find(Var);
					}
				}
				
				if (FoundIdx != INDEX_NONE)
				{
					FString DebugConstantStr;
					OutputChunkId = GetConstant(TranslationOptions.OverrideModuleConstants[FoundIdx], &DebugConstantStr);
					UE_LOG(LogNiagaraEditor, Display, TEXT("Converted parameter %s to constant %s for script %s"), *Var.GetName().ToString(), *DebugConstantStr, *CompileOptions.FullName);
					return;
				}
				else if (InputPin != nullptr && !InputPin->bDefaultValueIsIgnored) // Use the default from the input pin because this variable was previously never encountered.
				{
					FNiagaraVariable PinVar = Schema->PinToNiagaraVariable(InputPin, true);
					FString DebugConstantStr;
					OutputChunkId = GetConstant(PinVar, &DebugConstantStr);
					UE_LOG(LogNiagaraEditor, Display, TEXT("Converted default value of parameter %s to constant %s for script %s. Likely added since this system was last compiled."), *Var.GetName().ToString(), *DebugConstantStr, *CompileOptions.FullName);
					return;
				}
				
				Error(FText::Format(LOCTEXT("InvalidRapidIterationReplacement", "Variable {0} is a rapid iteration param, but it wasn't found in the override list to bake out!"), FText::FromName(Var.GetName())), ErrorNode, nullptr);
			}
		}

		// We have yet to write to this parameter, use the default value if specified and the parameter 
		// isn't a per-particle value.
		bool bIgnoreDefaultValue = ParamMapHistories[ParamMapHistoryIdx].ShouldIgnoreVariableDefault(Var);
		if (bIsPerInstanceAttribute)
		{
			FNiagaraVariable* ExistingVar = ParamMapDefinedAttributesToNamespaceVars.Find(Var.GetName());
			bool ExistsInAttribArrayAlready = ExistingVar != nullptr;
			if (ExistsInAttribArrayAlready&& ExistingVar->GetType() != Var.GetType())
			{
				Error(FText::Format(LOCTEXT("Mismatched Types", "Variable {0} was defined earlier, but its type is different! {1} != {2}"), FText::FromName(Var.GetName()),
					ExistingVar->GetType().GetNameText(), Var.GetType().GetNameText()), ErrorNode, nullptr);
			}

			if ((TranslationStages.Num() > 1 && !UNiagaraScript::IsParticleSpawnScript(TranslationStages[ActiveStageIdx].ScriptUsage) && ExistsInAttribArrayAlready) ||
				!UNiagaraScript::IsSpawnScript(TranslationStages[ActiveStageIdx].ScriptUsage))
			{
				bIgnoreDefaultValue = true;
			}
		}

		if (LastSetChunkIdx == INDEX_NONE && (UNiagaraScript::IsSpawnScript(TranslationStages[ActiveStageIdx].ScriptUsage)))
		{
			if (FNiagaraParameterMapHistory::IsInitialValue(Var))
			{
				FNiagaraVariable SourceForInitialValue = FNiagaraParameterMapHistory::GetSourceForInitialValue(Var);
				bool bFoundExistingSet = false;
				for (int32 OtherParamIdx = 0; OtherParamIdx < OtherOutputParamMapHistories.Num(); OtherParamIdx++)
				{
					if (INDEX_NONE != OtherOutputParamMapHistories[OtherParamIdx].FindVariableByName(SourceForInitialValue.GetName()))
					{
						bFoundExistingSet = true;
					}
				}

				if (bFoundExistingSet)
				{
					LastSetChunkIdx = AddBodyChunk(ParameterMapInstanceName + TEXT(".") + GetSanitizedSymbolName(Var.GetName().ToString()),
						ParameterMapInstanceName + TEXT(".") + GetSanitizedSymbolName(SourceForInitialValue.GetName().ToString()), Var.GetType(), false);
					ParamMapDefinedAttributesToNamespaceVars.FindOrAdd(Var.GetName()) = Var;
				}
				else
				{
					Error(FText::Format(LOCTEXT("MissingInitialValueSource", "Variable {0} is used, but its source variable {1} is not set!"), FText::FromName(Var.GetName()), FText::FromName(SourceForInitialValue.GetName())), nullptr, nullptr);
				}
			}
			else if (UniqueVars.Contains(Var) && UniqueVarToChunk.Contains(Var))
			{
				int32* FoundIdx = UniqueVarToChunk.Find(Var);
				if (FoundIdx != nullptr)
				{
					LastSetChunkIdx = *FoundIdx;
				}
			}
		}

		if (LastSetChunkIdx == INDEX_NONE && !bIgnoreDefaultValue)
		{
			if (Variable && Variable->DefaultMode == ENiagaraDefaultMode::Binding && Variable->DefaultBinding.IsValid())
			{
				FNiagaraScriptVariableBinding Bind = Variable->DefaultBinding;

				// Check whether we've encountered the binding before, if so return its chunk
				bool bFoundVariable = false;
				for (auto& UniqueVar : UniqueVars)
				{
					if (Bind.GetName() == UniqueVar.GetName())
					{
						bFoundVariable = true;
						break;
					}
				}

				if (bFoundVariable)
				{
					for (auto& UniqueVar : UniqueVarToChunk)
					{
						if (UniqueVar.Key.IsEquivalent(FNiagaraVariable(Variable->Variable.GetType(), Bind.GetName())))
						{
							LastSetChunkIdx = UniqueVar.Value;
							break;
						}
					}
				}
				else
				{
					// We haven't encountered the binding before, try to check whether it's a known variable or not.
					int Out = GetParameter(FNiagaraVariable(InVar.GetType(), Bind.GetName()));
					if (Out != INDEX_NONE)
					{
						LastSetChunkIdx = Out;
					} 
					else 
					{
						Error(FText::Format(LOCTEXT("CannotFindBinding", "The module input {0} is bound to {1}, but {1} is not yet defined. Make sure {1} is defined prior to this module call."),
							FText::FromName(Var.GetName()),
							FText::FromName(Bind.GetName())), ErrorNode, nullptr);
					}
				}
			}
			else if (InputPin != nullptr) // Default was found, trace back its inputs.
			{
				// Check to see if there are any overrides passed in to the translator. This allows us to bake in rapid iteration variables for performance.
				if (InputPin->LinkedTo.Num() == 0 && bIsCandidateForRapidIteration && !TranslationOptions.bParameterRapidIteration)
				{
					bool bVarChanged = false;
					FNiagaraVariable RapidIterationConstantVar;
					if (!bWasEmitterAliased && ActiveHistoryForFunctionCalls.GetEmitterAlias() != nullptr)
					{
						RapidIterationConstantVar = FNiagaraParameterMapHistory::ConvertVariableToRapidIterationConstantName(Var, *(*ActiveHistoryForFunctionCalls.GetEmitterAlias()), GetTargetUsage());
						bVarChanged = true;
					}
					else if (UNiagaraScript::IsSystemSpawnScript(CompileOptions.TargetUsage) || UNiagaraScript::IsSystemUpdateScript(CompileOptions.TargetUsage))
					{
						RapidIterationConstantVar = FNiagaraParameterMapHistory::ConvertVariableToRapidIterationConstantName(Var, nullptr, GetTargetUsage());
						bVarChanged = true;
					}

					int32 FoundIdx = TranslationOptions.OverrideModuleConstants.Find(RapidIterationConstantVar);
					if (FoundIdx != INDEX_NONE)
					{
						FString DebugConstantStr;
						OutputChunkId = GetConstant(TranslationOptions.OverrideModuleConstants[FoundIdx], &DebugConstantStr);
						UE_LOG(LogNiagaraEditor, Display, TEXT("Converted parameter %s to constant %s for script %s"), *Var.GetName().ToString(), *DebugConstantStr, *CompileOptions.FullName);
						return;
					}
					else if (InputPin != nullptr && !InputPin->bDefaultValueIsIgnored) // Use the default from the input pin because this variable was previously never encountered.
					{
						FNiagaraVariable PinVar = Schema->PinToNiagaraVariable(InputPin, true);
						FString DebugConstantStr;
						OutputChunkId = GetConstant(PinVar, &DebugConstantStr);
						UE_LOG(LogNiagaraEditor, Display, TEXT("Converted default value of parameter %s to constant %s for script %s. Likely added since this system was last compiled."), *Var.GetName().ToString(), *DebugConstantStr, *CompileOptions.FullName);
						return;
					}

					Error(FText::Format(LOCTEXT("InvalidRapidIterationReplacement", "Variable {0} is a rapid iteration param, but it wasn't found in the override list to bake out!"), FText::FromName(Var.GetName())), ErrorNode, nullptr);
				}

				CurrentDefaultPinTraversal.Push(InputPin);
				if (InputPin->LinkedTo.Num() != 0 && InputPin->LinkedTo[0] != nullptr)
				{
					// Double-check to make sure that we are connected to a TRANSLATOR_PARAM_BEGIN_DEFAULTS input node rather than
					// a normal parameter-based parameter map input node to ensure that we don't get into weird traversals.
					TArray<UNiagaraNode*> Nodes;
					UNiagaraGraph::BuildTraversal(Nodes, Cast<UNiagaraNode>(InputPin->LinkedTo[0]->GetOwningNode()));
					for (UNiagaraNode* Node : Nodes)
					{
						UNiagaraNodeInput* InputNode = Cast<UNiagaraNodeInput>(Node);
						if (InputNode && InputNode->Input.GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
						{
							if (InputNode->Usage != ENiagaraInputNodeUsage::TranslatorConstant)
							{
								Error(FText::Format(LOCTEXT("InvalidParamMapStartForDefaultPin", "Default found for {0}, but the parameter map source for default pins needs to be a {1} node, not a generic input node."),
									FText::FromName(Var.GetName()),
									FText::FromName(TRANSLATOR_PARAM_BEGIN_DEFAULTS.GetName())), ErrorNode, nullptr);
							}
						}
					}
				}
				LastSetChunkIdx = CompilePin(InputPin);
				CurrentDefaultPinTraversal.Pop();
			}
			else
			{
				LastSetChunkIdx = GetConstant(Var);
			}

			if (!Var.IsDataInterface() && LastSetChunkIdx != INDEX_NONE)
			{
				if (bTreatAsUnknownParameterMap == false)
				{
					if (VarIdx != INDEX_NONE && VarIdx < ParamMapSetVariablesToChunks[ParamMapHistoryIdx].Num())
					{
						// Record that we wrote to it.
						ParamMapSetVariablesToChunks[ParamMapHistoryIdx][VarIdx] = LastSetChunkIdx;
						ParamMapDefinedAttributesToNamespaceVars.FindOrAdd(Var.GetName()) = Var;
					}
					else if (VarIdx == INDEX_NONE && UniqueVars.Contains(Var))
					{
						ParamMapDefinedAttributesToNamespaceVars.FindOrAdd(Var.GetName()) = Var;
					}
					else
					{
						Error(FText::Format(LOCTEXT("NoVarDefaultFound", "Default found for {0}, but not found in ParameterMap traversal"), FText::FromName(Var.GetName())), ErrorNode, nullptr);
					}
				}

				// Actually insert the text that sets the default value
				if (LastSetChunkIdx != INDEX_NONE)
				{
					if (Var.GetType().GetClass() == nullptr) // Only need to do this wiring for things that aren't data interfaces.
					{
						AddBodyChunk(ParameterMapInstanceName + TEXT(".") + GetSanitizedSymbolName(Var.GetName().ToString()), TEXT("{0}"), Var.GetType(), LastSetChunkIdx, false);
					}
				}
			}

			if (LastSetChunkIdx == INDEX_NONE && VarIdx != INDEX_NONE && Var.IsDataInterface())
			{
				// If this variable is a data interface and it's in the parameter map, but hasn't been set yet, then is is an external data interface so try to register it.
				if (ParameterMapRegisterExternalConstantNamespaceVariable(Var, ErrorNode, ParamMapHistoryIdx, OutputChunkId, DefaultPin))
				{
					return;
				}
			}
		}
	}

	// If we are of a data interface, we should output the data interface registration index, otherwise output
	// the map namespace that we're writing to.
	if (Var.IsDataInterface())
	{
		// In order for a module to compile successfully, we potentially need to generate default values
		// for variables encountered without ever being set. We do this by creating an instance of the CDO.
		if (UNiagaraScript::IsStandaloneScript(CompileOptions.TargetUsage) && LastSetChunkIdx == INDEX_NONE)
		{
			UObject*const* Obj = CompileData->CDOs.Find(const_cast<UClass*>(Var.GetType().GetClass()));
			check(Obj != nullptr);
			UNiagaraDataInterface* DataInterface = CastChecked<UNiagaraDataInterface>(*Obj);
			if (DataInterface)
			{
				LastSetChunkIdx = RegisterDataInterface(Var, DataInterface, true, false);
			}
		}

		OutputChunkId = LastSetChunkIdx;
	}
	else
	{
		OutputChunkId = AddSourceChunk(ParameterMapInstanceName + TEXT(".") + GetSanitizedSymbolName(Var.GetName().ToString()), Var.GetType());
		ParamMapDefinedAttributesToNamespaceVars.FindOrAdd(Var.GetName()) = Var;
	}
}



void FHlslNiagaraTranslator::ReadDataSet(const FNiagaraDataSetID DataSet, const TArray<FNiagaraVariable>& Variables, ENiagaraDataSetAccessMode AccessMode, int32 InputChunk, TArray<int32>& Outputs)
{
	//Eventually may allow events that take in a direct index or condition but for now we don't
	int32 ParamMapHistoryIdx = InputChunk;

	if (ParamMapHistoryIdx == -1)
	{
		Error(LOCTEXT("NoParamMapIdxToReadDataSet", "Cannot find parameter map for input to ReadDataSet!"), nullptr, nullptr);
		for (int32 i = 0; i < Outputs.Num(); i++)
		{
			Outputs[i] = INDEX_NONE;
			return;
		}
	}
	else if (ParamMapHistoryIdx >= ParamMapHistories.Num())
	{
		Error(FText::Format(LOCTEXT("InvalidParamMapIdxToReadDataSet", "Invalid parameter map index for ReadDataSet input {0} of {1}!"), ParamMapHistoryIdx, ParamMapHistories.Num()), nullptr, nullptr);
		for (int32 i = 0; i < Outputs.Num(); i++)
		{
			Outputs[i] = INDEX_NONE;
			return;
		}
	}

	TMap<int32, FDataSetAccessInfo>& Reads = DataSetReadInfo[(int32)AccessMode].FindOrAdd(DataSet);
	FDataSetAccessInfo* DataSetReadForInput = Reads.Find(InputChunk);
	if (!DataSetReadForInput)
	{
		DataSetReadForInput = &Reads.Add(InputChunk);

		DataSetReadForInput->Variables = Variables;
		DataSetReadForInput->CodeChunks.Reserve(Variables.Num() + 1);

		FString DataSetAccessSymbol = GetDataSetAccessSymbol(DataSet, InputChunk, true);
		//Add extra output to indicate if event read is valid data.
		//DataSetReadForInput->CodeChunks.Add(AddSourceChunk(DataSetAccessSymbol + TEXT("_Valid"), FNiagaraTypeDefinition::GetIntDef()));
		for (int32 i = 0; i < Variables.Num(); ++i)
		{
			const FNiagaraVariable& Var = Variables[i];
			if (!AddStructToDefinitionSet(Var.GetType()))
			{
				Error(FText::Format(LOCTEXT("GetConstantFailTypeVar", "Cannot handle type {0}! Variable: {1}"), Var.GetType().GetNameText(), FText::FromName(Var.GetName())), nullptr, nullptr);
			}
			DataSetReadForInput->CodeChunks.Add(AddSourceChunk(DataSetAccessSymbol + TEXT(".") + GetSanitizedSymbolName(Var.GetName().ToString()), Var.GetType()));
		}
		Outputs.Add(ParamMapHistoryIdx);
		Outputs.Append(DataSetReadForInput->CodeChunks);
	}
	else
	{
		check(Variables.Num() == DataSetReadForInput->Variables.Num());
		Outputs.Add(ParamMapHistoryIdx);
		Outputs.Append(DataSetReadForInput->CodeChunks);
	}
}

void FHlslNiagaraTranslator::WriteDataSet(const FNiagaraDataSetID DataSet, const TArray<FNiagaraVariable>& Variables, ENiagaraDataSetAccessMode AccessMode, const TArray<int32>& Inputs, TArray<int32>& Outputs)
{
	int32 ParamMapHistoryIdx = Inputs[0];
	int32 ConditionalChunk = Inputs[1];
	int32 InputChunk = Inputs[2];
	Outputs.SetNum(1);
	Outputs[0] = ParamMapHistoryIdx;

	if (ParamMapHistoryIdx == -1)
	{
		Error(LOCTEXT("NoParamMapIdxToWriteDataSet", "Cannot find parameter map for input to WriteDataSet!"), nullptr, nullptr);
		for (int32 i = 0; i < Outputs.Num(); i++)
		{
			Outputs[i] = INDEX_NONE;
			return;
		}
	}
	else if (ParamMapHistoryIdx >= ParamMapHistories.Num())
	{
		Error(FText::Format(LOCTEXT("InvalidParamMapIdxToWriteDataSet", "Invalid parameter map index for WriteDataSet input {0} of {1}!"), ParamMapHistoryIdx, ParamMapHistories.Num()), nullptr, nullptr);
		for (int32 i = 0; i < Outputs.Num(); i++)
		{
			Outputs[i] = INDEX_NONE;
			return;
		}
	}


	TMap<int32, FDataSetAccessInfo>& Writes = DataSetWriteInfo[(int32)AccessMode].FindOrAdd(DataSet);
	FDataSetAccessInfo* DataSetWriteForInput = Writes.Find(InputChunk);
	int32& DataSetWriteConditionalIndex = DataSetWriteConditionalInfo[(int32)AccessMode].FindOrAdd(DataSet);

	//We should never try to write to the exact same dataset at the same index/condition twice.
	//This is still possible but we can catch easy cases here.
	if (DataSetWriteForInput)
	{
		//TODO: improve error report.
		Error(LOCTEXT("WritingToSameDataSetError", "Writing to the same dataset with the same condition/index."), NULL, NULL);
		return;
	}

	DataSetWriteConditionalIndex = ConditionalChunk;
	DataSetWriteForInput = &Writes.Add(InputChunk);

	DataSetWriteForInput->Variables = Variables;

	//FString DataSetAccessName = GetDataSetAccessSymbol(DataSet, InputChunk, false);
	FString DataSetAccessName = FString("Context.") + DataSet.Name.ToString() + "Write";	// TODO: HACK - need to get the real symbol name here

	//First chunk for a write is always the condition pin.
	//We always write the event payload into the temp storage but we can access this condition to pass to the final actual write to the buffer.

	DataSetWriteForInput->CodeChunks.Add(AddBodyChunk(DataSetAccessName + TEXT("_Valid"), TEXT("{0}"), FNiagaraTypeDefinition::GetBoolDef(), Inputs[1], false));
	for (int32 i = 0; i < Variables.Num(); ++i)
	{
		const FNiagaraVariable& Var = Variables[i];
		int32 Input = Inputs[i + 2];//input 0 is the valid input (no entry in variables array), so we need of offset all other inputs by 1.
		DataSetWriteForInput->CodeChunks.Add(AddBodyChunk(DataSetAccessName + TEXT(".") + GetSanitizedSymbolName(Var.GetName().ToString()), TEXT("{0}"), Var.GetType(), Input, false));
	}

}

int32 FHlslNiagaraTranslator::RegisterDataInterface(FNiagaraVariable& Var, UNiagaraDataInterface* DataInterface, bool bPlaceholder, bool bAddParameterMapRead)
{
	FString Id = DataInterface ? *DataInterface->GetMergeId().ToString() : TEXT("??");
	FString PathName = DataInterface ? *DataInterface->GetPathName() : TEXT("XX");
	//UE_LOG(LogNiagaraEditor, Log, TEXT("RegisterDataInterface %s %s %s '%s' %s"), *Var.GetName().ToString(), *Var.GetType().GetName(), *Id, *PathName,
	//	bPlaceholder ? TEXT("bPlaceholder=true") : TEXT("bPlaceholder=false"));

	int32 FuncParam;
	if (GetFunctionParameter(Var, FuncParam))
	{
		if (FuncParam != INDEX_NONE)
		{
			//This data interface param has been overridden by the function call so use that index.	
			UE_LOG(LogNiagaraEditor, Log, TEXT("RegisterDataInterface is funcParam"));
			return FuncParam;
		}
	}

	//If we get here then this is a new data interface.
	FName DataInterfaceName;
	if (FNiagaraParameterMapHistory::IsAliasedEmitterParameter(Var.GetName().ToString()))
	{
		FNiagaraVariable AliasedVar = ActiveHistoryForFunctionCalls.ResolveAliases(Var);
		DataInterfaceName = AliasedVar.GetName();
	}
	else
	{
		DataInterfaceName = Var.GetName();
	}

	int32 Idx = CompilationOutput.ScriptData.DataInterfaceInfo.IndexOfByPredicate([&](const FNiagaraScriptDataInterfaceCompileInfo& OtherInfo)
	{
		return OtherInfo.Name == DataInterfaceName;
	});

	if (Idx == INDEX_NONE)
	{
		Idx = CompilationOutput.ScriptData.DataInterfaceInfo.AddDefaulted();
		CompilationOutput.ScriptData.DataInterfaceInfo[Idx].Name = DataInterfaceName;
		CompilationOutput.ScriptData.DataInterfaceInfo[Idx].Type = Var.GetType();
		CompilationOutput.ScriptData.DataInterfaceInfo[Idx].bIsPlaceholder = bPlaceholder;

		//Interface requires per instance data so add a user pointer table entry.
		if (DataInterface != nullptr && DataInterface->PerInstanceDataSize() > 0)
		{
			CompilationOutput.ScriptData.DataInterfaceInfo[Idx].UserPtrIdx = CompilationOutput.ScriptData.NumUserPtrs++;
		}
	}
	else
	{
		check(CompilationOutput.ScriptData.DataInterfaceInfo[Idx].Name == Var.GetName());
		check(CompilationOutput.ScriptData.DataInterfaceInfo[Idx].Type == Var.GetType());
	}

	if (bAddParameterMapRead)
	{
		FName UsageName;
		if (FNiagaraParameterMapHistory::IsAliasedEmitterParameter(Var.GetName().ToString()))
		{
			FNiagaraVariable AliasedVar = ActiveHistoryForFunctionCalls.ResolveAliases(Var);
			UsageName = AliasedVar.GetName();
		}
		else
		{
			UsageName = Var.GetName();
		}
		CompilationOutput.ScriptData.DataInterfaceInfo[Idx].RegisteredParameterMapRead = UsageName;
	}

	return Idx;
}

void FHlslNiagaraTranslator::Operation(class UNiagaraNodeOp* Operation, TArray<int32>& Inputs, TArray<int32>& Outputs)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslTranslator_Operation);

	// Use the pins to determine the output type here since they may have been changed due to numeric pin fix up.
	const FNiagaraOpInfo* OpInfo = FNiagaraOpInfo::GetOpInfo(Operation->OpName);

	TArray<UEdGraphPin*> OutputPins;
	Operation->GetOutputPins(OutputPins);
	for (int32 OutputIndex = 0; OutputIndex < OutputPins.Num(); OutputIndex++)
	{
		UEdGraphPin* OutputPin = OutputPins[OutputIndex];
		FNiagaraTypeDefinition OutputType = Schema->PinToTypeDefinition(OutputPin);

		if (!AddStructToDefinitionSet(OutputType))
		{
			FText PinNameText = OutputPin->PinFriendlyName.IsEmpty() ? FText::FromName(OutputPin->PinName) : OutputPin->PinFriendlyName;
			Error(FText::Format(LOCTEXT("GetConstantFailTypePin", "Cannot handle type {0}! Output Pin: {1}"), OutputType.GetNameText(), PinNameText), Operation, OutputPin);
		}

		const FNiagaraOpInOutInfo& IOInfo = OpInfo->Outputs[OutputIndex];
		FString OutputHlsl;
		if (OpInfo->bSupportsAddedInputs)
		{
			if (!OpInfo->CreateHlslForAddedInputs(Inputs.Num(), OutputHlsl))
			{
				FText PinNameText = OutputPin->PinFriendlyName.IsEmpty() ? FText::FromName(OutputPin->PinName) : OutputPin->PinFriendlyName;
				Error(FText::Format(LOCTEXT("AggregateInputFailTypePin", "Cannot create hlsl output for type {0}! Output Pin: {1}"), OutputType.GetNameText(), PinNameText), Operation, OutputPin);
				OutputHlsl = IOInfo.HlslSnippet;
			}
		}
		else {
			OutputHlsl = IOInfo.HlslSnippet;
		}
		check(!OutputHlsl.IsEmpty());
		Outputs.Add(AddBodyChunk(GetUniqueSymbolName(IOInfo.Name), OutputHlsl, OutputType, Inputs));
	}
}

void FHlslNiagaraTranslator::FunctionCall(UNiagaraNodeFunctionCall* FunctionNode, TArray<int32>& Inputs, TArray<int32>& Outputs)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_FunctionCall);

	TArray<UEdGraphPin*> CallOutputs;
	TArray<UEdGraphPin*> CallInputs;
	FunctionNode->GetOutputPins(CallOutputs);
	FunctionNode->GetInputPins(CallInputs);

	// Validate that there are no input pins with the same name and type
	TMultiMap<FName, FEdGraphPinType> SeenPins;
	for (UEdGraphPin* Pin : CallInputs)
	{
		FEdGraphPinType* SeenType = SeenPins.FindPair(Pin->GetFName(), Pin->PinType);
		if (SeenType)
		{
			Error(LOCTEXT("FunctionCallDuplicateInput", "Function call has duplicated inputs. Please make sure that each function parameter is unique."), FunctionNode, Pin);
			return;
		}
		else
		{
			SeenPins.Add(Pin->GetFName(), Pin->PinType);
		}
	}

	// If the function call is disabled, we 
	// need to route the input parameter map pin to the output parameter map pin.
	// Any other outputs become invalid.
	if (!FunctionNode->IsNodeEnabled())
	{
		int32 InputPinIdx = INDEX_NONE;
		int32 OutputPinIdx = INDEX_NONE;

		for (int32 i = 0; i < CallInputs.Num(); i++)
		{
			const UEdGraphPin* Pin = CallInputs[i];
			if (Schema->PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				// Found the input pin
				InputPinIdx = Inputs[i];
				break;
			}
		}

		Outputs.SetNum(CallOutputs.Num());
		for (int32 i = 0; i < CallOutputs.Num(); i++)
		{
			Outputs[i] = INDEX_NONE;
			const UEdGraphPin* Pin = CallOutputs[i];
			if (Schema->PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				// Mapping the input parameter map pin to the output.
				Outputs[i] = InputPinIdx;
			}
		}
		return;
	}

	FNiagaraFunctionSignature OutputSignature;
	if (FunctionNode->FunctionScript == nullptr && !FunctionNode->Signature.IsValid())
	{
		Error(LOCTEXT("FunctionCallNonexistantFunctionScript", "Function call missing FunctionScript and invalid signature"), FunctionNode, nullptr);
		return;
	}

	// We need the generated string to generate the proper signature for now.
	ActiveHistoryForFunctionCalls.EnterFunction(FunctionNode->GetFunctionName(), FunctionNode->FunctionScript, FunctionNode);

	// Check if there are static switch parameters being set directly by a set node from the stack UI.
	// This can happen if a module was changed and the original parameter was replaced by a static switch with the same name, but the emitter was not yet updated.
	const FString* ModuleAlias = ActiveHistoryForFunctionCalls.GetModuleAlias();
	if (ModuleAlias)
	{
		for (int32 i = 0; i < ParamMapHistories.Num(); i++)
		{
			for (int32 j = 0; j < ParamMapHistories[i].VariablesWithOriginalAliasesIntact.Num(); j++)
			{
				const FNiagaraVariable Var = ParamMapHistories[i].VariablesWithOriginalAliasesIntact[j];
				FString VarStr = Var.GetName().ToString();
				if (VarStr.StartsWith(*ModuleAlias))
				{
					VarStr.MidInline(ModuleAlias->Len() + 1, MAX_int32, false);
					if (FunctionNode->FindStaticSwitchInputPin(*VarStr))
					{
						Error(FText::Format(LOCTEXT("SwitchPinFoundForSetPin", "A switch node pin exists but is being set directly using Set node! Please use the stack UI to resolve the conflict. Output Pin: {0}"), FText::FromName(Var.GetName())), FunctionNode, nullptr);
					}
				}
			}
		}
	}

	// Remove input add pin if it exists
	for (int32 i = 0; i < CallOutputs.Num(); i++)
	{
		if (FunctionNode->IsAddPin(CallOutputs[i]))
		{
			CallOutputs.RemoveAt(i);
			break;
		}
	}

	// Remove output add pin if it exists
	for (int32 i = 0; i < CallInputs.Num(); i++)
	{
		if (FunctionNode->IsAddPin(CallInputs[i]))
		{
			CallInputs.RemoveAt(i);
			break;
		}
	}

	ENiagaraScriptUsage ScriptUsage = ENiagaraScriptUsage::Function;
	FString Name;
	FString FullName;
	UNiagaraScriptSource* Source = nullptr;
	bool bCustomHlsl = false;
	FString CustomHlsl;
	FNiagaraFunctionSignature Signature = FunctionNode->Signature;

	if (FunctionNode->FunctionScript)
	{
		ScriptUsage = FunctionNode->FunctionScript->GetUsage();
		Name = FunctionNode->FunctionScript->GetName();
		FullName = FunctionNode->FunctionScript->GetFullName();
		Source = CastChecked<UNiagaraScriptSource>(FunctionNode->FunctionScript->GetSource());
		check(Source->GetOutermost() == GetTransientPackage());
	}
	UNiagaraNodeCustomHlsl* CustomFunctionHlsl = Cast<UNiagaraNodeCustomHlsl>(FunctionNode);
	if (CustomFunctionHlsl != nullptr)
	{
		// All of the arguments here are resolved withing the HandleCustomHlsl function..
		HandleCustomHlslNode(CustomFunctionHlsl, ScriptUsage, Name, FullName, bCustomHlsl, CustomHlsl, Signature, Inputs);
	}

	RegisterFunctionCall(ScriptUsage, Name, FullName, FunctionNode->NodeGuid, Source, Signature, bCustomHlsl, CustomHlsl, Inputs, CallInputs, CallOutputs, OutputSignature);

	if (OutputSignature.IsValid() == false)
	{
		Error(LOCTEXT("FunctionCallInvalidSignature", "Could not generate a valid function signature."), FunctionNode, nullptr);
		return;
	}

	GenerateFunctionCall(ScriptUsage, OutputSignature, Inputs, Outputs);

	if (bCustomHlsl)
	{
		// Re-add the add pins.
		Inputs.Add(INDEX_NONE);
		Outputs.Add(INDEX_NONE);
	}
	ActiveHistoryForFunctionCalls.ExitFunction(FunctionNode->GetFunctionName(), FunctionNode->FunctionScript, FunctionNode);
}

void FHlslNiagaraTranslator::EnterFunctionCallNode(const TSet<FName>& UnusedInputs)
{
	FunctionNodeStack.Add(UnusedInputs);
}

void FHlslNiagaraTranslator::ExitFunctionCallNode()
{
	ensure(FunctionNodeStack.Num() > 0);
	FunctionNodeStack.Pop(false);
}

bool FHlslNiagaraTranslator::IsFunctionVariableCulledFromCompilation(const FName& InputName) const
{
	if (FunctionNodeStack.Num() == 0)
	{
		return false;
	}
	return FunctionNodeStack.Last().Contains(InputName);
}

// From a valid list of namespaces, resolve any aliased tokens and promote namespaced variables without a master namespace to the input parameter map instance namespace
void FHlslNiagaraTranslator::FinalResolveNamespacedTokens(const FString& ParameterMapInstanceNamespace, TArray<FString>& Tokens, TArray<FString>& ValidChildNamespaces, FNiagaraParameterMapHistoryBuilder& Builder, TArray<FNiagaraVariable>& UniqueParameterMapEntriesAliasesIntact, TArray<FNiagaraVariable>& UniqueParameterMapEntries, int32 ParamMapHistoryIdx)
{
	for (int32 i = 0; i < Tokens.Num(); i++)
	{
		if (Tokens[i].Contains(TEXT("."))) // Only check tokens with namespaces in them..
		{
			for (const FString& ValidNamespace : ValidChildNamespaces)
			{
				FNiagaraVariable Var;

				// There are two possible paths here, one where we're using the namespace as-is from the valid list and one where we've already
				// prepended with the master parameter map instance namespace but may not have resolved any internal aliases yet.
				if (Tokens[i].StartsWith(ValidNamespace, ESearchCase::CaseSensitive))
				{
					FNiagaraVariable TempVar(FNiagaraTypeDefinition::GetFloatDef(), *Tokens[i]); // Declare a dummy name here that we will convert aliases for and use later
					Var = Builder.ResolveAliases(TempVar);
				}
				else if (Tokens[i].StartsWith(ParameterMapInstanceNamespace + ValidNamespace, ESearchCase::CaseSensitive))
				{
					FString BaseToken = Tokens[i].Mid(ParameterMapInstanceNamespace.Len());
					FNiagaraVariable TempVar(FNiagaraTypeDefinition::GetFloatDef(), *BaseToken); // Declare a dummy name here that we will convert aliases for and use later
					Var = Builder.ResolveAliases(TempVar);
				}

				if (Var.IsValid())
				{
					if (ParamMapHistoryIdx != INDEX_NONE)
					{
						bool bAdded = false;
						for (int32 j = 0; j < OtherOutputParamMapHistories.Num(); j++)
						{
							int32 VarIdx = OtherOutputParamMapHistories[j].FindVariableByName(Var.GetName(), true);
							if (VarIdx != INDEX_NONE)
							{
								if (OtherOutputParamMapHistories[j].VariablesWithOriginalAliasesIntact[VarIdx].IsValid())
								{
									UniqueParameterMapEntriesAliasesIntact.AddUnique(OtherOutputParamMapHistories[j].VariablesWithOriginalAliasesIntact[VarIdx]);
								}
								else
								{
									UniqueParameterMapEntriesAliasesIntact.AddUnique(OtherOutputParamMapHistories[j].Variables[VarIdx]);
								}
								UniqueParameterMapEntries.AddUnique(OtherOutputParamMapHistories[j].Variables[VarIdx]);
								bAdded = true;
								break;
							}
						}
						if (!bAdded && !UNiagaraScript::IsStandaloneScript(CompileOptions.TargetUsage)) // Don't warn in modules, they don't have enough context.
						{
							Error(FText::Format(LOCTEXT("GetCustomFail1", "Cannot use variable in custom expression, it hasn't been encountered yet: {0}"), FText::FromName(Var.GetName())), nullptr, nullptr);
						}

					}


					Tokens[i] = ParameterMapInstanceNamespace + GetSanitizedSymbolName(Var.GetName().ToString());
					break;
				}
			}
		}
	}
}

void FHlslNiagaraTranslator::HandleCustomHlslNode(UNiagaraNodeCustomHlsl* CustomFunctionHlsl, ENiagaraScriptUsage& OutScriptUsage, FString& OutName, FString& OutFullName, bool& bOutCustomHlsl, FString& OutCustomHlsl,
	FNiagaraFunctionSignature& OutSignature, TArray<int32>& Inputs)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_CustomHLSL);
	if (!CustomFunctionHlsl)
	{
		return;
	}

	// Determine the important outputs
	OutScriptUsage = CustomFunctionHlsl->ScriptUsage;
	OutName = GetSanitizedSymbolName(CustomFunctionHlsl->Signature.Name.ToString() + CustomFunctionHlsl->NodeGuid.ToString());
	OutSignature = CustomFunctionHlsl->Signature;
	OutFullName = CustomFunctionHlsl->GetFullName();
	OutSignature.Name = *OutName; // Force the name to be set to include the node guid for safety...
	bOutCustomHlsl = true;
	OutCustomHlsl = CustomFunctionHlsl->GetCustomHlsl();

	// Split up the hlsl into constituent tokens
	TArray<FString> Tokens;
	CustomFunctionHlsl->GetTokens(Tokens);

	int32 ParamMapHistoryIdx = INDEX_NONE;
	bool bHasParamMapOutputs = false;
	bool bHasParamMapInputs = false;

	// Resolve the names of any internal variables from the input variables.
	TArray<FNiagaraVariable> SigInputs;
	for (int32 i = 0; i < OutSignature.Inputs.Num(); i++)
	{
		FNiagaraVariable Input = OutSignature.Inputs[i];
		if (Input.GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
		{
			FString ParameterMapInstanceName = GetParameterMapInstanceName(0);
			FString ReplaceSrc = Input.GetName().ToString();
			FString ReplaceDest = ParameterMapInstanceName;
			CustomFunctionHlsl->ReplaceExactMatchTokens(Tokens, ReplaceSrc, ReplaceDest, true);
			SigInputs.Add(Input);
			OutSignature.bRequiresContext = true;
			ParamMapHistoryIdx = Inputs[i];
			bHasParamMapInputs = true;
		}
		else if (Input.GetType().IsDataInterface())
		{
			UObject* const* FoundCDO = CompileData->CDOs.Find(Input.GetType().GetClass());
			UNiagaraDataInterface* CDO = (FoundCDO ? Cast<UNiagaraDataInterface>(*FoundCDO) : nullptr);

			if (CDO == nullptr)
			{
				// If the cdo wasn't found, the data interface was not passed through a parameter map and so it won't be bound correctly, so add a compile error
				// and invalidate the signature.
				Error(LOCTEXT("DataInterfaceNotFoundCustomHLSL", "Data interface used by custom hlsl, but not found in precompiled data. Please notify Niagara team of bug."), nullptr, nullptr);
				return;
			}
			int32 OwnerIdx = Inputs[i];
			if (OwnerIdx < 0 || OwnerIdx >= CompilationOutput.ScriptData.DataInterfaceInfo.Num())
			{
				Error(LOCTEXT("FunctionCallDataInterfaceMissingRegistration", "Function call signature does not match to a registered DataInterface. Valid DataInterfaces should be wired into a DataInterface function call."), nullptr, nullptr);
				return;
			}
			
			// Go over all the supported functions in the DI and look to see if they occur in the 
			// actual custom hlsl source. If they do, then add them to the function table that we need to map.
			FNiagaraScriptDataInterfaceCompileInfo& Info = CompilationOutput.ScriptData.DataInterfaceInfo[OwnerIdx];
			TArray<FNiagaraFunctionSignature> Funcs;
			TArray<FNiagaraFunctionSignature> AddedFuncs;
			CDO->GetFunctions(Funcs);
			for (int32 FuncIdx = 0; FuncIdx < Funcs.Num(); FuncIdx++)
			{
				FNiagaraFunctionSignature Sig = Funcs[FuncIdx];
				FString ReplaceSrc = Input.GetName().ToString() + TEXT(".") + Sig.GetName();
				FString ReplaceDest = GetSanitizedSymbolName(Sig.GetName() + TEXT("_") + (Info.Name.ToString().Replace(TEXT("."), TEXT(""))));
				uint32 NumFound = CustomFunctionHlsl->ReplaceExactMatchTokens(Tokens, ReplaceSrc, ReplaceDest, false);
				if (NumFound != 0)
				{
					AddedFuncs.Add(Sig);
					DataInterfaceRegisteredFunctions.FindOrAdd(Input.GetType().GetFName()).Add(Sig);

					if (Info.UserPtrIdx != INDEX_NONE && CompilationTarget != ENiagaraSimTarget::GPUComputeSim)
					{
						//This interface requires per instance data via a user ptr so place the index to it at the end of the inputs.
						//Skip this for now... Inputs.Add(AddSourceChunk(LexToString(Info.UserPtrIdx), FNiagaraTypeDefinition::GetIntDef(), false));
						Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("InstanceData")));
					}
					//Override the owner id of the signature with the actual caller.
					Sig.OwnerName = Info.Name;
					Info.RegisteredFunctions.Add(Sig);
					Functions.FindOrAdd(Sig);
				}
			}
			SigInputs.Add(Input);
		}
		else
		{
			FString ReplaceSrc = Input.GetName().ToString();
			FString ReplaceDest = TEXT("In_") + ReplaceSrc;
			CustomFunctionHlsl->ReplaceExactMatchTokens(Tokens, ReplaceSrc, ReplaceDest, true);
			SigInputs.Add(Input);
		}
	}
	OutSignature.Inputs = SigInputs;

	// Resolve the names of any internal variables from the output variables.
	TArray<FNiagaraVariable> SigOutputs;
	for (FNiagaraVariable Output : OutSignature.Outputs)
	{
		if (Output.GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
		{
			FString ParameterMapInstanceName = GetParameterMapInstanceName(0);
			FString ReplaceSrc = Output.GetName().ToString();
			FString ReplaceDest = ParameterMapInstanceName;
			CustomFunctionHlsl->ReplaceExactMatchTokens(Tokens, ReplaceSrc, ReplaceDest, true);
			SigOutputs.Add(Output);
			OutSignature.bRequiresContext = true;
			bHasParamMapOutputs = true;
		}
		else
		{
			FString ReplaceSrc = Output.GetName().ToString();
			FString ReplaceDest = TEXT("Out_") + ReplaceSrc;
			CustomFunctionHlsl->ReplaceExactMatchTokens(Tokens, ReplaceSrc, ReplaceDest, true);
			SigOutputs.Add(Output);
		}
	}

	if (bHasParamMapOutputs || bHasParamMapInputs)
	{
		// Clean up any namespaced variables in the token list if they are aliased or promote any tokens that are namespaced to the parent 
		// parameter map.
		TArray<FString> PossibleNamespaces;
		FNiagaraParameterMapHistory::GetValidNamespacesForReading(CompileOptions.TargetUsage, 0, PossibleNamespaces);

		for (FNiagaraParameterMapHistory& History : ParamMapHistories)
		{
			for (FNiagaraVariable& Var : History.Variables)
			{
				FString Namespace = FNiagaraParameterMapHistory::GetNamespace(Var);
				PossibleNamespaces.AddUnique(Namespace);
			}
		}

		TArray<FNiagaraVariable> UniqueParamMapEntries;
		TArray<FNiagaraVariable> UniqueParamMapEntriesAliasesIntact;
		FinalResolveNamespacedTokens(GetParameterMapInstanceName(0) + TEXT("."), Tokens, PossibleNamespaces, ActiveHistoryForFunctionCalls, UniqueParamMapEntriesAliasesIntact, UniqueParamMapEntries, ParamMapHistoryIdx);

		// We must register any external constant variables that we encountered.
		for (FNiagaraVariable Var : UniqueParamMapEntriesAliasesIntact)
		{
			if (FNiagaraParameterMapHistory::IsExternalConstantNamespace(Var, CompileOptions.TargetUsage, CompileOptions.GetTargetUsageBitmask()))
			{
				int32 TempOutput;
				if (ParameterMapRegisterExternalConstantNamespaceVariable(Var, CustomFunctionHlsl, ParamMapHistoryIdx, TempOutput, nullptr))
				{
					continue;
				}
			}
		}
	}

	// Now reassemble the tokens into the final hlsl output
	OutSignature.Outputs = SigOutputs;
	OutCustomHlsl = FString::Join(Tokens, TEXT(""));

	// Dynamic inputs are assumed to be of the form 
	// "20.0f * Particles.Velocity.x + length(Particles.Velocity)", i.e. a mix of native functions, constants, operations, and variable names.
	// This needs to be modified to match the following requirements:	
	// 1) Write to the output variable of the dynamic input.
	// 2) Terminate in valid HLSL (i.e. have a ; at the end)
	// 3) Be guaranteed to write to the correct output type.
	if (OutScriptUsage == ENiagaraScriptUsage::DynamicInput)
	{
		if (CustomFunctionHlsl->Signature.Outputs.Num() != 1)
		{
			Error(LOCTEXT("CustomHlslDynamicInputMissingOutputs", "Custom hlsl dynamic input signature should have one and only one output."), CustomFunctionHlsl, nullptr);
			return;
		}
		if (CustomFunctionHlsl->Signature.Inputs.Num() < 1 || CustomFunctionHlsl->Signature.Inputs[0].GetType() != FNiagaraTypeDefinition::GetParameterMapDef())
		{
			Error(LOCTEXT("CustomHlslDynamicInputMissingInputs", "Custom hlsl dynamic input signature should have at least one input (a parameter map)."), CustomFunctionHlsl, nullptr);
			return;
		}

		OutSignature.bRequiresContext = true;
		FString ReplaceSrc = CustomFunctionHlsl->Signature.Outputs[0].GetName().ToString();
		FString ReplaceDest = TEXT("Out_") + ReplaceSrc;
		OutCustomHlsl = ReplaceDest + TEXT(" = (") + GetStructHlslTypeName(CustomFunctionHlsl->Signature.Outputs[0].GetType()) + TEXT(")(") + OutCustomHlsl + TEXT(");\n");
	}

	OutCustomHlsl = OutCustomHlsl.Replace(TEXT("\n"), TEXT("\n\t"));
	OutCustomHlsl = TEXT("\n") + OutCustomHlsl + TEXT("\n");
}

void FHlslNiagaraTranslator::RegisterFunctionCall(ENiagaraScriptUsage ScriptUsage, const FString& InName, const FString& InFullName, const FGuid& CallNodeId, UNiagaraScriptSource* Source, FNiagaraFunctionSignature& InSignature, bool bIsCustomHlsl, const FString& InCustomHlsl, TArray<int32>& Inputs, const TArray<UEdGraphPin*>& CallInputs, const TArray<UEdGraphPin*>& CallOutputs,
	FNiagaraFunctionSignature& OutSignature)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_RegisterFunctionCall);

	//////////////////////////////////////////////////////////////////////////
	if (Source)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_RegisterFunctionCall_Source);
		UNiagaraGraph* SourceGraph = CastChecked<UNiagaraGraph>(Source->NodeGraph);

		bool bHasNumericInputs = false;
		if (SourceGraph->HasNumericParameters())
		{
			for (int32 i = 0; i < CallInputs.Num(); i++)
			{
				if (Schema->PinToTypeDefinition(CallInputs[i]) == FNiagaraTypeDefinition::GetGenericNumericDef())
				{
					bHasNumericInputs = true;
				}
			}
		}
		TArray<UEdGraphPin*> StaticSwitchValues;
		for (FNiagaraVariable StaticSwitchInput : SourceGraph->FindStaticSwitchInputs())
		{
			for (UEdGraphPin* Pin : CallInputs)
			{
				if (StaticSwitchInput.GetName().IsEqual(Pin->GetFName()))
				{
					StaticSwitchValues.Add(Pin);
					break;
				}
			}
		}

		bool bHasParameterMapParameters = SourceGraph->HasParameterMapParameters();

		GenerateFunctionSignature(ScriptUsage, InName, InFullName, SourceGraph, Inputs, bHasNumericInputs, bHasParameterMapParameters, StaticSwitchValues, OutSignature);

		// 		//Sort the input and outputs to match the sorted parameters. They may be different.
		// 		TArray<FNiagaraVariable> OrderedInputs;
		// 		TArray<FNiagaraVariable> OrderedOutputs;
		// 		SourceGraph->GetParameters(OrderedInputs, OrderedOutputs);
		// 		TArray<UEdGraphPin*> InPins;
		// 		FunctionNode->GetInputPins(InPins);
		// 
		// 		TArray<int32> OrderedInputChunks;
		// 		OrderedInputChunks.SetNumUninitialized(Inputs.Num());
		// 		for (int32 i = 0; i < InPins.Num(); ++i)
		// 		{
		// 			FNiagaraVariable PinVar(Schema->PinToTypeDefinition(InPins[i]), *InPins[i]->PinName);
		// 			int32 InputIdx = OrderedInputs.IndexOfByKey(PinVar);
		// 			check(InputIdx != INDEX_NONE);
		// 			OrderedInputChunks[i] = Inputs[InputIdx];
		// 		}
		// 		Inputs = OrderedInputChunks;

		FString* FuncBody = Functions.Find(OutSignature);
		if (!FuncBody)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_FuncBody);

			if (OutSignature.Name == NAME_None)
			{
				const FString* ModuleAlias = ActiveHistoryForFunctionCalls.GetModuleAlias();
				Error(FText::Format(LOCTEXT("FunctionCallMissingFunction", "Function call signature does not reference a function. Top-level module: {0} Source: {1}"), ModuleAlias ? FText::FromString(*ModuleAlias) : FText::FromString(TEXT("Unknown module")), FText::FromString(CompileOptions.FullName)), nullptr, nullptr);
				return;
			}

			bool bIsModuleFunction = false;
			bool bStageMinFilter = false;
			bool bStageMaxFilter = false;
			FString MinParam;
			FString MaxParam;
			FString MinParamSpawn;
			FString MaxParamSpawn;

			static const auto UseShaderStagesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.UseShaderStages"));
			const bool UseShaderStages = (UseShaderStagesCVar->GetInt() == 1) && CompilationTarget == ENiagaraSimTarget::GPUComputeSim;

			//We've not compiled this function yet so compile it now.
			EnterFunction(InName, OutSignature, Inputs, CallNodeId);

			UNiagaraNodeOutput* FuncOutput = SourceGraph->FindOutputNode(ScriptUsage);
			check(FuncOutput);

			if (ActiveHistoryForFunctionCalls.GetModuleAlias() != nullptr)
			{
				bool bIsInTopLevelFunction = ActiveHistoryForFunctionCalls.InTopLevelFunctionCall(CompileOptions.TargetUsage);

				UEdGraphPin* ParamMapPin = nullptr;
				for (UEdGraphPin* Pin : CallInputs)
				{
					if (Schema->PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
					{
						ParamMapPin = Pin;
						break;
					}
				}

				if (ParamMapPin != nullptr)
				{
					bIsModuleFunction = (bIsInTopLevelFunction && ParamMapPin != nullptr && UNiagaraScript::IsGPUScript(CompileOptions.TargetUsage));

					UNiagaraNode* ParamNode = Cast<UNiagaraNode>(ParamMapPin->GetOwningNode());
					if (ParamNode)
					{
						check(ParamMapHistories.Num() == TranslationStages.Num());
						const FNiagaraParameterMapHistory& History = ParamMapHistories[ActiveStageIdx];
						uint32 FoundIdx = History.MapNodeVisitations.Find(ParamNode);
						if (FoundIdx != INDEX_NONE)
						{
							check((uint32)History.MapNodeVariableMetaData.Num() > FoundIdx);
							check(INDEX_NONE != History.MapNodeVariableMetaData[FoundIdx].Key);
							check(INDEX_NONE != History.MapNodeVariableMetaData[FoundIdx].Value);

							for (uint32 VarIdx = History.MapNodeVariableMetaData[FoundIdx].Key; VarIdx < History.MapNodeVariableMetaData[FoundIdx].Value; VarIdx++)
							{
								if (History.PerVariableReadHistory[VarIdx].Num() == 0)
								{
									// We don't need to worry about defaults if the variable is only written to.
									continue;
								}

								const FNiagaraVariable& Var = History.Variables[VarIdx];
								const FNiagaraVariable& AliasedVar = History.VariablesWithOriginalAliasesIntact[VarIdx];
								bool bIsAliased = Var.GetName() != AliasedVar.GetName();

								if (bIsAliased && UseShaderStages)
								{
									if (Var.GetName().ToString().Contains(TEXT("MinStage")))
									{
										MinParam = TEXT("Context.MapUpdate.Constants.Emitter.") + Var.GetName().ToString();
										MinParamSpawn = TEXT("Context.MapSpawn.Constants.Emitter.") + Var.GetName().ToString();
										bStageMinFilter = true;
									}
									if (Var.GetName().ToString().Contains(TEXT("MaxStage")))
									{
										MaxParam = TEXT("Context.MapUpdate.Constants.Emitter.") + Var.GetName().ToString();
										MaxParamSpawn = TEXT("Context.MapSpawn.Constants.Emitter.") + Var.GetName().ToString();
										bStageMaxFilter = true;
									}
								}

								// For non aliased values we resolve the defaults once at the top level since it's impossible to know which context they were actually used in, but
								// for aliased values we check to see if they're used in the current context by resolving the alias and checking against the current resolved variable
								// name since aliased values can only be resolved for reading in the correct context.
								bool bIsValidForCurrentCallingContext = (bIsInTopLevelFunction && bIsAliased == false) || (bIsAliased && ActiveHistoryForFunctionCalls.ResolveAliases(AliasedVar).GetName() == Var.GetName());
								if (bIsValidForCurrentCallingContext)
								{
									int32 LastSetChunkIdx = ParamMapSetVariablesToChunks[ActiveStageIdx][VarIdx];
									if (LastSetChunkIdx == INDEX_NONE)
									{
										const UEdGraphPin* DefaultPin = History.GetDefaultValuePin(VarIdx);
										UNiagaraScriptVariable* ScriptVariable = SourceGraph->GetScriptVariable(AliasedVar);
										HandleParameterRead(ActiveStageIdx, AliasedVar, DefaultPin, ParamNode, LastSetChunkIdx, ScriptVariable);
										
										// If this variable was in the pending defaults list, go ahead and remove it
										// as we added it before first use...
										if (DeferredVariablesMissingDefault.Contains(Var))
										{
											DeferredVariablesMissingDefault.Remove(Var);
											UniqueVarToChunk.Add(Var, LastSetChunkIdx);
										}
									}
								}
							}
						}
					}
				}
			}

			//Track the start of this function in the chunks so we can remove them after we grab the function hlsl.
			int32 ChunkStart = CodeChunks.Num();
			int32 ChunkStartsByMode[(int32)ENiagaraCodeChunkMode::Num];
			for (int32 i = 0; i < (int32)ENiagaraCodeChunkMode::Num; ++i)
			{
				ChunkStartsByMode[i] = ChunksByMode[i].Num();
			}

			FHlslNiagaraTranslator* ThisTranslator = this;
			TArray<int32> FuncOutputChunks;

			ENiagaraCodeChunkMode OldMode = CurrentBodyChunkMode;
			CurrentBodyChunkMode = ENiagaraCodeChunkMode::Body;
			{
				SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_RegisterFunctionCall_Compile);
				FuncOutput->Compile(ThisTranslator, FuncOutputChunks);
			}
			CurrentBodyChunkMode = OldMode;

			{
				SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_RegisterFunctionCall_FunctionDefStr);
				//Grab all the body chunks for this function.
				FString FunctionDefStr;
				FunctionDefStr.Reserve(256 * ChunksByMode[(int32)ENiagaraCodeChunkMode::Body].Num());

				if (bIsModuleFunction)
				{
					if (UseShaderStages)
					{
						if (bStageMinFilter && bStageMaxFilter)
						{
							FunctionDefStr += TEXT("if ((GCurrentPhase == 1 && ShaderStageIndex >= ") + MinParam + TEXT(" && ShaderStageIndex <= ") + MaxParam + TEXT(") || ") +
								TEXT("(GCurrentPhase == 0 && ShaderStageIndex >= ") + MinParamSpawn + TEXT(" && ShaderStageIndex <= ") + MaxParamSpawn + TEXT(")") +
								TEXT(")\n{\n");
						}
						else
						{
							FunctionDefStr += TEXT("if ((GCurrentPhase == 0 && ShaderStageIndex == 0) || (GCurrentPhase == 1 && ShaderStageIndex == DefaultShaderStageIndex))\n{\n");
						}
					}
				}

				for (int32 i = ChunkStartsByMode[(int32)ENiagaraCodeChunkMode::Body]; i < ChunksByMode[(int32)ENiagaraCodeChunkMode::Body].Num(); ++i)
				{
					FunctionDefStr += GetCode(ChunksByMode[(int32)ENiagaraCodeChunkMode::Body][i]);
				}

				if (bIsModuleFunction)
				{
					if (UseShaderStages)
					{
						FunctionDefStr += TEXT("}\n");
					}
				}

				//Now remove all chunks for the function again.			
				//This is super hacky. Should move chunks etc into a proper scoping system.

				TArray<FNiagaraCodeChunk> FuncUniforms;
				FuncUniforms.Reserve(1024);
				for (int32 i = 0; i < (int32)ENiagaraCodeChunkMode::Num; ++i)
				{
					//Keep uniform chunks.
					if (i == (int32)ENiagaraCodeChunkMode::Uniform)
					{
						for (int32 ChunkIdx = ChunkStartsByMode[i]; ChunkIdx < ChunksByMode[i].Num(); ++ChunkIdx)
						{
							FuncUniforms.Add(CodeChunks[ChunksByMode[i][ChunkIdx]]);
						}
					}

					ChunksByMode[i].RemoveAt(ChunkStartsByMode[i], ChunksByMode[i].Num() - ChunkStartsByMode[i]);
				}
				CodeChunks.RemoveAt(ChunkStart, CodeChunks.Num() - ChunkStart, false);

				//Re-add the uniforms. Really this is horrible. Rework soon.
				for (FNiagaraCodeChunk& Chunk : FuncUniforms)
				{
					ChunksByMode[(int32)ENiagaraCodeChunkMode::Uniform].Add(CodeChunks.Add(Chunk));
				}

				// We don't support an empty function definition when calling a real function.
				if (FunctionDefStr.IsEmpty())
				{
					FunctionDefStr += TEXT("\n");
				}

				Functions.Add(OutSignature, FunctionDefStr);
			}

			ExitFunction();
		}
	}
	else
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_RegisterFunctionCall_Signature);

		check(InSignature.IsValid());
		check(Inputs.Num() > 0);

		OutSignature = InSignature;

		//First input for these is the owner of the function.
		if (bIsCustomHlsl)
		{
			FString* FuncBody = Functions.Find(OutSignature);
			if (!FuncBody)
			{
				//We've not compiled this function yet so compile it now.
				EnterFunction(InName, OutSignature, Inputs, CallNodeId);

				FString FunctionDefStr = InCustomHlsl;
				// We don't support an empty function definition when calling a real function.
				if (FunctionDefStr.IsEmpty())
				{
					FunctionDefStr += TEXT("\n");
				}

				Functions.Add(OutSignature, FunctionDefStr);

				ExitFunction();
			}
		}
		else if (!InSignature.bMemberFunction) // Fastpath or other provided function
		{
			if (INDEX_NONE == CompilationOutput.ScriptData.AdditionalExternalFunctions.Find(OutSignature))
			{
				CompilationOutput.ScriptData.AdditionalExternalFunctions.Add(OutSignature);
			}
			Functions.FindOrAdd(OutSignature);
		}
		else
		{
			int32 OwnerIdx = Inputs[0];
			if (OwnerIdx < 0 || OwnerIdx >= CompilationOutput.ScriptData.DataInterfaceInfo.Num())
			{
				Error(LOCTEXT("FunctionCallDataInterfaceMissingRegistration", "Function call signature does not match to a registered DataInterface. Valid DataInterfaces should be wired into a DataInterface function call."), nullptr, nullptr);
				return;
			}
			FNiagaraScriptDataInterfaceCompileInfo& Info = CompilationOutput.ScriptData.DataInterfaceInfo[OwnerIdx];

			// Double-check to make sure that the signature matches those specified by the data 
			// interface. It could be that the existing node has been removed and the graph
			// needs to be refactored. If that's the case, emit an error.
			UObject* const* FoundCDO = CompileData->CDOs.Find(Info.Type.GetClass());
			if (FoundCDO == nullptr)
			{
				// If the cdo wasn't found, the data interface was not passed through a parameter map and so it won't be bound correctly, so add a compile error
				// and invalidate the signature.
				Error(LOCTEXT("DataInterfaceNotFoundInParameterMap", "Data interfaces can not be sampled directly, they must be passed through a parameter map to be bound correctly."), nullptr, nullptr);
				OutSignature.Name = NAME_None;
				return;
			}

			UNiagaraDataInterface* CDO = Cast<UNiagaraDataInterface>(*FoundCDO);
			if (CDO != nullptr && OutSignature.bMemberFunction)
			{
				TArray<FNiagaraFunctionSignature> DataInterfaceFunctions;
				CDO->GetFunctions(DataInterfaceFunctions);

				const bool bFoundMatch = DataInterfaceFunctions.ContainsByPredicate([&](const FNiagaraFunctionSignature& Sig) -> bool { return Sig.EqualsIgnoringSpecifiers(OutSignature); });
				if (!bFoundMatch)
				{
					Error(LOCTEXT("FunctionCallDataInterfaceMissing", "Function call signature does not match DataInterface possible signatures?"), nullptr, nullptr);
					return;
				}

				// We only use this for GPU systems currently so that we emit only the functionality required
				DataInterfaceRegisteredFunctions.FindOrAdd(Info.Type.GetFName()).Add(OutSignature);

				if (Info.UserPtrIdx != INDEX_NONE && CompilationTarget != ENiagaraSimTarget::GPUComputeSim)
				{
					//This interface requires per instance data via a user ptr so place the index to it at the end of the inputs.
					Inputs.Add(AddSourceChunk(LexToString(Info.UserPtrIdx), FNiagaraTypeDefinition::GetIntDef(), false));
					OutSignature.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("InstanceData")));
				}
			}

			//Override the owner id of the signature with the actual caller.
			OutSignature.OwnerName = Info.Name;
			Info.RegisteredFunctions.Add(OutSignature);

			Functions.FindOrAdd(OutSignature);
		}

	}
}

void FHlslNiagaraTranslator::GenerateFunctionCall(ENiagaraScriptUsage ScriptUsage, FNiagaraFunctionSignature& FunctionSignature, TArray<int32>& Inputs, TArray<int32>& Outputs)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_Module_NiagaraHLSLTranslator_GenerateFunctionCall);

	bool bEnteredStatScope = false;   
	if (ScriptUsage == ENiagaraScriptUsage::Module)
	{
		bEnteredStatScope = true;
		EnterStatsScope(FNiagaraStatScope(*GetFunctionSignatureSymbol(FunctionSignature), *(FunctionSignature.GetName())));
	}

	TArray<FString> MissingParameters;
	int32 ParamIdx = 0;
	TArray<int32> Params;
	Params.Reserve(Inputs.Num() + Outputs.Num());
	FString DefStr = GetFunctionSignatureSymbol(FunctionSignature) + TEXT("(");
	for (int32 i = 0; i < FunctionSignature.Inputs.Num(); ++i)
	{
		FNiagaraTypeDefinition Type = FunctionSignature.Inputs[i].GetType();
		//We don't write class types as real params in the hlsl
		if (!Type.GetClass())
		{
			if (!AddStructToDefinitionSet(Type))
			{
				Error(FText::Format(LOCTEXT("GetConstantFailTypeVar2", "Cannot handle type {0}! Variable: {1}"), Type.GetNameText(), FText::FromName(FunctionSignature.Inputs[i].GetName())), nullptr, nullptr);
			}

			int32 Input = Inputs[i];
			bool bSkip = false;

			if (FunctionSignature.Inputs[i].GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				Input = INDEX_NONE;
				bSkip = true;
			}

			if (!bSkip)
			{
				if (ParamIdx != 0)
				{
					DefStr += TEXT(", ");
				}

				Params.Add(Input);
				if (Input == INDEX_NONE)
				{
					MissingParameters.Add(FunctionSignature.Inputs[i].GetName().ToString());
				}
				else
				{
					DefStr += FString::Printf(TEXT("{%d}"), ParamIdx);
				}
				++ParamIdx;
			}
		}
	}

	for (int32 i = 0; i < FunctionSignature.Outputs.Num(); ++i)
	{
		FNiagaraVariable& OutVar = FunctionSignature.Outputs[i];
		FNiagaraTypeDefinition Type = OutVar.GetType();

		//We don't write class types as real params in the hlsl
		if (!Type.GetClass())
		{
			if (!AddStructToDefinitionSet(Type))
			{
				Error(FText::Format(LOCTEXT("GetConstantFailTypeVar3", "Cannot handle type {0}! Variable: {1}"), Type.GetNameText(), FText::FromName(FunctionSignature.Outputs[i].GetName())), nullptr, nullptr);
			}

			int32 Output = INDEX_NONE;
			int32 ParamOutput = INDEX_NONE;
			bool bSkip = false;
			if (FunctionSignature.Outputs[i].GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				if (i < FunctionSignature.Inputs.Num() && FunctionSignature.Inputs[i].GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
				{
					Output = Inputs[i];
				}
				bSkip = true;
			}
			else
			{
				FString OutputStr = FString::Printf(TEXT("%sOutput_%s"), *GetFunctionSignatureSymbol(FunctionSignature), *OutVar.GetName().ToString());
				Output = AddBodyChunk(GetUniqueSymbolName(*OutputStr), TEXT(""), OutVar.GetType());
				ParamOutput = Output;
			}

			Outputs.Add(Output);

			if (!bSkip)
			{
				if (ParamIdx > 0)
				{
					DefStr += TEXT(", ");
				}

				Params.Add(ParamOutput);
				if (ParamOutput == INDEX_NONE)
				{
					MissingParameters.Add(OutVar.GetName().ToString());
				}
				else
				{
					DefStr += FString::Printf(TEXT("{%d}"), ParamIdx);
				}
				++ParamIdx;
			}
		}
	}

	if (FunctionSignature.bRequiresContext)
	{
		if (ParamIdx > 0)
		{
			DefStr += TEXT(", ");
		}
		DefStr += "Context";
	}

	DefStr += TEXT(")");

	if (MissingParameters.Num() > 0)
	{
		for (FString MissingParam : MissingParameters)
		{
			FText Fmt = LOCTEXT("ErrorCompilingParameterFmt", "Error compiling parameter {0} in function call {1}");
			FText ErrorText = FText::Format(Fmt, FText::FromString(MissingParam), FText::FromString(GetFunctionSignatureSymbol(FunctionSignature)));
			Error(ErrorText, nullptr, nullptr);
		}
		return;
	}

	AddBodyChunk(TEXT(""), DefStr, FNiagaraTypeDefinition::GetFloatDef(), Params);

	if (bEnteredStatScope)
	{
		ExitStatsScope();
	}
}

FString FHlslNiagaraTranslator::GetFunctionSignatureSymbol(const FNiagaraFunctionSignature& Sig)
{
	FString SigStr = Sig.GetName();
	if (!Sig.OwnerName.IsNone() && Sig.OwnerName.IsValid())
	{
		SigStr += TEXT("_") + Sig.OwnerName.ToString().Replace(TEXT("."), TEXT(""));;
	}
	else
	{
		SigStr += TEXT("_Func_");
	}
	for (const TTuple<FName, FName>& Specifier : Sig.FunctionSpecifiers)
	{
		SigStr += TEXT("_") + Specifier.Key.ToString() + Specifier.Value.ToString().Replace(TEXT("."), TEXT(""));
	}
	return GetSanitizedSymbolName(SigStr);
}

FString FHlslNiagaraTranslator::GetFunctionSignature(const FNiagaraFunctionSignature& Sig)
{
	FString SigStr = TEXT("void ") + GetFunctionSignatureSymbol(Sig);

	SigStr += TEXT("(");
	int32 ParamIdx = 0;
	for (int32 i = 0; i < Sig.Inputs.Num(); ++i)
	{
		const FNiagaraVariable& Input = Sig.Inputs[i];
		//We don't write class types as real params in the hlsl
		if (Input.GetType().GetClass() == nullptr)
		{
			if (Input.GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				// Skip parameter maps.
			}
			else
			{
				if (ParamIdx > 0)
				{
					SigStr += TEXT(", ");
				}

				SigStr += FHlslNiagaraTranslator::GetStructHlslTypeName(Input.GetType()) + TEXT(" In_") + FHlslNiagaraTranslator::GetSanitizedSymbolName(Input.GetName().ToString(), true);
				++ParamIdx;
			}
		}
	}

	for (int32 i = 0; i < Sig.Outputs.Num(); ++i)
	{
		const FNiagaraVariable& Output = Sig.Outputs[i];
		//We don't write class types as real params in the hlsl
		if (Output.GetType().GetClass() == nullptr)
		{
			if (Output.GetType() == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				// Skip output parameter maps..
			}
			else
			{
				if (ParamIdx > 0)
				{
					SigStr += TEXT(", ");
				}

				SigStr += TEXT("out ") + FHlslNiagaraTranslator::GetStructHlslTypeName(Output.GetType()) + TEXT(" ") + FHlslNiagaraTranslator::GetSanitizedSymbolName(TEXT("Out_") + Output.GetName().ToString());
				++ParamIdx;
			}
		}
	}
	if (Sig.bRequiresContext)
	{
		if (ParamIdx > 0)
		{
			SigStr += TEXT(", ");
		}
		SigStr += TEXT("inout FSimulationContext Context");
	}
	return SigStr + TEXT(")");
}

int32 GetPinIndexById(const TArray<UEdGraphPin*>& Pins, FGuid PinId)
{
	for (int32 i = 0; i < Pins.Num(); ++i)
	{
		if (Pins[i]->PinId == PinId)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

FNiagaraTypeDefinition FHlslNiagaraTranslator::GetChildType(const FNiagaraTypeDefinition& BaseType, const FName& PropertyName)
{
	const UScriptStruct* Struct = BaseType.GetScriptStruct();
	if (Struct != nullptr)
	{
		// Dig through properties to find the matching property native type (if it exists)
		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			if (Property->GetName() == PropertyName.ToString())
			{
				if (Property->IsA(FFloatProperty::StaticClass()))
				{
					return FNiagaraTypeDefinition::GetFloatDef();
				}
				else if (Property->IsA(FIntProperty::StaticClass()))
				{
					return FNiagaraTypeDefinition::GetIntDef();
				}
				else if (Property->IsA(FBoolProperty::StaticClass()))
				{
					return FNiagaraTypeDefinition::GetBoolDef();
				}
				else if (Property->IsA(FEnumProperty::StaticClass()))
				{
					const FEnumProperty* EnumProp = CastField<FEnumProperty>(Property);
					return FNiagaraTypeDefinition(EnumProp->GetEnum());
				}
				else if (Property->IsA(FByteProperty::StaticClass()))
				{
					const FByteProperty* ByteProp = CastField<FByteProperty>(Property);
					return FNiagaraTypeDefinition(ByteProp->GetIntPropertyEnum());
				}
				else if (const FStructProperty* StructProp = CastFieldChecked<const FStructProperty>(Property))
				{
					return FNiagaraTypeDefinition(StructProp->Struct);
				}
			}
		}
	}
	return FNiagaraTypeDefinition();
}

FString FHlslNiagaraTranslator::ComputeMatrixColumnAccess(const FString& Name)
{
	FString Value;
	int32 Column = -1;

	if (Name.Find("X", ESearchCase::IgnoreCase) != -1)
		Column = 0;
	else if (Name.Find("Y", ESearchCase::IgnoreCase) != -1)
		Column = 1;
	else if (Name.Find("Z", ESearchCase::IgnoreCase) != -1)
		Column = 2;
	else if (Name.Find("W", ESearchCase::IgnoreCase) != -1)
		Column = 3;

	if (Column != -1)
	{
		Value.Append("[");
		Value.AppendInt(Column);
		Value.Append("]");
	}
	else
	{
		Error(FText::FromString("Failed to generate type for " + Name + " up to path " + Value), nullptr, nullptr);
	}
	return Value;
}

FString FHlslNiagaraTranslator::ComputeMatrixRowAccess(const FString& Name)
{
	FString Value;
	int32 Row = -1;
	if (Name.Find("Row0", ESearchCase::IgnoreCase) != -1)
		Row = 0;
	else if (Name.Find("Row1", ESearchCase::IgnoreCase) != -1)
		Row = 1;
	else if (Name.Find("Row2", ESearchCase::IgnoreCase) != -1)
		Row = 2;
	else if (Name.Find("Row3", ESearchCase::IgnoreCase) != -1)
		Row = 3;

	if (Row != -1)
	{
		Value.Append("[");
		Value.AppendInt(Row);
		Value.Append("]");
	}
	else
	{
		Error(FText::FromString("Failed to generate type for " + Name + " up to path " + Value), nullptr, nullptr);
	}
	return Value;
}

FString FHlslNiagaraTranslator::NamePathToString(const FString& Prefix, const FNiagaraTypeDefinition& RootType, const TArray<FName>& NamePath)
{
	// We need to deal with matrix parameters differently than any other type by using array syntax.
	// As we recurse down the tree, we stay away of when we're dealing with a matrix and adjust 
	// accordingly.
	FString Value = Prefix;
	FNiagaraTypeDefinition CurrentType = RootType;
	bool bParentWasMatrix = (RootType == FNiagaraTypeDefinition::GetMatrix4Def());
	int32 ParentMatrixRow = -1;
	for (int32 i = 0; i < NamePath.Num(); i++)
	{
		FString Name = NamePath[i].ToString();
		CurrentType = GetChildType(CurrentType, NamePath[i]);
		// Found a matrix... brackets from here on out.
		if (CurrentType == FNiagaraTypeDefinition::GetMatrix4Def())
		{
			bParentWasMatrix = true;
			Value.Append("." + Name);
		}
		// Parent was a matrix, determine row..
		else if (bParentWasMatrix && CurrentType == FNiagaraTypeDefinition::GetVec4Def())
		{
			Value.Append(ComputeMatrixRowAccess(Name));
		}
		// Parent was a matrix, determine column...
		else if (bParentWasMatrix && CurrentType == FNiagaraTypeDefinition::GetFloatDef())
		{
			Value.Append(ComputeMatrixColumnAccess(Name));
		}
		// Handle all other valid types by just using "." 
		else if (CurrentType.IsValid())
		{
			Value.Append("." + Name);
		}
		else
		{
			Error(FText::FromString("Failed to generate type for " + Name + " up to path " + Value), nullptr, nullptr);
		}
	}
	return Value;
}

FString FHlslNiagaraTranslator::GenerateAssignment(const FNiagaraTypeDefinition& SrcPinType, const TArray<FName>& ConditionedSourcePath, const FNiagaraTypeDefinition& DestPinType, const TArray<FName>& ConditionedDestinationPath)
{
	FString SourceDefinition = NamePathToString("{1}", SrcPinType, ConditionedSourcePath);
	FString DestinationDefinition = NamePathToString("{0}", DestPinType, ConditionedDestinationPath);

	return DestinationDefinition + " = " + SourceDefinition;
}

void FHlslNiagaraTranslator::Convert(class UNiagaraNodeConvert* Convert, TArray <int32>& Inputs, TArray<int32>& Outputs)
{
	if (ValidateTypePins(Convert) == false)
	{
		return;
	}

	TArray<UEdGraphPin*> InputPins;
	Convert->GetInputPins(InputPins);

	TArray<UEdGraphPin*> OutputPins;
	Convert->GetOutputPins(OutputPins);

	// Add input struct definitions if necessary.
	for (UEdGraphPin* InputPin : InputPins)
	{
		if (InputPin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryType ||
			InputPin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryEnum)
		{
			FNiagaraTypeDefinition Type = Schema->PinToTypeDefinition(InputPin);
			if (!AddStructToDefinitionSet(Type))
			{
				Error(FText::Format(LOCTEXT("ConvertTypeError_InvalidInput", "Cannot handle input pin type {0}! Pin: {1}"), Type.GetNameText(), InputPin->GetDisplayName()), nullptr, nullptr);
			}
		}
	}

	// Generate outputs.
	for (UEdGraphPin* OutputPin : OutputPins)
	{
		if (OutputPin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryType ||
			OutputPin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryEnum)
		{
			FNiagaraTypeDefinition Type = Schema->PinToTypeDefinition(OutputPin);
			if (!AddStructToDefinitionSet(Type))
			{
				Error(FText::Format(LOCTEXT("ConvertTypeError_InvalidOutput", "Cannot handle output pin type {0}! Pin: {1}"), Type.GetNameText(), OutputPin->GetDisplayName()), nullptr, nullptr);
			}
			int32 OutChunk = AddBodyChunk(GetUniqueSymbolName(OutputPin->PinName), TEXT(""), Type);
			Outputs.Add(OutChunk);
		}
	}

	// Add an additional invalid output for the add pin which doesn't get compiled.
	Outputs.Add(INDEX_NONE);

	// Set output values based on connections.
	for (FNiagaraConvertConnection& Connection : Convert->GetConnections())
	{
		int32 SourceIndex = GetPinIndexById(InputPins, Connection.SourcePinId);
		int32 DestinationIndex = GetPinIndexById(OutputPins, Connection.DestinationPinId);
		if (SourceIndex != INDEX_NONE && SourceIndex < Inputs.Num() && DestinationIndex != INDEX_NONE && DestinationIndex < Outputs.Num())
		{
			FNiagaraTypeDefinition SrcPinType = Schema->PinToTypeDefinition(InputPins[SourceIndex]);
			if (!AddStructToDefinitionSet(SrcPinType))
			{
				Error(FText::Format(LOCTEXT("ConvertTypeError_InvalidSubpinInput", "Cannot handle input subpin type {0}! Subpin: {1}"), SrcPinType.GetNameText(), InputPins[SourceIndex]->GetDisplayName()), nullptr, nullptr);
			}
			TArray<FName> ConditionedSourcePath = ConditionPropertyPath(SrcPinType, Connection.SourcePath);

			FNiagaraTypeDefinition DestPinType = Schema->PinToTypeDefinition(OutputPins[DestinationIndex]);
			if (!AddStructToDefinitionSet(DestPinType))
			{
				Error(FText::Format(LOCTEXT("ConvertTypeError_InvalidSubpinOutput", "Cannot handle output subpin type type {0}! Subpin: {1}"), DestPinType.GetNameText(), OutputPins[SourceIndex]->GetDisplayName()), nullptr, nullptr);
			}
			TArray<FName> ConditionedDestinationPath = ConditionPropertyPath(DestPinType, Connection.DestinationPath);

			FString ConvertDefinition = GenerateAssignment(SrcPinType, ConditionedSourcePath, DestPinType, ConditionedDestinationPath);

			TArray<int32> SourceChunks;
			SourceChunks.Add(Outputs[DestinationIndex]);
			SourceChunks.Add(Inputs[SourceIndex]);
			AddBodyChunk(TEXT(""), ConvertDefinition, FNiagaraTypeDefinition::GetFloatDef(), SourceChunks);
		}
	}
}

void FHlslNiagaraTranslator::If(UNiagaraNodeIf* IfNode, TArray<FNiagaraVariable>& Vars, int32 Condition, TArray<int32>& PathA, TArray<int32>& PathB, TArray<int32>& Outputs)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslTranslator_If);

	int32 NumVars = Vars.Num();
	check(PathA.Num() == NumVars);
	check(PathB.Num() == NumVars);

	TArray<FString> OutSymbols;
	OutSymbols.Reserve(Vars.Num());
	int32 PinIdx = 1;
	for (FNiagaraVariable& Var : Vars)
	{
		FNiagaraTypeDefinition Type = Schema->PinToTypeDefinition(IfNode->GetInputPin(PinIdx++));
		if (!AddStructToDefinitionSet(Type))
		{
			FText OutErrorMessage = FText::Format(LOCTEXT("UnknownNumeric", "Variable in If node uses invalid type. Var: {0} Type: {1}"),
				FText::FromName(Var.GetName()), Type.GetNameText());

			Error(OutErrorMessage, IfNode, nullptr);
		}
		OutSymbols.Add(GetUniqueSymbolName(*(Var.GetName().ToString() + TEXT("_IfResult"))));
		Outputs.Add(AddBodyChunk(OutSymbols.Last(), TEXT(""), Type, true));
	}
	AddBodyChunk(TEXT(""), TEXT("if({0})\n\t{"), FNiagaraTypeDefinition::GetFloatDef(), Condition, false, false);
	for (int32 i = 0; i < NumVars; ++i)
	{
		FNiagaraTypeDefinition OutChunkType = CodeChunks[Outputs[i]].Type;
		FNiagaraCodeChunk& BranchChunk = CodeChunks[AddBodyChunk(OutSymbols[i], TEXT("{0}"), OutChunkType, false)];
		BranchChunk.AddSourceChunk(PathA[i]);
	}
	AddBodyChunk(TEXT(""), TEXT("}\n\telse\n\t{"), FNiagaraTypeDefinition::GetFloatDef(), false, false);
	for (int32 i = 0; i < NumVars; ++i)
	{
		FNiagaraTypeDefinition OutChunkType = CodeChunks[Outputs[i]].Type;
		FNiagaraCodeChunk& BranchChunk = CodeChunks[AddBodyChunk(OutSymbols[i], TEXT("{0}"), OutChunkType, false)];
		BranchChunk.AddSourceChunk(PathB[i]);
	}
	AddBodyChunk(TEXT(""), TEXT("}"), FNiagaraTypeDefinition::GetFloatDef(), false, false);

	// Add an additional invalid output for the add pin which doesn't get compiled.
	Outputs.Add(INDEX_NONE);
}

int32 FHlslNiagaraTranslator::CompilePin(const UEdGraphPin* Pin)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslTranslator_CompilePin);

	check(Pin);
	int32 Ret = INDEX_NONE;
	FNiagaraTypeDefinition TypeDef = Schema->PinToTypeDefinition(Pin);
	if (Pin->Direction == EGPD_Input)
	{
		if (Pin->LinkedTo.Num() > 0)
		{
			Ret = CompileOutputPin(Pin->LinkedTo[0]);
		}
		else if (!Pin->bDefaultValueIsIgnored && Pin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryType)
		{
			if (TypeDef == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				Error(FText::FromString(TEXT("Parameter Maps must be created via an Input Node, not the default value of a pin! Please connect to a valid input Parameter Map.")), Cast<UNiagaraNode>(Pin->GetOwningNode()), nullptr);
				return INDEX_NONE;
			}
			else
			{
				//No connections to this input so add the default as a const expression.			
				FNiagaraVariable PinVar = Schema->PinToNiagaraVariable(Pin, true);
				return GetConstant(PinVar);
			}
		}
		else if (!Pin->bDefaultValueIsIgnored && Pin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryEnum)
		{
			//No connections to this input so add the default as a const expression.			
			FNiagaraVariable PinVar = Schema->PinToNiagaraVariable(Pin, true);
			return GetConstant(PinVar);
		}
	}
	else
	{
		Ret = CompileOutputPin(Pin);
	}

	return Ret;
}

int32 FHlslNiagaraTranslator::CompileOutputPin(const UEdGraphPin* InPin)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_HlslTranslator_CompileOutputPin);

	if (InPin)
	{
		UpdateStaticSwitchConstants(InPin->GetOwningNode());
	}

	// The incoming pin to compile may be pointing to a reroute node. If so, we just jump over it
	// to where it really came from.
	UEdGraphPin* Pin = UNiagaraNode::TraceOutputPin(const_cast<UEdGraphPin*>(InPin));
	check(Pin->Direction == EGPD_Output);

	// The node can also replace our pin with another pin (e.g. in the case of static switches), so we need to make sure we don't run into a circular dependency
	TSet<UEdGraphPin*> SeenPins;
	UNiagaraNode* Node = Cast<UNiagaraNode>(Pin->GetOwningNode());
	UEdGraphPin* OriginalPin = Pin;
	while (Node->SubstituteCompiledPin(this, &Pin))
	{
		bool bIsAlreadyInSet = false;
		SeenPins.Add(Pin, &bIsAlreadyInSet);
		Node = Cast<UNiagaraNode>(Pin->GetOwningNode());
		if (bIsAlreadyInSet)
		{
			Error(LOCTEXT("CircularGraphSubstitutionError", "Circular dependency detected, please check your module graph."), Node, Pin);
			return INDEX_NONE;
		}
	}

	// It is possible that the output pin was substituted by an input pin (e.g. the default value pin on a node).
	// If that is the case we try to compile that pin directly.
	if (Pin->Direction == EGPD_Input)
	{
		int32* ExistingChunk = PinToCodeChunks.Last().Find(OriginalPin); // Check if the pin was already compiled before
		if (ExistingChunk)
		{
			return *ExistingChunk;
		}
		int32 Chunk = CompilePin(Pin);
		PinToCodeChunks.Last().Add(OriginalPin, Chunk);
		return Chunk;
	}

	int32 Ret = INDEX_NONE;
	int32* Chunk = PinToCodeChunks.Last().Find(Pin);
	if (Chunk)
	{
		Ret = *Chunk; //We've compiled this pin before. Return it's chunk.
	}
	else
	{
		//Otherwise we need to compile the node to get its output pins.
		if (ValidateTypePins(Node))
		{
			TArray<int32> Outputs;
			TArray<UEdGraphPin*> OutputPins;
			Node->GetOutputPins(OutputPins);
			FHlslNiagaraTranslator* ThisTranslator = this;
			Node->Compile(ThisTranslator, Outputs);
			if (OutputPins.Num() == Outputs.Num())
			{
				for (int32 i = 0; i < Outputs.Num(); ++i)
				{
					//Cache off the pin.
					//Can we allow the caching of local defaults in numerous function calls?
					PinToCodeChunks.Last().Add(OutputPins[i], Outputs[i]);

					if (Outputs[i] != INDEX_NONE)
					{
						//Grab the expression for the pin we're currently interested in. Otherwise we'd have to search the map for it.
						Ret = OutputPins[i] == Pin ? Outputs[i] : Ret;
					}
				}
			}
			else
			{
				Error(LOCTEXT("IncorrectNumOutputsError", "Incorect number of outputs. Can possibly be fixed with a graph refresh."), Node, nullptr);
			}
		}
	}

	return Ret;
}

void FHlslNiagaraTranslator::Error(FText ErrorText, const UNiagaraNode* Node, const UEdGraphPin* Pin)
{
	FString NodePinStr = TEXT("");
	FString NodePinPrefix = TEXT(" - ");
	FString NodePinSuffix = TEXT("");
	if (Node)
	{
		FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		if (NodeTitle.Len() > 0)
		{
			NodePinStr += TEXT("Node: ") + NodeTitle;
			NodePinSuffix = TEXT(" - ");
		}
		else
		{
			FString NodeName = Node->GetName();
			if (NodeName.Len() > 0)
			{
				NodePinStr += TEXT("Node: ") + NodeName;
				NodePinSuffix = TEXT(" - ");
			}
		}
	}
	if (Pin)
	{
		NodePinStr += TEXT(" Pin: ") + (Pin->PinFriendlyName.ToString().Len() > 0 ? Pin->PinFriendlyName.ToString() : Pin->GetName());
		NodePinSuffix = TEXT(" - ");
	}

	FString ErrorString = FString::Printf(TEXT("%s%s%s%s"), *ErrorText.ToString(), *NodePinPrefix, *NodePinStr, *NodePinSuffix);
	TranslateResults.CompileEvents.Add(FNiagaraCompileEvent(FNiagaraCompileEventSeverity::Error, ErrorString, Node ? Node->NodeGuid : FGuid(), Pin ? Pin->PersistentGuid : FGuid(), GetCallstackGuids()));
	TranslateResults.NumErrors++;
}

void FHlslNiagaraTranslator::Warning(FText WarningText, const UNiagaraNode* Node, const UEdGraphPin* Pin)
{
	FString NodePinStr = TEXT("");
	FString NodePinPrefix = TEXT(" - ");
	FString NodePinSuffix = TEXT("");
	if (Node && Node->GetName().Len() > 0)
	{
		NodePinStr += TEXT("Node: ") + Node->GetName();
		NodePinSuffix = TEXT(" - ");
	}
	if (Pin && Pin->PinFriendlyName.ToString().Len() > 0)
	{
		NodePinStr += TEXT(" Pin: ") + Pin->PinFriendlyName.ToString();
		NodePinSuffix = TEXT(" - ");
	}

	FString WarnString = FString::Printf(TEXT("%s%s%s%s"), *WarningText.ToString(), *NodePinPrefix, *NodePinStr, *NodePinSuffix);
	TranslateResults.CompileEvents.Add(FNiagaraCompileEvent(FNiagaraCompileEventSeverity::Warning, WarnString, Node ? Node->NodeGuid : FGuid(), Pin ? Pin->PersistentGuid : FGuid(), GetCallstackGuids()));
	TranslateResults.NumWarnings++;
}

bool FHlslNiagaraTranslator::GetFunctionParameter(const FNiagaraVariable& Parameter, int32& OutParam)const
{
	// Assume that it wasn't bound by default.
	OutParam = INDEX_NONE;
	if (const FFunctionContext* FunctionContext = FunctionCtx())
	{
		int32 ParamIdx = FunctionContext->Signature.Inputs.IndexOfByPredicate([&](const FNiagaraVariable& InVar) { return InVar.IsEquivalent(Parameter); });
		if (ParamIdx != INDEX_NONE)
		{
			OutParam = FunctionContext->Inputs[ParamIdx];
		}
		return true;
	}
	return false;
}

bool FHlslNiagaraTranslator::CanReadAttributes()const
{
	if (UNiagaraScript::IsParticleUpdateScript(TranslationStages[ActiveStageIdx].ScriptUsage))
	{
		return true;
	}
	return false;
}

ENiagaraScriptUsage FHlslNiagaraTranslator::GetCurrentUsage() const
{
	if (UNiagaraScript::IsParticleScript(CompileOptions.TargetUsage))
	{
		return CompileOptions.TargetUsage;
	}
	else if (UNiagaraScript::IsSystemSpawnScript(CompileOptions.TargetUsage) || UNiagaraScript::IsSystemUpdateScript(CompileOptions.TargetUsage))
	{
		if (ActiveHistoryForFunctionCalls.ContextContains(ENiagaraScriptUsage::EmitterSpawnScript))
		{
			return ENiagaraScriptUsage::EmitterSpawnScript;
		}
		else if (ActiveHistoryForFunctionCalls.ContextContains(ENiagaraScriptUsage::EmitterUpdateScript))
		{
			return ENiagaraScriptUsage::EmitterUpdateScript;
		}
		return CompileOptions.TargetUsage;
	}
	else if (UNiagaraScript::IsStandaloneScript(CompileOptions.TargetUsage))
	{
		// Since we never use the results of a standalone script directly, just choose one by default.
		return ENiagaraScriptUsage::ParticleSpawnScript;
	}
	else
	{
		check(false);
		return ENiagaraScriptUsage::ParticleSpawnScript;
	}
}

ENiagaraScriptUsage FHlslNiagaraTranslator::GetTargetUsage() const
{
	if (CompileOptions.TargetUsage == ENiagaraScriptUsage::ParticleGPUComputeScript) // Act as if building spawn script.
	{
		return ENiagaraScriptUsage::ParticleSpawnScript;
	}
	if (UNiagaraScript::IsInterpolatedParticleSpawnScript(CompileOptions.TargetUsage))
	{
		return ENiagaraScriptUsage::ParticleSpawnScript;
	}
	return CompileOptions.TargetUsage;
}

FGuid FHlslNiagaraTranslator::GetTargetUsageId() const
{
	return CompileOptions.TargetUsageId;
}
//////////////////////////////////////////////////////////////////////////

FString FHlslNiagaraTranslator::GetHlslDefaultForType(FNiagaraTypeDefinition Type)
{
	if (Type == FNiagaraTypeDefinition::GetFloatDef())
	{
		return "(0.0)";
	}
	else if (Type == FNiagaraTypeDefinition::GetVec2Def())
	{
		return "float2(0.0,0.0)";
	}
	else if (Type == FNiagaraTypeDefinition::GetVec3Def())
	{
		return "float3(0.0,0.0,0.0)";
	}
	else if (Type == FNiagaraTypeDefinition::GetVec4Def())
	{
		return "float4(0.0,0.0,0.0,0.0)";
	}
	else if (Type == FNiagaraTypeDefinition::GetQuatDef())
	{
		return "float4(0.0,0.0,0.0,1.0)";
	}
	else if (Type == FNiagaraTypeDefinition::GetColorDef())
	{
		return "float4(1.0,1.0,1.0,1.0)";
	}
	else if (Type == FNiagaraTypeDefinition::GetIntDef())
	{
		return "(0)";
	}
	else if (Type == FNiagaraTypeDefinition::GetBoolDef())
	{
		return "(false)";
	}
	else
	{
		return TEXT("(") + GetStructHlslTypeName(Type) + TEXT(")0");
	}
}

bool FHlslNiagaraTranslator::IsBuiltInHlslType(FNiagaraTypeDefinition Type)
{
	return
		Type == FNiagaraTypeDefinition::GetFloatDef() ||
		Type == FNiagaraTypeDefinition::GetVec2Def() ||
		Type == FNiagaraTypeDefinition::GetVec3Def() ||
		Type == FNiagaraTypeDefinition::GetVec4Def() ||
		Type == FNiagaraTypeDefinition::GetColorDef() ||
		Type == FNiagaraTypeDefinition::GetQuatDef() ||
		Type == FNiagaraTypeDefinition::GetMatrix4Def() ||
		Type == FNiagaraTypeDefinition::GetIntDef() ||
		Type.GetStruct() == FNiagaraTypeDefinition::GetIntStruct() ||
		Type == FNiagaraTypeDefinition::GetBoolDef();
}

FString FHlslNiagaraTranslator::GetStructHlslTypeName(FNiagaraTypeDefinition Type)
{
	if (Type.IsValid() == false)
	{
		return "undefined";
	}
	else if (Type == FNiagaraTypeDefinition::GetFloatDef())
	{
		return "float";
	}
	else if (Type == FNiagaraTypeDefinition::GetVec2Def())
	{
		return "float2";
	}
	else if (Type == FNiagaraTypeDefinition::GetVec3Def())
	{
		return "float3";
	}
	else if (Type == FNiagaraTypeDefinition::GetVec4Def() || Type == FNiagaraTypeDefinition::GetColorDef() || Type == FNiagaraTypeDefinition::GetQuatDef())
	{
		return "float4";
	}
	else if (Type == FNiagaraTypeDefinition::GetMatrix4Def())
	{
		return "float4x4";
	}
	else if (Type == FNiagaraTypeDefinition::GetIntDef() || Type.GetEnum() != nullptr)
	{
		return "int";
	}
	else if (Type == FNiagaraTypeDefinition::GetBoolDef())
	{
		return "bool";
	}
	else if (Type == FNiagaraTypeDefinition::GetParameterMapDef())
	{
		return "FParamMap0";
	}
	else
	{
		return Type.GetName();
	}
}

FString FHlslNiagaraTranslator::GetPropertyHlslTypeName(const FProperty* Property)
{
	if (Property->IsA(FFloatProperty::StaticClass()))
	{
		return "float";
	}
	else if (Property->IsA(FIntProperty::StaticClass()))
	{
		return "int";
	}
	else if (Property->IsA(FUInt32Property::StaticClass()))
	{
		return "int";
	}
	else if (Property->IsA(FStructProperty::StaticClass()))
	{
		const FStructProperty* StructProp = CastField<const FStructProperty>(Property);
		return GetStructHlslTypeName(StructProp->Struct);
	}
	else if (Property->IsA(FEnumProperty::StaticClass()))
	{
		return "int";
	}
	else if (Property->IsA(FByteProperty::StaticClass()))
	{
		return "int";
	}
	else if (Property->IsA(FBoolProperty::StaticClass()))
	{
		return "bool";
	}
	else
	{
		return TEXT("");
	}
}

FString FHlslNiagaraTranslator::BuildHLSLStructDecl(FNiagaraTypeDefinition Type, FText& OutErrorMessage)
{
	if (!IsBuiltInHlslType(Type))
	{
		FString StructName = GetStructHlslTypeName(Type);

		FString Decl = "struct " + StructName + "\n{\n";
		for (TFieldIterator<FProperty> PropertyIt(Type.GetStruct(), EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			FString PropertyTypeName = GetPropertyHlslTypeName(Property);
			if (PropertyTypeName.IsEmpty())
			{
				OutErrorMessage = FText::Format(LOCTEXT("UnknownPropertyTypeErrorFormat", "Failed to build hlsl struct declaration for type {0}.  Property {1} has an unsuported type {2}."),
					FText::FromString(Type.GetName()), Property->GetDisplayNameText(), FText::FromString(Property->GetClass()->GetName()));
				return TEXT("");
			}
			Decl += TEXT("\t") + PropertyTypeName + TEXT(" ") + Property->GetName() + TEXT(";\n");
		}
		Decl += "};\n\n";
		return Decl;
	}

	return TEXT("");
}

bool FHlslNiagaraTranslator::IsHlslBuiltinVector(FNiagaraTypeDefinition Type)
{
	if ((Type == FNiagaraTypeDefinition::GetVec2Def()) ||
		(Type == FNiagaraTypeDefinition::GetVec3Def()) ||
		(Type == FNiagaraTypeDefinition::GetVec4Def()) ||
		(Type == FNiagaraTypeDefinition::GetQuatDef()) ||
		(Type == FNiagaraTypeDefinition::GetColorDef()))
	{
		return true;
	}
	return false;
}


bool FHlslNiagaraTranslator::AddStructToDefinitionSet(const FNiagaraTypeDefinition& TypeDef)
{
	// First make sure that this is a type that we do need to define...
	if (IsBuiltInHlslType(TypeDef))
	{
		return true;
	}

	if (TypeDef == FNiagaraTypeDefinition::GetGenericNumericDef())
	{
		return false;
	}

	// We build these types on-the-fly.
	if (TypeDef == FNiagaraTypeDefinition::GetParameterMapDef())
	{
		return true;
	}

	// Now make sure that we don't have any other struct types within our struct. Add them prior to the struct in question to make sure
	// that the syntax works out properly.
	const UScriptStruct* Struct = TypeDef.GetScriptStruct();
	if (Struct != nullptr)
	{
		// We need to recursively dig through the struct to get at the lowest level of the input struct, which
		// could be a native type.
		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			const FStructProperty* StructProp = CastField<const FStructProperty>(Property);
			if (StructProp)
			{
				if (!AddStructToDefinitionSet(StructProp->Struct))
				{
					return false;
				}
			}
		}

		// Add the new type def
		StructsToDefine.AddUnique(TypeDef);
	}

	return true;
}

TArray<FName> FHlslNiagaraTranslator::ConditionPropertyPath(const FNiagaraTypeDefinition& Type, const TArray<FName>& InPath)
{
	// TODO: Build something more extensible and less hard coded for path conditioning.
	UScriptStruct* Struct = Type.GetScriptStruct();
	if (InPath.Num() == 0) // Pointing to the root
	{
		return TArray<FName>();
	}
	else if (IsHlslBuiltinVector(Type))
	{
		checkf(InPath.Num() == 1, TEXT("Invalid path for vector"));
		TArray<FName> ConditionedPath;
		ConditionedPath.Add(*InPath[0].ToString().ToLower());
		return ConditionedPath;
	}
	else if (FNiagaraTypeDefinition::IsScalarDefinition(Struct))
	{
		return TArray<FName>();
	}
	else if (Struct != nullptr)
	{
		// We need to recursively dig through the struct to get at the lowest level of the input path specified, which
		// could be a native type.
		for (TFieldIterator<FProperty> PropertyIt(Struct, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			const FProperty* Property = *PropertyIt;
			const FStructProperty* StructProp = CastField<const FStructProperty>(Property);
			// The names match, but even then things might not match up properly..
			if (InPath[0].ToString() == Property->GetName())
			{
				// The names match and this is a nested type, so we can keep digging...
				if (StructProp != nullptr)
				{
					// If our path continues onward, keep recursively digging. Otherwise, just return where we've gotten to so far.
					if (InPath.Num() > 1)
					{
						TArray<FName> ReturnPath;
						ReturnPath.Add(InPath[0]);
						TArray<FName> Subset = InPath;
						Subset.RemoveAt(0);
						TArray<FName> Children = ConditionPropertyPath(StructProp->Struct, Subset);
						for (const FName& Child : Children)
						{
							ReturnPath.Add(Child);
						}
						return ReturnPath;
					}
					else
					{
						TArray<FName> ConditionedPath;
						ConditionedPath.Add(InPath[0]);
						return ConditionedPath;
					}
				}
			}
		}
		return InPath;
	}
	return InPath;
}
//////////////////////////////////////////////////////////////////////////


FString FHlslNiagaraTranslator::CompileDataInterfaceFunction(UNiagaraDataInterface* DataInterface, FNiagaraFunctionSignature& Signature)
{
	//For now I'm compiling data interface functions like this. 
	//Not the prettiest thing in the world but it'll suffice for now.

	if (UNiagaraDataInterfaceCurve* Curve = Cast<UNiagaraDataInterfaceCurve>(DataInterface))
	{
		//For now, VM only which needs no body. GPU will need a body.
		return TEXT("");
	}
	else if (UNiagaraDataInterfaceVectorCurve* VecCurve = Cast<UNiagaraDataInterfaceVectorCurve>(DataInterface))
	{
		//For now, VM only which needs no body. GPU will need a body.
		return TEXT("");
	}
	else if (UNiagaraDataInterfaceColorCurve* ColorCurve = Cast<UNiagaraDataInterfaceColorCurve>(DataInterface))
	{
		//For now, VM only which needs no body. GPU will need a body.
		return TEXT("");
	}
	else if (UNiagaraDataInterfaceVector2DCurve* Vec2DCurve = Cast<UNiagaraDataInterfaceVector2DCurve>(DataInterface))
	{
		//For now, VM only which needs no body. GPU will need a body.
		return TEXT("");
	}
	else if (UNiagaraDataInterfaceVector4Curve* Vec4Curve = Cast<UNiagaraDataInterfaceVector4Curve>(DataInterface))
	{
		//For now, VM only which needs no body. GPU will need a body.
		return TEXT("");
	}
	else if (UNiagaraDataInterfaceStaticMesh* Mesh = Cast<UNiagaraDataInterfaceStaticMesh>(DataInterface))
	{
		return TEXT("");
	}
	else if (UNiagaraDataInterfaceCurlNoise* Noise = Cast<UNiagaraDataInterfaceCurlNoise>(DataInterface))
	{
		return TEXT("");
	}
	else
	{
		return TEXT("");
		check(0);
	}
}



#undef LOCTEXT_NAMESPACE
