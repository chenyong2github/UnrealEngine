// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEditorUtilities.h"
#include "NiagaraEditorModule.h"
#include "INiagaraEditorTypeUtilities.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeParameterMapSet.h"
#include "NiagaraDataInterface.h"
#include "NiagaraComponent.h"
#include "UObject/StructOnScope.h"
#include "NiagaraGraph.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScript.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraOverviewNode.h"
#include "EdGraphUtilities.h"
#include "NiagaraConstants.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "HAL/PlatformApplicationMisc.h"
#include "NiagaraEditorStyle.h"
#include "EditorStyleSet.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraOverviewGraphViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "HAL/PlatformApplicationMisc.h"
#include "AssetRegistryModule.h"
#include "Misc/FeedbackContext.h"
#include "EdGraphSchema_Niagara.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "EdGraph/EdGraphPin.h"
#include "NiagaraNodeWriteDataSet.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraNodeStaticSwitch.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraParameterMapHistory.h"
#include "ScopedTransaction.h"
#include "NiagaraStackEditorData.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Commands/GenericCommands.h"
#include "UObject/TextProperty.h"
#include "Editor/EditorEngine.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Styling/CoreStyle.h"
#include "Framework/Notifications/NotificationManager.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraEditorSettings.h"

#define LOCTEXT_NAMESPACE "FNiagaraEditorUtilities"

TSet<FName> FNiagaraEditorUtilities::GetSystemConstantNames()
{
	TSet<FName> SystemConstantNames;
	for (const FNiagaraVariable& SystemConstant : FNiagaraConstants::GetEngineConstants())
	{
		SystemConstantNames.Add(SystemConstant.GetName());
	}
	return SystemConstantNames;
}

void FNiagaraEditorUtilities::GetTypeDefaultValue(const FNiagaraTypeDefinition& Type, TArray<uint8>& DefaultData)
{
	if (const UScriptStruct* ScriptStruct = Type.GetScriptStruct())
	{
		FNiagaraVariable DefaultVariable(Type, NAME_None);
		ResetVariableToDefaultValue(DefaultVariable);

		DefaultData.SetNumUninitialized(Type.GetSize());
		DefaultVariable.CopyTo(DefaultData.GetData());
	}
}

void FNiagaraEditorUtilities::ResetVariableToDefaultValue(FNiagaraVariable& Variable)
{
	if (const UScriptStruct* ScriptStruct = Variable.GetType().GetScriptStruct())
	{
		FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::GetModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
		TSharedPtr<INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> TypeEditorUtilities = NiagaraEditorModule.GetTypeUtilities(Variable.GetType());
		if (TypeEditorUtilities.IsValid() && TypeEditorUtilities->CanProvideDefaultValue())
		{
			TypeEditorUtilities->UpdateVariableWithDefaultValue(Variable);
		}
		else
		{
			Variable.AllocateData();
			ScriptStruct->InitializeDefaultValue(Variable.GetData());
		}
	}
}

void FNiagaraEditorUtilities::InitializeParameterInputNode(UNiagaraNodeInput& InputNode, const FNiagaraTypeDefinition& Type, const UNiagaraGraph* InGraph, FName InputName)
{
	InputNode.Usage = ENiagaraInputNodeUsage::Parameter;
	InputNode.bCanRenameNode = true;
	InputName = UNiagaraNodeInput::GenerateUniqueName(InGraph, InputName, ENiagaraInputNodeUsage::Parameter);
	InputNode.Input.SetName(InputName);
	InputNode.Input.SetType(Type);
	if (InGraph) // Only compute sort priority if a graph was passed in, similar to the way that GenrateUniqueName works above.
	{
		InputNode.CallSortPriority = UNiagaraNodeInput::GenerateNewSortPriority(InGraph, InputName, ENiagaraInputNodeUsage::Parameter);
	}
	if (Type.GetScriptStruct() != nullptr)
	{
		ResetVariableToDefaultValue(InputNode.Input);
		if (InputNode.GetDataInterface() != nullptr)
		{
			InputNode.SetDataInterface(nullptr);
		}
	}
	else if(Type.IsDataInterface())
	{
		InputNode.Input.AllocateData(); // Frees previously used memory if we're switching from a struct to a class type.
		InputNode.SetDataInterface(NewObject<UNiagaraDataInterface>(&InputNode, const_cast<UClass*>(Type.GetClass()), NAME_None, RF_Transactional));
	}
}

void FNiagaraEditorUtilities::GetParameterVariablesFromSystem(UNiagaraSystem& System, TArray<FNiagaraVariable>& ParameterVariables,
	FNiagaraEditorUtilities::FGetParameterVariablesFromSystemOptions Options)
{
	UNiagaraScript* SystemScript = System.GetSystemSpawnScript();
	if (SystemScript != nullptr)
	{
		UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(SystemScript->GetSource());
		if (ScriptSource != nullptr)
		{
			UNiagaraGraph* SystemGraph = ScriptSource->NodeGraph;
			if (SystemGraph != nullptr)
			{
				UNiagaraGraph::FFindInputNodeOptions FindOptions;
				FindOptions.bIncludeAttributes = false;
				FindOptions.bIncludeSystemConstants = false;
				FindOptions.bIncludeTranslatorConstants = false;
				FindOptions.bFilterDuplicates = true;

				TArray<UNiagaraNodeInput*> InputNodes;
				SystemGraph->FindInputNodes(InputNodes, FindOptions);
				for (UNiagaraNodeInput* InputNode : InputNodes)
				{
					bool bIsStructParameter = InputNode->Input.GetType().GetScriptStruct() != nullptr;
					bool bIsDataInterfaceParameter = InputNode->Input.GetType().GetClass() != nullptr;
					if ((bIsStructParameter && Options.bIncludeStructParameters) || (bIsDataInterfaceParameter && Options.bIncludeDataInterfaceParameters))
					{
						ParameterVariables.Add(InputNode->Input);
					}
				}
			}
		}
	}
}

// TODO: This is overly complicated.
void FNiagaraEditorUtilities::FixUpPastedNodes(UEdGraph* Graph, TSet<UEdGraphNode*> PastedNodes)
{
	// Collect existing inputs.
	TArray<UNiagaraNodeInput*> CurrentInputs;
	Graph->GetNodesOfClass<UNiagaraNodeInput>(CurrentInputs);
	TSet<FNiagaraVariable> ExistingInputs;
	TMap<FNiagaraVariable, UNiagaraNodeInput*> ExistingNodes;
	int32 HighestSortOrder = -1; // Set to -1 initially, so that in the event of no nodes, we still get zero.
	for (UNiagaraNodeInput* CurrentInput : CurrentInputs)
	{
		if (PastedNodes.Contains(CurrentInput) == false && CurrentInput->Usage == ENiagaraInputNodeUsage::Parameter)
		{
			ExistingInputs.Add(CurrentInput->Input);
			ExistingNodes.Add(CurrentInput->Input) = CurrentInput;
			if (CurrentInput->CallSortPriority > HighestSortOrder)
			{
				HighestSortOrder = CurrentInput->CallSortPriority;
			}
		}
	}

	// Collate pasted inputs nodes by their input for further processing.
	TMap<FNiagaraVariable, TArray<UNiagaraNodeInput*>> InputToPastedInputNodes;
	for (UEdGraphNode* PastedNode : PastedNodes)
	{
		UNiagaraNodeInput* PastedInputNode = Cast<UNiagaraNodeInput>(PastedNode);
		if (PastedInputNode != nullptr && PastedInputNode->Usage == ENiagaraInputNodeUsage::Parameter && ExistingInputs.Contains(PastedInputNode->Input) == false)
		{
			TArray<UNiagaraNodeInput*>* NodesForInput = InputToPastedInputNodes.Find(PastedInputNode->Input);
			if (NodesForInput == nullptr)
			{
				NodesForInput = &InputToPastedInputNodes.Add(PastedInputNode->Input);
			}
			NodesForInput->Add(PastedInputNode);
		}
	}

	// Fix up the nodes based on their relationship to the existing inputs.
	for (auto PastedPairIterator = InputToPastedInputNodes.CreateIterator(); PastedPairIterator; ++PastedPairIterator)
	{
		FNiagaraVariable PastedInput = PastedPairIterator.Key();
		TArray<UNiagaraNodeInput*>& PastedNodesForInput = PastedPairIterator.Value();

		// Try to find an existing input which matches the pasted input by both name and type so that the pasted nodes
		// can be assigned the same id and value, to facilitate pasting multiple times from the same source graph.
		FNiagaraVariable* MatchingInputByNameAndType = nullptr;
		UNiagaraNodeInput* MatchingNode = nullptr;
		for (FNiagaraVariable& ExistingInput : ExistingInputs)
		{
			if (PastedInput.GetName() == ExistingInput.GetName() && PastedInput.GetType() == ExistingInput.GetType())
			{
				MatchingInputByNameAndType = &ExistingInput;
				UNiagaraNodeInput** FoundNode = ExistingNodes.Find(ExistingInput);
				if (FoundNode != nullptr)
				{
					MatchingNode = *FoundNode;
				}
				break;
			}
		}

		if (MatchingInputByNameAndType != nullptr && MatchingNode != nullptr)
		{
			// Update the id and value on the matching pasted nodes.
			for (UNiagaraNodeInput* PastedNodeForInput : PastedNodesForInput)
			{
				if (nullptr == PastedNodeForInput)
				{
					continue;
				}
				PastedNodeForInput->CallSortPriority = MatchingNode->CallSortPriority;
				PastedNodeForInput->ExposureOptions = MatchingNode->ExposureOptions;
				PastedNodeForInput->Input.AllocateData();
				PastedNodeForInput->Input.SetData(MatchingInputByNameAndType->GetData());
			}
		}
		else
		{
			// Check for duplicate names
			TSet<FName> ExistingNames;
			for (FNiagaraVariable& ExistingInput : ExistingInputs)
			{
				ExistingNames.Add(ExistingInput.GetName());
			}
			if (ExistingNames.Contains(PastedInput.GetName()))
			{
				FName UniqueName = FNiagaraUtilities::GetUniqueName(PastedInput.GetName(), ExistingNames.Union(FNiagaraEditorUtilities::GetSystemConstantNames()));
				for (UNiagaraNodeInput* PastedNodeForInput : PastedNodesForInput)
				{
					PastedNodeForInput->Input.SetName(UniqueName);
				}
			}

			// Assign the pasted inputs the same new id and add them to the end of the parameters list.
			int32 NewSortOrder = ++HighestSortOrder;
			for (UNiagaraNodeInput* PastedNodeForInput : PastedNodesForInput)
			{
				PastedNodeForInput->CallSortPriority = NewSortOrder;
			}
		}
	}

	// Fix up pasted function call nodes
	TArray<UNiagaraNodeFunctionCall*> FunctionCallNodes;
	Graph->GetNodesOfClass<UNiagaraNodeFunctionCall>(FunctionCallNodes);
	TSet<FName> ExistingNames;
	for (UNiagaraNodeFunctionCall* FunctionCallNode : FunctionCallNodes)
	{
		if (PastedNodes.Contains(FunctionCallNode) == false)
		{
			ExistingNames.Add(*FunctionCallNode->GetFunctionName());
		}
	}

	TMap<FName, FName> OldFunctionToNewFunctionNameMap;
	for (UEdGraphNode* PastedNode : PastedNodes)
	{
		UNiagaraNodeFunctionCall* PastedFunctionCallNode = Cast<UNiagaraNodeFunctionCall>(PastedNode);
		if (PastedFunctionCallNode != nullptr)
		{
			if (ExistingNames.Contains(*PastedFunctionCallNode->GetFunctionName()))
			{
				FName FunctionCallName = *PastedFunctionCallNode->GetFunctionName();
				FName UniqueFunctionCallName = FNiagaraUtilities::GetUniqueName(FunctionCallName, ExistingNames);
				PastedFunctionCallNode->SuggestName(UniqueFunctionCallName.ToString());
				FName ActualPastedFunctionCallName = *PastedFunctionCallNode->GetFunctionName();
				ExistingNames.Add(ActualPastedFunctionCallName);
				OldFunctionToNewFunctionNameMap.Add(FunctionCallName, ActualPastedFunctionCallName);
			}
		}
	}

	for (UEdGraphNode* PastedNode : PastedNodes)
	{
		UNiagaraNodeParameterMapSet* ParameterMapSetNode = Cast<UNiagaraNodeParameterMapSet>(PastedNode);
		if (ParameterMapSetNode != nullptr)
		{
			TArray<UEdGraphPin*> InputPins;
			ParameterMapSetNode->GetInputPins(InputPins);
			for (UEdGraphPin* InputPin : InputPins)
			{
				FNiagaraParameterHandle InputHandle(InputPin->PinName);
				if (OldFunctionToNewFunctionNameMap.Contains(InputHandle.GetNamespace()))
				{
					// Rename any inputs pins on parameter map sets who's function calls were renamed.
					InputPin->PinName = FNiagaraParameterHandle(OldFunctionToNewFunctionNameMap[InputHandle.GetNamespace()], InputHandle.GetName()).GetParameterHandleString();
				}
			}
		}
	}
}

void FNiagaraEditorUtilities::WriteTextFileToDisk(FString SaveDirectory, FString FileName, FString TextToSave, bool bAllowOverwriting)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// CreateDirectoryTree returns true if the destination
	// directory existed prior to call or has been created
	// during the call.
	if (PlatformFile.CreateDirectoryTree(*SaveDirectory))
	{
		// Get absolute file path
		FString AbsoluteFilePath = SaveDirectory + "/" + FileName;

		// Allow overwriting or file doesn't already exist
		if (bAllowOverwriting || !PlatformFile.FileExists(*AbsoluteFilePath))
		{
			if (FFileHelper::SaveStringToFile(TextToSave, *AbsoluteFilePath))
			{
				UE_LOG(LogNiagaraEditor, Log, TEXT("Wrote file to %s"), *AbsoluteFilePath);
				return;
			}

		}
	}
}


