// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackParameterStoreEntry.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEditorUtilities.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemScriptViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScriptGraphViewModel.h"
#include "ViewModels/Stack/NiagaraStackObject.h"
#include "NiagaraNodeParameterMapGet.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraStackEditorData.h"

#include "ScopedTransaction.h"
#include "Editor.h"
#include "UObject/StructOnScope.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistryModule.h"
#include "ARFilter.h"
#include "EdGraph/EdGraphPin.h"
#include "NiagaraConstants.h"


#define LOCTEXT_NAMESPACE "UNiagaraStackParameterStoreEntry"
UNiagaraStackParameterStoreEntry::UNiagaraStackParameterStoreEntry()
	: ValueObjectEntry(nullptr)
{
}

void UNiagaraStackParameterStoreEntry::Initialize(
	FRequiredEntryData InRequiredEntryData,
	UObject* InOwner,
	FNiagaraParameterStore* InParameterStore,
	FString InInputParameterHandle,
	FNiagaraTypeDefinition InInputType,
	FString InOwnerStackItemEditorDataKey)
{
	bool bParameterIsAdvanced = false;
	FString ParameterStackEditorDataKey = FString::Printf(TEXT("Parameter-%s"), *InInputParameterHandle);
	Super::Initialize(InRequiredEntryData, bParameterIsAdvanced, InOwnerStackItemEditorDataKey, ParameterStackEditorDataKey);
	DisplayName = FText::FromString(InInputParameterHandle);
	ParameterName = *InInputParameterHandle;
	InputType = InInputType;
	Owner = InOwner;
	ParameterStore = InParameterStore;
}

const FNiagaraTypeDefinition& UNiagaraStackParameterStoreEntry::GetInputType() const
{
	return InputType;
}

void UNiagaraStackParameterStoreEntry::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	RefreshValueAndHandle();

	if (ValueObject.IsValid() && ValueObject->IsA<UNiagaraDataInterface>())
	{
		if(ValueObjectEntry == nullptr || ValueObjectEntry->GetObject() != ValueObject)
		{
			ValueObjectEntry = NewObject<UNiagaraStackObject>(this);
			ValueObjectEntry->Initialize(CreateDefaultChildRequiredData(), ValueObject.Get(), GetOwnerStackItemEditorDataKey());
		}
		NewChildren.Add(ValueObjectEntry);
	}
	else
	{
		ValueObjectEntry = nullptr;
	}
}

void UNiagaraStackParameterStoreEntry::RefreshValueAndHandle()
{
	TSharedPtr<FNiagaraVariable> CurrentValueVariable = GetCurrentValueVariable();
	if (CurrentValueVariable.IsValid() && CurrentValueVariable->GetType() == InputType && CurrentValueVariable->IsDataAllocated())
	{
		if (LocalValueStruct.IsValid() == false || LocalValueStruct->GetStruct() != CurrentValueVariable->GetType().GetScriptStruct())
		{
			LocalValueStruct = MakeShared<FStructOnScope>(InputType.GetScriptStruct());
		}
		CurrentValueVariable->CopyTo(LocalValueStruct->GetStructMemory());
	}
	else
	{
		LocalValueStruct.Reset();
	}

	ValueObject = GetCurrentValueObject();

	ValueChangedDelegate.Broadcast();
}

FText UNiagaraStackParameterStoreEntry::GetDisplayName() const
{
	return DisplayName;
}

TSharedPtr<FStructOnScope> UNiagaraStackParameterStoreEntry::GetValueStruct()
{
	return LocalValueStruct;
}

UObject* UNiagaraStackParameterStoreEntry::GetValueObject()
{
	return ValueObject.Get();
}

void UNiagaraStackParameterStoreEntry::NotifyBeginValueChange()
{
	GEditor->BeginTransaction(LOCTEXT("ModifyInputValue", "Modify input value."));
	Owner->Modify();
}

