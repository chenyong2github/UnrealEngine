// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
 
#include "NiagaraScriptVariableCustomization.h"
 
#include "NiagaraScriptVariable.h"
#include "NiagaraGraph.h"
#include "NiagaraEditorModule.h"
 
#include "SNiagaraParameterEditor.h"
 
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
 
#include "TypeEditorUtilities/NiagaraFloatTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraIntegerTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraEnumTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraBoolTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraFloatTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraVectorTypeEditorUtilities.h"
#include "TypeEditorUtilities/NiagaraColorTypeEditorUtilities.h"
#include "NiagaraEditorStyle.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "NiagaraScriptVariableVariableDetails"

TSharedRef<IDetailCustomization> FNiagaraScriptVariableDetails::MakeInstance()
{
	return MakeShareable(new FNiagaraScriptVariableDetails());
}
 
FNiagaraScriptVariableDetails::FNiagaraScriptVariableDetails()
{
	GEditor->RegisterForUndo(this);
}

FNiagaraScriptVariableDetails::~FNiagaraScriptVariableDetails()
{
	GEditor->UnregisterForUndo(this);
}

UEdGraphPin* FNiagaraScriptVariableDetails::GetDefaultPin()
{
	// TODO: We don't know the usage at this point, so we'll try each script type in order
	//       This could probably be made much more robust, but works for now.
	if (UNiagaraGraph* Graph = Cast<UNiagaraGraph>(Variable->GetOuter())) 
	{
		UEdGraphPin* Pin = Graph->FindParameterMapDefaultValuePin(Variable->Variable.GetName(), ENiagaraScriptUsage::Module, ENiagaraScriptUsage::Module);
		if (Pin == nullptr)
		{
			Pin = Graph->FindParameterMapDefaultValuePin(Variable->Variable.GetName(), ENiagaraScriptUsage::DynamicInput, ENiagaraScriptUsage::Module);
		}
		if (Pin == nullptr)
		{
			Pin = Graph->FindParameterMapDefaultValuePin(Variable->Variable.GetName(), ENiagaraScriptUsage::Function, ENiagaraScriptUsage::Module);
		}
		return Pin;
	}
	return nullptr;
}

void FNiagaraScriptVariableDetails::PostUndo(bool bSuccess)
{
	if (Variable == nullptr)
	{
		return;
	}

	if (Variable->Metadata.bIsStaticSwitch)
	{
		if (TypeUtilityStaticSwitchValue && ParameterEditorStaticSwitchValue)
		{
			TSharedPtr<FStructOnScope> ParameterValue = MakeShareable(new FStructOnScope(Variable->Variable.GetType().GetStruct()));
			Variable->Variable.SetValue(Variable->Metadata.StaticSwitchDefaultValue);
			Variable->Variable.CopyTo(ParameterValue->GetStructMemory());
			ParameterEditorStaticSwitchValue->UpdateInternalValueFromStruct(ParameterValue.ToSharedRef());
		}
	}
	else
	{
		if (UEdGraphPin* Pin = GetDefaultPin())
		{
			if (TypeUtilityValue && ParameterEditorValue)
			{
				TypeUtilityValue->SetValueFromPinDefaultString(Pin->DefaultValue, Variable->Variable);
				TSharedPtr<FStructOnScope> ParameterValue = MakeShareable(new FStructOnScope(Variable->Variable.GetType().GetStruct()));
				Variable->Variable.CopyTo(ParameterValue->GetStructMemory());
				ParameterEditorValue->UpdateInternalValueFromStruct(ParameterValue.ToSharedRef());
			}
		}
	}
}
 
void FNiagaraScriptVariableDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	CachedDetailBuilder = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FNiagaraScriptVariableDetails::OnComboValueChanged()
{
	IDetailLayoutBuilder* DetailBuilderPtr = nullptr;
	if (TSharedPtr<IDetailLayoutBuilder> LockedDetailBuilder = CachedDetailBuilder.Pin())
	{
		// WARNING: We do this because ForceRefresh will lock while pinning...
		DetailBuilderPtr = LockedDetailBuilder.Get();
	}
	if (DetailBuilderPtr != nullptr)
	{
		TSharedRef<IPropertyUtilities> PropertyUtilities = DetailBuilderPtr->GetPropertyUtilities();
		PropertyUtilities->ForceRefresh();
	}
}

void FNiagaraScriptVariableDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	static const FName CategoryName = TEXT("Default Value");
 
	TArray<TWeakObjectPtr<UObject>> ObjectsCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsCustomized);
 
	if (ObjectsCustomized.Num() != 1)
	{
		// TODO: Could we allow selecting multiple items in the future?
		return;
	}
	if (!ObjectsCustomized[0]->IsA<UNiagaraScriptVariable>())
	{
		return;
	}
 
	Variable = Cast<UNiagaraScriptVariable>(ObjectsCustomized[0].Get());
	if (Variable == nullptr)
	{
		return;
	}
 
	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(CategoryName);		
 	
 	// NOTE: Automatically generated widgets from UProperties are placed below custom properties by default. 
	//       In this case DefaultMode is just a built in combo box, while value widget is custom and added afterwards. 
	//       This guarantees that the combo box always show above the value widget instead of at the bottom of the window. 
	const TSharedPtr<IPropertyHandle> DefaultModeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UNiagaraScriptVariable, DefaultMode));
	DefaultModeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FNiagaraScriptVariableDetails::OnComboValueChanged));

	FNiagaraEditorModule& EditorModule = FNiagaraEditorModule::Get();
	if (Variable->Metadata.bIsStaticSwitch)
	{
		TypeUtilityStaticSwitchValue = EditorModule.GetTypeUtilities(Variable->Variable.GetType());
		if (TypeUtilityStaticSwitchValue && TypeUtilityStaticSwitchValue->CanCreateParameterEditor())
		{
			ParameterEditorStaticSwitchValue = TypeUtilityStaticSwitchValue->CreateParameterEditor(Variable->Variable.GetType());
			
			TSharedPtr<FStructOnScope> ParameterValue = MakeShareable(new FStructOnScope(Variable->Variable.GetType().GetStruct()));
			Variable->Variable.SetValue(Variable->Metadata.StaticSwitchDefaultValue);
			Variable->Variable.CopyTo(ParameterValue->GetStructMemory());
			ParameterEditorStaticSwitchValue->UpdateInternalValueFromStruct(ParameterValue.ToSharedRef());
			ParameterEditorStaticSwitchValue->SetOnValueChanged(SNiagaraParameterEditor::FOnValueChange::CreateSP(this, &FNiagaraScriptVariableDetails::OnStaticSwitchValueChanged));
 
			FDetailWidgetRow& DefaultValueWidget = CategoryBuilder.AddCustomRow(LOCTEXT("DefaultValueFilterText", "Default Value"));
			DefaultValueWidget
			.NameContent()
			[
				SNew(STextBlock)
				.Font(FNiagaraEditorStyle::Get().GetFontStyle("NiagaraEditor.ParameterFont"))
				.Text(FText::FromString(TEXT("Default Value")))
			]
			.ValueContent()
			.HAlign(HAlign_Fill)
			[
				ParameterEditorStaticSwitchValue.ToSharedRef()
			];
		}
		else
		{
			TypeUtilityStaticSwitchValue = nullptr;
		}
	}
	else
	{
		DetailBuilder.HideProperty(DefaultModeHandle);
		DetailBuilder.AddPropertyToCategory(DefaultModeHandle);

		if (UEdGraphPin* Pin = GetDefaultPin())
		{
			TypeUtilityValue = EditorModule.GetTypeUtilities(Variable->Variable.GetType());
			if (TypeUtilityValue && TypeUtilityValue->CanCreateParameterEditor() && Variable->DefaultMode == ENiagaraDefaultMode::Value)
			{
				ParameterEditorValue = TypeUtilityValue->CreateParameterEditor(Variable->Variable.GetType());

				TypeUtilityValue->SetValueFromPinDefaultString(Pin->DefaultValue, Variable->Variable);
				TSharedPtr<FStructOnScope> ParameterValue = MakeShareable(new FStructOnScope(Variable->Variable.GetType().GetStruct()));
				Variable->Variable.CopyTo(ParameterValue->GetStructMemory());
				ParameterEditorValue->UpdateInternalValueFromStruct(ParameterValue.ToSharedRef());
				ParameterEditorValue->SetOnValueChanged(SNiagaraParameterEditor::FOnValueChange::CreateSP(this, &FNiagaraScriptVariableDetails::OnValueChanged));
				ParameterEditorValue->SetOnBeginValueChange(SNiagaraParameterEditor::FOnValueChange::CreateSP(this, &FNiagaraScriptVariableDetails::OnBeginValueChanged));
				ParameterEditorValue->SetOnEndValueChange(SNiagaraParameterEditor::FOnValueChange::CreateSP(this, &FNiagaraScriptVariableDetails::OnEndValueChanged));
				
				FDetailWidgetRow& DefaultValueWidget = CategoryBuilder.AddCustomRow(LOCTEXT("DefaultValueFilterText", "Default Value"));
				DefaultValueWidget
				.NameContent()
				[
					SNew(STextBlock)
					.Font(FNiagaraEditorStyle::Get().GetFontStyle("NiagaraEditor.ParameterFont"))
					.Text(FText::FromString(TEXT("Default Value")))
				]
				.ValueContent()
				.HAlign(HAlign_Fill)
				[
					ParameterEditorValue.ToSharedRef()
				];
			}
			else
			{
				TypeUtilityValue = nullptr;
			}
		}
	}
	

	if (Variable->Metadata.bIsStaticSwitch) {
		// Hide metadata UProperties that aren't useful for static switch variables
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraScriptVariable, Metadata.EditCondition));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraScriptVariable, Metadata.VisibleCondition));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraScriptVariable, DefaultBinding));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraScriptVariable, DefaultMode));
		DetailBuilder.HideProperty(DefaultModeHandle); // TODO: Redundant?
	}
	else
	{
		if (Variable->DefaultMode != ENiagaraDefaultMode::Binding)
		{
			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraScriptVariable, DefaultBinding));
		}

		if (Variable->Variable.GetType() != FNiagaraTypeDefinition::GetBoolDef())
		{
			DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UNiagaraScriptVariable, Metadata.bInlineEditConditionToggle));
		}
	}
}
 