bool FNiagaraEditorUtilities::PODPropertyAppendCompileHash(const void* Container, FProperty* Property, const FString& PropertyName, FNiagaraCompileHashVisitor* InVisitor) 
{
	if (Property->IsA(FFloatProperty::StaticClass()))
	{
		FFloatProperty* CastProp = CastFieldChecked<FFloatProperty>(Property);
		float Value = CastProp->GetPropertyValue_InContainer(Container, 0);
		InVisitor->UpdatePOD(*PropertyName, Value);
		return true;
	}
	else if (Property->IsA(FIntProperty::StaticClass()))
	{
		FIntProperty* CastProp = CastFieldChecked<FIntProperty>(Property);
		int32 Value = CastProp->GetPropertyValue_InContainer(Container, 0);
		InVisitor->UpdatePOD(*PropertyName, Value);
		return true;
	}
	else if (Property->IsA(FInt16Property::StaticClass()))
	{
		FInt16Property* CastProp = CastFieldChecked<FInt16Property>(Property);
		int16 Value = CastProp->GetPropertyValue_InContainer(Container, 0);
		InVisitor->UpdatePOD(*PropertyName, Value);
		return true;
	}
	else if (Property->IsA(FUInt32Property::StaticClass()))
	{
		FUInt32Property* CastProp = CastFieldChecked<FUInt32Property>(Property);
		uint32 Value = CastProp->GetPropertyValue_InContainer(Container, 0);
		InVisitor->UpdatePOD(*PropertyName, Value);
		return true;
	}
	else if (Property->IsA(FUInt16Property::StaticClass()))
	{
		FUInt16Property* CastProp = CastFieldChecked<FUInt16Property>(Property);
		uint16 Value = CastProp->GetPropertyValue_InContainer(Container, 0);
		InVisitor->UpdatePOD(*PropertyName, Value);
		return true;
	}
	else if (Property->IsA(FByteProperty::StaticClass()))
	{
		FByteProperty* CastProp = CastFieldChecked<FByteProperty>(Property);
		uint8 Value = CastProp->GetPropertyValue_InContainer(Container, 0);
		InVisitor->UpdatePOD(*PropertyName, Value);
		return true;
	}
	else if (Property->IsA(FBoolProperty::StaticClass()))
	{
		FBoolProperty* CastProp = CastFieldChecked<FBoolProperty>(Property);
		bool Value = CastProp->GetPropertyValue_InContainer(Container, 0);
		InVisitor->UpdatePOD(*PropertyName, Value);
		return true;
	}
	else if (Property->IsA(FNameProperty::StaticClass()))
	{
		FNameProperty* CastProp = CastFieldChecked<FNameProperty>(Property);
		FName Value = CastProp->GetPropertyValue_InContainer(Container);
		InVisitor->UpdateString(*PropertyName, Value.ToString());
		return true;
	}
	else if (Property->IsA(FStrProperty::StaticClass()))
	{
		FStrProperty* CastProp = CastFieldChecked<FStrProperty>(Property);
		FString Value = CastProp->GetPropertyValue_InContainer(Container);
		InVisitor->UpdateString(*PropertyName, Value);
		return true;
	}
	return false;
}

bool FNiagaraEditorUtilities::NestedPropertiesAppendCompileHash(const void* Container, const UStruct* Struct, EFieldIteratorFlags::SuperClassFlags IteratorFlags, const FString& BaseName, FNiagaraCompileHashVisitor* InVisitor) 
{
	// We special case FNiagaraTypeDefinitions here because they need to write out a lot more than just their standalone uproperties.
	if (Struct == FNiagaraTypeDefinition::StaticStruct())
	{
		FNiagaraTypeDefinition* TypeDef = (FNiagaraTypeDefinition*)Container;
		if (TypeDef)
		{
			TypeDef->AppendCompileHash(InVisitor);
			return true;
		}
	}

	TFieldIterator<FProperty> PropertyCountIt(Struct, IteratorFlags);
	int32 NumProperties = 0;
	for (; PropertyCountIt; ++PropertyCountIt)
	{
		NumProperties++;
	}

	for (TFieldIterator<FProperty> PropertyIt(Struct, IteratorFlags); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;

		static FName SkipMeta = TEXT("SkipForCompileHash");
		if (Property->HasMetaData(SkipMeta))
		{
			continue;
		}

		FString PropertyName = (NumProperties == 1) ? (BaseName) : (BaseName + "." + Property->GetName());

		if (PODPropertyAppendCompileHash(Container, Property, PropertyName, InVisitor))
		{
			continue;
		}
		else if (Property->IsA(FMapProperty::StaticClass()))
		{
			FMapProperty* CastProp = CastFieldChecked<FMapProperty>(Property);
			FScriptMapHelper MapHelper(CastProp, CastProp->ContainerPtrToValuePtr<void>(Container));
			InVisitor->UpdatePOD(*PropertyName, MapHelper.Num());
			if (MapHelper.GetKeyProperty())
			{
				InVisitor->UpdateString(TEXT("KeyPathname"), MapHelper.GetKeyProperty()->GetPathName());
				InVisitor->UpdateString(TEXT("ValuePathname"), MapHelper.GetValueProperty()->GetPathName());

				// We currently only support maps with keys of FNames. Anything else should generate a warning.
				if (MapHelper.GetKeyProperty()->GetClass() == FNameProperty::StaticClass())
				{
					// To be safe, let's gather up all the keys and sort them lexicographically so that this is stable across application runs.
					TArray<FName> Names;
					Names.AddUninitialized(MapHelper.Num());
					for (int32 i = 0; i < MapHelper.Num(); i++)
					{
						FName* KeyPtr = (FName*)(MapHelper.GetKeyPtr(i));
						if (KeyPtr)
						{
							Names[i] = *KeyPtr;
						}
						else
						{
							Names[i] = FName();
							UE_LOG(LogNiagaraEditor, Warning, TEXT("Bad key in %s at %d"), *Property->GetName(), i);
						}
					}
					// Sort stably over runs
					Names.Sort(FNameLexicalLess());

					// Now hash out the values directly.
					// We support map values of POD types or map values of structs with POD types internally. Anything else we should generate a warning on.
					if (MapHelper.GetValueProperty()->IsA(FStructProperty::StaticClass()))
					{
						bool bPassed = true;
						FStructProperty* StructProp = CastFieldChecked<FStructProperty>(MapHelper.GetValueProperty());

						for (int32 ArrayIdx = 0; ArrayIdx < MapHelper.Num(); ArrayIdx++)
						{
							InVisitor->UpdateString(*FString::Printf(TEXT("Key[%d]"), ArrayIdx), Names[ArrayIdx].ToString());
							if (!NestedPropertiesAppendCompileHash(MapHelper.GetValuePtr(ArrayIdx), StructProp->Struct, EFieldIteratorFlags::IncludeSuper, FString::Printf(TEXT("Value[%d]"), ArrayIdx), InVisitor))
							{
								UE_LOG(LogNiagaraEditor, Warning, TEXT("Skipping %s because it is an map value property of unsupported underlying type, please add \"meta = (SkipForCompileHash=\"true\")\" to avoid this warning in the future or handle it yourself in NestedPropertiesAppendCompileHash!"), *Property->GetName());
								bPassed = false;
								continue;
							}
						}
						if (bPassed)
						{
							continue;
						}
					}
					else
					{
						bool bPassed = true;
						for (int32 ArrayIdx = 0; ArrayIdx < MapHelper.Num(); ArrayIdx++)
						{
							InVisitor->UpdateString(*FString::Printf(TEXT("Key[%d]"), ArrayIdx), Names[ArrayIdx].ToString());
							if (!PODPropertyAppendCompileHash(MapHelper.GetPairPtr(ArrayIdx), MapHelper.GetValueProperty(), FString::Printf(TEXT("Value[%d]"), ArrayIdx), InVisitor))
							{
								UE_LOG(LogNiagaraEditor, Warning, TEXT("Skipping %s because it is an map value property of unsupported underlying type, please add \"meta = (SkipForCompileHash=\"true\")\" to avoid this warning in the future or handle it yourself in PODPropertyAppendCompileHash!"), *Property->GetName());
								bPassed = false;
								continue;
							}
						}
						if (bPassed)
						{
							continue;
						}
					}
				}
				else
				{
					UE_LOG(LogNiagaraEditor, Warning, TEXT("Skipping %s because it is a map property, please add \"meta = (SkipForCompileHash=\"true\")\" to avoid this warning in the future or handle it yourself in NestedPropertiesAppendCompileHash!"), *Property->GetName());
				}
			}
			continue;
		}
		else if (Property->IsA(FArrayProperty::StaticClass()))
		{
			FArrayProperty* CastProp = CastFieldChecked<FArrayProperty>(Property);

			FScriptArrayHelper ArrayHelper(CastProp, CastProp->ContainerPtrToValuePtr<void>(Container));
			InVisitor->UpdatePOD(*PropertyName, ArrayHelper.Num());
			InVisitor->UpdateString(TEXT("InnerPathname"), CastProp->Inner->GetPathName());

			// We support arrays of POD types or arrays of structs with POD types internally. Anything else we should generate a warning on.
			if (CastProp->Inner->IsA(FStructProperty::StaticClass()))
			{
				bool bPassed = true;
				FStructProperty* StructProp = CastFieldChecked<FStructProperty>(CastProp->Inner);

				for (int32 ArrayIdx = 0; ArrayIdx < ArrayHelper.Num(); ArrayIdx++)
				{
					if (!NestedPropertiesAppendCompileHash(ArrayHelper.GetRawPtr(ArrayIdx), StructProp->Struct, EFieldIteratorFlags::IncludeSuper, PropertyName, InVisitor))
					{
						UE_LOG(LogNiagaraEditor, Warning, TEXT("Skipping %s because it is an array property of unsupported underlying type, please add \"meta = (SkipForCompileHash=\"true\")\" to avoid this warning in the future or handle it yourself in NestedPropertiesAppendCompileHash!"), *Property->GetName());
						bPassed = false;
						continue;
					}
				}
				if (bPassed)
				{
					continue;
				}
			}
			else
			{
				bool bPassed = true;
				for (int32 ArrayIdx = 0; ArrayIdx < ArrayHelper.Num(); ArrayIdx++)
				{
					if (!PODPropertyAppendCompileHash(ArrayHelper.GetRawPtr(ArrayIdx), CastProp->Inner, PropertyName, InVisitor))
					{
						if (bPassed)
						{
							UE_LOG(LogNiagaraEditor, Warning, TEXT("Skipping %s because it is an array property of unsupported underlying type, please add \"meta = (SkipForCompileHash=\"true\")\" to avoid this warning in the future or handle it yourself in NestedPropertiesAppendCompileHash!"), *Property->GetName());
						}
						bPassed = false;
						continue;
					}
				}
				if (bPassed)
				{
					continue;
				}
			}

			UE_LOG(LogNiagaraEditor, Warning, TEXT("Skipping %s because it is an array property, please add \"meta = (SkipForCompileHash=\"true\")\" to avoid this warning in the future or handle it yourself in NestedPropertiesAppendCompileHash!"), *Property->GetName());
			continue;
		}
		else if (Property->IsA(FTextProperty::StaticClass()))
		{
			FTextProperty* CastProp = CastFieldChecked<FTextProperty>(Property);
			UE_LOG(LogNiagaraEditor, Warning, TEXT("Skipping %s because it is a UText property, please add \"meta = (SkipForCompileHash=\"true\")\" to avoid this warning in the future or handle it yourself in NestedPropertiesAppendCompileHash!"), *Property->GetName());
			return true;
		}
		else if (Property->IsA(FEnumProperty::StaticClass()))
		{
			FEnumProperty* CastProp = CastFieldChecked<FEnumProperty>(Property);
			const void* EnumContainer = Property->ContainerPtrToValuePtr<uint8>(Container);
			if (PODPropertyAppendCompileHash(EnumContainer, CastProp->GetUnderlyingProperty(), PropertyName, InVisitor))
			{
				continue;
			}
			check(false);
			return false;
		}
		else if (Property->IsA(FObjectProperty::StaticClass()))
		{
			FObjectProperty* CastProp = CastFieldChecked<FObjectProperty>(Property);
			UObject* Obj = CastProp->GetObjectPropertyValue_InContainer(Container);
			if (Obj != nullptr)
			{
				// We just do name here as sometimes things will be in a transient package or something tricky.
				// Because we do nested id's for each called graph, it should work out in the end to have a different
				// value in the compile array if the scripts are the same name but different locations.
				InVisitor->UpdateString(*PropertyName, Obj->GetName());
			}
			else
			{
				InVisitor->UpdateString(*PropertyName, TEXT("nullptr"));
			}
			continue;
		}
		else if (Property->IsA(FStructProperty::StaticClass()))
		{
			FStructProperty* StructProp = CastFieldChecked<FStructProperty>(Property);
			const void* StructContainer = Property->ContainerPtrToValuePtr<uint8>(Container);
			NestedPropertiesAppendCompileHash(StructContainer, StructProp->Struct, EFieldIteratorFlags::IncludeSuper, PropertyName, InVisitor);
			continue;
		}
		else
		{
			check(false);
			return false;
		}
	}
	return true;
}

void FNiagaraEditorUtilities::GatherChangeIds(UNiagaraEmitter& Emitter, TMap<FGuid, FGuid>& ChangeIds, const FString& InDebugName, bool bWriteToLogDir)
{
	FString ExportText;
	ChangeIds.Empty();
	TArray<UNiagaraGraph*> Graphs;
	TArray<UNiagaraScript*> Scripts;
	Emitter.GetScripts(Scripts);

	// First gather all the graphs used by this emitter..
	for (UNiagaraScript* Script : Scripts)
	{
		if (Script != nullptr && Script->GetSource() != nullptr)
		{
			UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(Script->GetSource());
			if (ScriptSource != nullptr)
			{
				Graphs.AddUnique(ScriptSource->NodeGraph);
			}

			if (bWriteToLogDir)
			{
				FNiagaraVMExecutableDataId Id;
				Script->ComputeVMCompilationId(Id);
				FString KeyString;
				Id.AppendKeyString(KeyString);

				UEnum* FoundEnum = StaticEnum<ENiagaraScriptUsage>();

				FString ResultsEnum = TEXT("??");
				if (FoundEnum)
				{
					ResultsEnum = FoundEnum->GetNameStringByValue((int64)Script->Usage);
				}

				ExportText += FString::Printf(TEXT("Usage: %s CompileKey: %s\n"), *ResultsEnum, *KeyString );
			}
		}
	}

	
	// Now gather all the node change id's within these graphs.
	for (UNiagaraGraph* Graph : Graphs)
	{
		TArray<UNiagaraNode*> Nodes;
		Graph->GetNodesOfClass(Nodes);

		for (UNiagaraNode* Node : Nodes)
		{
			ChangeIds.Add(Node->NodeGuid, Node->GetChangeId());

			if (bWriteToLogDir)
			{
				ExportText += FString::Printf(TEXT("%40s    guid: %25s    changeId: %25s\n"), *Node->GetName(), *Node->NodeGuid.ToString(), *Node->GetChangeId().ToString());

			}
		}
	}

	if (bWriteToLogDir)
	{
		FNiagaraEditorUtilities::WriteTextFileToDisk(FPaths::ProjectLogDir(), InDebugName + TEXT(".txt"), ExportText, true);
	}
	
}

