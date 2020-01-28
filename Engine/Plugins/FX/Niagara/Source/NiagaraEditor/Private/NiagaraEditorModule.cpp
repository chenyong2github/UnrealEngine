// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEditorModule.h"
#include "NiagaraModule.h"
#include "NiagaraEditorTickables.h"
#include "Modules/ModuleManager.h"
#include "IAssetTypeActions.h"
#include "AssetToolsModule.h"
#include "Misc/ConfigCacheIni.h"
#include "ISequencerModule.h"
#include "ISettingsModule.h"
#include "SequencerChannelInterface.h"
#include "SequencerSettings.h"
#include "AssetRegistryModule.h"
#include "ThumbnailRendering/ThumbnailManager.h"

#include "AssetTypeActions/AssetTypeActions_NiagaraSystem.h"
#include "AssetTypeActions/AssetTypeActions_NiagaraEmitter.h"
#include "AssetTypeActions/AssetTypeActions_NiagaraScript.h"
#include "AssetTypeActions/AssetTypeActions_NiagaraParameterCollection.h"
#include "AssetTypeActions/AssetTypeActions_NiagaraEffectType.h"

#include "EdGraphUtilities.h"
#include "SGraphPin.h"
#include "KismetPins/SGraphPinVector4.h"
#include "KismetPins/SGraphPinNum.h"
#include "KismetPins/SGraphPinExec.h"
#include "KismetPins/SGraphPinInteger.h"
#include "KismetPins/SGraphPinVector.h"
#include "KismetPins/SGraphPinVector2D.h"
#include "KismetPins/SGraphPinObject.h"
#include "KismetPins/SGraphPinColor.h"
#include "KismetPins/SGraphPinBool.h"
#include "Editor/GraphEditor/Private/KismetPins/SGraphPinEnum.h"
#include "SNiagaraGraphPinNumeric.h"
#include "SNiagaraGraphPinAdd.h"
#include "NiagaraNodeConvert.h"
#include "NiagaraNodeAssignment.h"
#include "EdGraphSchema_Niagara.h"
#include "TypeEditorUtilities/NiagaraFloatTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraIntegerTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraEnumTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraBoolTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraFloatTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraVectorTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraColorTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraMatrixTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraDataInterfaceCurveTypeEditorUtilities.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEditorCommands.h"
#include "Sequencer/NiagaraSequence/NiagaraEmitterTrackEditor.h"
#include "Sequencer/LevelSequence/NiagaraSystemTrackEditor.h"
#include "PropertyEditorModule.h"
#include "NiagaraSettings.h"
#include "NiagaraModule.h"
#include "NiagaraShaderModule.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceCurve.h"
#include "NiagaraDataInterfaceVector2DCurve.h"
#include "NiagaraDataInterfaceVectorCurve.h"
#include "NiagaraDataInterfaceVector4Curve.h"
#include "NiagaraDataInterfaceColorCurve.h"
#include "NiagaraScriptViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "TNiagaraGraphPinEditableName.h"
#include "Sequencer/NiagaraSequence/Sections/MovieSceneNiagaraEmitterSection.h"
#include "UObject/Class.h"
#include "NiagaraScriptMergeManager.h"
#include "NiagaraEmitter.h"
#include "NiagaraScriptSource.h"
#include "NiagaraTypes.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraEditorCommands.h"
#include "NiagaraClipboard.h"

#include "MovieScene/Parameters/MovieSceneNiagaraBoolParameterTrack.h"
#include "MovieScene/Parameters/MovieSceneNiagaraFloatParameterTrack.h"
#include "MovieScene/Parameters/MovieSceneNiagaraIntegerParameterTrack.h"
#include "MovieScene/Parameters/MovieSceneNiagaraVectorParameterTrack.h"
#include "MovieScene/Parameters/MovieSceneNiagaraColorParameterTrack.h"

#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneIntegerSection.h"
#include "Sections/MovieSceneVectorSection.h"
#include "Sections/MovieSceneColorSection.h"

#include "ISequencerSection.h"
#include "Sections/BoolPropertySection.h"
#include "Sections/ColorPropertySection.h"

#include "Customizations/NiagaraComponentDetails.h"
#include "Customizations/NiagaraTypeCustomizations.h"
#include "Customizations/NiagaraEventScriptPropertiesCustomization.h"
#include "Customizations/NiagaraScriptVariableCustomization.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraHlslTranslator.h"
#include "NiagaraThumbnailRenderer.h"
#include "Misc/FeedbackContext.h"
#include "Customizations/NiagaraStaticSwitchNodeDetails.h"
#include "Customizations/NiagaraFunctionCallNodeDetails.h"
#include "NiagaraNodeFunctionCall.h"
#include "Engine/Selection.h"
#include "NiagaraActor.h"
#include "NiagaraNodeFunctionCall.h"
#include "INiagaraEditorOnlyDataUtlities.h"

#include "Editor.h"
#include "Factories/Factory.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "Containers/ArrayView.h"
#include "EditorReimportHandler.h"
#include "ISourceControlOperation.h"
#include "SourceControlOperations.h"
#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"

IMPLEMENT_MODULE( FNiagaraEditorModule, NiagaraEditor );

PRAGMA_DISABLE_OPTIMIZATION

#define LOCTEXT_NAMESPACE "NiagaraEditorModule"

const FName FNiagaraEditorModule::NiagaraEditorAppIdentifier( TEXT( "NiagaraEditorApp" ) );
const FLinearColor FNiagaraEditorModule::WorldCentricTabColorScale(0.0f, 0.0f, 0.2f, 0.5f);

EAssetTypeCategories::Type FNiagaraEditorModule::NiagaraAssetCategory;

int32 GbShowFastPathOptions = 0;
static FAutoConsoleVariableRef CVarShowFastPathOptions(
	TEXT("fx.Niagara.ShowFastPathOptions"),
	GbShowFastPathOptions,
	TEXT("If > 0 the experimental fast path options will be shown in the system and emitter properties in the niagara system editor.\n"),
	ECVF_Default
);

//////////////////////////////////////////////////////////////////////////

class FNiagaraEditorOnlyDataUtilities : public INiagaraEditorOnlyDataUtilities
{
	UNiagaraScriptSourceBase* CreateDefaultScriptSource(UObject* InOuter) const
	{
		return NewObject<UNiagaraScriptSource>(InOuter);
	}

	virtual UNiagaraEditorDataBase* CreateDefaultEditorData(UObject* InOuter) const
	{
		UNiagaraSystem* System = Cast<UNiagaraSystem>(InOuter);
		if (System != nullptr)
		{
			UNiagaraSystemEditorData* SystemEditorData = NewObject<UNiagaraSystemEditorData>(InOuter);
			SystemEditorData->SynchronizeOverviewGraphWithSystem(*System);
			return SystemEditorData;
		}
		return nullptr;
	}
};

class FNiagaraScriptGraphPanelPinFactory : public FGraphPanelPinFactory
{
public:
	DECLARE_DELEGATE_RetVal_OneParam(TSharedPtr<class SGraphPin>, FCreateGraphPin, UEdGraphPin*);

	/** Registers a delegate for creating a pin for a specific type. */
	void RegisterTypePin(const UScriptStruct* Type, FCreateGraphPin CreateGraphPin)
	{
		TypeToCreatePinDelegateMap.Add(Type, CreateGraphPin);
	}

	/** Registers a delegate for creating a pin for for a specific miscellaneous sub category. */
	void RegisterMiscSubCategoryPin(FName SubCategory, FCreateGraphPin CreateGraphPin)
	{
		MiscSubCategoryToCreatePinDelegateMap.Add(SubCategory, CreateGraphPin);
	}

