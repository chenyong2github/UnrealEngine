// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackFunctionInputCollection.h"

#include "EdGraphSchema_Niagara.h"
#include "NiagaraClipboard.h"
#include "NiagaraDataInterface.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeParameterMapSet.h"
#include "ScopedTransaction.h"
#include "EdGraph/EdGraphPin.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraStackInputCategory.h"

#define LOCTEXT_NAMESPACE "UNiagaraStackFunctionInputCollection"

FText UNiagaraStackFunctionInputCollection::UncategorizedName = LOCTEXT("Uncategorized", "Uncategorized");

UNiagaraStackFunctionInputCollection::UNiagaraStackFunctionInputCollection()
	: ModuleNode(nullptr)
	, InputFunctionCallNode(nullptr)
	, bShouldShowInStack(true)
{
}

UNiagaraNodeFunctionCall* UNiagaraStackFunctionInputCollection::GetModuleNode() const
{
	return ModuleNode;
}

UNiagaraNodeFunctionCall* UNiagaraStackFunctionInputCollection::GetInputFunctionCallNode() const
{
	return InputFunctionCallNode;
}

void UNiagaraStackFunctionInputCollection::Initialize(
	FRequiredEntryData InRequiredEntryData,
	UNiagaraNodeFunctionCall& InModuleNode,
	UNiagaraNodeFunctionCall& InInputFunctionCallNode,
	FString InOwnerStackItemEditorDataKey)
{
	checkf(ModuleNode == nullptr && InputFunctionCallNode == nullptr, TEXT("Can not set the node more than once."));
	FString InputCollectionStackEditorDataKey = FString::Printf(TEXT("%s-Inputs"), *InInputFunctionCallNode.NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	Super::Initialize(InRequiredEntryData, InOwnerStackItemEditorDataKey, InputCollectionStackEditorDataKey);
	ModuleNode = &InModuleNode;
	InputFunctionCallNode = &InInputFunctionCallNode;
	InputFunctionCallNode->OnInputsChanged().AddUObject(this, &UNiagaraStackFunctionInputCollection::OnFunctionInputsChanged);
}

void UNiagaraStackFunctionInputCollection::FinalizeInternal()
{
	InputFunctionCallNode->OnInputsChanged().RemoveAll(this);
	Super::FinalizeInternal();
}

FText UNiagaraStackFunctionInputCollection::GetDisplayName() const
{
	return LOCTEXT("InputCollectionDisplayName", "Inputs");
}

bool UNiagaraStackFunctionInputCollection::GetShouldShowInStack() const
{
	return bShouldShowInStack;
}

bool UNiagaraStackFunctionInputCollection::GetIsEnabled() const
{
	return InputFunctionCallNode->GetDesiredEnabledState() == ENodeEnabledState::Enabled;
}

void UNiagaraStackFunctionInputCollection::SetShouldShowInStack(bool bInShouldShowInStack)
{
	bShouldShowInStack = bInShouldShowInStack;
}

void UNiagaraStackFunctionInputCollection::ToClipboardFunctionInputs(UObject* InOuter, TArray<const UNiagaraClipboardFunctionInput*>& OutClipboardFunctionInputs) const
{
	TArray<UNiagaraStackInputCategory*> ChildCategories;
	GetUnfilteredChildrenOfType(ChildCategories);
	for (UNiagaraStackInputCategory* ChildCategory : ChildCategories)
	{
		ChildCategory->ToClipboardFunctionInputs(InOuter, OutClipboardFunctionInputs);
	}
}

void UNiagaraStackFunctionInputCollection::SetValuesFromClipboardFunctionInputs(const TArray<const UNiagaraClipboardFunctionInput*>& ClipboardFunctionInputs)
{
	TArray<const UNiagaraClipboardFunctionInput*> StaticSwitchInputs;
	TArray<const UNiagaraClipboardFunctionInput*> StandardInputs;

	TArray<UNiagaraStackInputCategory*> ChildCategories;
	GetUnfilteredChildrenOfType(ChildCategories);

	// Set static switches first so that other inputs will be available to set.
	for (UNiagaraStackInputCategory* ChildCategory : ChildCategories)
	{
		ChildCategory->SetStaticSwitchValuesFromClipboardFunctionInputs(ClipboardFunctionInputs);
	}

	RefreshChildren();
	for (UNiagaraStackInputCategory* ChildCategory : ChildCategories)
	{
		ChildCategory->SetStandardValuesFromClipboardFunctionInputs(ClipboardFunctionInputs);
	}
}

void UNiagaraStackFunctionInputCollection::GetChildInputs(TArray<UNiagaraStackFunctionInput*>& OutResult) const
{
	TArray<UNiagaraStackInputCategory*> ChildCategories;
	GetUnfilteredChildrenOfType(ChildCategories);
	for (UNiagaraStackInputCategory* ChildCategory : ChildCategories)
	{
		ChildCategory->GetUnfilteredChildrenOfType(OutResult);
	}
}

struct FNiagaraParentData
{
	const UEdGraphPin* ParentPin;
	TArray<int32> ChildIndices;
};

void UNiagaraStackFunctionInputCollection::AddInvalidChildStackIssue(FName PinName, TArray<FStackIssue>& OutIssues)
{
	FStackIssue InvalidHierarchyWarning(
        EStackIssueSeverity::Warning,
        FText::Format(LOCTEXT("InvalidHierarchyWarningSummaryFormat", "Invalid ParentAttribute {0} in module metadata."), FText::FromString(PinName.ToString())),
        FText::Format(LOCTEXT("InvalidHierarchyWarningFormat", "The attribute {0} was used as parent in the metadata although it is itself the child of another attribute.\nPlease check the module metadata to fix this."),
            FText::FromString(PinName.ToString())), GetStackEditorDataKey(), true);
	OutIssues.Add(InvalidHierarchyWarning);
}

void UNiagaraStackFunctionInputCollection::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	TSet<const UEdGraphPin*> HiddenPins;
	TArray<const UEdGraphPin*> InputPins;
	FCompileConstantResolver ConstantResolver;
	if (GetEmitterViewModel().IsValid())
	{
		ConstantResolver = FCompileConstantResolver(GetEmitterViewModel()->GetEmitter(), FNiagaraStackGraphUtilities::GetOutputNodeUsage(*InputFunctionCallNode));
	}
	else
	{
		// if we don't have an emitter model, we must be in a system context
		ConstantResolver = FCompileConstantResolver(&GetSystemViewModel()->GetSystem(), FNiagaraStackGraphUtilities::GetOutputNodeUsage(*InputFunctionCallNode));
	}
	GetStackFunctionInputPins(*InputFunctionCallNode, InputPins, HiddenPins, ConstantResolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly);

	const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();

	TArray<FName> ProcessedInputNames;
	TArray<FName> DuplicateInputNames;
	TArray<FName> ValidAliasedInputNames;
	TMap<FName, UEdGraphPin*> StaticSwitchInputs;
	TArray<const UEdGraphPin*> PinsWithInvalidTypes;

	UNiagaraGraph* InputFunctionGraph = InputFunctionCallNode->GetCalledGraph();
	TArray<FInputData> InputDataCollection;
	TMap<FName, FNiagaraParentData> ParentMapping;
	
	// Gather input data
	for (const UEdGraphPin* InputPin : InputPins)
	{
		if (ProcessedInputNames.Contains(InputPin->PinName))
		{
			DuplicateInputNames.AddUnique(InputPin->PinName);
			continue;
		}
		ProcessedInputNames.Add(InputPin->PinName);

		FNiagaraVariable InputVariable = NiagaraSchema->PinToNiagaraVariable(InputPin);
		if (InputVariable.GetType().IsValid() == false)
		{
			PinsWithInvalidTypes.Add(InputPin);
			continue;
		}
		ValidAliasedInputNames.Add(
			FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle(InputPin->PinName), InputFunctionCallNode).GetParameterHandleString());

		TOptional<FNiagaraVariableMetaData> InputMetaData;
		if (InputFunctionGraph != nullptr)
		{
			InputMetaData = InputFunctionGraph->GetMetaData(InputVariable);
		}

		FText InputCategory = InputMetaData.IsSet() && InputMetaData->CategoryName.IsEmptyOrWhitespace() == false
			? InputMetaData->CategoryName
			: UncategorizedName;

		bool bIsInputHidden = HiddenPins.Contains(InputPin);
		FInputData InputData = { InputPin, InputVariable.GetType(), InputMetaData ? InputMetaData->EditorSortPriority : 0, InputCategory, false, bIsInputHidden };
		int32 Index = InputDataCollection.Add(InputData);

		// set up the data for the parent-child mapping
		if (InputMetaData &&  !InputMetaData->ParentAttribute.IsNone())
		{
			if (InputMetaData->ParentAttribute.ToString().StartsWith(PARAM_MAP_MODULE_STR))
			{
				ParentMapping.FindOrAdd(InputMetaData->ParentAttribute).ChildIndices.Add(Index);
			}
			else
			{
				FString NamespacedParent = PARAM_MAP_MODULE_STR + InputMetaData->ParentAttribute.ToString();
				ParentMapping.FindOrAdd(FName(*NamespacedParent)).ChildIndices.Add(Index);
			}
		}
	}

	// Gather static switch parameters
	TSet<UEdGraphPin*> HiddenSwitchPins;
	TArray<UEdGraphPin*> SwitchPins;
	FNiagaraStackGraphUtilities::GetStackFunctionStaticSwitchPins(*InputFunctionCallNode, SwitchPins, HiddenSwitchPins);
	for (UEdGraphPin* InputPin : SwitchPins)
	{
		// The static switch pin names to not contain the module namespace, as they are not part of the parameter maps.
		// We add it here only to check for name clashes with actual module parameters.
		FString ModuleName = PARAM_MAP_MODULE_STR;
		InputPin->PinName.AppendString(ModuleName);
		FName SwitchPinName(*ModuleName); 

		if (ProcessedInputNames.Contains(SwitchPinName))
		{
			DuplicateInputNames.AddUnique(SwitchPinName);
			continue;
		}
		ProcessedInputNames.Add(SwitchPinName);

		FNiagaraVariable InputVariable = NiagaraSchema->PinToNiagaraVariable(InputPin);
		if (InputVariable.GetType().IsValid() == false)
		{
			PinsWithInvalidTypes.Add(InputPin);
			continue;
		}

		FName AliasedName = FNiagaraParameterHandle(*InputFunctionCallNode->GetFunctionName(), InputPin->PinName).GetParameterHandleString();
		StaticSwitchInputs.Add(AliasedName, InputPin);

		TOptional<FNiagaraVariableMetaData> InputMetaData;
		if (InputFunctionGraph != nullptr)
		{
			InputMetaData = InputFunctionGraph->GetMetaData(InputVariable);
		}

		FText InputCategory = InputMetaData.IsSet() && InputMetaData->CategoryName.IsEmptyOrWhitespace() == false
			? InputMetaData->CategoryName
			: UncategorizedName;

		bool bIsInputHidden = HiddenSwitchPins.Contains(InputPin);
		FInputData InputData = { InputPin, InputVariable.GetType(), InputMetaData ? InputMetaData->EditorSortPriority : 0, InputCategory, true, bIsInputHidden };
		int32 Index = InputDataCollection.Add(InputData);

		// set up the data for the parent-child mapping
		if (InputMetaData)
		{
			FNiagaraParentData& ParentData = ParentMapping.FindOrAdd(SwitchPinName);
			ParentData.ParentPin = InputPin;
			if (!InputMetaData->ParentAttribute.IsNone())
			{
				if (InputMetaData->ParentAttribute.ToString().StartsWith(PARAM_MAP_MODULE_STR))
				{
					ParentMapping.FindOrAdd(InputMetaData->ParentAttribute).ChildIndices.Add(Index);
				}
				else
				{
					FString NamespacedParent = PARAM_MAP_MODULE_STR + InputMetaData->ParentAttribute.ToString();
					ParentMapping.FindOrAdd(FName(*NamespacedParent)).ChildIndices.Add(Index);
				}
			}
		}
	}

	// resolve the parent/child relationships
	for (auto& Entry : ParentMapping)
	{
		FNiagaraParentData& Data = Entry.Value;
		if (Data.ChildIndices.Num() == 0) {continue;}
		for (FInputData& InputData : InputDataCollection)
		{
			if (InputData.Pin != Data.ParentPin) { continue; }
			if (InputData.bIsChild)
			{
				AddInvalidChildStackIssue(InputData.Pin->PinName, NewIssues);
				continue;
			}
			for (int32 ChildIndex : Data.ChildIndices)
			{
				if (InputDataCollection[ChildIndex].Children.Num() > 0)
				{
					AddInvalidChildStackIssue(InputDataCollection[ChildIndex].Pin->PinName, NewIssues);
					continue;
				}
				InputDataCollection[ChildIndex].bIsChild = true;
				InputDataCollection[ChildIndex].Category = InputData.Category; // children get the parent category to prevent inconsistencies there
				InputData.Children.Add(&InputDataCollection[ChildIndex]);
			}
		}
	}

	auto SortPredicate = [](const FInputData& A, const FInputData& B)
	{
		// keep the uncategorized attributes first
		if (A.Category.CompareTo(UncategorizedName) == 0 && B.Category.CompareTo(UncategorizedName) != 0)
		{
			return true;
		}
		if (A.Category.CompareTo(UncategorizedName) != 0 && B.Category.CompareTo(UncategorizedName) == 0)
		{
			return false;
		}
		if (A.SortKey != B.SortKey)
		{
			return A.SortKey < B.SortKey;
		}
		return A.Pin->PinName.LexicalLess(B.Pin->PinName);
	};

	// Sort child and parent data separately
	TArray<FInputData*> ParentDataCollection;
	for (FInputData& InputData : InputDataCollection)
	{
		if (!InputData.bIsChild)
		{
			ParentDataCollection.Add(&InputData);
			InputData.Children.Sort(SortPredicate);
		}
	}
	ParentDataCollection.Sort(SortPredicate);

	// Populate the categories
	for (FInputData* ParentData : ParentDataCollection)
	{
		AddInputToCategory(*ParentData, CurrentChildren, NewChildren);
		for (FInputData* ChildData : ParentData->Children)
		{
			AddInputToCategory(*ChildData, CurrentChildren, NewChildren);			
		}
	}
	RefreshIssues(DuplicateInputNames, ValidAliasedInputNames, PinsWithInvalidTypes, StaticSwitchInputs, NewIssues);
}