void FNiagaraEditorUtilities::GatherChangeIds(UNiagaraGraph& Graph, TMap<FGuid, FGuid>& ChangeIds, const FString& InDebugName, bool bWriteToLogDir)
{
	ChangeIds.Empty();
	TArray<UNiagaraGraph*> Graphs;
	
	FString ExportText;
	// Now gather all the node change id's within these graphs.
	{
		TArray<UNiagaraNode*> Nodes;
		Graph.GetNodesOfClass(Nodes);

		for (UNiagaraNode* Node : Nodes)
		{
			ChangeIds.Add(Node->NodeGuid, Node->GetChangeId());
			if (bWriteToLogDir)
			{
				ExportText += FString::Printf(TEXT("%40s    guid: %25s    changeId: %25s\n"), *Node->GetName(), *Node->NodeGuid.ToString(), *Node->GetChangeId().ToString());
			}
		}
	}

	if (bWriteToLogDir)
	{
		FNiagaraEditorUtilities::WriteTextFileToDisk(FPaths::ProjectLogDir(), InDebugName + TEXT(".txt"), ExportText, true);
	}
}

FText FNiagaraEditorUtilities::StatusToText(ENiagaraScriptCompileStatus Status)
{
	switch (Status)
	{
	default:
	case ENiagaraScriptCompileStatus::NCS_Unknown:
		return LOCTEXT("Recompile_Status", "Unknown status; should recompile");
	case ENiagaraScriptCompileStatus::NCS_Dirty:
		return LOCTEXT("Dirty_Status", "Dirty; needs to be recompiled");
	case ENiagaraScriptCompileStatus::NCS_Error:
		return LOCTEXT("CompileError_Status", "There was an error during compilation, see the log for details");
	case ENiagaraScriptCompileStatus::NCS_UpToDate:
		return LOCTEXT("GoodToGo_Status", "Good to go");
	case ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings:
		return LOCTEXT("GoodToGoWarning_Status", "There was a warning during compilation, see the log for details");
	}
}

ENiagaraScriptCompileStatus FNiagaraEditorUtilities::UnionCompileStatus(const ENiagaraScriptCompileStatus& StatusA, const ENiagaraScriptCompileStatus& StatusB)
{
	if (StatusA != StatusB)
	{
		if (StatusA == ENiagaraScriptCompileStatus::NCS_Unknown || StatusB == ENiagaraScriptCompileStatus::NCS_Unknown)
		{
			return ENiagaraScriptCompileStatus::NCS_Unknown;
		}
		else if (StatusA >= ENiagaraScriptCompileStatus::NCS_MAX || StatusB >= ENiagaraScriptCompileStatus::NCS_MAX)
		{
			return ENiagaraScriptCompileStatus::NCS_MAX;
		}
		else if (StatusA == ENiagaraScriptCompileStatus::NCS_Dirty || StatusB == ENiagaraScriptCompileStatus::NCS_Dirty)
		{
			return ENiagaraScriptCompileStatus::NCS_Dirty;
		}
		else if (StatusA == ENiagaraScriptCompileStatus::NCS_Error || StatusB == ENiagaraScriptCompileStatus::NCS_Error)
		{
			return ENiagaraScriptCompileStatus::NCS_Error;
		}
		else if (StatusA == ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings || StatusB == ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings)
		{
			return ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings;
		}
		else if (StatusA == ENiagaraScriptCompileStatus::NCS_BeingCreated || StatusB == ENiagaraScriptCompileStatus::NCS_BeingCreated)
		{
			return ENiagaraScriptCompileStatus::NCS_BeingCreated;
		}
		else if (StatusA == ENiagaraScriptCompileStatus::NCS_UpToDate || StatusB == ENiagaraScriptCompileStatus::NCS_UpToDate)
		{
			return ENiagaraScriptCompileStatus::NCS_UpToDate;
		}
		else
		{
			return ENiagaraScriptCompileStatus::NCS_Unknown;
		}
	}
	else
	{
		return StatusA;
	}
}

bool FNiagaraEditorUtilities::DataMatches(const FNiagaraVariable& Variable, const FStructOnScope& StructOnScope)
{
	if (Variable.GetType().GetScriptStruct() != StructOnScope.GetStruct() ||
		Variable.IsDataAllocated() == false)
	{
		return false;
	}

	return FMemory::Memcmp(Variable.GetData(), StructOnScope.GetStructMemory(), Variable.GetSizeInBytes()) == 0;
}

bool FNiagaraEditorUtilities::DataMatches(const FNiagaraVariable& VariableA, const FNiagaraVariable& VariableB)
{
	if (VariableA.GetType() != VariableB.GetType())
	{
		return false;
	}

	if (VariableA.IsDataAllocated() != VariableB.IsDataAllocated())
	{
		return false;
	}

	if (VariableA.IsDataAllocated())
	{
		return FMemory::Memcmp(VariableA.GetData(), VariableB.GetData(), VariableA.GetSizeInBytes()) == 0;
	}

	return true;
}

bool FNiagaraEditorUtilities::DataMatches(const FStructOnScope& StructOnScopeA, const FStructOnScope& StructOnScopeB)
{
	if (StructOnScopeA.GetStruct() != StructOnScopeB.GetStruct())
	{
		return false;
	}

	return FMemory::Memcmp(StructOnScopeA.GetStructMemory(), StructOnScopeB.GetStructMemory(), StructOnScopeA.GetStruct()->GetStructureSize()) == 0;
}

void FNiagaraEditorUtilities::CopyDataTo(FStructOnScope& DestinationStructOnScope, const FStructOnScope& SourceStructOnScope, bool bCheckTypes)
{
	checkf(DestinationStructOnScope.GetStruct()->GetStructureSize() == SourceStructOnScope.GetStruct()->GetStructureSize() &&
		(bCheckTypes == false || DestinationStructOnScope.GetStruct() == SourceStructOnScope.GetStruct()),
		TEXT("Can not copy data from one struct to another if their size is different or if the type is different and type checking is enabled."));
	FMemory::Memcpy(DestinationStructOnScope.GetStructMemory(), SourceStructOnScope.GetStructMemory(), SourceStructOnScope.GetStruct()->GetStructureSize());
}

TSharedPtr<SWidget> FNiagaraEditorUtilities::CreateInlineErrorText(TAttribute<FText> ErrorMessage, TAttribute<FText> ErrorTooltip)
{
	TSharedPtr<SHorizontalBox> ErrorInternalBox = SNew(SHorizontalBox);
		ErrorInternalBox->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
				.Text(ErrorMessage)
			];

		return SNew(SHorizontalBox)
			.ToolTipText(ErrorTooltip)
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FEditorStyle::GetBrush("Icons.Error"))
				]
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					ErrorInternalBox.ToSharedRef()
				];
}

void FNiagaraEditorUtilities::CompileExistingEmitters(const TArray<UNiagaraEmitter*>& AffectedEmitters)
{
	TArray<TSharedPtr<FNiagaraSystemViewModel>> ExistingSystemViewModels;

	{
		FNiagaraSystemUpdateContext UpdateCtx;

		TSet<UNiagaraEmitter*> CompiledEmitters;
		for (UNiagaraEmitter* Emitter : AffectedEmitters)
		{
			// If we've already compiled this emitter, or it's invalid skip it.
			if (Emitter == nullptr || CompiledEmitters.Contains(Emitter) || Emitter->IsPendingKillOrUnreachable())
			{
				continue;
			}

			// We only need to compile emitters referenced directly as instances by systems since emitters can now only be used in the context 
			// of a system.
			for (TObjectIterator<UNiagaraSystem> SystemIterator; SystemIterator; ++SystemIterator)
			{
				if (SystemIterator->ReferencesInstanceEmitter(*Emitter))
				{
					SystemIterator->RequestCompile(false, &UpdateCtx);

					FNiagaraSystemViewModel::GetAllViewModelsForObject(*SystemIterator, ExistingSystemViewModels);

					for (const FNiagaraEmitterHandle& EmitterHandle : SystemIterator->GetEmitterHandles())
					{
						CompiledEmitters.Add(EmitterHandle.GetInstance());
					}
				}
			}
		}
	}

	for (TSharedPtr<FNiagaraSystemViewModel> SystemViewModel : ExistingSystemViewModels)
	{
		SystemViewModel->RefreshAll();
	}
}

bool FNiagaraEditorUtilities::TryGetEventDisplayName(UNiagaraEmitter* Emitter, FGuid EventUsageId, FText& OutEventDisplayName)
{
	if (Emitter != nullptr)
	{
		for (const FNiagaraEventScriptProperties& EventScriptProperties : Emitter->GetEventHandlers())
		{
			if (EventScriptProperties.Script->GetUsageId() == EventUsageId)
			{
				OutEventDisplayName = FText::FromName(EventScriptProperties.SourceEventName);
				return true;
			}
		}
	}
	return false;
}

bool FNiagaraEditorUtilities::IsCompilableAssetClass(UClass* AssetClass)
{
	static const TSet<UClass*> CompilableClasses = { UNiagaraScript::StaticClass(), UNiagaraEmitter::StaticClass(), UNiagaraSystem::StaticClass() };
	return CompilableClasses.Contains(AssetClass);
}

void FNiagaraEditorUtilities::MarkDependentCompilableAssetsDirty(TArray<UObject*> InObjects)
{
	const FText LoadAndMarkDirtyDisplayName = NSLOCTEXT("NiagaraEditor", "MarkDependentAssetsDirtySlowTask", "Loading and marking dependent assets dirty.");
	GWarn->BeginSlowTask(LoadAndMarkDirtyDisplayName, true, true);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetIdentifier> ReferenceNames;

	TArray<FAssetData> AssetsToLoadAndMarkDirty;
	TArray<FAssetData> AssetsToCheck;

	for (UObject* InObject : InObjects)
	{	
		AssetsToCheck.Add(FAssetData(InObject));
	}

	while (AssetsToCheck.Num() > 0)
	{
		FAssetData AssetToCheck = AssetsToCheck[0];
		AssetsToCheck.RemoveAtSwap(0);
		if (IsCompilableAssetClass(AssetToCheck.GetClass()))
		{
			if (AssetsToLoadAndMarkDirty.Contains(AssetToCheck) == false)
			{
				AssetsToLoadAndMarkDirty.Add(AssetToCheck);
				TArray<FName> Referencers;
				AssetRegistryModule.Get().GetReferencers(AssetToCheck.PackageName, Referencers);
				for (FName& Referencer : Referencers)
				{
					AssetRegistryModule.Get().GetAssetsByPackageName(Referencer, AssetsToCheck);
				}
			}
		}
	}

	int32 ItemIndex = 0;
	for (FAssetData AssetDataToLoadAndMarkDirty : AssetsToLoadAndMarkDirty)
	{	
		if (GWarn->ReceivedUserCancel())
		{
			break;
		}
		GWarn->StatusUpdate(ItemIndex++, AssetsToLoadAndMarkDirty.Num(), LoadAndMarkDirtyDisplayName);
		UObject* AssetToMarkDirty = AssetDataToLoadAndMarkDirty.GetAsset();
		AssetToMarkDirty->Modify(true);
	}

	GWarn->EndSlowTask();
}

template<typename Action>
void TraverseGraphFromOutputDepthFirst(const UEdGraphSchema_Niagara* Schema, UNiagaraNode* Node, Action& VisitAction)
{
	UNiagaraGraph* Graph = Node->GetNiagaraGraph();
	TArray<UNiagaraNode*> Nodes;
	Graph->BuildTraversal(Nodes, Node);
	for (UNiagaraNode* GraphNode : Nodes)
	{
		VisitAction(Schema, GraphNode);
	}
}

void FixUpNumericPinsVisitor(const UEdGraphSchema_Niagara* Schema, UNiagaraNode* Node)
{
	Node->ResolveNumerics(Schema, true, nullptr);
}

void FNiagaraEditorUtilities::FixUpNumericPins(const UEdGraphSchema_Niagara* Schema, UNiagaraNode* Node)
{
	auto FixUpVisitor = [&](const UEdGraphSchema_Niagara* LSchema, UNiagaraNode* LNode) { FixUpNumericPinsVisitor(LSchema, LNode); };
	TraverseGraphFromOutputDepthFirst(Schema, Node, FixUpVisitor);
}

void FNiagaraEditorUtilities::SetStaticSwitchConstants(UNiagaraGraph* Graph, const TArray<UEdGraphPin*>& CallInputs, const FCompileConstantResolver& ConstantResolver)
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		// if there is a static switch node its value must be set by the caller
		UNiagaraNodeStaticSwitch* SwitchNode = Cast<UNiagaraNodeStaticSwitch>(Node);
		if (SwitchNode)
		{
			if (SwitchNode->IsSetByCompiler())
			{
				SwitchNode->SetSwitchValue(ConstantResolver);
			}
			else
			{
				FEdGraphPinType VarType = Schema->TypeDefinitionToPinType(SwitchNode->GetInputType());
				SwitchNode->ClearSwitchValue();
				for (UEdGraphPin* InputPin : CallInputs)
				{
					if (InputPin->GetFName().IsEqual(SwitchNode->InputParameterName) && InputPin->PinType == VarType)
					{
						int32 SwitchValue = 0;
						if (ResolveConstantValue(InputPin, SwitchValue))
						{
							SwitchNode->SetSwitchValue(SwitchValue);
							break;
						}
					}
				}
			}
		}

		// if there is a function node, it might have delegated some of the static switch values inside its script graph
		// to be set by the next higher caller instead of directly by the user
		UNiagaraNodeFunctionCall* FunctionNode = Cast<UNiagaraNodeFunctionCall>(Node);
		if (FunctionNode && FunctionNode->PropagatedStaticSwitchParameters.Num() > 0)
		{
			for (const FNiagaraPropagatedVariable& SwitchValue : FunctionNode->PropagatedStaticSwitchParameters)
			{
				UEdGraphPin* ValuePin = FunctionNode->FindPin(SwitchValue.SwitchParameter.GetName(), EGPD_Input);
				if (!ValuePin)
				{
					continue;
				}
				ValuePin->DefaultValue = FString();
				FName PinName = SwitchValue.ToVariable().GetName();
				for (UEdGraphPin* InputPin : CallInputs)
				{
					if (InputPin->GetFName().IsEqual(PinName) && InputPin->PinType == ValuePin->PinType)
					{
						ValuePin->DefaultValue = InputPin->DefaultValue;
						break;
					}
				}				
			}

		}
	}
}