	//~ FGraphPanelPinFactory interface
	virtual TSharedPtr<class SGraphPin> CreatePin(class UEdGraphPin* InPin) const override
	{
		if (const UEdGraphSchema_Niagara* NSchema = Cast<UEdGraphSchema_Niagara>(InPin->GetSchema()))
		{
			if (InPin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryType)
			{
				if (InPin->PinType.PinSubCategoryObject != nullptr && InPin->PinType.PinSubCategoryObject->IsA<UScriptStruct>())
				{
					const UScriptStruct* Struct = CastChecked<const UScriptStruct>(InPin->PinType.PinSubCategoryObject.Get());
					const FCreateGraphPin* CreateGraphPin = TypeToCreatePinDelegateMap.Find(Struct);
					if (CreateGraphPin != nullptr)
					{
						return (*CreateGraphPin).Execute(InPin);
					}
					// Otherwise, fall back to the generic pin for Niagara types. Previous iterations put out an error here, but this 
					// was not correct as the above list is just overrides from the default renamable pin, usually numeric types with their own custom 
					// editors for default values. Things like the parameter map can safely just fall through to the end condition and create a
					// generic renamable pin.
				}
				else
				{
					UE_LOG(LogNiagaraEditor, Error, TEXT("Pin type is invalid! Pin Name '%s' Owning Node '%s'. Turning into standard int definition!"), *InPin->PinName.ToString(),
						*InPin->GetOwningNode()->GetName());
					InPin->PinType.PinSubCategoryObject = MakeWeakObjectPtr(const_cast<UScriptStruct*>(FNiagaraTypeDefinition::GetIntStruct()));
					InPin->DefaultValue.Empty();
					return CreatePin(InPin);
				}
			}
			else if (InPin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryEnum)
			{
				const UEnum* Enum = Cast<const UEnum>(InPin->PinType.PinSubCategoryObject.Get());
				if (Enum == nullptr)
				{
					UE_LOG(LogNiagaraEditor, Error, TEXT("Pin states that it is of Enum type, but is missing its Enum! Pin Name '%s' Owning Node '%s'. Turning into standard int definition!"), *InPin->PinName.ToString(),
						*InPin->GetOwningNode()->GetName());
					InPin->PinType.PinCategory = UEdGraphSchema_Niagara::PinCategoryType;
					InPin->PinType.PinSubCategoryObject = MakeWeakObjectPtr(const_cast<UScriptStruct*>(FNiagaraTypeDefinition::GetIntStruct()));
					InPin->DefaultValue.Empty();
					return CreatePin(InPin);
				}
				return SNew(TNiagaraGraphPinEditableName<SGraphPinEnum>, InPin);
			}
			else if (InPin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryMisc)
			{
				const FCreateGraphPin* CreateGraphPin = MiscSubCategoryToCreatePinDelegateMap.Find(InPin->PinType.PinSubCategory);
				if (CreateGraphPin != nullptr)
				{
					return (*CreateGraphPin).Execute(InPin);
				}
			}

			return SNew(TNiagaraGraphPinEditableName<SGraphPin>, InPin);
		}
		return nullptr;
	}

private:
	TMap<const UScriptStruct*, FCreateGraphPin> TypeToCreatePinDelegateMap;
	TMap<FName, FCreateGraphPin> MiscSubCategoryToCreatePinDelegateMap;
};

FNiagaraEditorModule::FNiagaraEditorModule() 
	: SequencerSettings(nullptr)
	, TestCompileScriptCommand(nullptr)
	, DumpCompileIdDataForAssetCommand(nullptr)
	, Clipboard(MakeShared<FNiagaraClipboard>())
{
}

void DumpParameterStore(const FNiagaraParameterStore& ParameterStore)
{
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::Get().GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
	TArray<FNiagaraVariable> ParameterVariables;
	ParameterStore.GetParameters(ParameterVariables);
	for (const FNiagaraVariable& ParameterVariable : ParameterVariables)
	{
		FString Name = ParameterVariable.GetName().ToString();
		FString Type = ParameterVariable.GetType().GetName();
		FString Value;
		TSharedPtr<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> ParameterTypeUtilities = NiagaraEditorModule.GetTypeUtilities(ParameterVariable.GetType());
		if (ParameterTypeUtilities.IsValid() && ParameterTypeUtilities->CanHandlePinDefaults())
		{
			FNiagaraVariable ParameterVariableWithValue = ParameterVariable;
			ParameterVariableWithValue.SetData(ParameterStore.GetParameterData(ParameterVariable));
			Value = ParameterTypeUtilities->GetPinDefaultStringFromValue(ParameterVariableWithValue);
		}
		else
		{
			Value = "(unsupported)";
		}
		UE_LOG(LogNiagaraEditor, Log, TEXT("%s\t%s\t%s"), *Name, *Type, *Value);
	}
}

void DumpRapidIterationParametersForScript(UNiagaraScript* Script, const FString& HeaderName)
{
	UEnum* NiagaraScriptUsageEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("ENiagaraScriptUsage"), true);
	FString UsageName = NiagaraScriptUsageEnum->GetNameByValue((int64)Script->GetUsage()).ToString();
	UE_LOG(LogNiagaraEditor, Log, TEXT("%s - %s - %s"), *Script->GetPathName(), *HeaderName, *UsageName);
	DumpParameterStore(Script->RapidIterationParameters);
}

void DumpRapidIterationParametersForEmitter(UNiagaraEmitter* Emitter, const FString& EmitterName)
{
	TArray<UNiagaraScript*> Scripts;
	Emitter->GetScripts(Scripts, false);
	for (UNiagaraScript* Script : Scripts)
	{
		DumpRapidIterationParametersForScript(Script, EmitterName);
	}
}

void DumpRapidIterationParamersForAsset(const TArray<FString>& Arguments)
{
	if (Arguments.Num() == 1)
	{
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(*Arguments[0]);
		UObject* Asset = AssetData.GetAsset();
		if (Asset != nullptr)
		{
			UNiagaraSystem* SystemAsset = Cast<UNiagaraSystem>(Asset);
			if (SystemAsset != nullptr)
			{
				DumpRapidIterationParametersForScript(SystemAsset->GetSystemSpawnScript(), SystemAsset->GetName());
				DumpRapidIterationParametersForScript(SystemAsset->GetSystemUpdateScript(), SystemAsset->GetName());
				for (const FNiagaraEmitterHandle& EmitterHandle : SystemAsset->GetEmitterHandles())
				{
					DumpRapidIterationParametersForEmitter(EmitterHandle.GetInstance(), EmitterHandle.GetName().ToString());
				}
			}
			else
			{
				UNiagaraEmitter* EmitterAsset = Cast<UNiagaraEmitter>(Asset);
				if (EmitterAsset != nullptr)
				{
					DumpRapidIterationParametersForEmitter(EmitterAsset, EmitterAsset->GetName());
				}
				else
				{
					UE_LOG(LogNiagaraEditor, Warning, TEXT("DumpRapidIterationParameters - Only niagara system and niagara emitter assets are supported"));
				}
			}
		}
		else
		{
			UE_LOG(LogNiagaraEditor, Warning, TEXT("DumpRapidIterationParameters - Asset not found"));
		}
	}
	else
	{
		UE_LOG(LogNiagaraEditor, Warning, TEXT("DumpRapidIterationParameters - Must supply an asset path to dump"));
	}
}

void CompileEmitterStandAlone(UNiagaraEmitter* Emitter, TSet<UNiagaraEmitter*>& InOutCompiledEmitters)
{
	if (InOutCompiledEmitters.Contains(Emitter) == false)
	{
		if (Emitter->GetParent() != nullptr)
		{
			// If the emitter has a parent emitter make sure to compile that one first.
			CompileEmitterStandAlone(Emitter->GetParent(), InOutCompiledEmitters);

			if (Emitter->IsSynchronizedWithParent() == false)
			{
				// If compiling the parent caused it to become out of sync with the current emitter merge in changes before compiling.
				Emitter->MergeChangesFromParent();
			}
		}

		Emitter->MarkPackageDirty();
		UNiagaraSystem* TransientSystem = NewObject<UNiagaraSystem>(GetTransientPackage(), NAME_None, RF_Transient);
		UNiagaraSystemFactoryNew::InitializeSystem(TransientSystem, true);
		TransientSystem->AddEmitterHandle(*Emitter, TEXT("Emitter"));
		FNiagaraStackGraphUtilities::RebuildEmitterNodes(*TransientSystem);
		TransientSystem->RequestCompile(false);
		TransientSystem->WaitForCompilationComplete();

		InOutCompiledEmitters.Add(Emitter);
	}
}