void UNiagaraStackParameterStoreEntry::NotifyEndValueChange()
{
	if (GEditor->IsTransactionActive())
	{
		GEditor->EndTransaction();
	}
}

void UNiagaraStackParameterStoreEntry::NotifyValueChanged()
{
	TSharedPtr<FNiagaraVariable> CurrentValue = GetCurrentValueVariable();
	if ((CurrentValue.IsValid() && LocalValueStruct.IsValid()) && FNiagaraEditorUtilities::DataMatches(*CurrentValue.Get(), *LocalValueStruct.Get()))
	{
		return;
	}
	else if ((CurrentValue.IsValid() && LocalValueStruct.IsValid()))
	{
		FNiagaraVariable DefaultVariable(InputType, ParameterName);
		ParameterStore->SetParameterData(LocalValueStruct->GetStructMemory(), DefaultVariable);
	}
}

bool UNiagaraStackParameterStoreEntry::CanReset() const
{
	return true;
}

void UNiagaraStackParameterStoreEntry::Reset()
{
	NotifyBeginValueChange();
	FNiagaraVariable Var (InputType, ParameterName);
	if (InputType.GetClass() == nullptr)
	{
		FNiagaraEditorUtilities::ResetVariableToDefaultValue(Var);
		Var.CopyTo(LocalValueStruct->GetStructMemory()); 
		ParameterStore->SetParameterData(LocalValueStruct->GetStructMemory(), Var);
	}
	else
	{
		if (Var.IsDataInterface())
		{
			UNiagaraDataInterface* DefaultObject = NewObject<UNiagaraDataInterface>(this, const_cast<UClass*>(InputType.GetClass()));
			DefaultObject->CopyTo(ParameterStore->GetDataInterface(Var));
		}
		else if (Var.IsUObject())
		{
			ParameterStore->SetUObject(nullptr, Var);
		}
	}
	RefreshValueAndHandle();
	RefreshChildren();
	NotifyEndValueChange();
	GetSystemViewModel()->ResetSystem();
}

TArray<UEdGraphPin*> UNiagaraStackParameterStoreEntry::GetOwningPins()
{
	TArray<UNiagaraGraph*> GraphsToCheck;
	// search system graph
	UNiagaraScript* SystemScript = GetSystemViewModel()->GetSystem().GetSystemSpawnScript();
	if (SystemScript != nullptr)
	{
		UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(SystemScript->GetSource());
		if (ScriptSource != nullptr)
		{
			UNiagaraGraph* SystemGraph = ScriptSource->NodeGraph;
			if (SystemGraph != nullptr)
			{
				GraphsToCheck.Add(SystemGraph);
			}
		}
	}

	// search emitter graphs
	auto EmitterHandles = GetSystemViewModel()->GetSystem().GetEmitterHandles();
	for (FNiagaraEmitterHandle Handle : EmitterHandles)
	{
		UNiagaraGraph* EmitterGraph = CastChecked<UNiagaraScriptSource>(Handle.GetInstance()->GraphSource)->NodeGraph;
		GraphsToCheck.Add(EmitterGraph);
	}
	TArray<UEdGraphPin*> OwningPins;
	for (UNiagaraGraph* Graph : GraphsToCheck)
	{
		TArray<UNiagaraNodeParameterMapGet*> MapReadNodes;
		Graph->GetNodesOfClass<UNiagaraNodeParameterMapGet>(MapReadNodes);
		for (UNiagaraNode* Node : MapReadNodes)
		{
			for (UEdGraphPin* GraphPin : Node->Pins)
			{
				if (GraphPin->GetName() == ParameterName.ToString())
				{
					OwningPins.Add(GraphPin);
					break;
				}
			}
		}
	}
	return OwningPins;
}