bool FNiagaraEditorUtilities::ResolveConstantValue(UEdGraphPin* Pin, int32& Value)
{
	if (Pin->LinkedTo.Num() > 0)
	{
		return false;
	}
	
	const FEdGraphPinType& PinType = Pin->PinType;
	if (PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryType && PinType.PinSubCategoryObject.IsValid())
	{
		FString PinTypeName = PinType.PinSubCategoryObject->GetName();
		if (PinTypeName.Equals(FString(TEXT("NiagaraBool"))))
		{
			Value = Pin->DefaultValue.Equals(FString(TEXT("true"))) ? 1 : 0;
			return true;
		}
		else if (PinTypeName.Equals(FString(TEXT("NiagaraInt32"))))
		{
			Value = FCString::Atoi(*Pin->DefaultValue);
			return true;
		}
	}
	else if (PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryEnum && PinType.PinSubCategoryObject.IsValid())
	{
		UEnum* Enum = Cast<UEnum>(PinType.PinSubCategoryObject);
		FString FullName = Enum->GenerateFullEnumName(*Pin->DefaultValue);
		Value = Enum->GetIndexByName(FName(*FullName));
		return Value != INDEX_NONE;
	}
	return false;
}

TSharedPtr<FStructOnScope> FNiagaraEditorUtilities::StaticSwitchDefaultIntToStructOnScope(int32 InStaticSwitchDefaultValue, FNiagaraTypeDefinition InSwitchType)
{
	if (InSwitchType == FNiagaraTypeDefinition::GetBoolDef())
	{
		checkf(FNiagaraBool::StaticStruct()->GetStructureSize() == InSwitchType.GetSize(), TEXT("Value to type def size mismatch."));

		FNiagaraBool BoolValue;
		BoolValue.SetValue(InStaticSwitchDefaultValue != 0);

		TSharedPtr<FStructOnScope> StructValue = MakeShared<FStructOnScope>(InSwitchType.GetStruct());
		FMemory::Memcpy(StructValue->GetStructMemory(), (uint8*)(&BoolValue), InSwitchType.GetSize());

		return StructValue;
	}
	else if (InSwitchType == FNiagaraTypeDefinition::GetIntDef() || InSwitchType.IsEnum())
	{
		checkf(FNiagaraInt32::StaticStruct()->GetStructureSize() == InSwitchType.GetSize(), TEXT("Value to type def size mismatch."));

		FNiagaraInt32 IntValue;
		IntValue.Value = InStaticSwitchDefaultValue;

		TSharedPtr<FStructOnScope> StructValue = MakeShared<FStructOnScope>(InSwitchType.GetStruct());
		FMemory::Memcpy(StructValue->GetStructMemory(), (uint8*)(&IntValue), InSwitchType.GetSize());

		return StructValue;
	}

	return TSharedPtr<FStructOnScope>();
}

/* Go through the graph and attempt to auto-detect the type of any numeric pins by working back from the leaves of the graph. Only change the types of pins, not FNiagaraVariables.*/
void PreprocessGraph(const UEdGraphSchema_Niagara* Schema, UNiagaraGraph* Graph, UNiagaraNodeOutput* OutputNode)
{
	check(OutputNode);
	{
		FNiagaraEditorUtilities::FixUpNumericPins(Schema, OutputNode);
	}
}

/* Go through the graph and force any input nodes with Numeric types to a hard-coded type of float. This will allow modules and functions to compile properly.*/
void PreProcessGraphForInputNumerics(const UEdGraphSchema_Niagara* Schema, UNiagaraGraph* Graph, TArray<FNiagaraVariable>& OutChangedNumericParams)
{
	// Visit all input nodes
	TArray<UNiagaraNodeInput*> InputNodes;
	Graph->FindInputNodes(InputNodes);
	for (UNiagaraNodeInput* InputNode : InputNodes)
	{
		// See if any of the output pins are of Numeric type. If so, force to floats.
		TArray<UEdGraphPin*> OutputPins;
		InputNode->GetOutputPins(OutputPins);
		for (UEdGraphPin* OutputPin : OutputPins)
		{
			FNiagaraTypeDefinition OutputPinType = Schema->PinToTypeDefinition(OutputPin);
			if (OutputPinType == FNiagaraTypeDefinition::GetGenericNumericDef())
			{
				OutputPin->PinType = Schema->TypeDefinitionToPinType(FNiagaraTypeDefinition::GetFloatDef());
			}
		}

		// Record that we touched this variable for later cleanup and make sure that the 
		// variable's type now matches the pin.
		if (InputNode->Input.GetType() == FNiagaraTypeDefinition::GetGenericNumericDef())
		{
			OutChangedNumericParams.Add(InputNode->Input);
			InputNode->Input.SetType(FNiagaraTypeDefinition::GetFloatDef());
		}
	}
}

/* Should be called after all pins have been successfully auto-detected for type. This goes through and synchronizes any Numeric FNiagaraVarible outputs with the deduced pin type. This will allow modules and functions to compile properly.*/
void PreProcessGraphForAttributeNumerics(const UEdGraphSchema_Niagara* Schema, UNiagaraGraph* Graph, UNiagaraNodeOutput* OutputNode, TArray<FNiagaraVariable>& OutChangedNumericParams)
{
	// Visit the output node
	if (OutputNode != nullptr)
	{
		// For each pin, make sure that if it has a valid type, but the associated variable is still Numeric,
		// force the variable to match the pin's new type. Record that we touched this variable for later cleanup.
		TArray<UEdGraphPin*> InputPins;
		OutputNode->GetInputPins(InputPins);
		check(OutputNode->Outputs.Num() == InputPins.Num());
		for (int32 i = 0; i < InputPins.Num(); i++)
		{
			FNiagaraVariable& Param = OutputNode->Outputs[i];
			UEdGraphPin* InputPin = InputPins[i];

			FNiagaraTypeDefinition InputPinType = Schema->PinToTypeDefinition(InputPin);
			if (Param.GetType() == FNiagaraTypeDefinition::GetGenericNumericDef() &&
				InputPinType != FNiagaraTypeDefinition::GetGenericNumericDef())
			{
				OutChangedNumericParams.Add(Param);
				Param.SetType(InputPinType);
			}
		}
	}
}

void FNiagaraEditorUtilities::ResolveNumerics(UNiagaraGraph* SourceGraph, bool bForceParametersToResolveNumerics, TArray<FNiagaraVariable>& ChangedNumericParams)
{
	const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(SourceGraph->GetSchema());

	// In the case of functions or modules, we may not have enough information at this time to fully resolve the type. In that case,
	// we circumvent the resulting errors by forcing a type. This gives the user an appropriate level of type checking. We will, however need to clean this up in
	// the parameters that we output.
	//bool bForceParametersToResolveNumerics = InScript->IsStandaloneScript();
	if (bForceParametersToResolveNumerics)
	{
		PreProcessGraphForInputNumerics(Schema, SourceGraph, ChangedNumericParams);
	}

	// Auto-deduce the input types for numerics in the graph and overwrite the types on the pins. If PreProcessGraphForInputNumerics occurred, then
	// we will have pre-populated the inputs with valid types.
	TArray<UNiagaraNodeOutput*> OutputNodes;
	SourceGraph->FindOutputNodes(OutputNodes);

	for (UNiagaraNodeOutput* OutputNode : OutputNodes)
	{
		PreprocessGraph(Schema, SourceGraph, OutputNode);

		// Now that we've auto-deduced the types, we need to handle any lingering Numerics in the Output's FNiagaraVariable outputs. 
		// We use the pin's deduced type to temporarily overwrite the variable's type.
		if (bForceParametersToResolveNumerics)
		{
			PreProcessGraphForAttributeNumerics(Schema, SourceGraph, OutputNode, ChangedNumericParams);
		}
	}
}

void FNiagaraEditorUtilities::PreprocessFunctionGraph(const UEdGraphSchema_Niagara* Schema, UNiagaraGraph* Graph, const TArray<UEdGraphPin*>& CallInputs, const TArray<UEdGraphPin*>& CallOutputs, ENiagaraScriptUsage ScriptUsage, const FCompileConstantResolver& ConstantResolver)
{
	// Change any numeric inputs or outputs to match the types from the call node.
	TArray<UNiagaraNodeInput*> InputNodes;

	// Only handle nodes connected to the correct output node in the event of multiple output nodes in the graph.
	UNiagaraGraph::FFindInputNodeOptions Options;
	Options.bFilterByScriptUsage = true;
	Options.TargetScriptUsage = ScriptUsage;

	Graph->FindInputNodes(InputNodes, Options);

	for (UNiagaraNodeInput* InputNode : InputNodes)
	{
		FNiagaraVariable& Input = InputNode->Input;
		if (Input.GetType() == FNiagaraTypeDefinition::GetGenericNumericDef())
		{
			UEdGraphPin* const* MatchingPin = CallInputs.FindByPredicate([&](const UEdGraphPin* Pin) { return (Pin->PinName == Input.GetName()); });

			if (MatchingPin != nullptr)
			{
				FNiagaraTypeDefinition PinType = Schema->PinToTypeDefinition(*MatchingPin);
				Input.SetType(PinType);
				TArray<UEdGraphPin*> OutputPins;
				InputNode->GetOutputPins(OutputPins);
				check(OutputPins.Num() == 1);
				OutputPins[0]->PinType = (*MatchingPin)->PinType;
			}
		}
	}

	UNiagaraNodeOutput* OutputNode = Graph->FindOutputNode(ScriptUsage);
	check(OutputNode);

	TArray<UEdGraphPin*> InputPins;
	OutputNode->GetInputPins(InputPins);

	for (FNiagaraVariable& Output : OutputNode->Outputs)
	{
		if (Output.GetType() == FNiagaraTypeDefinition::GetGenericNumericDef())
		{
			UEdGraphPin* const* MatchingPin = CallOutputs.FindByPredicate([&](const UEdGraphPin* Pin) { return (Pin->PinName == Output.GetName()); });

			if (MatchingPin != nullptr)
			{
				FNiagaraTypeDefinition PinType = Schema->PinToTypeDefinition(*MatchingPin);
				Output.SetType(PinType);
			}
		}
	}
	
	FNiagaraEditorUtilities::FixUpNumericPins(Schema, OutputNode);
	FNiagaraEditorUtilities::SetStaticSwitchConstants(Graph, CallInputs, ConstantResolver);
}

void FNiagaraEditorUtilities::GetFilteredScriptAssets(FGetFilteredScriptAssetsOptions InFilter, TArray<FAssetData>& OutFilteredScriptAssets)
{
	FARFilter ScriptFilter;
	ScriptFilter.ClassNames.Add(UNiagaraScript::StaticClass()->GetFName());

	const UEnum* NiagaraScriptUsageEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("ENiagaraScriptUsage"), true);
	const FString QualifiedScriptUsageString = NiagaraScriptUsageEnum->GetNameStringByValue(static_cast<uint8>(InFilter.ScriptUsageToInclude));
	int32 LastColonIndex;
	QualifiedScriptUsageString.FindLastChar(TEXT(':'), LastColonIndex);
	const FString UnqualifiedScriptUsageString = QualifiedScriptUsageString.RightChop(LastColonIndex + 1);
	ScriptFilter.TagsAndValues.Add(GET_MEMBER_NAME_CHECKED(UNiagaraScript, Usage), UnqualifiedScriptUsageString);

	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> FilteredScriptAssets;
	AssetRegistryModule.Get().GetAssets(ScriptFilter, FilteredScriptAssets);

	for (int i = 0; i < FilteredScriptAssets.Num(); ++i)
	{
		// Get the custom version the asset was saved with so it can be used below.
		int32 NiagaraVersion = INDEX_NONE;
		FilteredScriptAssets[i].GetTagValue(UNiagaraScript::NiagaraCustomVersionTagName, NiagaraVersion);

		// Check if the script is deprecated
		if (InFilter.bIncludeDeprecatedScripts == false)
		{
			bool bScriptIsDeprecated = false;
			bool bFoundDeprecatedTag = FilteredScriptAssets[i].GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraScript, bDeprecated), bScriptIsDeprecated);
			if (bFoundDeprecatedTag == false)
			{
				if (FilteredScriptAssets[i].IsAssetLoaded())
				{
					UNiagaraScript* Script = static_cast<UNiagaraScript*>(FilteredScriptAssets[i].GetAsset());
					if (Script != nullptr)
					{
						bScriptIsDeprecated = Script->bDeprecated;
					}
				}
			}
			if (bScriptIsDeprecated)
			{
				continue;
			}
		}

		// Check if usage bitmask matches
		if (InFilter.TargetUsageToMatch.IsSet())
		{
			int32 BitfieldValue = 0;
			if (NiagaraVersion == INDEX_NONE || NiagaraVersion < FNiagaraCustomVersion::AddSimulationStageUsageEnum)
			{
				// If there is no custom version, or it's less than the simulation stage enum fix up, we need to load the 
				// asset to get the correct bitmask since the shader stage enum broke the old ones.
				UNiagaraScript* AssetScript = Cast<UNiagaraScript>(FilteredScriptAssets[i].GetAsset());
				if (AssetScript != nullptr)
				{
					BitfieldValue = AssetScript->ModuleUsageBitmask;
				}
			}
			else
			{
				// Otherwise the asset is new enough to have a valid bitmask.
				FString BitfieldTagValue;
				BitfieldTagValue = FilteredScriptAssets[i].GetTagValueRef<FString>(GET_MEMBER_NAME_CHECKED(UNiagaraScript, ModuleUsageBitmask));
				BitfieldValue = FCString::Atoi(*BitfieldTagValue);
			}

			int32 TargetBit = (BitfieldValue >> (int32)InFilter.TargetUsageToMatch.GetValue()) & 1;
			if (TargetBit != 1)
			{
				continue;
			}
		}

		// Check if library script
		if (InFilter.bIncludeNonLibraryScripts == false)
		{
			bool bScriptIsLibrary = true;
			bool bFoundLibScriptTag = FilteredScriptAssets[i].GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraScript, bExposeToLibrary), bScriptIsLibrary);

			if (bFoundLibScriptTag == false)
			{
				if (FilteredScriptAssets[i].IsAssetLoaded())
				{
					UNiagaraScript* Script = static_cast<UNiagaraScript*>(FilteredScriptAssets[i].GetAsset());
					if (Script != nullptr)
					{
						bScriptIsLibrary = Script->bExposeToLibrary;
					}
				}
			}
			if (bScriptIsLibrary == false)
			{
				continue;
			}
		}

		OutFilteredScriptAssets.Add(FilteredScriptAssets[i]);
	}
}