void PreventSystemRecompile(FAssetData SystemAsset, TSet<UNiagaraEmitter*>& InOutCompiledEmitters)
{
	UNiagaraSystem* System = Cast<UNiagaraSystem>(SystemAsset.GetAsset());
	if (System != nullptr)
	{
		for (const FNiagaraEmitterHandle& EmitterHandle : System->GetEmitterHandles())
		{
			CompileEmitterStandAlone(EmitterHandle.GetInstance(), InOutCompiledEmitters);
		}
		
		System->MarkPackageDirty();
		System->RequestCompile(false);
		System->WaitForCompilationComplete();
	}
}

void PreventSystemRecompile(const TArray<FString>& Arguments)
{
	if (Arguments.Num() > 0)
	{
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FAssetData SystemAsset = AssetRegistryModule.Get().GetAssetByObjectPath(*Arguments[0]);
		if (SystemAsset.IsValid() == false)
		{
			TArray<FAssetData> AssetsInPackage;
			AssetRegistryModule.Get().GetAssetsByPackageName(*Arguments[0], AssetsInPackage);
			if (AssetsInPackage.Num() == 1)
			{
				SystemAsset = AssetsInPackage[0];
			}
		}
		TSet<UNiagaraEmitter*> CompiledEmitters;
		PreventSystemRecompile(SystemAsset, CompiledEmitters);
	}
}

void PreventAllSystemRecompiles()
{
	const FText SlowTaskText = NSLOCTEXT("NiagaraEditor", "PreventAllSystemRecompiles", "Refreshing all systems to prevent recompiles.");
	GWarn->BeginSlowTask(SlowTaskText, true, true);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	TArray<FAssetData> SystemAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UNiagaraSystem::StaticClass()->GetFName(), SystemAssets);

	TSet<UNiagaraEmitter*> CompiledEmitters;
	int32 ItemIndex = 0;
	for (FAssetData& SystemAsset : SystemAssets)
	{
		if (GWarn->ReceivedUserCancel())
		{
			return;
		}
		GWarn->UpdateProgress(ItemIndex++, SystemAssets.Num());

		PreventSystemRecompile(SystemAsset, CompiledEmitters);
	}

	GWarn->EndSlowTask();
}

void UpgradeAllNiagaraAssets()
{
	//First Load All Niagara Assets.
	const FText SlowTaskText_Load = NSLOCTEXT("NiagaraEditor", "UpgradeAllNiagaraAssets_Load", "Loading all Niagara Assets ready to upgrade.");
	GWarn->BeginSlowTask(SlowTaskText_Load, true, true);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	TArray<FAssetData> SystemAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UNiagaraSystem::StaticClass()->GetFName(), SystemAssets);

	TArray<UNiagaraSystem*> Systems;
	Systems.Reserve(SystemAssets.Num());
	TSet<UNiagaraEmitter*> CompiledEmitters;
	int32 ItemIndex = 0;
	for (FAssetData& SystemAsset : SystemAssets)
	{
		if (GWarn->ReceivedUserCancel())
		{
			return;
		}
		GWarn->UpdateProgress(ItemIndex++, SystemAssets.Num());

		UNiagaraSystem* System = Cast<UNiagaraSystem>(SystemAsset.GetAsset());
		if (System != nullptr)
		{
			Systems.Add(System);
		}
	}

	GWarn->EndSlowTask();

	//////////////////////////////////////////////////////////////////////////

	//Now process any data that needs to be updated.
	const FText SlowTaskText_Upgrade = NSLOCTEXT("NiagaraEditor", "UpgradeAllNiagaraAssets_Upgrade", "Upgrading All Niagara Assets.");
	GWarn->BeginSlowTask(SlowTaskText_Upgrade, true, true);

	//Upgrade any data interface function call nodes.
	TArray<UObject*> FunctionCallNodes;
	GetObjectsOfClass(UNiagaraNodeFunctionCall::StaticClass(), FunctionCallNodes);
	ItemIndex = 0;
	for (UObject* Object : FunctionCallNodes)
	{
		if (GWarn->ReceivedUserCancel())
		{
			return;
		}

		if (UNiagaraNodeFunctionCall* FuncCallNode = Cast<UNiagaraNodeFunctionCall>(Object))
		{
			FuncCallNode->UpgradeDIFunctionCalls();
		}

		GWarn->UpdateProgress(ItemIndex++, FunctionCallNodes.Num());
	}

	GWarn->EndSlowTask();
}

void MakeIndent(int32 IndentLevel, FString& OutIndentString)
{
	OutIndentString.Reserve(IndentLevel * 2);
	int32 InsertStart = OutIndentString.Len();
	for (int32 i = 0; i < IndentLevel * 2; i++)
	{
		OutIndentString.AppendChar(TCHAR(' '));
	}
}

void DumpCompileIdDataForScript(UNiagaraScript* Script, int32 IndentLevel, FString& Dump)
{
	FString Indent;
	MakeIndent(IndentLevel, Indent);
	Dump.Append(FString::Printf(TEXT("%sScript: %s\n"), *Indent, *Script->GetPathName()));
	UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(Script->GetSource());
	TArray<UNiagaraNode*> Nodes;
	ScriptSource->NodeGraph->GetNodesOfClass<UNiagaraNode>(Nodes);
	for (UNiagaraNode* Node : Nodes)
	{
		Dump.Append(FString::Printf(TEXT("%s%s - %s-%s\n"), *Indent, *Node->GetFullName(), *Node->NodeGuid.ToString(EGuidFormats::Digits), *Node->GetChangeId().ToString(EGuidFormats::Digits)));
		UNiagaraNodeFunctionCall* FunctionCallNode = Cast<UNiagaraNodeFunctionCall>(Node);
		if (FunctionCallNode != nullptr)
		{
			UNiagaraNodeAssignment* AssignmentNode = Cast<UNiagaraNodeAssignment>(FunctionCallNode);
			if (AssignmentNode != nullptr)
			{
				Dump.Append(FString::Printf(TEXT("%sAssignment Node: %s\n"), *Indent, *FunctionCallNode->GetFunctionName()));
				for (const FNiagaraVariable& AssignmentTarget : AssignmentNode->GetAssignmentTargets())
				{
					Dump.Append(FString::Printf(TEXT("%s  Assignment Target: %s - %s\n"), *Indent, *AssignmentTarget.GetName().ToString(), *AssignmentTarget.GetType().GetName()));
				}
			}
			else if (FunctionCallNode->FunctionScript != nullptr)
			{
				Dump.Append(FString::Printf(TEXT("%sFunction Call: %s\n"), *Indent, *FunctionCallNode->GetFunctionName()));
				DumpCompileIdDataForScript(FunctionCallNode->FunctionScript, IndentLevel + 1, Dump);
			}
		}
	}
}

void DumpCompileIdDataForEmitter(UNiagaraEmitter* Emitter, int32 IndentLevel, FString& Dump)
{

	FString Indent;
	MakeIndent(IndentLevel, Indent);
	Dump.Append(FString::Printf(TEXT("%sEmitter: %s\n"), *Indent, *Emitter->GetUniqueEmitterName()));

	TArray<UNiagaraScript*> Scripts;
	Emitter->GetScripts(Scripts, false);
	for (UNiagaraScript* Script : Scripts)
	{
		DumpCompileIdDataForScript(Script, IndentLevel + 1, Dump);
	}
}

void DumpCompileIdDataForSystem(UNiagaraSystem* System, FString& Dump)
{
	Dump.Append(FString::Printf(TEXT("\nSystem %s\n"), *System->GetPathName()));
	DumpCompileIdDataForScript(System->GetSystemSpawnScript(), 1, Dump);
	DumpCompileIdDataForScript(System->GetSystemUpdateScript(), 1, Dump);
	for (const FNiagaraEmitterHandle& EmitterHandle : System->GetEmitterHandles())
	{
		DumpCompileIdDataForEmitter(EmitterHandle.GetInstance(), 1, Dump);
	}
}

