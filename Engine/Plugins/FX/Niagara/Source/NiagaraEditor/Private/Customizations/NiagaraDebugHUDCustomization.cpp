// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDebugHUDCustomization.h"
#include "Widgets/SNiagaraDebugger.h"

//#include "CoreMinimal.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
//#include "IDetailChildrenBuilder.h"
//#include "NiagaraConstants.h"
//#include "NiagaraConstants.h"
//#include "NiagaraEditorStyle.h"
//#include "NiagaraEmitter.h"
//#include "NiagaraNodeOutput.h"
//#include "NiagaraNodeParameterMapBase.h"
//#include "NiagaraParameterMapHistory.h"
//#include "NiagaraPlatformSet.h"
//#include "NiagaraRendererProperties.h"
//#include "NiagaraScriptSource.h"
//#include "NiagaraScriptVariable.h"
//#include "NiagaraSystem.h"
//#include "NiagaraTypes.h"
//#include "PlatformInfo.h"
//#include "PropertyHandle.h"
//#include "SGraphActionMenu.h"
//#include "Scalability.h"
//#include "ScopedTransaction.h"
//#include "DeviceProfiles/DeviceProfile.h"
//#include "DeviceProfiles/DeviceProfileManager.h"
//#include "Framework/Application/SlateApplication.h"
//#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
//#include "Widgets/Input/SComboButton.h"
//#include "Widgets/Input/STextComboBox.h"
//#include "Widgets/Layout/SWrapBox.h"
//#include "NiagaraSimulationStageBase.h"
//#include "Widgets/Text/STextBlock.h"
//#include "NiagaraDataInterfaceRW.h"
//#include "NiagaraSettings.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "NiagaraDebugHUDCustomization"

void FNiagaraDebugHUDVariableCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	EnabledPropertyHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FNiagaraDebugHUDVariable, bEnabled));
	NamePropertyHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FNiagaraDebugHUDVariable, Name));
	check(EnabledPropertyHandle.IsValid() && NamePropertyHandle.IsValid())

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(200.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked(this, &FNiagaraDebugHUDVariableCustomization::IsEnabled)
				.OnCheckStateChanged(this, &FNiagaraDebugHUDVariableCustomization::SetEnabled)
			]
			+ SHorizontalBox::Slot()
			[
				SNew(SEditableTextBox)
				.IsEnabled(this, &FNiagaraDebugHUDVariableCustomization::IsTextEditable)
				.Text(this, &FNiagaraDebugHUDVariableCustomization::GetText)
				.OnTextCommitted(this, &FNiagaraDebugHUDVariableCustomization::SetText)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
}

ECheckBoxState FNiagaraDebugHUDVariableCustomization::IsEnabled() const
{
	bool bEnabled = false;
	EnabledPropertyHandle->GetValue(bEnabled);
	return bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FNiagaraDebugHUDVariableCustomization::SetEnabled(ECheckBoxState NewState)
{
	bool bEnabled = NewState == ECheckBoxState::Checked;
	EnabledPropertyHandle->SetValue(bEnabled);
}

FText FNiagaraDebugHUDVariableCustomization::GetText() const
{
	FString Text;
	NamePropertyHandle->GetValue(Text);
	return FText::FromString(Text);
}

void FNiagaraDebugHUDVariableCustomization::SetText(const FText& NewText, ETextCommit::Type CommitInfo)
{
	NamePropertyHandle->SetValue(NewText.ToString());
}

bool FNiagaraDebugHUDVariableCustomization::IsTextEditable() const
{
	bool bEnabled = false;
	EnabledPropertyHandle->GetValue(bEnabled);
	return bEnabled;
}

#undef LOCTEXT_NAMESPACE