UNiagaraNodeOutput* FNiagaraEditorUtilities::GetScriptOutputNode(UNiagaraScript& Script)
{
	UNiagaraScriptSource* Source = CastChecked<UNiagaraScriptSource>(Script.GetSource());
	return Source->NodeGraph->FindEquivalentOutputNode(Script.GetUsage(), Script.GetUsageId());
}

UNiagaraScript* FNiagaraEditorUtilities::GetScriptFromSystem(UNiagaraSystem& System, FGuid EmitterHandleId, ENiagaraScriptUsage Usage, FGuid UsageId)
{
	if (UNiagaraScript::IsEquivalentUsage(Usage, ENiagaraScriptUsage::SystemSpawnScript))
	{
		return System.GetSystemSpawnScript();
	}
	else if (UNiagaraScript::IsEquivalentUsage(Usage, ENiagaraScriptUsage::SystemUpdateScript))
	{
		return System.GetSystemUpdateScript();
	}
	else if (EmitterHandleId.IsValid())
	{
		const FNiagaraEmitterHandle* ScriptEmitterHandle = System.GetEmitterHandles().FindByPredicate(
			[EmitterHandleId](const FNiagaraEmitterHandle& EmitterHandle) { return EmitterHandle.GetId() == EmitterHandleId; });
		if (ScriptEmitterHandle != nullptr)
		{
			if (UNiagaraScript::IsEquivalentUsage(Usage, ENiagaraScriptUsage::EmitterSpawnScript))
			{
				return ScriptEmitterHandle->GetInstance()->EmitterSpawnScriptProps.Script;
			}
			else if (UNiagaraScript::IsEquivalentUsage(Usage, ENiagaraScriptUsage::EmitterUpdateScript))
			{
				return ScriptEmitterHandle->GetInstance()->EmitterUpdateScriptProps.Script;
			}
			else if (UNiagaraScript::IsEquivalentUsage(Usage, ENiagaraScriptUsage::ParticleSpawnScript))
			{
				return ScriptEmitterHandle->GetInstance()->SpawnScriptProps.Script;
			}
			else if (UNiagaraScript::IsEquivalentUsage(Usage, ENiagaraScriptUsage::ParticleUpdateScript))
			{
				return ScriptEmitterHandle->GetInstance()->UpdateScriptProps.Script;
			}
			else if (UNiagaraScript::IsEquivalentUsage(Usage, ENiagaraScriptUsage::ParticleEventScript))
			{
				for (const FNiagaraEventScriptProperties& EventScriptProperties : ScriptEmitterHandle->GetInstance()->GetEventHandlers())
				{
					if (EventScriptProperties.Script->GetUsageId() == UsageId)
					{
						return EventScriptProperties.Script;
					}
				}
			}
			else if (UNiagaraScript::IsEquivalentUsage(Usage, ENiagaraScriptUsage::ParticleSimulationStageScript))
			{
				for (const UNiagaraSimulationStageBase* SimulationStage : ScriptEmitterHandle->GetInstance()->GetSimulationStages())
				{
					if (SimulationStage && SimulationStage->Script && SimulationStage->Script->GetUsageId() == UsageId)
					{
						return SimulationStage->Script;
					}
				}
			}
		}
	}
	return nullptr;
}

const FNiagaraEmitterHandle* FNiagaraEditorUtilities::GetEmitterHandleForEmitter(UNiagaraSystem& System, UNiagaraEmitter& Emitter)
{
	return System.GetEmitterHandles().FindByPredicate(
		[&Emitter](const FNiagaraEmitterHandle& EmitterHandle) { return EmitterHandle.GetInstance() == &Emitter; });
}

bool FNiagaraEditorUtilities::IsScriptAssetInLibrary(const FAssetData& ScriptAssetData)
{
	bool bIsInLibrary;
	bool bIsLibraryTagFound = ScriptAssetData.GetTagValue(GET_MEMBER_NAME_CHECKED(UNiagaraScript, bExposeToLibrary), bIsInLibrary);
	if (bIsLibraryTagFound == false)
	{
		if (ScriptAssetData.IsAssetLoaded())
		{
			UNiagaraScript* Script = static_cast<UNiagaraScript*>(ScriptAssetData.GetAsset());
			if (Script != nullptr)
			{
				bIsInLibrary = Script->bExposeToLibrary;
			}
		}
		else
		{
			bIsInLibrary = false;
		}
	}
	return bIsInLibrary;
}

NIAGARAEDITOR_API FText FNiagaraEditorUtilities::FormatScriptName(FName Name, bool bIsInLibrary)
{
	return FText::FromString(FName::NameToDisplayString(Name.ToString(), false) + (bIsInLibrary ? TEXT("") : TEXT("*")));
}

FText FNiagaraEditorUtilities::FormatScriptDescription(FText Description, FName Path, bool bIsInLibrary)
{
	FText LibrarySuffix = !bIsInLibrary
		? LOCTEXT("LibrarySuffix", "\n* Script is not exposed to the library.")
		: FText();

	return Description.IsEmptyOrWhitespace()
		? FText::Format(LOCTEXT("ScriptAssetDescriptionFormatPathOnly", "Path: {0}{1}"), FText::FromName(Path), LibrarySuffix)
		: FText::Format(LOCTEXT("ScriptAssetDescriptionFormat", "{1}\nPath: {0}{2}"), FText::FromName(Path), Description, LibrarySuffix);
}

FText FNiagaraEditorUtilities::FormatVariableDescription(FText Description, FText Name, FText Type)
{
	if (Description.IsEmptyOrWhitespace() == false)
	{
		return FText::Format(LOCTEXT("VariableDescriptionFormat", "{0}\nName: \"{1}\"\nType: {2}"), Description, Name, Type);
	}

	return FText::Format(LOCTEXT("VariableDescriptionFormat_NoDesc", "Name: \"{0}\"\nType: {1}"), Name, Type);
}

void FNiagaraEditorUtilities::ResetSystemsThatReferenceSystemViewModel(const FNiagaraSystemViewModel& ReferencedSystemViewModel)
{
	checkf(&ReferencedSystemViewModel, TEXT("ResetSystemsThatReferenceSystemViewModel() called on destroyed SystemViewModel."));
	TArray<TSharedPtr<FNiagaraSystemViewModel>> ComponentSystemViewModels;
	TArray<UNiagaraComponent*> ReferencingComponents = GetComponentsThatReferenceSystemViewModel(ReferencedSystemViewModel);
	for (auto Component : ReferencingComponents)
	{
		ComponentSystemViewModels.Reset();
		FNiagaraSystemViewModel::GetAllViewModelsForObject(Component->GetAsset(), ComponentSystemViewModels);
		if (ComponentSystemViewModels.Num() > 0)
		{
			//The component has a viewmodel, call ResetSystem() on the viewmodel 
			for (auto SystemViewModel : ComponentSystemViewModels)
			{
				if (SystemViewModel.IsValid() && SystemViewModel.Get() != &ReferencedSystemViewModel)
				{
					SystemViewModel->ResetSystem(FNiagaraSystemViewModel::ETimeResetMode::AllowResetTime, FNiagaraSystemViewModel::EMultiResetMode::ResetThisInstance, FNiagaraSystemViewModel::EReinitMode::ResetSystem);
				}
			}
		}
		else
		{
			//The component does not have a viewmodel, call ResetSystem() on the component
			Component->ResetSystem();
		}
	}
}

TArray<UNiagaraComponent*> FNiagaraEditorUtilities::GetComponentsThatReferenceSystem(const UNiagaraSystem& ReferencedSystem)
{
	check(&ReferencedSystem);
	TArray<UNiagaraComponent*> ReferencingComponents;
	for (TObjectIterator<UNiagaraComponent> ComponentIt; ComponentIt; ++ComponentIt)
	{
		UNiagaraComponent* Component = *ComponentIt;
		if (Component && Component->GetAsset())
		{
			if (Component->GetAsset() == &ReferencedSystem)
			{
				ReferencingComponents.Add(Component);
			}
		}
	}
	return ReferencingComponents;
}

TArray<UNiagaraComponent*> FNiagaraEditorUtilities::GetComponentsThatReferenceSystemViewModel(const FNiagaraSystemViewModel& ReferencedSystemViewModel)
{
	check(&ReferencedSystemViewModel);
	TArray<UNiagaraComponent*> ReferencingComponents;
	for (TObjectIterator<UNiagaraComponent> ComponentIt; ComponentIt; ++ComponentIt)
	{
		UNiagaraComponent* Component = *ComponentIt;
		if (Component && Component->GetAsset())
		{
			for (auto EmitterHandle : ReferencedSystemViewModel.GetSystem().GetEmitterHandles())
			{
				if (Component->GetAsset()->UsesEmitter(EmitterHandle.GetInstance()->GetParent()))
				{
					ReferencingComponents.Add(Component);
				}
			}
		}
	}
	return ReferencingComponents;
}

const FGuid FNiagaraEditorUtilities::AddEmitterToSystem(UNiagaraSystem& InSystem, UNiagaraEmitter& InEmitterToAdd)
{
	// Kill all system instances before modifying the emitter handle list to prevent accessing deleted data.
	KillSystemInstances(InSystem);

	TSet<FName> EmitterHandleNames;
	for (const FNiagaraEmitterHandle& EmitterHandle : InSystem.GetEmitterHandles())
	{
		EmitterHandleNames.Add(EmitterHandle.GetName());
	}

	UNiagaraSystemEditorData* SystemEditorData = CastChecked<UNiagaraSystemEditorData>(InSystem.GetEditorData(), ECastCheckedType::NullChecked);
	FNiagaraEmitterHandle EmitterHandle;
	if (SystemEditorData->GetOwningSystemIsPlaceholder() == false)
	{
		InSystem.Modify();
		EmitterHandle = InSystem.AddEmitterHandle(InEmitterToAdd, FNiagaraUtilities::GetUniqueName(InEmitterToAdd.GetFName(), EmitterHandleNames));
	}
	else
	{
		// When editing an emitter asset we add the emitter as a duplicate so that the parent emitter is duplicated, but it's parent emitter
		// information is maintained.
		checkf(InSystem.GetNumEmitters() == 0, TEXT("Can not add multiple emitters to a system being edited in emitter asset mode."));
		FNiagaraEmitterHandle TemporaryEmitterHandle(InEmitterToAdd);
		EmitterHandle = InSystem.DuplicateEmitterHandle(TemporaryEmitterHandle, *InEmitterToAdd.GetUniqueEmitterName());
	}
	
	FNiagaraStackGraphUtilities::RebuildEmitterNodes(InSystem);
	SystemEditorData->SynchronizeOverviewGraphWithSystem(InSystem);

	return EmitterHandle.GetId();
}

void FNiagaraEditorUtilities::RemoveEmittersFromSystemByEmitterHandleId(UNiagaraSystem& InSystem, TSet<FGuid> EmitterHandleIdsToDelete)
{
	// Kill all system instances before modifying the emitter handle list to prevent accessing deleted data.
	KillSystemInstances(InSystem);

	const FScopedTransaction DeleteTransaction(EmitterHandleIdsToDelete.Num() == 1
		? LOCTEXT("DeleteEmitter", "Delete emitter")
		: LOCTEXT("DeleteEmitters", "Delete emitters"));

	InSystem.Modify();
	InSystem.RemoveEmitterHandlesById(EmitterHandleIdsToDelete);

	FNiagaraStackGraphUtilities::RebuildEmitterNodes(InSystem);
	UNiagaraSystemEditorData* SystemEditorData = CastChecked<UNiagaraSystemEditorData>(InSystem.GetEditorData(), ECastCheckedType::NullChecked);
	SystemEditorData->SynchronizeOverviewGraphWithSystem(InSystem);
}

void FNiagaraEditorUtilities::KillSystemInstances(const UNiagaraSystem& System)
{
	TArray<UNiagaraComponent*> ReferencingComponents = FNiagaraEditorUtilities::GetComponentsThatReferenceSystem(System);
	for (auto Component : ReferencingComponents)
	{
		Component->DestroyInstance();
	}
}

bool FNiagaraEditorUtilities::VerifyNameChangeForInputOrOutputNode(const UNiagaraNode& NodeBeingChanged, FName OldName, FName NewName, FText& OutErrorMessage)
{
	if (NewName == NAME_None)
	{
		OutErrorMessage = LOCTEXT("EmptyNameError", "Name can not be empty.");
		return false;
	}

	if (GetSystemConstantNames().Contains(NewName))
	{
		OutErrorMessage = LOCTEXT("SystemConstantNameError", "Name can not be the same as a system constant");
	}

	if (NodeBeingChanged.IsA<UNiagaraNodeInput>())
	{
		TArray<UNiagaraNodeInput*> InputNodes;
		NodeBeingChanged.GetGraph()->GetNodesOfClass<UNiagaraNodeInput>(InputNodes);
		for (UNiagaraNodeInput* InputNode : InputNodes)
		{
			if (InputNode->Input.GetName() != OldName && InputNode->Input.GetName() == NewName)
			{
				OutErrorMessage = LOCTEXT("DuplicateInputNameError", "Name can not match an existing input name.");
				return false;
			}
		}
	}

	if (NodeBeingChanged.IsA<UNiagaraNodeOutput>())
	{
		const UNiagaraNodeOutput* OutputNodeBeingChanged = CastChecked<const UNiagaraNodeOutput>(&NodeBeingChanged);
		for (const FNiagaraVariable& Output : OutputNodeBeingChanged->GetOutputs())
		{
			if (Output.GetName() != OldName && Output.GetName() == NewName)
			{
				OutErrorMessage = LOCTEXT("DuplicateOutputNameError", "Name can not match an existing output name.");
				return false;
			}
		}
	}

	return true;
}

bool FNiagaraEditorUtilities::AddParameter(FNiagaraVariable& NewParameterVariable, FNiagaraParameterStore& TargetParameterStore, UObject& ParameterStoreOwner, UNiagaraStackEditorData* StackEditorData)
{
	FScopedTransaction AddTransaction(LOCTEXT("AddParameter", "Add Parameter"));
	ParameterStoreOwner.Modify();

	TSet<FName> ExistingParameterStoreNames;
	TArray<FNiagaraVariable> ParameterStoreVariables;
	TargetParameterStore.GetParameters(ParameterStoreVariables);
	for (const FNiagaraVariable& Var : ParameterStoreVariables)
	{
		ExistingParameterStoreNames.Add(Var.GetName());
	}

	FNiagaraEditorUtilities::ResetVariableToDefaultValue(NewParameterVariable);
	NewParameterVariable.SetName(FNiagaraUtilities::GetUniqueName(NewParameterVariable.GetName(), ExistingParameterStoreNames));

	bool bSuccess = TargetParameterStore.AddParameter(NewParameterVariable);
	if (bSuccess && StackEditorData != nullptr)
	{
		StackEditorData->SetStackEntryIsRenamePending(NewParameterVariable.GetName().ToString(), true);
	}
	return bSuccess;
}