void DumpCompileIdDataForAsset(const TArray<FString>& Arguments)
{
	if (Arguments.Num() > 0)
	{
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FAssetData SystemAsset = AssetRegistryModule.Get().GetAssetByObjectPath(*Arguments[0]);
		if (SystemAsset.IsValid() == false)
		{
			TArray<FAssetData> AssetsInPackage;
			AssetRegistryModule.Get().GetAssetsByPackageName(*Arguments[0], AssetsInPackage);
			if (AssetsInPackage.Num() == 1)
			{
				SystemAsset = AssetsInPackage[0];
			}
		}
		if (SystemAsset.IsValid())
		{
			UNiagaraSystem* System = Cast<UNiagaraSystem>(SystemAsset.GetAsset());
			if (System != nullptr)
			{
				FString Dump;
				DumpCompileIdDataForSystem(System, Dump);
				UE_LOG(LogNiagaraEditor, Log, TEXT("%s"), *Dump);
			}
			else
			{
				UE_LOG(LogNiagaraEditor, Warning, TEXT("Could not load system asset for argument: %s"), *Arguments[0]);
			}
		}
		else
		{
			UE_LOG(LogNiagaraEditor, Warning, TEXT("Could not find asset for argument: %s"), *Arguments[0]);
		}
	}
	else
	{
		UE_LOG(LogNiagaraEditor, Warning, TEXT("Command required an asset reference to be passed in."));
	}
}

void ExecuteInvalidateNiagaraCachedScripts(const TArray< FString >& Args)
{
	if (Args.Num() == 0)
	{
		// todo: log error, at least one command is needed
		UE_LOG(LogConsoleResponse, Display, TEXT("fx.InvalidateCachedScripts failed\nAs this command should not be executed accidentally it requires you to specify an extra parameter."));
		return;
	}

	FString FileName = FPaths::EngineDir() + TEXT("Plugins/FX/Niagara/Shaders/Private/NiagaraShaderVersion.ush");

	FileName = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FileName);

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	SourceControlProvider.Init();

	FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(FileName, EStateCacheUsage::ForceUpdate);
	if (SourceControlState.IsValid())
	{
		if (SourceControlState->CanCheckout() || SourceControlState->IsCheckedOutOther())
		{
			if (SourceControlProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), FileName) == ECommandResult::Failed)
			{
				UE_LOG(LogConsoleResponse, Display, TEXT("fx.InvalidateCachedScripts failed\nCouldn't check out \"NiagaraShaderVersion.ush\""));
				return;
			}
		}
		else if (!SourceControlState->IsSourceControlled())
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("fx.InvalidateCachedScripts failed\n\"NiagaraShaderVersion.ush\" is not under source control."));
		}
		else if (SourceControlState->IsCheckedOutOther())
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("fx.InvalidateCachedScripts failed\n\"NiagaraShaderVersion.ush\" is already checked out by someone else\n(UE4 SourceControl needs to be fixed to allow multiple checkout.)"));
			return;
		}
		else if (SourceControlState->IsDeleted())
		{
			UE_LOG(LogConsoleResponse, Display, TEXT("fx.InvalidateCachedScripts failed\n\"NiagaraShaderVersion.ush\" is marked for delete"));
			return;
		}
	}

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	IFileHandle* FileHandle = PlatformFile.OpenWrite(*FileName);
	if (FileHandle)
	{
		FString Guid = FString(
			TEXT("// This file is automatically generated by the console command fx.InvalidateCachedScripts\n")
			TEXT("// Each time the console command is executed it generates a new GUID. As a hash of this file is included\n")
			TEXT("// in the DDC key, it will automatically invalidate.\n")
			TEXT("// \n")
			TEXT("// If you are merging streams and there is a conflict with this GUID you should make a new GUID rather than taking one or the other.\n")
			TEXT("// GUID = "))
			+ FGuid::NewGuid().ToString();

		FileHandle->Write((const uint8*)TCHAR_TO_ANSI(*Guid), Guid.Len());
		delete FileHandle;

		UE_LOG(LogConsoleResponse, Display, TEXT("fx.InvalidateCachedScripts succeeded\n\"NiagaraShaderVersion.ush\" was updated.\n"));
	}
	else
	{
		UE_LOG(LogConsoleResponse, Display, TEXT("fx.InvalidateCachedScripts failed\nCouldn't open \"NiagaraShaderVersion.ush\".\n"));
	}
}

FAutoConsoleCommand InvalidateCachedNiagaraScripts(
	TEXT("fx.InvalidateCachedScripts"),
	TEXT("Invalidate Niagara script cache by making a unique change to NiagaraShaderVersion.ush which is included in common.usf.")
	TEXT("To initiate actual the recompile of all shaders use \"recompileshaders changed\" or press \"Ctrl Shift .\".\n")
	TEXT("The NiagaraShaderVersion.ush file should be automatically checked out but it needs to be checked in to have effect on other machines."),
	FConsoleCommandWithArgsDelegate::CreateStatic(ExecuteInvalidateNiagaraCachedScripts)
);

void ExecuteRebuildNiagaraCachedScripts(const TArray< FString >& Args)
{
	UE_LOG(LogConsoleResponse, Display, TEXT("fx.RebuildDirtyScripts started.\n"));

	// Need to flush the cache to make sure that we have the latest files.
	FlushShaderFileCache();
	for (TObjectIterator<UNiagaraSystem> SystemIterator; SystemIterator; ++SystemIterator)
	{
		SystemIterator->RequestCompile(false);
	}
}

FAutoConsoleCommand ExecuteRebuildNiagaraCachedScriptsCmd(
	TEXT("fx.RebuildDirtyScripts"),
	TEXT("Go through all loaded assets and force them to recompute their script hash. If dirty, regenerate."),
	FConsoleCommandWithArgsDelegate::CreateStatic(ExecuteRebuildNiagaraCachedScripts)
);


class FNiagaraSystemBoolParameterTrackEditor : public FNiagaraSystemParameterTrackEditor<UMovieSceneNiagaraBoolParameterTrack, UMovieSceneBoolSection>
{
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override
	{
		checkf(SectionObject.GetClass()->IsChildOf<UMovieSceneBoolSection>(), TEXT("Unsupported section."));
		return MakeShareable(new FBoolPropertySection(SectionObject));
	}
};

class FNiagaraSystemColorParameterTrackEditor : public FNiagaraSystemParameterTrackEditor<UMovieSceneNiagaraColorParameterTrack, UMovieSceneColorSection>
{
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override
	{
		checkf(SectionObject.GetClass()->IsChildOf<UMovieSceneColorSection>(), TEXT("Unsupported section."));
		return MakeShareable(new FColorPropertySection(*Cast<UMovieSceneColorSection>(&SectionObject), ObjectBinding, GetSequencer()));
	}
};