void FNiagaraScriptVariableDetails::OnValueChanged()
{

	if (TypeUtilityValue && ParameterEditorValue)
	{
		if (UEdGraphPin* Pin = GetDefaultPin())
		{
			if (!ParameterEditorValue->CanChangeContinuously())
			{
				const FScopedTransaction Transaction( NSLOCTEXT("ScriptVariableCustomization", "ChangeValue", "Change Default Value" ) );
				Pin->Modify();
				Variable->Modify();

				TSharedPtr<FStructOnScope> ParameterValue = MakeShareable(new FStructOnScope(Variable->Variable.GetType().GetStruct()));
				ParameterEditorValue->UpdateStructFromInternalValue(ParameterValue.ToSharedRef());
				Variable->Variable.SetData(ParameterValue->GetStructMemory());
				Pin->DefaultValue = TypeUtilityValue->GetPinDefaultStringFromValue(Variable->Variable);
			}		
			else
			{
				TSharedPtr<FStructOnScope> ParameterValue = MakeShareable(new FStructOnScope(Variable->Variable.GetType().GetStruct()));
				ParameterEditorValue->UpdateStructFromInternalValue(ParameterValue.ToSharedRef());
				Variable->Variable.SetData(ParameterValue->GetStructMemory());
				Pin->DefaultValue = TypeUtilityValue->GetPinDefaultStringFromValue(Variable->Variable);	
			}
			
		}
	} 
}
 

void FNiagaraScriptVariableDetails::OnBeginValueChanged()
{
	if (!ParameterEditorValue->CanChangeContinuously())
	{
		return;
	}

	if (TypeUtilityValue && ParameterEditorValue)
	{
		if (UEdGraphPin* Pin = GetDefaultPin())
		{
			GEditor->BeginTransaction(NSLOCTEXT("ScriptVariableCustomization", "ChangeValue", "Change Default Value"));
			Variable->Modify();
			Pin->Modify();
			TSharedPtr<FStructOnScope> ParameterValue = MakeShareable(new FStructOnScope(Variable->Variable.GetType().GetStruct()));
			ParameterEditorValue->UpdateStructFromInternalValue(ParameterValue.ToSharedRef());
			Variable->Variable.SetData(ParameterValue->GetStructMemory());
			Pin->DefaultValue = TypeUtilityValue->GetPinDefaultStringFromValue(Variable->Variable);
		}
	} 
}

void FNiagaraScriptVariableDetails::OnEndValueChanged()
{
	if (GEditor->IsTransactionActive())
	{
		GEditor->EndTransaction();
	}
}

void FNiagaraScriptVariableDetails::OnStaticSwitchValueChanged()
{
	if (TypeUtilityStaticSwitchValue && ParameterEditorStaticSwitchValue)
	{
		const FScopedTransaction Transaction(NSLOCTEXT("ScriptVariableCustomization", "ChangeStaticSwitchValue", "Change Static Switch Default Value"));
		Variable->Modify();
		TSharedPtr<FStructOnScope> ParameterValue = MakeShareable(new FStructOnScope(Variable->Variable.GetType().GetStruct()));
		ParameterEditorStaticSwitchValue->UpdateStructFromInternalValue(ParameterValue.ToSharedRef());
		Variable->Variable.SetData(ParameterValue->GetStructMemory());
		Variable->Metadata.StaticSwitchDefaultValue = Variable->Variable.GetValue<int>();
	} 
}
 
#undef LOCTEXT_NAMESPACE