void FNiagaraEditorUtilities::ShowParentEmitterInContentBrowser(TSharedRef<FNiagaraEmitterViewModel> Emitter)
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	ContentBrowserModule.Get().SyncBrowserToAssets(TArray<FAssetData> { FAssetData(Emitter->GetParentEmitter()) });
}

void FNiagaraEditorUtilities::OpenParentEmitterForEdit(TSharedRef<FNiagaraEmitterViewModel> Emitter)
{
	UNiagaraEmitter* ParentEmitter = const_cast<UNiagaraEmitter*>(Emitter->GetParentEmitter());
	if (ParentEmitter != nullptr)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ParentEmitter);
	}
}

ECheckBoxState FNiagaraEditorUtilities::GetSelectedEmittersEnabledCheckState(TSharedRef<FNiagaraSystemViewModel> SystemViewModel)
{
	bool bFirst = true;
	ECheckBoxState CurrentState = ECheckBoxState::Undetermined;

	const TArray<FGuid>& SelectedHandleIds = SystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds();
	for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterHandle : SystemViewModel->GetEmitterHandleViewModels())
	{
		if (SelectedHandleIds.Contains(EmitterHandle->GetId()))
		{
			ECheckBoxState EmitterState = EmitterHandle->GetIsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			if (bFirst)
			{
				CurrentState = EmitterState;
				bFirst = false;
				continue;
			}

			if (CurrentState != EmitterState)
			{
				return ECheckBoxState::Undetermined;
			}
		}
	}

	return CurrentState;
}

void FNiagaraEditorUtilities::ToggleSelectedEmittersEnabled(TSharedRef<FNiagaraSystemViewModel> SystemViewModel)
{
	bool bEnabled = GetSelectedEmittersEnabledCheckState(SystemViewModel) != ECheckBoxState::Checked;

	const TArray<FGuid>& SelectedHandleIds = SystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds();
	for (const FGuid& HandleId : SelectedHandleIds)
	{
		TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel = SystemViewModel->GetEmitterHandleViewModelById(HandleId);
		if (EmitterHandleViewModel.IsValid())
		{
			EmitterHandleViewModel->SetIsEnabled(bEnabled);
		}
	}
}

ECheckBoxState FNiagaraEditorUtilities::GetSelectedEmittersIsolatedCheckState(TSharedRef<FNiagaraSystemViewModel> SystemViewModel)
{
	bool bFirst = true;
	ECheckBoxState CurrentState = ECheckBoxState::Undetermined;

	const TArray<FGuid>& SelectedHandleIds = SystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds();
	for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterHandle : SystemViewModel->GetEmitterHandleViewModels())
	{
		if (SelectedHandleIds.Contains(EmitterHandle->GetId()))
		{
			ECheckBoxState EmitterState = EmitterHandle->GetIsIsolated() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			if (bFirst)
			{
				CurrentState = EmitterState;
				bFirst = false;
				continue;
			}

			if (CurrentState != EmitterState)
			{
				return ECheckBoxState::Undetermined;
			}
		}
	}

	return CurrentState;
}

void FNiagaraEditorUtilities::ToggleSelectedEmittersIsolated(TSharedRef<FNiagaraSystemViewModel> SystemViewModel)
{
	bool bIsolated = GetSelectedEmittersIsolatedCheckState(SystemViewModel) != ECheckBoxState::Checked;

	TArray<FGuid> EmittersToIsolate;
	for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterHandle : SystemViewModel->GetEmitterHandleViewModels())
	{
		if (EmitterHandle->GetIsIsolated())
		{
			EmittersToIsolate.Add(EmitterHandle->GetId());
		}
	}

	const TArray<FGuid>& SelectedHandleIds = SystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds();
	for (const FGuid& HandleId : SelectedHandleIds)
	{
		if (bIsolated)
		{
			EmittersToIsolate.Add(HandleId);
		}
		else
		{
			EmittersToIsolate.Remove(HandleId);
		}
	}

	SystemViewModel->IsolateEmitters(EmittersToIsolate);
}


void FNiagaraEditorUtilities::CreateAssetFromEmitter(TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel)
{
	TSharedRef<FNiagaraSystemViewModel> SystemViewModel = EmitterHandleViewModel->GetOwningSystemViewModel();
	if (SystemViewModel->GetEditMode() != ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		return;
	}

	UNiagaraEmitter* EmitterToCopy = EmitterHandleViewModel->GetEmitterViewModel()->GetEmitter();
	const FString PackagePath = FPackageName::GetLongPackagePath(EmitterToCopy->GetOutermost()->GetName());
	const FName EmitterName = EmitterToCopy->GetFName();
	
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UNiagaraEmitter* CreatedAsset = Cast<UNiagaraEmitter>(AssetToolsModule.Get().DuplicateAssetWithDialogAndTitle(EmitterName.GetPlainNameString(), PackagePath, EmitterToCopy, LOCTEXT("CreateEmitterAssetDialogTitle", "Create Emitter As")));
	if (CreatedAsset != nullptr)
	{
		CreatedAsset->SetFlags(RF_Standalone | RF_Public);

		CreatedAsset->SetUniqueEmitterName(CreatedAsset->GetName());

		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(CreatedAsset);

		// find the existing overview node to store the position
		UEdGraph* OverviewGraph = SystemViewModel->GetOverviewGraphViewModel()->GetGraph();
		
		TArray<UNiagaraOverviewNode*> OverviewNodes;
		OverviewGraph->GetNodesOfClass<UNiagaraOverviewNode>(OverviewNodes);

		const UNiagaraOverviewNode* CurrentNode = *OverviewNodes.FindByPredicate(
			[Guid = EmitterHandleViewModel->GetId()](const UNiagaraOverviewNode* Node)
			{
				return Node->GetEmitterHandleGuid() == Guid;
			});

		int32 CurrentX = CurrentNode->NodePosX;
		int32 CurrentY = CurrentNode->NodePosY;
		FString CurrentComment = CurrentNode->NodeComment;
		bool bCommentBubbleVisible = CurrentNode->bCommentBubbleVisible;
		bool bCommentBubblePinned = CurrentNode->bCommentBubblePinned;

		CurrentNode = nullptr;

		FScopedTransaction ScopedTransaction(LOCTEXT("CreateAssetFromEmitter", "Create asset from emitter"));
		SystemViewModel->GetSystem().Modify();

		// Replace existing emitter
		SystemViewModel->DeleteEmitters(TSet<FGuid> { EmitterHandleViewModel->GetId() });
		TSharedPtr<FNiagaraEmitterHandleViewModel> NewEmitterHandleViewModel = SystemViewModel->AddEmitter(*CreatedAsset);

		NewEmitterHandleViewModel->SetName(EmitterName);

		OverviewGraph->GetNodesOfClass<UNiagaraOverviewNode>(OverviewNodes);

		UNiagaraOverviewNode* NewNode = *OverviewNodes.FindByPredicate(
			[Guid = NewEmitterHandleViewModel->GetId()](const UNiagaraOverviewNode* Node)
			{
				return Node->GetEmitterHandleGuid() == Guid;
			});

		NewNode->NodePosX = CurrentX;
		NewNode->NodePosY = CurrentY;
		NewNode->NodeComment = CurrentComment;
		NewNode->bCommentBubbleVisible = bCommentBubbleVisible;
		NewNode->bCommentBubblePinned = bCommentBubblePinned;
	}
}

void FNiagaraEditorUtilities::GetScriptRunAndExecutionIndexFromUsage(const ENiagaraScriptUsage& InUsage, int32& OutRunIndex, int32&OutExecutionIndex)
{
	switch (InUsage)
	{
	case ENiagaraScriptUsage::SystemSpawnScript:
		OutRunIndex = 0;
		OutExecutionIndex = 0;
		break;
	case ENiagaraScriptUsage::EmitterSpawnScript:
		OutRunIndex = 0;
		OutExecutionIndex = 1;
		break;
	case ENiagaraScriptUsage::ParticleSpawnScript:
	case ENiagaraScriptUsage::ParticleSpawnScriptInterpolated:
		OutRunIndex = 2;
		OutExecutionIndex = 2;
		break;
	case ENiagaraScriptUsage::SystemUpdateScript:
		OutRunIndex = 1;
		OutExecutionIndex = 0;
		break;
	case ENiagaraScriptUsage::EmitterUpdateScript:
		OutRunIndex = 1;
		OutExecutionIndex = 1;
		break;
	case ENiagaraScriptUsage::ParticleUpdateScript:
	case ENiagaraScriptUsage::ParticleGPUComputeScript:
		OutRunIndex = 2;
		OutExecutionIndex = 3;
		break;
	case ENiagaraScriptUsage::ParticleEventScript:
		OutRunIndex = 2;
		OutExecutionIndex = 4;
		break;
	case ENiagaraScriptUsage::ParticleSimulationStageScript:
		//@todo(ng) implement getter for shader stages; for now same as particle update
		
		OutRunIndex = 2;
		OutExecutionIndex = 4;
		break;
	case ENiagaraScriptUsage::DynamicInput:
	case ENiagaraScriptUsage::Function:
	case ENiagaraScriptUsage::Module:
		OutRunIndex = INDEX_NONE;
		OutExecutionIndex = INDEX_NONE;
		break;
	default:
		checkf(false, TEXT("No execution index implemented for usage!"));
		OutRunIndex = INDEX_NONE;
		OutExecutionIndex = INDEX_NONE;
		break;
	}
}

bool FNiagaraEditorUtilities::AddEmitterContextMenuActions(FMenuBuilder& MenuBuilder, const TSharedPtr<FNiagaraEmitterHandleViewModel>& EmitterHandleViewModelPtr)
{
	if (EmitterHandleViewModelPtr.IsValid())
	{
		TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel = EmitterHandleViewModelPtr.ToSharedRef();
		TSharedRef<FNiagaraSystemViewModel> OwningSystemViewModel = EmitterHandleViewModel->GetOwningSystemViewModel();

		bool bSingleSelection = OwningSystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds().Num() == 1;
		TSharedRef<FNiagaraEmitterViewModel> EmitterViewModel = EmitterHandleViewModel->GetEmitterViewModel();
		MenuBuilder.BeginSection("EmitterActions", LOCTEXT("EmitterActions", "Emitter Actions"));
		{
		
			if (OwningSystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("ToggleEmittersEnabled", "Enabled"),
					LOCTEXT("ToggleEmittersEnabledToolTip", "Toggle whether or not the selected emitters are enabled."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateStatic(&FNiagaraEditorUtilities::ToggleSelectedEmittersEnabled, OwningSystemViewModel),
						FCanExecuteAction(),
						FGetActionCheckState::CreateStatic(&FNiagaraEditorUtilities::GetSelectedEmittersEnabledCheckState, OwningSystemViewModel)
					),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
				);

				MenuBuilder.AddMenuEntry(
					LOCTEXT("ToggleEmittersIsolated", "Isolated"),
					LOCTEXT("ToggleEmittersIsolatedToolTip", "Toggle whether or not the selected emitters are isolated."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateStatic(&FNiagaraEditorUtilities::ToggleSelectedEmittersIsolated, OwningSystemViewModel),
						FCanExecuteAction(),
						FGetActionCheckState::CreateStatic(&FNiagaraEditorUtilities::GetSelectedEmittersIsolatedCheckState, OwningSystemViewModel)
					),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
				);
			}

			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreateAssetFromThisEmitter", "Create Asset From This"),
				LOCTEXT("CreateAssetFromThisEmitterToolTip", "Create an emitter asset from this emitter."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateStatic(&FNiagaraEditorUtilities::CreateAssetFromEmitter, EmitterHandleViewModel),
					FCanExecuteAction::CreateLambda(
						[bSingleSelection, EmitterHandleViewModel]()
						{
							return bSingleSelection && EmitterHandleViewModel->GetOwningSystemEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset;
						}
					)
				)
			);
		}

		MenuBuilder.EndSection();
		MenuBuilder.BeginSection("EmitterActions", LOCTEXT("ParentActions", "Parent Actions"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("UpdateParentEmitter", "Update Parent Emitter"),
				LOCTEXT("UpdateParentEmitterToolTip", "Change or add a parent emitter."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSP(EmitterViewModel, &FNiagaraEmitterViewModel::CreateNewParentWindow, EmitterHandleViewModel),
					FCanExecuteAction::CreateLambda(
						[bSingleSelection]()
						{
							return bSingleSelection;
						}
					)
				)
			);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("RemoveParentEmitter", "Remove Parent Emitter"),
			LOCTEXT("RemoveParentEmitterToolTip", "Remove this emitter's parent, preventing inheritance of any further changes."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(EmitterViewModel, &FNiagaraEmitterViewModel::RemoveParentEmitter),
				FCanExecuteAction::CreateLambda(
					[bSingleSelection, bHasParent = EmitterViewModel->HasParentEmitter()]()
					{
						return bSingleSelection && bHasParent;
					}
				)
			)
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("OpenParentEmitterForEdit", "Open Parent For Edit"),
			LOCTEXT("OpenParentEmitterForEditToolTip", "Open and Focus Parent Emitter."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&FNiagaraEditorUtilities::OpenParentEmitterForEdit, EmitterViewModel),
				FCanExecuteAction::CreateLambda(
					[bSingleSelection, bHasParent = EmitterViewModel->HasParentEmitter()]()
		{
			return bSingleSelection && bHasParent;
		}
		)
			)
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ShowParentEmitterInContentBrowser", "Show Parent in Content Browser"),
			LOCTEXT("ShowParentEmitterInContentBrowserToolTip", "Show the selected emitter's parent emitter in the Content Browser."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateStatic(&FNiagaraEditorUtilities::ShowParentEmitterInContentBrowser, EmitterViewModel),
				FCanExecuteAction::CreateLambda(
					[bSingleSelection, bHasParent = EmitterViewModel->HasParentEmitter()]()
					{
						return bSingleSelection && bHasParent;
					}
				)
			)
		);

	
		}
		MenuBuilder.EndSection();

		return true;
	}

	return false;
}

void FNiagaraEditorUtilities::WarnWithToastAndLog(FText WarningMessage)
{
	FNotificationInfo WarningNotification(WarningMessage);
	WarningNotification.ExpireDuration = 5.0f;
	WarningNotification.bFireAndForget = true;
	WarningNotification.bUseLargeFont = false;
	WarningNotification.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Warning"));
	FSlateNotificationManager::Get().AddNotification(WarningNotification);
	UE_LOG(LogNiagaraEditor, Warning, TEXT("%s"), *WarningMessage.ToString());
}