void FNiagaraEditorModule::StartupModule()
{
	bThumbnailRenderersRegistered = false;

	FHlslNiagaraTranslator::Init();
	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	NiagaraAssetCategory = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("FX")), LOCTEXT("NiagaraAssetsCategory", "FX"));
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FAssetTypeActions_NiagaraSystem()));
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FAssetTypeActions_NiagaraEmitter()));
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FAssetTypeActions_NiagaraScriptFunctions()));
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FAssetTypeActions_NiagaraScriptModules()));
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FAssetTypeActions_NiagaraScriptDynamicInputs()));
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FAssetTypeActions_NiagaraParameterCollection())); 
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FAssetTypeActions_NiagaraParameterCollectionInstance()));
	RegisterAssetTypeAction(AssetTools, MakeShareable(new FAssetTypeActions_NiagaraEffectType()));

	UNiagaraSettings::OnSettingsChanged().AddRaw(this, &FNiagaraEditorModule::OnNiagaraSettingsChangedEvent);
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddRaw(this, &FNiagaraEditorModule::OnPreGarbageCollection);
	
	// Any attempt to use GEditor right now will fail as it hasn't been initialized yet. Waiting for post engine init resolves that.
	FCoreDelegates::OnPostEngineInit.AddRaw(this, &FNiagaraEditorModule::OnPostEngineInit);
	
	// register details customization
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("NiagaraComponent", FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraComponentDetails::MakeInstance));

	PropertyModule.RegisterCustomClassLayout("NiagaraNodeStaticSwitch", FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraStaticSwitchNodeDetails::MakeInstance));

	PropertyModule.RegisterCustomClassLayout("NiagaraScriptVariable", FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraScriptVariableDetails::MakeInstance));

	PropertyModule.RegisterCustomClassLayout("NiagaraNodeFunctionCall", FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraFunctionCallNodeDetails::MakeInstance));
	
	PropertyModule.RegisterCustomPropertyTypeLayout("NiagaraFloat",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNiagaraNumericCustomization::MakeInstance)
	);

	PropertyModule.RegisterCustomPropertyTypeLayout("NiagaraInt32",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNiagaraNumericCustomization::MakeInstance)
	);

	PropertyModule.RegisterCustomPropertyTypeLayout("NiagaraNumeric",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNiagaraNumericCustomization::MakeInstance)
	);

	PropertyModule.RegisterCustomPropertyTypeLayout("NiagaraParameterMap",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNiagaraNumericCustomization::MakeInstance)
	);


	PropertyModule.RegisterCustomPropertyTypeLayout("NiagaraBool",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNiagaraBoolCustomization::MakeInstance)
	);

	PropertyModule.RegisterCustomPropertyTypeLayout("NiagaraMatrix",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNiagaraMatrixCustomization::MakeInstance)
	);

	PropertyModule.RegisterCustomPropertyTypeLayout("NiagaraVariableAttributeBinding",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNiagaraVariableAttributeBindingCustomization::MakeInstance)
	);
	
	PropertyModule.RegisterCustomPropertyTypeLayout("NiagaraScriptVariableBinding",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNiagaraScriptVariableBindingCustomization::MakeInstance)
	);
		
	PropertyModule.RegisterCustomPropertyTypeLayout("NiagaraUserParameterBinding",
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNiagaraUserParameterBindingCustomization::MakeInstance)
	);

	FNiagaraEditorStyle::Initialize();
	ReinitializeStyleCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("fx.NiagaraEditor.ReinitializeStyle"),
		TEXT("Reinitializes the style for the niagara editor module.  Used in conjuction with live coding for UI tweaks.  May crash the editor if style objects are in use."),
		FConsoleCommandDelegate::CreateRaw(this, &FNiagaraEditorModule::ReinitializeStyle));



	FNiagaraEditorCommands::Register();

	TSharedPtr<FNiagaraScriptGraphPanelPinFactory> GraphPanelPinFactory = MakeShareable(new FNiagaraScriptGraphPanelPinFactory());

	GraphPanelPinFactory->RegisterTypePin(FNiagaraTypeDefinition::GetFloatStruct(), FNiagaraScriptGraphPanelPinFactory::FCreateGraphPin::CreateLambda(
		[](UEdGraphPin* GraphPin) -> TSharedRef<SGraphPin> { return SNew(TNiagaraGraphPinEditableName<SGraphPinNum<float>>, GraphPin); }));

	GraphPanelPinFactory->RegisterTypePin(FNiagaraTypeDefinition::GetIntStruct(), FNiagaraScriptGraphPanelPinFactory::FCreateGraphPin::CreateLambda(
		[](UEdGraphPin* GraphPin) -> TSharedRef<SGraphPin> { return SNew(TNiagaraGraphPinEditableName<SGraphPinInteger>, GraphPin); }));

	GraphPanelPinFactory->RegisterTypePin(FNiagaraTypeDefinition::GetVec2Struct(), FNiagaraScriptGraphPanelPinFactory::FCreateGraphPin::CreateLambda(
		[](UEdGraphPin* GraphPin) -> TSharedRef<SGraphPin> { return SNew(TNiagaraGraphPinEditableName<SGraphPinVector2D>, GraphPin); }));

	GraphPanelPinFactory->RegisterTypePin(FNiagaraTypeDefinition::GetVec3Struct(), FNiagaraScriptGraphPanelPinFactory::FCreateGraphPin::CreateLambda(
		[](UEdGraphPin* GraphPin) -> TSharedRef<SGraphPin> { return SNew(TNiagaraGraphPinEditableName<SGraphPinVector>, GraphPin); }));

	GraphPanelPinFactory->RegisterTypePin(FNiagaraTypeDefinition::GetVec4Struct(), FNiagaraScriptGraphPanelPinFactory::FCreateGraphPin::CreateLambda(
		[](UEdGraphPin* GraphPin) -> TSharedRef<SGraphPin> { return SNew(TNiagaraGraphPinEditableName<SGraphPinVector4>, GraphPin); }));

	GraphPanelPinFactory->RegisterTypePin(FNiagaraTypeDefinition::GetColorStruct(), FNiagaraScriptGraphPanelPinFactory::FCreateGraphPin::CreateLambda(
		[](UEdGraphPin* GraphPin) -> TSharedRef<SGraphPin> { return SNew(TNiagaraGraphPinEditableName<SGraphPinColor>, GraphPin); }));

	GraphPanelPinFactory->RegisterTypePin(FNiagaraTypeDefinition::GetBoolStruct(), FNiagaraScriptGraphPanelPinFactory::FCreateGraphPin::CreateLambda(
		[](UEdGraphPin* GraphPin) -> TSharedRef<SGraphPin> { return SNew(TNiagaraGraphPinEditableName<SGraphPinBool>, GraphPin); }));

	GraphPanelPinFactory->RegisterTypePin(FNiagaraTypeDefinition::GetGenericNumericStruct(), FNiagaraScriptGraphPanelPinFactory::FCreateGraphPin::CreateLambda(
		[](UEdGraphPin* GraphPin) -> TSharedRef<SGraphPin> { return SNew(TNiagaraGraphPinEditableName<SNiagaraGraphPinNumeric>, GraphPin); }));

	// TODO: Don't register this here.
	GraphPanelPinFactory->RegisterMiscSubCategoryPin(UNiagaraNodeWithDynamicPins::AddPinSubCategory, FNiagaraScriptGraphPanelPinFactory::FCreateGraphPin::CreateLambda(
		[](UEdGraphPin* GraphPin) -> TSharedRef<SGraphPin> { return SNew(SNiagaraGraphPinAdd, GraphPin); }));

	GraphPanelPinFactory->RegisterTypePin(FNiagaraTypeDefinition::GetParameterMapStruct(), FNiagaraScriptGraphPanelPinFactory::FCreateGraphPin::CreateLambda(
		[](UEdGraphPin* GraphPin) -> TSharedRef<SGraphPin> { return SNew(SGraphPinExec, GraphPin); }));


	EnumTypeUtilities = MakeShareable(new FNiagaraEditorEnumTypeUtilities());
	RegisterTypeUtilities(FNiagaraTypeDefinition::GetFloatDef(), MakeShareable(new FNiagaraEditorFloatTypeUtilities()));
	RegisterTypeUtilities(FNiagaraTypeDefinition::GetIntDef(), MakeShareable(new FNiagaraEditorIntegerTypeUtilities()));
	RegisterTypeUtilities(FNiagaraTypeDefinition::GetBoolDef(), MakeShareable(new FNiagaraEditorBoolTypeUtilities()));
	RegisterTypeUtilities(FNiagaraTypeDefinition::GetVec2Def(), MakeShareable(new FNiagaraEditorVector2TypeUtilities()));
	RegisterTypeUtilities(FNiagaraTypeDefinition::GetVec3Def(), MakeShareable(new FNiagaraEditorVector3TypeUtilities()));
	RegisterTypeUtilities(FNiagaraTypeDefinition::GetVec4Def(), MakeShareable(new FNiagaraEditorVector4TypeUtilities()));
	RegisterTypeUtilities(FNiagaraTypeDefinition::GetQuatDef(), MakeShareable(new FNiagaraEditorQuatTypeUtilities()));
	RegisterTypeUtilities(FNiagaraTypeDefinition::GetColorDef(), MakeShareable(new FNiagaraEditorColorTypeUtilities()));
	RegisterTypeUtilities(FNiagaraTypeDefinition::GetMatrix4Def(), MakeShareable(new FNiagaraEditorMatrixTypeUtilities()));

	RegisterTypeUtilities(FNiagaraTypeDefinition(UNiagaraDataInterfaceCurve::StaticClass()), MakeShared<FNiagaraDataInterfaceCurveTypeEditorUtilities, ESPMode::ThreadSafe>());
	RegisterTypeUtilities(FNiagaraTypeDefinition(UNiagaraDataInterfaceVector2DCurve::StaticClass()), MakeShared<FNiagaraDataInterfaceCurveTypeEditorUtilities, ESPMode::ThreadSafe>());
	RegisterTypeUtilities(FNiagaraTypeDefinition(UNiagaraDataInterfaceVectorCurve::StaticClass()), MakeShared<FNiagaraDataInterfaceVectorCurveTypeEditorUtilities, ESPMode::ThreadSafe>());
	RegisterTypeUtilities(FNiagaraTypeDefinition(UNiagaraDataInterfaceVector4Curve::StaticClass()), MakeShared<FNiagaraDataInterfaceVectorCurveTypeEditorUtilities, ESPMode::ThreadSafe>());
	RegisterTypeUtilities(FNiagaraTypeDefinition(UNiagaraDataInterfaceColorCurve::StaticClass()), MakeShared<FNiagaraDataInterfaceColorCurveTypeEditorUtilities, ESPMode::ThreadSafe>());

	FEdGraphUtilities::RegisterVisualPinFactory(GraphPanelPinFactory);

	FNiagaraOpInfo::Init();

	RegisterSettings();

	// Register sequencer track editors
	ISequencerModule &SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
	CreateEmitterTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FNiagaraEmitterTrackEditor::CreateTrackEditor));
	CreateSystemTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FNiagaraSystemTrackEditor::CreateTrackEditor));

	SequencerModule.RegisterChannelInterface<FMovieSceneNiagaraEmitterChannel>();

	CreateBoolParameterTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(
		&FNiagaraSystemBoolParameterTrackEditor::CreateTrackEditor));
	CreateFloatParameterTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(
		&FNiagaraSystemParameterTrackEditor<UMovieSceneNiagaraFloatParameterTrack, UMovieSceneFloatSection>::CreateTrackEditor));
	CreateIntegerParameterTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(
		&FNiagaraSystemParameterTrackEditor<UMovieSceneNiagaraIntegerParameterTrack, UMovieSceneIntegerSection>::CreateTrackEditor));
	CreateVectorParameterTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(
		&FNiagaraSystemParameterTrackEditor<UMovieSceneNiagaraVectorParameterTrack, UMovieSceneVectorSection>::CreateTrackEditor));
	CreateColorParameterTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(
		&FNiagaraSystemColorParameterTrackEditor::CreateTrackEditor));

	RegisterParameterTrackCreatorForType(*FNiagaraBool::StaticStruct(), FOnCreateMovieSceneTrackForParameter::CreateLambda([](FNiagaraVariable InParameter) {
		return NewObject<UMovieSceneNiagaraBoolParameterTrack>(); }));
	RegisterParameterTrackCreatorForType(*FNiagaraFloat::StaticStruct(), FOnCreateMovieSceneTrackForParameter::CreateLambda([](FNiagaraVariable InParameter) {
		return NewObject<UMovieSceneNiagaraFloatParameterTrack>(); }));
	RegisterParameterTrackCreatorForType(*FNiagaraInt32::StaticStruct(), FOnCreateMovieSceneTrackForParameter::CreateLambda([](FNiagaraVariable InParameter) {
		return NewObject<UMovieSceneNiagaraIntegerParameterTrack>(); }));
	RegisterParameterTrackCreatorForType(*FNiagaraTypeDefinition::GetVec2Struct(), FOnCreateMovieSceneTrackForParameter::CreateLambda([](FNiagaraVariable InParameter) 
	{
		UMovieSceneNiagaraVectorParameterTrack* VectorTrack = NewObject<UMovieSceneNiagaraVectorParameterTrack>();
		VectorTrack->SetChannelsUsed(2);
		return VectorTrack;
	}));
	RegisterParameterTrackCreatorForType(*FNiagaraTypeDefinition::GetVec3Struct(), FOnCreateMovieSceneTrackForParameter::CreateLambda([](FNiagaraVariable InParameter)
	{
		UMovieSceneNiagaraVectorParameterTrack* VectorTrack = NewObject<UMovieSceneNiagaraVectorParameterTrack>();
		VectorTrack->SetChannelsUsed(3);
		return VectorTrack;
	}));
	RegisterParameterTrackCreatorForType(*FNiagaraTypeDefinition::GetVec4Struct(), FOnCreateMovieSceneTrackForParameter::CreateLambda([](FNiagaraVariable InParameter)
	{
		UMovieSceneNiagaraVectorParameterTrack* VectorTrack = NewObject<UMovieSceneNiagaraVectorParameterTrack>();
		VectorTrack->SetChannelsUsed(4);
		return VectorTrack;
	}));
	RegisterParameterTrackCreatorForType(*FNiagaraTypeDefinition::GetColorStruct(), FOnCreateMovieSceneTrackForParameter::CreateLambda([](FNiagaraVariable InParameter) {
		return NewObject<UMovieSceneNiagaraColorParameterTrack>(); }));

	// Register the shader queue processor (for cooking)
	INiagaraModule& NiagaraModule = FModuleManager::LoadModuleChecked<INiagaraModule>("Niagara");
	NiagaraModule.SetOnProcessShaderCompilationQueue(INiagaraModule::FOnProcessQueue::CreateLambda([]()
	{
		FNiagaraShaderQueueTickable::ProcessQueue();
	}));

	INiagaraShaderModule& NiagaraShaderModule = FModuleManager::LoadModuleChecked<INiagaraShaderModule>("NiagaraShader");
	NiagaraShaderModule.SetOnProcessShaderCompilationQueue(INiagaraShaderModule::FOnProcessQueue::CreateLambda([]()
	{
		FNiagaraShaderQueueTickable::ProcessQueue();
	}));

	// Register the emitter merge handler and editor data utilities.
	ScriptMergeManager = MakeShared<FNiagaraScriptMergeManager>();
	NiagaraModule.RegisterMergeManager(ScriptMergeManager.ToSharedRef());

	EditorOnlyDataUtilities = MakeShared<FNiagaraEditorOnlyDataUtilities>();
	NiagaraModule.RegisterEditorOnlyDataUtilities(EditorOnlyDataUtilities.ToSharedRef());

	// Register the script compiler
	ScriptCompilerHandle = NiagaraModule.RegisterScriptCompiler(INiagaraModule::FScriptCompiler::CreateLambda([this](const FNiagaraCompileRequestDataBase* CompileRequest, const FNiagaraCompileOptions& Options)
	{
		return CompileScript(CompileRequest, Options);
	}));

	PrecompilerHandle = NiagaraModule.RegisterPrecompiler(INiagaraModule::FOnPrecompile::CreateLambda([this](UObject* InObj)
	{
		return Precompile(InObj);
	}));

	TestCompileScriptCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("fx.TestCompileNiagaraScript"),
		TEXT("Compiles the specified script on disk for the niagara vector vm"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FNiagaraEditorModule::TestCompileScriptFromConsole));

	DumpRapidIterationParametersForAsset = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("fx.DumpRapidIterationParametersForAsset"),
		TEXT("Dumps the values of the rapid iteration parameters for the specified asset by path."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DumpRapidIterationParamersForAsset));

	PreventSystemRecompileCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("fx.PreventSystemRecompile"),
		TEXT("Forces the system to refresh all it's dependencies so it won't recompile on load.  This may mark multiple assets dirty for re-saving."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PreventSystemRecompile));

	PreventAllSystemRecompilesCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("fx.PreventAllSystemRecompiles"),
		TEXT("Loads all of the systems in the project and forces each system to refresh all it's dependencies so it won't recompile on load.  This may mark multiple assets dirty for re-saving."),
		FConsoleCommandDelegate::CreateStatic(&PreventAllSystemRecompiles));

	UpgradeAllNiagaraAssetsCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("fx.UpgradeAllNiagaraAssets"),
		TEXT("Loads all Niagara assets and preforms any data upgrade processes required. This may mark multiple assets dirty for re-saving."),
		FConsoleCommandDelegate::CreateStatic(&UpgradeAllNiagaraAssets));

	DumpCompileIdDataForAssetCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("fx.DumpCompileIdDataForAsset"),
		TEXT("Dumps data relevant to generating the compile id for an asset."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DumpCompileIdDataForAsset));

}