void UNiagaraStackParameterStoreEntry::OnRenamed(FText NewName)
{
	FString ActualNameString = NewName.ToString();
	FString NamespacePrefix = FNiagaraConstants::UserNamespace.ToString() + ".";
	if (ActualNameString.Contains(NamespacePrefix))
	{
		ActualNameString = ActualNameString.Replace(*NamespacePrefix, TEXT(""));
	}
	FName ActualName = FName(*ActualNameString);
	// what if it's not user namespace? dehardcode.
	FNiagaraParameterHandle ParameterHandle(FNiagaraConstants::UserNamespace, ActualName); 
	FName VariableName = ParameterHandle.GetParameterHandleString();
	if (VariableName != ParameterName)
	{
		// destroy links, rename parameter and rebuild links
		TArray<UEdGraphPin*> OwningPins = GetOwningPins();
		TArray<UEdGraphPin*> LinkedPins;
		for (UEdGraphPin* GraphPin : OwningPins)
		{
			for (UEdGraphPin* OverridePin : GraphPin->LinkedTo)
			{
				LinkedPins.Add(OverridePin);
			}
		}

		FScopedTransaction ScopedTransaction(LOCTEXT("RenameUserParameter", "Rename user parameter"));
		Owner->Modify();
		// remove old one (a bit overkill but it beats duplicating code)
		RemovePins(OwningPins);
		// TODO Would it be better to actually keep variable name being ActualName and ParameterName being ParameterHandle.GetParameterHandleString(), and rewrite the way the Entry is built?
		ParameterStore->RenameParameter(FNiagaraVariable(InputType, ParameterName), VariableName); 
		// rebuild all links
		for (UEdGraphPin* LinkedPin : LinkedPins)
		{
			// remove links
			TArray<TWeakObjectPtr<UNiagaraDataInterface>> RemovedDataObjects;
			FNiagaraStackGraphUtilities::RemoveNodesForStackFunctionInputOverridePin(*LinkedPin, RemovedDataObjects); // no need to broadcast data objects modified here, the graph will recompile
			// generate current link
			FNiagaraStackGraphUtilities::SetLinkedValueHandleForFunctionInput(*LinkedPin, ParameterHandle);
		}

		ParameterName = VariableName;
		DisplayName = FText::FromName(ParameterName);
	}
}

void UNiagaraStackParameterStoreEntry::ReplaceValueObject(UObject* Obj)
{
	NotifyBeginValueChange();
	FNiagaraVariable Var(InputType, ParameterName);
	if (Obj == nullptr || Obj->GetClass()->IsChildOf(GetInputType().GetClass()))
	{
		ParameterStore->SetUObject(Obj, Var);

		RefreshValueAndHandle();
		RefreshChildren();
	}
	NotifyEndValueChange();
	GetSystemViewModel()->ResetSystem();
}


void UNiagaraStackParameterStoreEntry::Delete()
{
	FScopedTransaction ScopedTransaction(LOCTEXT("RemoveUserParameter", "Remove user parameter"));

	// for delete, do a parameter map traversal to deduce all the usages and then  remove them 
	TArray<UEdGraphPin*> OwningPins = GetOwningPins();
	RemovePins(OwningPins);

	// Cache these here since the entry will be finalized when the owning parameter store changes.
	TSharedPtr<FNiagaraSystemViewModel> CachedSystemViewModel = GetSystemViewModel();
	UNiagaraDataInterface* DataInterface = Cast<UNiagaraDataInterface>(ValueObject.Get());

	//remove from store
	Owner->Modify();
	ParameterStore->RemoveParameter(FNiagaraVariable(InputType, ParameterName));

	// Notify the system view model that the DI has been modifier.
	if (CachedSystemViewModel.IsValid() && DataInterface != nullptr)
	{
		CachedSystemViewModel->NotifyDataObjectChanged(DataInterface);
	}
}