void FNiagaraEditorUtilities::InfoWithToastAndLog(FText InfoMessage, float ToastDuration)
{
	FNotificationInfo WarningNotification(InfoMessage);
	WarningNotification.ExpireDuration = ToastDuration;
	WarningNotification.bFireAndForget = true;
	WarningNotification.bUseLargeFont = false;
	WarningNotification.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Note"));
	FSlateNotificationManager::Get().AddNotification(WarningNotification);
	UE_LOG(LogNiagaraEditor, Log, TEXT("%s"), *InfoMessage.ToString());
}

FName FNiagaraEditorUtilities::GetUniqueObjectName(UObject* Outer, UClass* ObjectClass, const FString& CandidateName)
{
	if (StaticFindObject(ObjectClass, Outer, *CandidateName) == nullptr)
	{
		return *CandidateName;
	}
	else
	{
		FString BaseCandidateName;
		int32 LastUnderscoreIndex;
		int32 NameIndex = 0;
		if (CandidateName.FindLastChar('_', LastUnderscoreIndex) && LexTryParseString(NameIndex, *CandidateName.RightChop(LastUnderscoreIndex + 1)))
		{
			BaseCandidateName = CandidateName.Left(LastUnderscoreIndex);
			NameIndex++;
		}
		else
		{
			BaseCandidateName = CandidateName;
			NameIndex = 1;
		}

		FString UniqueCandidateName = FString::Printf(TEXT("%s_%02i"), *BaseCandidateName, NameIndex);
		while (StaticFindObject(ObjectClass, Outer, *UniqueCandidateName) != nullptr)
		{
			NameIndex++;
			UniqueCandidateName = FString::Printf(TEXT("%s_%02i"), *BaseCandidateName, NameIndex);
		}
		return *UniqueCandidateName;
	}
}

bool FNiagaraEditorUtilities::GetVariableMetaDataScope(const FNiagaraVariableMetaData& MetaData, ENiagaraParameterScope& OutScope)
{
	if (MetaData.GetIsUsingLegacyNameString())
	{
		OutScope = ENiagaraParameterScope::Custom;
		return false;
	}

	const FName& MetaDataScopeName = MetaData.GetScopeName();
	const FNiagaraParameterScopeInfo* ScopeInfo = FNiagaraEditorModule::FindParameterScopeInfo(MetaDataScopeName);
	if (ensureMsgf(ScopeInfo != nullptr, TEXT("Failed to find registered parameter scope info for scope name %s!"), *MetaDataScopeName.ToString()))
	{
		OutScope = ScopeInfo->GetScope();
		return true;
	}
	OutScope = ENiagaraParameterScope::Custom;
	return false;
}

bool FNiagaraEditorUtilities::GetVariableMetaDataNamespaceString(const FNiagaraVariableMetaData& MetaData, FString& OutNamespaceString)
{
	if (MetaData.GetIsUsingLegacyNameString())
	{
		return false;
	}

	const FName& MetaDataScopeName = MetaData.GetScopeName();
	const FNiagaraParameterScopeInfo* ScopeInfo = FNiagaraEditorModule::FindParameterScopeInfo(MetaDataScopeName);
	if (ScopeInfo == nullptr)
	{
		ensureMsgf(false, TEXT("Failed to find registered parameter scope info for scope name %s!"), *MetaDataScopeName.ToString());
		return false;
	}

	FString NamespaceString = ScopeInfo->GetNamespaceString();
	if (MetaData.GetUsage() == ENiagaraScriptParameterUsage::InitialValueInput)
	{
		NamespaceString.Append(PARAM_MAP_INITIAL_STR);
	}
	OutNamespaceString = NamespaceString;
	return true;
}

bool FNiagaraEditorUtilities::GetVariableMetaDataNamespaceStringForNewScope(const FNiagaraVariableMetaData& MetaData, const FName& NewScopeName, FString& OutNamespaceString)
{
	if (MetaData.GetIsUsingLegacyNameString())
	{
		return false;
	}

	const FNiagaraParameterScopeInfo* ScopeInfo = FNiagaraEditorModule::FindParameterScopeInfo(NewScopeName);
	if (ScopeInfo == nullptr)
	{
		ensureMsgf(false, TEXT("Failed to find registered parameter scope info for scope name %s!"), *NewScopeName.ToString());
		return false;
	}

	FString NamespaceString = ScopeInfo->GetNamespaceString();
	if (MetaData.GetUsage() == ENiagaraScriptParameterUsage::InitialValueInput)
	{
		NamespaceString.Append(PARAM_MAP_INITIAL_STR);
	}
	OutNamespaceString = NamespaceString;

	return true;
}

FName FNiagaraEditorUtilities::GetScopeNameForParameterScope(ENiagaraParameterScope InScope)
{
	switch (InScope) {
	case ENiagaraParameterScope::User:
		return FNiagaraConstants::UserNamespace;
	case ENiagaraParameterScope::Engine:
		return FNiagaraConstants::EngineNamespace;
	case ENiagaraParameterScope::Owner:
		return FNiagaraConstants::EngineOwnerScopeName;
	case ENiagaraParameterScope::System:
		return FNiagaraConstants::SystemNamespace;
	case ENiagaraParameterScope::Emitter:
		return FNiagaraConstants::EmitterNamespace;
	case ENiagaraParameterScope::Particles:
		return FNiagaraConstants::ParticleAttributeNamespace;
	case ENiagaraParameterScope::Local:
		return FNiagaraConstants::LocalNamespace;
	case ENiagaraParameterScope::Input:
		return FNiagaraConstants::InputScopeName;
	case ENiagaraParameterScope::Custom:
		return FNiagaraConstants::CustomScopeName;
	case ENiagaraParameterScope::ScriptPersistent:
		return FNiagaraConstants::ScriptPersistentScopeName;
	case ENiagaraParameterScope::ScriptTransient:
		return FNiagaraConstants::ScriptTransientScopeName; 
	case ENiagaraParameterScope::Output:
		return FNiagaraConstants::OutputScopeName;
	default:
		ensureMsgf(false, TEXT("Tried to get scope name for unknown parameter scope!"));
	}
	return FNiagaraConstants::ParticleAttributeNamespace;
}


TArray<FName> FNiagaraEditorUtilities::DecomposeVariableNamespace(const FName& InVarNameToken, FName& OutName)
{
	int32 DotIndex;
	TArray<FName> OutNamespaces;
	FString VarNameString = InVarNameToken.ToString();
	while (VarNameString.FindChar(TEXT('.'), DotIndex))
	{
		OutNamespaces.Add(FName(*VarNameString.Left(DotIndex)));
		VarNameString = *VarNameString.RightChop(DotIndex + 1);
	}
	OutName = FName(*VarNameString);
	return OutNamespaces;
}

void  FNiagaraEditorUtilities::RecomposeVariableNamespace(const FName& InVarNameToken, const TArray<FName>& InParentNamespaces, FName& OutName)
{
	FString VarNameString;
	for (FName Name : InParentNamespaces)
	{
		VarNameString += Name.ToString() + TEXT(".");
	}
	VarNameString += InVarNameToken.ToString();
	OutName = FName(*VarNameString);
}

bool FNiagaraEditorUtilities::IsScopeEditable(const FName& InScopeName)
{
	if (InScopeName == FNiagaraConstants::EngineNamespace || InScopeName == FNiagaraConstants::EngineOwnerScopeName || InScopeName == FNiagaraConstants::EngineSystemScopeName || InScopeName == FNiagaraConstants::EngineEmitterScopeName)
	{
		return false;
	}
	return true;
}

bool FNiagaraEditorUtilities::IsScopeUserAssignable(const FName& InScopeName)
{
	if (InScopeName == FNiagaraConstants::EngineNamespace || InScopeName == FNiagaraConstants::EngineOwnerScopeName || InScopeName == FNiagaraConstants::EngineSystemScopeName || InScopeName == FNiagaraConstants::EngineEmitterScopeName)
	{
		return false;
	}
	return true;
}


void FNiagaraEditorUtilities::GetParameterMetaDataFromName(const FName& InVarNameToken, FNiagaraVariableMetaData& OutMetaData)
{
	auto MarkAsLegacyCustomName = [&OutMetaData]() {
		OutMetaData.SetScopeName(FNiagaraConstants::CustomScopeName);
		OutMetaData.SetIsUsingLegacyNameString(true);
	};

	auto GetMetaDataForFirstNamespace = [MarkAsLegacyCustomName](const FName& Namespace, FNiagaraVariableMetaData& OutMetaData)-> bool /*bNextNamespaceCanBeValid*/ {
		if (Namespace == FNiagaraConstants::LocalNamespace)
		{
			OutMetaData.SetScopeName(Namespace);
			OutMetaData.SetUsage(ENiagaraScriptParameterUsage::Local);
			return true;
		}
		else if (Namespace == FNiagaraConstants::ModuleNamespace)
		{
			OutMetaData.SetScopeName(FNiagaraConstants::InputScopeName);
			OutMetaData.SetUsage(ENiagaraScriptParameterUsage::Input);
			return false;
		}
		else if (Namespace == FNiagaraConstants::UserNamespace)
		{
			OutMetaData.SetScopeName(Namespace);
			OutMetaData.SetUsage(ENiagaraScriptParameterUsage::Input);
			return false;
		}
		else if (Namespace == FNiagaraConstants::EngineNamespace)
		{
			OutMetaData.SetScopeName(Namespace);
			OutMetaData.SetUsage(ENiagaraScriptParameterUsage::Input);
			return true;
		}
		else if (Namespace == FNiagaraConstants::SystemNamespace)
		{
			OutMetaData.SetScopeName(Namespace);
			OutMetaData.SetUsage(ENiagaraScriptParameterUsage::Input);
			return true;
		}
		else if (Namespace == FNiagaraConstants::EmitterNamespace)
		{
			OutMetaData.SetScopeName(Namespace);
			OutMetaData.SetUsage(ENiagaraScriptParameterUsage::Input);
			return true;
		}
		else if (Namespace == FNiagaraConstants::ParticleAttributeNamespace)
		{
			OutMetaData.SetScopeName(Namespace);
			OutMetaData.SetUsage(ENiagaraScriptParameterUsage::Input);
			return true;
		}
		else if (Namespace == FNiagaraConstants::OutputScopeName)
		{
			OutMetaData.SetScopeName(Namespace);
			OutMetaData.SetUsage(ENiagaraScriptParameterUsage::Output);
			return true;
		}

		MarkAsLegacyCustomName();
		return false;
	};

	auto GetMetaDataForInitialNamespace = [](const FName& Namespace, FNiagaraVariableMetaData& OutMetaData)-> bool /*bFoundInitialNamespace*/ {
		if (Namespace == FNiagaraConstants::InitialNamespace)
		{
			OutMetaData.SetUsage(ENiagaraScriptParameterUsage::InitialValueInput);
			return true;
		}
		return false;
	};

	auto GetScopeCanHaveInitialNamespaceSuffix = [](ENiagaraParameterScope InScope)-> bool /*bCanHaveInitialNamespaceSuffix*/ {
		switch (InScope) {
		case ENiagaraParameterScope::System:
		case ENiagaraParameterScope::Emitter:
		case ENiagaraParameterScope::Particles:
			return true;
		default:
			return false;
		};
		return false;
	};

	auto GetMetaDataForEngineSubNamespace = [](const FName& Namespace, FNiagaraVariableMetaData& OutMetaData)-> bool /*bFoundValidEngineSubNamespace*/ {
		if (Namespace == FNiagaraConstants::OwnerNamespace)
		{
			OutMetaData.SetScopeName(FNiagaraConstants::EngineOwnerScopeName);
			return true;
		}
		else if (Namespace == FNiagaraConstants::SystemNamespace)
		{
			OutMetaData.SetScopeName(FNiagaraConstants::EngineSystemScopeName);
			return true;
		}
		else if (Namespace == FNiagaraConstants::EmitterNamespace)
		{
			OutMetaData.SetScopeName(FNiagaraConstants::EngineEmitterScopeName);
			return true;
		}
		return false;
	};

	FName NamespacelessName;
	TArray<FName> DecomposedNamespaces = FNiagaraEditorUtilities::DecomposeVariableNamespace(InVarNameToken, NamespacelessName);
	OutMetaData.SetCachedNamespacelessVariableName(NamespacelessName);
	if (DecomposedNamespaces.Num() == 0)
	{
		UE_LOG(LogNiagaraEditor, Display, TEXT("Unexpected parameter encountered without a namespace: %s"), *InVarNameToken.ToString());
		MarkAsLegacyCustomName();
		return;
	}
	else if (DecomposedNamespaces.Num() == 1)
	{
		GetMetaDataForFirstNamespace(DecomposedNamespaces[0], OutMetaData);
		return;
	}
	else if (DecomposedNamespaces.Num() == 2)
	{
		bool bNextNamespaceCanBeValid = GetMetaDataForFirstNamespace(DecomposedNamespaces[0], OutMetaData);
		if (bNextNamespaceCanBeValid)
		{
			ENiagaraParameterScope FirstNamespaceScope;
			FNiagaraEditorUtilities::GetVariableMetaDataScope(OutMetaData, FirstNamespaceScope);
			if (FirstNamespaceScope == ENiagaraParameterScope::Local)
			{
				// "local.module." namespaces may be handled as local scopes and do not need to be marked as legacy namespaces.
				if (DecomposedNamespaces[1] == FNiagaraConstants::ModuleNamespace)
				{
					return;
				}
			}
			else if (FirstNamespaceScope == ENiagaraParameterScope::Engine)
			{
				bool bFoundEngineSubNamespace = GetMetaDataForEngineSubNamespace(DecomposedNamespaces[1], OutMetaData);
				if (bFoundEngineSubNamespace)
				{
					return;
				}
			}
			else if (DecomposedNamespaces[0] == FNiagaraConstants::OutputScopeName)
			{
				if (DecomposedNamespaces[1] == FNiagaraConstants::ModuleNamespace)
				{
					OutMetaData.SetScopeName(FNiagaraConstants::UniqueOutputScopeName);
					return;
				}
			}
			else if (GetScopeCanHaveInitialNamespaceSuffix(FirstNamespaceScope))
			{
				bool bFoundInitialNamespace = GetMetaDataForInitialNamespace(DecomposedNamespaces[1], OutMetaData);
				if (bFoundInitialNamespace)
				{
					return;
				}
			}
		}
		MarkAsLegacyCustomName();
		return;
	}

	MarkAsLegacyCustomName();
}