void FNiagaraEditorModule::ShutdownModule()
{
	// Ensure that we don't have any lingering compiles laying around that will explode after this module shuts down.
	for (TObjectIterator<UNiagaraSystem> It; It; ++It)
	{
		UNiagaraSystem* Sys = *It;
		if (Sys)
		{
			Sys->WaitForCompilationComplete();
		}
	}

	MenuExtensibilityManager.Reset();
	ToolBarExtensibilityManager.Reset();
	
	if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
		for (auto CreatedAssetTypeAction : CreatedAssetTypeActions)
		{
			AssetTools.UnregisterAssetTypeActions(CreatedAssetTypeAction.ToSharedRef());
		}
	}
	CreatedAssetTypeActions.Empty();

	UNiagaraSettings::OnSettingsChanged().RemoveAll(this);

	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().RemoveAll(this);
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	
	if (GEditor)
	{
		GEditor->OnExecParticleInvoked().RemoveAll(this);
	}
	
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout("NiagaraComponent");
	}

	FNiagaraEditorStyle::Shutdown();

	UnregisterSettings();

	ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModule != nullptr)
	{
		SequencerModule->UnRegisterTrackEditor(CreateEmitterTrackEditorHandle);
		SequencerModule->UnRegisterTrackEditor(CreateSystemTrackEditorHandle);
		SequencerModule->UnRegisterTrackEditor(CreateBoolParameterTrackEditorHandle);
		SequencerModule->UnRegisterTrackEditor(CreateFloatParameterTrackEditorHandle);
		SequencerModule->UnRegisterTrackEditor(CreateIntegerParameterTrackEditorHandle);
		SequencerModule->UnRegisterTrackEditor(CreateVectorParameterTrackEditorHandle);
		SequencerModule->UnRegisterTrackEditor(CreateColorParameterTrackEditorHandle);
	}

	INiagaraModule* NiagaraModule = FModuleManager::GetModulePtr<INiagaraModule>("Niagara");
	if (NiagaraModule != nullptr)
	{
		NiagaraModule->UnregisterMergeManager(ScriptMergeManager.ToSharedRef());
		NiagaraModule->UnregisterEditorOnlyDataUtilities(EditorOnlyDataUtilities.ToSharedRef());
		NiagaraModule->UnregisterScriptCompiler(ScriptCompilerHandle);
		NiagaraModule->UnregisterPrecompiler(PrecompilerHandle);
	}

	// Verify that we've cleaned up all the view models in the world.
	FNiagaraSystemViewModel::CleanAll();
	FNiagaraEmitterViewModel::CleanAll();
	FNiagaraScriptViewModel::CleanAll();

	if (TestCompileScriptCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(TestCompileScriptCommand);
	}

	if (DumpRapidIterationParametersForAsset != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(DumpRapidIterationParametersForAsset);
	}

	if (PreventSystemRecompileCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(PreventSystemRecompileCommand);
	}

	if (PreventAllSystemRecompilesCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(PreventAllSystemRecompilesCommand);
	}

	if (DumpCompileIdDataForAssetCommand != nullptr)
	{
		IConsoleManager::Get().UnregisterConsoleObject(DumpCompileIdDataForAssetCommand);
		DumpCompileIdDataForAssetCommand = nullptr;
	}

	if (UObjectInitialized() && GIsEditor && bThumbnailRenderersRegistered)
	{
		UThumbnailManager::Get().UnregisterCustomRenderer(UNiagaraEmitter::StaticClass());
		UThumbnailManager::Get().UnregisterCustomRenderer(UNiagaraSystem::StaticClass());
	}
}