void UNiagaraStackFunctionInputCollection::AddInputToCategory(const FInputData& InputData, const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren)
{
	// Try to find an existing category in the already processed children.
	UNiagaraStackInputCategory* InputCategory = FindCurrentChildOfTypeByPredicate<UNiagaraStackInputCategory>(NewChildren,
        [&](UNiagaraStackInputCategory* CurrentCategory) { return CurrentCategory->GetCategoryName().CompareTo(InputData.Category) == 0; });

	if (InputCategory == nullptr)
	{
		// If we haven't added any children to this category yet see if there is one that can be reused from the current children.
		InputCategory = FindCurrentChildOfTypeByPredicate<UNiagaraStackInputCategory>(CurrentChildren,
            [&](UNiagaraStackInputCategory* CurrentCategory) { return CurrentCategory->GetCategoryName().CompareTo(InputData.Category) == 0; });
		if (InputCategory == nullptr)
		{
			// If we don't have a current child for this category make a new one.
			InputCategory = NewObject<UNiagaraStackInputCategory>(this);
			InputCategory->Initialize(CreateDefaultChildRequiredData(), *ModuleNode, *InputFunctionCallNode, InputData.Category, GetOwnerStackItemEditorDataKey());
		}
		else
		{
			// We found a category to reuse, but we need to reset the inputs before we can start adding the current set of inputs.
			InputCategory->ResetInputs();
		}

		if (InputData.Category.CompareTo(UncategorizedName) == 0)
		{
			InputCategory->SetShouldShowInStack(false);
		}
		NewChildren.Add(InputCategory);
	}
	InputCategory->AddInput(InputData.Pin->PinName, InputData.Type, InputData.bIsStatic ? EStackParameterBehavior::Static : EStackParameterBehavior::Dynamic, InputData.bIsHidden, InputData.bIsChild);
}

UNiagaraStackEntry::FStackIssueFix UNiagaraStackFunctionInputCollection::GetNodeRemovalFix(UEdGraphPin* PinToRemove, FText FixDescription)
{
	return FStackIssueFix(
		FixDescription,
		UNiagaraStackEntry::FStackIssueFixDelegate::CreateLambda([=]()
	{
		FScopedTransaction ScopedTransaction(FixDescription);
		TArray<TWeakObjectPtr<UNiagaraDataInterface>> RemovedDataObjects;
		FNiagaraStackGraphUtilities::RemoveNodesForStackFunctionInputOverridePin(*PinToRemove, RemovedDataObjects);
		TArray<UObject*> RemovedObjects;
		for (TWeakObjectPtr<UNiagaraDataInterface> RemovedDataObject : RemovedDataObjects)
		{
			if (RemovedDataObject.IsValid())
			{
				RemovedObjects.Add(RemovedDataObject.Get());
			}
		}
		OnDataObjectModified().Broadcast(RemovedObjects, ENiagaraDataObjectChange::Removed);
		PinToRemove->GetOwningNode()->RemovePin(PinToRemove);
	}));
}

UNiagaraStackEntry::FStackIssueFix UNiagaraStackFunctionInputCollection::GetResetPinFix(UEdGraphPin* PinToReset, FText FixDescription)
{
	return FStackIssueFix(
		FixDescription,
		UNiagaraStackEntry::FStackIssueFixDelegate::CreateLambda([=]()
	{
		FScopedTransaction ScopedTransaction(FixDescription);
		const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
		UNiagaraNode* OwningNiagaraNode = Cast<UNiagaraNode>(PinToReset->GetOwningNode());
		NiagaraSchema->ResetPinToAutogeneratedDefaultValue(PinToReset);
		if (OwningNiagaraNode != nullptr)
		{
			OwningNiagaraNode->MarkNodeRequiresSynchronization("Pin reset to default value.", true);
		}
	}));
}