FString FNiagaraEditorUtilities::GetNamespacelessVariableNameString(const FName& InVarName)
{
	int32 DotIndex;
	FString VarNameString = InVarName.ToString();
	if (VarNameString.FindLastChar(TEXT('.'), DotIndex))
	{
		return VarNameString.RightChop(DotIndex + 1);
	}
	// No dot index, must be a namespaceless variable name (e.g. static switch name) just return the name to string.
	return VarNameString;
}

void FNiagaraEditorUtilities::GetReferencingFunctionCallNodes(UNiagaraScript* Script, TArray<UNiagaraNodeFunctionCall*>& OutReferencingFunctionCallNodes)
{
	for (TObjectIterator<UNiagaraNodeFunctionCall> It; It; ++It)
	{
		UNiagaraNodeFunctionCall* FunctionCallNode = *It;
		if (FunctionCallNode->FunctionScript == Script)
		{
			OutReferencingFunctionCallNodes.Add(FunctionCallNode);
		}
	}
}

bool FNiagaraEditorUtilities::GetVariableSortPriority(const FName& VarNameA, const FName& VarNameB)
{
	const FNiagaraNamespaceMetadata& NamespaceMetaDataA = GetNamespaceMetaDataForVariableName(VarNameA);
	if (NamespaceMetaDataA.IsValid() == false)
	{
		return false;
	}

	const FNiagaraNamespaceMetadata& NamespaceMetaDataB = GetNamespaceMetaDataForVariableName(VarNameB);
	const int32 NamespaceAPriority = GetNamespaceMetaDataSortPriority(NamespaceMetaDataA, NamespaceMetaDataB);
	if (NamespaceAPriority == 0)
	{
		return VarNameA.LexicalLess(VarNameB);
	}
	return NamespaceAPriority > 0;
}

int32 FNiagaraEditorUtilities::GetNamespaceMetaDataSortPriority(const FNiagaraNamespaceMetadata& A, const FNiagaraNamespaceMetadata& B)
{
	if (A.IsValid() == false)
	{
		return false;
	}
	else if (B.IsValid() == false)
	{
		return true;
	}

	const int32 ANum = A.Namespaces.Num();
	const int32 BNum = B.Namespaces.Num();
	for (int32 i = 0; i < FMath::Min(ANum, BNum); ++i)
	{
		const int32 ANamespacePriority = GetNamespaceSortPriority(A.Namespaces[i]);
		const int32 BNamespacePriority = GetNamespaceSortPriority(B.Namespaces[i]);
		if (ANamespacePriority != BNamespacePriority)
			return ANamespacePriority < BNamespacePriority ? 1 : -1;
	}
	if (ANum == BNum)
		return 0;

	return ANum < BNum ? 1 : -1;
}

int32 FNiagaraEditorUtilities::GetNamespaceSortPriority(const FName& Namespace)
{
	if (Namespace == FNiagaraConstants::UserNamespace)
		return 0;
	else if (Namespace == FNiagaraConstants::ModuleNamespace)
		return 1;
	else if (Namespace == FNiagaraConstants::StaticSwitchNamespace)
		return 2;
	else if (Namespace == FNiagaraConstants::DataInstanceNamespace)
		return 3;
	else if (Namespace == FNiagaraConstants::OutputNamespace)
		return 4;
	else if (Namespace == FNiagaraConstants::EngineNamespace)
		return 5;
	else if (Namespace == FNiagaraConstants::ParameterCollectionNamespace)
		return 6;
	else if (Namespace == FNiagaraConstants::SystemNamespace)
		return 7;
	else if (Namespace == FNiagaraConstants::EmitterNamespace)
		return 8;
	else if (Namespace == FNiagaraConstants::ParticleAttributeNamespace)
		return 9;
	else if (Namespace == FNiagaraConstants::TransientNamespace)
		return 10;

	return 11;
}

const FNiagaraNamespaceMetadata FNiagaraEditorUtilities::GetNamespaceMetaDataForVariableName(const FName& VarName)
{
	const FNiagaraParameterHandle VarHandle = FNiagaraParameterHandle(VarName);
	const TArray<FName> VarHandleNameParts = VarHandle.GetHandleParts();
	return GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(VarHandleNameParts);
}

bool FNiagaraParameterUtilities::DoesParameterNameMatchSearchText(FName ParameterName, const FString& SearchTextString)
{
	FNiagaraParameterHandle ParameterHandle(ParameterName);
	TArray<FName> HandleParts = ParameterHandle.GetHandleParts();
	FNiagaraNamespaceMetadata NamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(HandleParts);
	if (NamespaceMetadata.IsValid())
	{
		// If it's a registered namespace, check the display name of the namespace.
		if (NamespaceMetadata.DisplayName.ToString().Contains(SearchTextString))
		{
			return true;
		}

		// Check the namespace modifier if it has one
		if (HandleParts.Num() - NamespaceMetadata.Namespaces.Num() > 1)
		{
			FNiagaraNamespaceMetadata NamespaceModifierMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaceModifier(HandleParts[NamespaceMetadata.Namespaces.Num()]);
			if (NamespaceModifierMetadata.IsValid())
			{
				// Check first by modifier metadata display name.
				if (NamespaceModifierMetadata.DisplayName.ToString().Contains(SearchTextString))
				{
					return true;
				}
			}
			else
			{
				// Otherwise just check the string.
				if (HandleParts[NamespaceMetadata.Namespaces.Num()].ToString().Contains(SearchTextString))
				{
					return true;
				}
			}
		}

		// Last check the variable name.
		if (HandleParts.Last().ToString().Contains(SearchTextString))
		{
			return true;
		}
	}
	else if (HandleParts.ContainsByPredicate([&SearchTextString](FName NamePart) { return NamePart.ToString().Contains(SearchTextString); }))
	{
		// Otherwise if it's not in a valid namespace, just check all name parts.
		return true;
	}
	return false;
}

FText FNiagaraParameterUtilities::FormatParameterNameForTextDisplay(FName ParameterName)
{
	FNiagaraParameterHandle ParameterHandle(ParameterName);
	TArray<FName> HandleParts = ParameterHandle.GetHandleParts();
	FString DisplayString;
	for (int32 HandlePartIndex = 0; HandlePartIndex < HandleParts.Num() - 1; HandlePartIndex++)
	{
		DisplayString += TEXT("(") + HandleParts[HandlePartIndex].ToString().ToUpper() + TEXT(") ");
	}
	DisplayString += HandleParts[HandleParts.Num() - 1].ToString();
	return FText::FromString(DisplayString);
}

FName NamePartsToName(const TArray<FName>& NameParts)
{
	TArray<FString> NamePartStrings;
	for (FName NamePart : NameParts)
	{
		NamePartStrings.Add(NamePart.ToString());
	}
	return *FString::Join(NamePartStrings, TEXT("."));
}

bool FNiagaraParameterUtilities::GetNamespaceEditData(
	FName InParameterName, 
	FNiagaraParameterHandle& OutParameterHandle,
	FNiagaraNamespaceMetadata& OutNamespaceMetadata,
	FText& OutErrorMessage)
{
	OutParameterHandle = FNiagaraParameterHandle(InParameterName);
	TArray<FName> NameParts = OutParameterHandle.GetHandleParts();
	OutNamespaceMetadata = GetDefault<UNiagaraEditorSettings>()->GetMetaDataForNamespaces(NameParts);
	if (OutNamespaceMetadata.IsValid() == false || OutNamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::PreventEditing))
	{
		OutErrorMessage = LOCTEXT("NoMetadataForNamespace", "This parameter doesn't support editing.");
		return false;
	}

	return true;
}

bool FNiagaraParameterUtilities::GetNamespaceModifierEditData(
	FName InParameterName,
	FNiagaraParameterHandle& OutParameterHandle,
	FNiagaraNamespaceMetadata& OutNamespaceMetadata,
	FText& OutErrorMessage)
{
	if (GetNamespaceEditData(InParameterName, OutParameterHandle, OutNamespaceMetadata, OutErrorMessage))
	{
		if (OutNamespaceMetadata.IsValid() == false || OutNamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::CanChangeNamespaceModifier) == false)
		{
			OutErrorMessage = LOCTEXT("NotSupportedForThisNamespace", "This parameter doesn't support namespace modifiers.");
			return false;
		}
		return true;
	}
	else
	{
		return false;
	}
}

bool FNiagaraParameterUtilities::TestCanChangeNamespaceWithMessage(FName ParameterName, const FNiagaraNamespaceMetadata& NewNamespaceMetadata, FText& OutMessage)
{
	FNiagaraParameterHandle ParameterHandle;
	FNiagaraNamespaceMetadata NamespaceMetadata;
	if (GetNamespaceEditData(ParameterName, ParameterHandle, NamespaceMetadata, OutMessage))
	{
		if (NewNamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::PreventEditing))
		{
			OutMessage = LOCTEXT("NewNamespaceIsntValid", "The new namespace does not support editing so it can not be assigned.");
			return false;
		}
		else
		{
			OutMessage = FText::Format(LOCTEXT("ChagneNamespaceFormat", "Change this parameters namespace to {0}"), NewNamespaceMetadata.DisplayName);
			return true;
		}
	}
	return false;
}

FName FNiagaraParameterUtilities::ChangeNamespace(FName ParameterName, const FNiagaraNamespaceMetadata& NewNamespaceMetadata)
{
	FNiagaraParameterHandle ParameterHandle;
	FNiagaraNamespaceMetadata NamespaceMetadata;
	FText Unused;
	if (NewNamespaceMetadata.IsValid() &&
		NewNamespaceMetadata.Options.Contains(ENiagaraNamespaceMetadataOptions::PreventEditing) == false &&
		GetNamespaceEditData(ParameterName, ParameterHandle, NamespaceMetadata, Unused))
	{
		TArray<FName> NameParts = ParameterHandle.GetHandleParts();
		NameParts.RemoveAt(0, NamespaceMetadata.Namespaces.Num());
		NameParts.Insert(NewNamespaceMetadata.Namespaces, 0);
		return NamePartsToName(NameParts);
	}
	return NAME_None;
}

bool FNiagaraParameterUtilities::TestCanAddNamespaceModifierWithMessage(FName ParameterName, FText& OutMessage)
{
	FNiagaraParameterHandle ParameterHandle;
	FNiagaraNamespaceMetadata NamespaceMetadata;
	if (GetNamespaceModifierEditData(ParameterName, ParameterHandle, NamespaceMetadata, OutMessage))
	{
		int32 NumberOfNamePartsAfterNamespace = ParameterHandle.GetHandleParts().Num() - NamespaceMetadata.Namespaces.Num();
		if (NumberOfNamePartsAfterNamespace == 1)
		{
			OutMessage = LOCTEXT("AddNamespaceModifier", "Add a namespace modifier to this parameter.");
			return true;
		}
		else
		{
			OutMessage = LOCTEXT("CantAddAnotherNamespaceModfier", "Only one namespace modifier is supported.");
			return false;
		}
	}
	return false;
}

FName FNiagaraParameterUtilities::AddNamespaceModifier(FName InParameterName)
{
	FNiagaraParameterHandle ParameterHandle;
	FNiagaraNamespaceMetadata NamespaceMetadata;
	FText Unused;
	if (GetNamespaceModifierEditData(InParameterName, ParameterHandle, NamespaceMetadata, Unused))
	{
		TArray<FName> NameParts = ParameterHandle.GetHandleParts();
		int32 NumberOfNamePartsAfterNamespace = NameParts.Num() - NamespaceMetadata.Namespaces.Num();
		if (NumberOfNamePartsAfterNamespace == 1)
		{
			NameParts.Insert(FNiagaraConstants::ModuleNamespace, NamespaceMetadata.Namespaces.Num());
			return NamePartsToName(NameParts);
		}
	}
	return NAME_None;
}

bool FNiagaraParameterUtilities::TestCanRemoveNamespaceModifierWithMessage(FName ParameterName, FText& OutMessage)
{
	FNiagaraParameterHandle ParameterHandle;
	FNiagaraNamespaceMetadata NamespaceMetadata;
	if (GetNamespaceModifierEditData(ParameterName, ParameterHandle, NamespaceMetadata, OutMessage))
	{
		int32 NumberOfNamePartsAfterNamespace = ParameterHandle.GetHandleParts().Num() - NamespaceMetadata.Namespaces.Num();
		if (NumberOfNamePartsAfterNamespace == 2)
		{
			OutMessage = LOCTEXT("RemoveNamespaceModifier", "Remove the namespace modifier from this parameter.");
			return true;
		}
		else
		{
			OutMessage = LOCTEXT("NoNamespaceModifierToRemove", "No namespace modifier to remove.");
			return false;
		}
	}
	return false;
}

FName FNiagaraParameterUtilities::RemoveNamespaceModifier(FName InParameterName)
{
	FNiagaraParameterHandle ParameterHandle;
	FNiagaraNamespaceMetadata NamespaceMetadata;
	FText Unused;
	if (GetNamespaceModifierEditData(InParameterName, ParameterHandle, NamespaceMetadata, Unused))
	{
		TArray<FName> NameParts = ParameterHandle.GetHandleParts();
		int32 NumberOfNamePartsAfterNamespace = NameParts.Num() - NamespaceMetadata.Namespaces.Num();
		if (NumberOfNamePartsAfterNamespace == 2)
		{
			NameParts.RemoveAt(NamespaceMetadata.Namespaces.Num());
			return NamePartsToName(NameParts);
		}
	}
	return NAME_None;
}

bool FNiagaraParameterUtilities::TestCanEditNamespaceModifierWithMessage(FName ParameterName, FText& OutMessage)
{
	FNiagaraParameterHandle ParameterHandle;
	FNiagaraNamespaceMetadata NamespaceMetadata;
	if (GetNamespaceModifierEditData(ParameterName, ParameterHandle, NamespaceMetadata, OutMessage))
	{
		int32 NumberOfNamePartsAfterNamespace = ParameterHandle.GetHandleParts().Num() - NamespaceMetadata.Namespaces.Num();
		if (NumberOfNamePartsAfterNamespace == 2)
		{
			OutMessage = LOCTEXT("EditNamespaceModifier", "Edit the namespace modifier for this parameter.");
			return true;
		}
		else
		{
			OutMessage = LOCTEXT("NoNamespaceModifierToEdit", "No namespace modifier to edit.");
			return false;
		}
	}
	return false;
}

bool FNiagaraParameterUtilities::TestCanRenameWithMessage(FName ParameterName, FText& OutMessage)
{
	FNiagaraParameterHandle ParameterHandle;
	FNiagaraNamespaceMetadata NamespaceMetadata;
	return GetNamespaceEditData(ParameterName, ParameterHandle, NamespaceMetadata, OutMessage);
}

#undef LOCTEXT_NAMESPACE