void FNiagaraEditorModule::OnPostEngineInit()
{
	if (GIsEditor)
	{
		UThumbnailManager::Get().RegisterCustomRenderer(UNiagaraEmitter::StaticClass(), UNiagaraEmitterThumbnailRenderer::StaticClass());
		UThumbnailManager::Get().RegisterCustomRenderer(UNiagaraSystem::StaticClass(), UNiagaraSystemThumbnailRenderer::StaticClass());
		bThumbnailRenderersRegistered = true;
	}

	// The editor should be valid at this point.. log a warning if not!
	if (GEditor)
	{
		GEditor->OnExecParticleInvoked().AddRaw(this, &FNiagaraEditorModule::OnExecParticleInvoked);
	}
	else
	{
		UE_LOG(LogNiagaraEditor, Warning, TEXT("GEditor isn't valid! Particle reset commands will not work for Niagara components!"));
	}
}

FNiagaraEditorModule& FNiagaraEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
}

void FNiagaraEditorModule::OnNiagaraSettingsChangedEvent(const FString& PropertyName, const UNiagaraSettings* Settings)
{
	if (PropertyName == "AdditionalParameterTypes" || PropertyName == "AdditionalPayloadTypes")
	{
		FNiagaraTypeDefinition::RecreateUserDefinedTypeRegistry();
	}
}

void FNiagaraEditorModule::RegisterTypeUtilities(FNiagaraTypeDefinition Type, TSharedRef<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> EditorUtilities)
{
	TypeEditorsCS.Lock();
	TypeToEditorUtilitiesMap.Add(Type, EditorUtilities);
	TypeEditorsCS.Unlock();
}


TSharedPtr<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> FNiagaraEditorModule::GetTypeUtilities(const FNiagaraTypeDefinition& Type)
{
	TypeEditorsCS.Lock();
	TSharedRef<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe>* EditorUtilities = TypeToEditorUtilitiesMap.Find(Type);
	TypeEditorsCS.Unlock();

	if(EditorUtilities != nullptr)
	{
		return *EditorUtilities;
	}

	if (Type.IsEnum())
	{
		return EnumTypeUtilities;
	}

	return TSharedPtr<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe>();
}


void FNiagaraEditorModule::RegisterWidgetProvider(TSharedRef<INiagaraEditorWidgetProvider> InWidgetProvider)
{
	checkf(WidgetProvider.IsValid() == false, TEXT("Widget provider has already been set."));
	WidgetProvider = InWidgetProvider;
}

void FNiagaraEditorModule::UnregisterWidgetProvider(TSharedRef<INiagaraEditorWidgetProvider> InWidgetProvider)
{	
	checkf(WidgetProvider.IsValid() && WidgetProvider == InWidgetProvider, TEXT("Can only unregister the widget provider that was originally registered."));
	WidgetProvider.Reset();
}

TSharedRef<INiagaraEditorWidgetProvider> FNiagaraEditorModule::GetWidgetProvider() const
{
	return WidgetProvider.ToSharedRef();
}

TSharedRef<FNiagaraScriptMergeManager> FNiagaraEditorModule::GetScriptMergeManager() const
{
	return ScriptMergeManager.ToSharedRef();
}

void FNiagaraEditorModule::RegisterParameterTrackCreatorForType(const UScriptStruct& StructType, FOnCreateMovieSceneTrackForParameter CreateTrack)
{
	checkf(TypeToParameterTrackCreatorMap.Contains(&StructType) == false, TEXT("Type already registered"));
	TypeToParameterTrackCreatorMap.Add(&StructType, CreateTrack);
}