FText GetUserFriendlyFunctionName(UNiagaraNodeFunctionCall* Node)
{
	if (Node->IsA<UNiagaraNodeAssignment>())
	{
		// The function name of assignment nodes contains a guid, which is just confusing for the user to see 
		return LOCTEXT("AssignmentNodeName", "SetVariables");
	}
	return FText::FromString(Node->GetFunctionName());
}

void UNiagaraStackFunctionInputCollection::RefreshIssues(const TArray<FName>& DuplicateInputNames, const TArray<FName>& ValidAliasedInputNames, const TArray<const UEdGraphPin*>& PinsWithInvalidTypes,
                                                         const TMap<FName, UEdGraphPin*>& StaticSwitchInputs, TArray<FStackIssue>& NewIssues)
{
	if (!GetIsEnabled())
	{
		NewIssues.Empty();
		return;
	}

	// Gather override nodes to find candidates that were replaced by static switches and are no longer valid
	FPinCollectorArray OverridePins;
	UNiagaraNodeParameterMapSet* OverrideNode = FNiagaraStackGraphUtilities::GetStackFunctionOverrideNode(*InputFunctionCallNode);
	if (OverrideNode != nullptr)
	{
		OverrideNode->GetInputPins(OverridePins);
	}
	for (UEdGraphPin* OverridePin : OverridePins)
	{
		// Try to find function input overrides which are no longer valid so we can generate errors for them.
		UEdGraphPin*const* PinReference = StaticSwitchInputs.Find(OverridePin->PinName);
		if (PinReference == nullptr)
		{
			if (FNiagaraStackGraphUtilities::IsOverridePinForFunction(*OverridePin, *InputFunctionCallNode) &&
				ValidAliasedInputNames.Contains(OverridePin->PinName) == false)
			{
				FStackIssue InvalidInputOverrideError(
					EStackIssueSeverity::Warning,
					FText::Format(LOCTEXT("InvalidInputOverrideSummaryFormat", "Invalid Input Override: {0}"), FText::FromString(OverridePin->PinName.ToString())),
					FText::Format(LOCTEXT("InvalidInputOverrideFormat", "The input {0} was previously overriden but is no longer exposed by the function {1}.\nPress the fix button to remove this unused override data,\nor check the function definition to see why this input is no longer exposed."),
						FText::FromString(OverridePin->PinName.ToString()), GetUserFriendlyFunctionName(InputFunctionCallNode)),
					GetStackEditorDataKey(),
					false,
					GetNodeRemovalFix(OverridePin, LOCTEXT("RemoveInvalidInputTransaction", "Remove input override")));

				NewIssues.Add(InvalidInputOverrideError);
			}
		}
		else
		{
			// If we have an override pin that is no longer valid, but has the same name and type as a static switch parameter, then it is safe to assume
			// that the parameter was replaced by the static switch. So we ask the user to copy over its value or remove the override.
			UEdGraphPin* SwitchPin = *PinReference;
			bool bIsSameType = OverridePin->PinType.PinCategory == SwitchPin->PinType.PinCategory &&
				OverridePin->PinType.PinSubCategoryObject == SwitchPin->PinType.PinSubCategoryObject;
			if (bIsSameType && !ValidAliasedInputNames.Contains(OverridePin->PinName))
			{
				TArray<FStackIssueFix> Fixes;

				// first possible fix: convert the value over to the static switch
				FText ConversionFixDescription = LOCTEXT("ConvertInputToStaticSwitchTransaction", "Copy value to static switch parameter");
				FStackIssueFix ConvertInputOverrideFix(
					ConversionFixDescription,
					UNiagaraStackEntry::FStackIssueFixDelegate::CreateLambda([=]()
				{
					FScopedTransaction ScopedTransaction(ConversionFixDescription);
					SwitchPin->Modify();
					SwitchPin->DefaultValue = OverridePin->DefaultValue;

					TArray<TWeakObjectPtr<UNiagaraDataInterface>> RemovedDataObjects;
					FNiagaraStackGraphUtilities::RemoveNodesForStackFunctionInputOverridePin(*OverridePin, RemovedDataObjects);
					TArray<UObject*> RemovedObjects;
					for (TWeakObjectPtr<UNiagaraDataInterface> RemovedDataObject : RemovedDataObjects)
					{
						if (RemovedDataObject.IsValid())
						{
							RemovedObjects.Add(RemovedDataObject.Get());
						}
					}
					OnDataObjectModified().Broadcast(RemovedObjects, ENiagaraDataObjectChange::Removed);
					OverridePin->GetOwningNode()->RemovePin(OverridePin);
				}));
				Fixes.Add(ConvertInputOverrideFix);

				// second possible fix: remove the override completely
				Fixes.Add(GetNodeRemovalFix(OverridePin, LOCTEXT("RemoveInvalidInputTransactionExt", "Remove input override (WARNING: this could result in different behavior!)")));

				FStackIssue DeprecatedInputOverrideError(
					EStackIssueSeverity::Error,
					FText::Format(LOCTEXT("DeprecatedInputSummaryFormat", "Deprecated Input Override: {0}"), FText::FromString(OverridePin->PinName.ToString())),
					FText::Format(LOCTEXT("DeprecatedInputFormat", "The input {0} is no longer exposed by the function {1}, but there exists a static switch parameter with the same name instead.\nYou can choose to copy the previously entered data over to the new parameter or remove the override to discard it."),
						FText::FromString(OverridePin->PinName.ToString()), GetUserFriendlyFunctionName(InputFunctionCallNode)),
					GetStackEditorDataKey(),
					false,
					Fixes);

				NewIssues.Add(DeprecatedInputOverrideError);
				break;
			}
		}
		
	}

	// Generate issues for duplicate input names.
	for (const FName& DuplicateInputName : DuplicateInputNames)
	{
		FStackIssue DuplicateInputError(
			EStackIssueSeverity::Error,
			FText::Format(LOCTEXT("DuplicateInputSummaryFormat", "Duplicate Input: {0}"), FText::FromName(DuplicateInputName)),
			FText::Format(LOCTEXT("DuplicateInputFormat", "There are multiple inputs with the same name {0} exposed by the function {1}.\nThis is not supported and must be fixed in the script that defines this function.\nCheck for inputs with the same name and different types or static switches."),
				FText::FromName(DuplicateInputName), GetUserFriendlyFunctionName(InputFunctionCallNode)),
			GetStackEditorDataKey(),
			false);
		NewIssues.Add(DuplicateInputError);
	}

	// Generate issues for invalid types.
	for (const UEdGraphPin* PinWithInvalidType : PinsWithInvalidTypes)
	{
		FStackIssue InputWithInvalidTypeError(
			EStackIssueSeverity::Error,
			FText::Format(LOCTEXT("InputWithInvalidTypeSummaryFormat", "Input has an invalid type: {0}"), FText::FromName(PinWithInvalidType->PinName)),
			FText::Format(LOCTEXT("InputWithInvalidTypeFormat", "The input {0} on function {1} has a type which is invalid.\nThe type of this input doesn't exist anymore.\nThe type must be brought back into the project or this input must be removed from the script."),
				FText::FromName(PinWithInvalidType->PinName), GetUserFriendlyFunctionName(InputFunctionCallNode)),
			GetStackEditorDataKey(),
			false);
		NewIssues.Add(InputWithInvalidTypeError);
	}

	// Generate issues for orphaned input pins from static switches which are no longer valid.
	for (UEdGraphPin* InputFunctionCallNodePin : InputFunctionCallNode->Pins)
	{
		if (InputFunctionCallNodePin->Direction == EEdGraphPinDirection::EGPD_Input && InputFunctionCallNodePin->bOrphanedPin)
		{
			FNiagaraTypeDefinition InputType = UEdGraphSchema_Niagara::PinToTypeDefinition(InputFunctionCallNodePin);
			if (InputType == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				FStackIssue InvalidInputError(
					EStackIssueSeverity::Warning,
					FText::Format(LOCTEXT("InvalidParameterMapInputSummaryFormat", "Invalid Input: {0}"), FText::FromString(InputFunctionCallNodePin->PinName.ToString())),
					FText::Format(LOCTEXT("InvalidParameterMapInputFormat", "The parameter map input {0} was removed from this module. Modules will not function without a valid parameter map input.  This must be fixed in the script that defines this module."),
						FText::FromString(InputFunctionCallNodePin->PinName.ToString()), GetUserFriendlyFunctionName(InputFunctionCallNode)),
					GetStackEditorDataKey(),
					false);
				NewIssues.Add(InvalidInputError);
			}
			else
			{
				FStackIssue InvalidInputError(
					EStackIssueSeverity::Warning,
					FText::Format(LOCTEXT("InvalidInputSummaryFormat", "Invalid Input: {0}"), FText::FromString(InputFunctionCallNodePin->PinName.ToString())),
					FText::Format(LOCTEXT("InvalidInputFormat", "The input {0} was previously set but is no longer exposed by the function {1}.\nPress the fix button to remove this unused input data,\nor check the function definition to see why this input is no longer exposed."),
						FText::FromString(InputFunctionCallNodePin->PinName.ToString()), GetUserFriendlyFunctionName(InputFunctionCallNode)),
					GetStackEditorDataKey(),
					false,
					GetResetPinFix(InputFunctionCallNodePin, LOCTEXT("RemoveInvalidInputPinFix", "Remove invalid input.")));
				NewIssues.Add(InvalidInputError);
			}
		}
	}
}

void UNiagaraStackFunctionInputCollection::OnFunctionInputsChanged()
{
	RefreshChildren();
}

#undef LOCTEXT_NAMESPACE