void UNiagaraStackParameterStoreEntry::RemovePins(TArray<UEdGraphPin*> OwningPins /*, bool bSetPreviousValue*/)
{
	for (UEdGraphPin* GraphPin : OwningPins)
	{
		UNiagaraGraph* Graph = CastChecked<UNiagaraGraph>(GraphPin->GetOwningNode()->GetGraph());
	
		if (GraphPin->LinkedTo.Num() != 0)
		{
			// remember output of pin 
			UEdGraphPin* OverridePin = GraphPin->LinkedTo[0];
			//break old pin links
			GraphPin->BreakAllPinLinks();
			
			//now set value of pin output to the value of the GetCurrentValueVariable()
			const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
			//FNiagaraVariable Var = Schema->PinToNiagaraVariable(GraphPin, true); // use this instead of GetCurrentValueVariable() for default value
			FString PinDefaultValue;
			if (InputType.GetClass() == nullptr)
			{
				if (Schema->TryGetPinDefaultValueFromNiagaraVariable(*GetCurrentValueVariable(), PinDefaultValue))
				{
					OverridePin->DefaultValue = PinDefaultValue;
				}
			}
			else
			{
				if (InputType.IsDataInterface())
				{
					UNiagaraDataInterface* CurrentValueDataInterface = Cast<UNiagaraDataInterface>(GetCurrentValueObject());
					if (CurrentValueDataInterface != nullptr)
					{
						UNiagaraDataInterface* OverrideObj;
						FNiagaraStackGraphUtilities::SetDataValueObjectForFunctionInput(*OverridePin, const_cast<UClass*>(InputType.GetClass()), InputType.GetClass()->GetName(), OverrideObj);
						CurrentValueDataInterface->CopyTo(OverrideObj);
					}
				}
			}
		}
		// now also remove node
		Graph->RemoveNode(GraphPin->GetOwningNode());
		Graph->NotifyGraphNeedsRecompile();
	}
}

UNiagaraStackParameterStoreEntry::FOnValueChanged& UNiagaraStackParameterStoreEntry::OnValueChanged()
{
	return ValueChangedDelegate;
}

TSharedPtr<FNiagaraVariable> UNiagaraStackParameterStoreEntry::GetCurrentValueVariable()
{
	if (InputType.GetClass() == nullptr)
	{
		const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
		
		FNiagaraVariable DefaultVariable(InputType, ParameterName);
		const uint8* Data = ParameterStore->GetParameterData(DefaultVariable);
		DefaultVariable.SetData(Data);
		return MakeShared<FNiagaraVariable>(DefaultVariable);
	}
	return TSharedPtr<FNiagaraVariable>();
}

UObject* UNiagaraStackParameterStoreEntry::GetCurrentValueObject()
{
	if (InputType.GetClass() != nullptr)
	{
		FNiagaraVariable DefaultVariable(InputType, ParameterName);
		if (DefaultVariable.IsDataInterface())
		{
			return ParameterStore->GetDataInterface(DefaultVariable);
		}
		else if (DefaultVariable.IsUObject())
		{
			return ParameterStore->GetUObject(DefaultVariable);
		}
	}
	return nullptr;
}

bool UNiagaraStackParameterStoreEntry::IsUniqueName(FString NewName)
{
	FString NamespacePrefix = FNiagaraConstants::UserNamespace.ToString() + "."; // correcting name of variable for comparison, all user variables start with "User."
	if (!NewName.Contains(NamespacePrefix))
	{
		NewName = NamespacePrefix + NewName;
	}
	TArray<FNiagaraVariable> Variables;
	ParameterStore->GetParameters(Variables);
	// check for duplicates, but exclude self from search
	for (auto parameter : Variables)
	{
		if (parameter.GetName().ToString() == NewName)
		{
			if (GetCurrentValueVariable().IsValid())
			{
				if (parameter != *GetCurrentValueVariable())
				{
					return false;
				}
			}
			else if (GetCurrentValueObject() != nullptr)
			{
				if (ParameterStore->GetDataInterface(parameter) != GetCurrentValueObject())
				{
					return false;
				}
			}
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