void FNiagaraEditorModule::UnregisterParameterTrackCreatorForType(const UScriptStruct& StructType)
{
	TypeToParameterTrackCreatorMap.Remove(&StructType);
}

bool FNiagaraEditorModule::CanCreateParameterTrackForType(const UScriptStruct& StructType)
{
	return TypeToParameterTrackCreatorMap.Contains(&StructType);
}

UMovieSceneNiagaraParameterTrack* FNiagaraEditorModule::CreateParameterTrackForType(const UScriptStruct& StructType, FNiagaraVariable Parameter)
{
	FOnCreateMovieSceneTrackForParameter* CreateTrack = TypeToParameterTrackCreatorMap.Find(&StructType);
	checkf(CreateTrack != nullptr, TEXT("Type not supported"));
	UMovieSceneNiagaraParameterTrack* ParameterTrack = CreateTrack->Execute(Parameter);
	ParameterTrack->SetParameter(Parameter);
	return ParameterTrack;
}

const FNiagaraEditorCommands& FNiagaraEditorModule::Commands()
{
	return FNiagaraEditorCommands::Get();
}

TSharedPtr<FNiagaraSystemViewModel> FNiagaraEditorModule::GetExistingViewModelForSystem(UNiagaraSystem* InSystem)
{
	return FNiagaraSystemViewModel::GetExistingViewModelForObject(InSystem);
}

const FNiagaraEditorCommands& FNiagaraEditorModule::GetCommands() const
{
	return FNiagaraEditorCommands::Get();
}

void FNiagaraEditorModule::InvalidateCachedScriptAssetData()
{
	CachedScriptAssetHighlights.Reset();
}

const TArray<FNiagaraScriptHighlight>& FNiagaraEditorModule::GetCachedScriptAssetHighlights() const
{
	if (CachedScriptAssetHighlights.IsSet() == false)
	{
		CachedScriptAssetHighlights = TArray<FNiagaraScriptHighlight>();
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> ScriptAssets;
		AssetRegistryModule.Get().GetAssetsByClass(UNiagaraScript::StaticClass()->GetFName(), ScriptAssets);
		for (const FAssetData& ScriptAsset : ScriptAssets)
		{
			if (ScriptAsset.IsAssetLoaded())
			{
				UNiagaraScript* Script = CastChecked<UNiagaraScript>(ScriptAsset.GetAsset());
				for (const FNiagaraScriptHighlight& Highlight : Script->Highlights)
				{
					if (Highlight.IsValid())
					{
						CachedScriptAssetHighlights->AddUnique(Highlight);
					}
				}
			}
			else
			{
				FString HighlightsString;
				if (ScriptAsset.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraScript, Highlights), HighlightsString))
				{
					TArray<FNiagaraScriptHighlight> Highlights;
					FNiagaraScriptHighlight::JsonToArray(HighlightsString, Highlights);
					for (const FNiagaraScriptHighlight& Highlight : Highlights)
					{
						if (Highlight.IsValid())
						{
							CachedScriptAssetHighlights->AddUnique(Highlight);
						}
					}
				}
			}
		}
	}
	return CachedScriptAssetHighlights.GetValue();
}

void FNiagaraEditorModule::GetScriptAssetsMatchingHighlight(const FNiagaraScriptHighlight& InHighlight, TArray<FAssetData>& OutMatchingScriptAssets) const
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> ScriptAssets;
	AssetRegistryModule.Get().GetAssetsByClass(UNiagaraScript::StaticClass()->GetFName(), ScriptAssets);
	for (const FAssetData& ScriptAsset : ScriptAssets)
	{
		if (ScriptAsset.IsAssetLoaded())
		{
			UNiagaraScript* Script = CastChecked<UNiagaraScript>(ScriptAsset.GetAsset());
			for (const FNiagaraScriptHighlight& Highlight : Script->Highlights)
			{
				if (Highlight == InHighlight)
				{
					OutMatchingScriptAssets.Add(ScriptAsset);
					break;
				}
			}
		}
		else
		{
			FString HighlightsString;
			if (ScriptAsset.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraScript, Highlights), HighlightsString))
			{
				TArray<FNiagaraScriptHighlight> Highlights;
				FNiagaraScriptHighlight::JsonToArray(HighlightsString, Highlights);
				for (const FNiagaraScriptHighlight& Highlight : Highlights)
				{
					if (Highlight == InHighlight)
					{
						OutMatchingScriptAssets.Add(ScriptAsset);
						break;
					}
				}
			}
		}
	}
}

FNiagaraClipboard& FNiagaraEditorModule::GetClipboard() const
{
	return Clipboard.Get();
}

void FNiagaraEditorModule::RegisterAssetTypeAction(IAssetTools& AssetTools, TSharedRef<IAssetTypeActions> Action)
{
	AssetTools.RegisterAssetTypeActions(Action);
	CreatedAssetTypeActions.Add(Action);
}

void FNiagaraEditorModule::RegisterSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	if (SettingsModule != nullptr)
	{
		SequencerSettings = USequencerSettingsContainer::GetOrCreate<USequencerSettings>(TEXT("NiagaraSequenceEditor"));

		SettingsModule->RegisterSettings("Editor", "ContentEditors", "NiagaraSequenceEditor",
			LOCTEXT("NiagaraSequenceEditorSettingsName", "Niagara Sequence Editor"),
			LOCTEXT("NiagaraSequenceEditorSettingsDescription", "Configure the look and feel of the Niagara Sequence Editor."),
			SequencerSettings);	
	}
}

void FNiagaraEditorModule::UnregisterSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	if (SettingsModule != nullptr)
	{
		SettingsModule->UnregisterSettings("Editor", "ContentEditors", "NiagaraSequenceEditor");
	}
}

void FNiagaraEditorModule::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (SequencerSettings)
	{
		Collector.AddReferencedObject(SequencerSettings);
	}
}

void FNiagaraEditorModule::OnPreGarbageCollection()
{
	// For commandlets like GenerateDistillFileSetsCommandlet, they just load the package and do some hierarchy navigation within it 
	// tracking sub-assets, then they garbage collect. Since nothing is holding onto the system at the root level, it will be summarily
	// killed and any of references will also be killed. To thwart this for now, we are forcing the compilations to complete BEFORE
	// garbage collection kicks in. To do otherwise for now has too many loose ends (a system may be left around after the level has been
	// unloaded, leaving behind weird external references, etc). This should be revisited when more time is available (i.e. not days before a 
	// release is due to go out).
	for (TObjectIterator<UNiagaraSystem> It; It; ++It)
	{
		UNiagaraSystem* System = *It;
		if (System && System->HasOutstandingCompilationRequests())
		{
			System->WaitForCompilationComplete();
		}
	}
}

void FNiagaraEditorModule::OnExecParticleInvoked(const TCHAR* Str)
{
	// Very similar logic to UEditorEngine::Exec_Particle
	if (FParse::Command(&Str, TEXT("RESET")))
	{
		TArray<AEmitter*> EmittersToReset;
		if (FParse::Command(&Str, TEXT("SELECTED")))
		{
			// Reset any selected emitters in the level
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				AActor* Actor = static_cast<AActor*>(*It);
				checkSlow(Actor->IsA(AActor::StaticClass()));

				ANiagaraActor* Emitter = Cast<ANiagaraActor>(Actor);
				if (Emitter)
				{
					Emitter->ResetInLevel();
				}
			}
		}
		else if (FParse::Command(&Str, TEXT("ALL")))
		{
			// Reset ALL emitters in the level
			for (TObjectIterator<ANiagaraActor> It; It; ++It)
			{
				ANiagaraActor* Emitter = *It;
				Emitter->ResetInLevel();
			}
		}
	}
}

void FNiagaraEditorModule::ReinitializeStyle()
{
	FNiagaraEditorStyle::Shutdown();
	FNiagaraEditorStyle::Initialize();
}

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE
