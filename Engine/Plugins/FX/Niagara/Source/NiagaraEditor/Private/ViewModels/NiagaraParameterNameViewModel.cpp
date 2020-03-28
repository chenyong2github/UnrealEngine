// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraParameterNameViewModel.h"
#include "EditorWidgets/Public/SEnumCombobox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "SGraphActionMenu.h"
#include "NiagaraActions.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraNode.h"
#include "ViewModels/NiagaraParameterPanelViewModel.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEditorModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "EdGraph/EdGraphPin.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraConstants.h"

///////////////////////////////////////////////////////////////////////////////
/// Parameter Panel Entry Parameter Name ViewModel							///
///////////////////////////////////////////////////////////////////////////////

FNiagaraParameterPanelEntryParameterNameViewModel::FNiagaraParameterPanelEntryParameterNameViewModel(/*FCreateWidgetForActionData* const InCreateData,*/ const FNiagaraScriptVariableAndViewInfo& InScriptVarAndViewInfo)
	/*: CreateData(InCreateData)*/
	: CachedScriptVarAndViewInfo(InScriptVarAndViewInfo)
{};

TSharedRef<SWidget> FNiagaraParameterPanelEntryParameterNameViewModel::CreateScopeSlotWidget() const
{
	if (CachedScriptVarAndViewInfo.MetaData.GetIsUsingLegacyNameString())
	{
		return SNullWidget::NullWidget;
	}

	bool bEnableScopeSlotWidget = true;
	if (CachedScriptVarAndViewInfo.MetaData.GetIsStaticSwitch())
	{
		bEnableScopeSlotWidget = false;
	}
	else if (!FNiagaraEditorUtilities::IsScopeEditable(CachedScriptVarAndViewInfo.MetaData.GetScopeName()))
	{
		bEnableScopeSlotWidget = false;
	}

	UEnum* ParameterScopeEnum = FNiagaraTypeDefinition::GetParameterScopeEnum();
	TSharedPtr< SWidget > ScopeComboBoxWidget;
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");

	SAssignNew(ScopeComboBoxWidget, SBox)
		.MinDesiredWidth(80.0f) //@todo(ng) tune and make const static
		.Content()
		[
			SNew(SEnumComboBox, ParameterScopeEnum)
			.IsEnabled(bEnableScopeSlotWidget)
			//.ForegroundColor(NiagaraEditorModule.GetWidgetProvider()->GetColorForParameterScope(CachedScriptVarAndViewInfo.MetaData.Scope)) //@todo(ng) get background color instead
			.CurrentValue(this, &FNiagaraParameterPanelEntryParameterNameViewModel::GetScopeValue)
			.OnEnumSelectionChanged(this, &FNiagaraParameterPanelEntryParameterNameViewModel::OnScopeValueChanged)
			.OnGetToolTipForValue(this, &FNiagaraParameterPanelEntryParameterNameViewModel::GetToolTipForScopeValue)
			//.OnIsValueEnabled(this, &FNiagaraParameterPanelEntryParameterNameViewModel::GetScopeValueIsEnabled) //@todo(ng) impl
		];

	return ScopeComboBoxWidget.ToSharedRef();
}

TSharedRef<SInlineEditableTextBlock> FNiagaraParameterPanelEntryParameterNameViewModel::CreateTextSlotWidget() const
{
	TSharedPtr< SInlineEditableTextBlock > DisplayWidget;
	const FName FontType = FName("Italic");
	FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle(FontType, 10); //@todo(ng) get correct font

	SAssignNew(DisplayWidget, SInlineEditableTextBlock)
		.Text(this, &FNiagaraParameterPanelEntryParameterNameViewModel::GetParameterNameText)
		.Font(NameFont)
		/*.HighlightText(CreateData->HighlightText)*/
		.OnTextCommitted(this, &FNiagaraParameterPanelEntryParameterNameViewModel::OnParameterRenamed)
		.OnVerifyTextChanged(this, &FNiagaraParameterPanelEntryParameterNameViewModel::VerifyParameterNameChanged)
		/*.IsSelected(CreateData->IsRowSelectedDelegate)*/
		.IsReadOnly(bIsReadOnly);

	/*CreateData->OnRenameRequest->BindSP(static_cast<SInlineEditableTextBlock*>(DisplayWidget.Get()), &SInlineEditableTextBlock::EnterEditingMode);*/

	return DisplayWidget.ToSharedRef();
}

int32 FNiagaraParameterPanelEntryParameterNameViewModel::GetScopeValue() const
{
	if (CachedScriptVarAndViewInfo.MetaData.GetIsStaticSwitch())
	{
		return int32(ENiagaraParameterScope::DISPLAY_ONLY_StaticSwitch);
	}

	ENiagaraParameterScope CachedScope;
	if (ensureMsgf(FNiagaraEditorUtilities::GetVariableMetaDataScope(CachedScriptVarAndViewInfo.MetaData, CachedScope), TEXT("Failed to get scope value for param as override namespace is set! This method should not be bound!")))
	{
		return (int32)CachedScope;
	}
	return int32(ENiagaraParameterScope::Custom);
}

void FNiagaraParameterPanelEntryParameterNameViewModel::OnScopeValueChanged(int32 NewScopeValue, ESelectInfo::Type SelectionType) const
{
	OnScopeSelectionChangedDelegate.ExecuteIfBound(CachedScriptVarAndViewInfo.ScriptVariable, CachedScriptVarAndViewInfo.MetaData, ENiagaraParameterScope(NewScopeValue));
}

FText FNiagaraParameterPanelEntryParameterNameViewModel::GetToolTipForScopeValue(int32 ScopeValue) const
{
	return CachedScriptVarAndViewInfo.ParameterScopeToDisplayInfo[ScopeValue].Tooltip;
}

bool FNiagaraParameterPanelEntryParameterNameViewModel::GetScopeValueIsEnabled(int32 ScopeValue) const
{
	return CachedScriptVarAndViewInfo.ParameterScopeToDisplayInfo[ScopeValue].bEnabled;
}

FText FNiagaraParameterPanelEntryParameterNameViewModel::GetParameterNameText() const
{
	if (CachedScriptVarAndViewInfo.MetaData.GetIsUsingLegacyNameString())
	{
		return FText::FromName(CachedScriptVarAndViewInfo.ScriptVariable.GetName());
	}
	FName ParameterName;
	CachedScriptVarAndViewInfo.MetaData.GetParameterName(ParameterName);
	return FText::FromName(ParameterName);
}

bool FNiagaraParameterPanelEntryParameterNameViewModel::VerifyParameterNameChanged(const FText& NewNameText, FText& OutErrorText) const
{
	if (OnVerifyParameterRenamedDelegate.IsBound())
	{
		return OnVerifyParameterRenamedDelegate.Execute(CachedScriptVarAndViewInfo.ScriptVariable, CachedScriptVarAndViewInfo.MetaData, NewNameText, OutErrorText);
	}
	ensureMsgf(false, TEXT("Verify parameter renamed delegate was not bound!"));
	return true;
}

void FNiagaraParameterPanelEntryParameterNameViewModel::OnParameterRenamed(const FText& NewNameText, ETextCommit::Type TextCommitType) const
{
	OnParameterRenamedDelegate.ExecuteIfBound(CachedScriptVarAndViewInfo.ScriptVariable, CachedScriptVarAndViewInfo.MetaData, NewNameText);
}

///////////////////////////////////////////////////////////////////////////////
/// EdGraphPin Parameter Name ViewModel										///
///////////////////////////////////////////////////////////////////////////////

FNiagaraGraphPinParameterNameViewModel::FNiagaraGraphPinParameterNameViewModel(
	UEdGraphPin* InOwningPin
	, const FNiagaraScriptVariableAndViewInfo& InScriptVarAndViewInfo
	, const FNiagaraScriptToolkitParameterPanelViewModel* InParameterPanelViewModel
)
	: OwningPin(InOwningPin)
	, CachedScriptVarAndViewInfo(InScriptVarAndViewInfo)
	, ParameterPanelViewModel(InParameterPanelViewModel)
{};

TSharedRef<SWidget> FNiagaraGraphPinParameterNameViewModel::CreateScopeSlotWidget() const
{
	// If using legacy name mode, skip the scope widget as we only need a text slot widget.
	if (CachedScriptVarAndViewInfo.MetaData.GetIsUsingLegacyNameString())
	{
		return SNullWidget::NullWidget;
	}

	UEnum* ParameterScopeEnum = FNiagaraTypeDefinition::GetParameterScopeEnum();
	TSharedPtr< SWidget > ScopeComboBoxWidget;
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");

	bool bEnableScopeSlotWidget = false;// OwningPin->Direction == EEdGraphPinDirection::EGPD_Output;

	SAssignNew(ScopeComboBoxWidget, SBox)
		.MinDesiredWidth(80.0f) //@todo(ng) tune and make const static
		.Content()
		[
			SNew(SEnumComboBox, ParameterScopeEnum)
			.IsEnabled(bEnableScopeSlotWidget)
			//.ForegroundColor(NiagaraEditorModule.GetWidgetProvider()->GetColorForParameterScope(CachedScriptVarAndViewInfo.MetaData.Scope)) //@todo(ng) get background color instead
			.CurrentValue(this, &FNiagaraGraphPinParameterNameViewModel::GetScopeValue)
			.OnEnumSelectionChanged(this, &FNiagaraGraphPinParameterNameViewModel::OnScopeValueChanged)
			.OnGetToolTipForValue(this, &FNiagaraGraphPinParameterNameViewModel::GetToolTipForScopeValue)
			//.OnIsValueEnabled(this, &FNiagaraGraphPinParameterNameViewModel::GetScopeValueIsEnabled) //@todo(ng) impl
		];

	return ScopeComboBoxWidget.ToSharedRef();
}

TSharedRef<SInlineEditableTextBlock> FNiagaraGraphPinParameterNameViewModel::CreateTextSlotWidget() const
{
	TSharedPtr< SInlineEditableTextBlock > DisplayWidget;
	const FName FontType = FName("Italic");
	FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle(FontType, 10); //@todo(ng) get correct style

	SAssignNew(DisplayWidget, SInlineEditableTextBlock)
		.Text(this, &FNiagaraGraphPinParameterNameViewModel::GetParameterNameText)
		.Font(NameFont)
		.OnTextCommitted(this, &FNiagaraGraphPinParameterNameViewModel::OnParameterRenamed)
		.OnVerifyTextChanged(this, &FNiagaraGraphPinParameterNameViewModel::VerifyParameterNameChanged)
		.IsReadOnly(bIsReadOnly);

	return DisplayWidget.ToSharedRef();
}

int32 FNiagaraGraphPinParameterNameViewModel::GetScopeValue() const
{
	if (CachedScriptVarAndViewInfo.MetaData.GetIsStaticSwitch())
	{
		return int32(ENiagaraParameterScope::DISPLAY_ONLY_StaticSwitch);
	}

	ENiagaraParameterScope CachedScope;
	if (ensureMsgf(FNiagaraEditorUtilities::GetVariableMetaDataScope(CachedScriptVarAndViewInfo.MetaData, CachedScope), TEXT("Failed to get scope value for param as override namespace is set! This method should not be bound!")))
	{
		return (int32)CachedScope;
	}
	return int32(ENiagaraParameterScope::Custom);
}

void FNiagaraGraphPinParameterNameViewModel::OnScopeValueChanged(int32 NewScopeValue, ESelectInfo::Type SelectionType) const
{
	ParameterPanelViewModel->ChangePinScope(OwningPin, ENiagaraParameterScope(NewScopeValue));
}

FText FNiagaraGraphPinParameterNameViewModel::GetToolTipForScopeValue(int32 ScopeValue) const
{
	return CachedScriptVarAndViewInfo.ParameterScopeToDisplayInfo[ScopeValue].Tooltip;
}

bool FNiagaraGraphPinParameterNameViewModel::GetScopeValueIsEnabled(int32 ScopeValue) const
{
	return CachedScriptVarAndViewInfo.ParameterScopeToDisplayInfo[ScopeValue].bEnabled;
}

FText FNiagaraGraphPinParameterNameViewModel::GetParameterNameText() const
{
	if (CachedScriptVarAndViewInfo.MetaData.GetIsUsingLegacyNameString())
	{
		return FText::FromName(CachedScriptVarAndViewInfo.ScriptVariable.GetName());
	}
	FName ParameterName;
	CachedScriptVarAndViewInfo.MetaData.GetParameterName(ParameterName);
	return FText::FromName(ParameterName);
}

bool FNiagaraGraphPinParameterNameViewModel::VerifyParameterNameChanged(const FText& NewNameText, FText& OutErrorText) const
{
	return ParameterPanelViewModel->GetCanRenameParameterAndToolTip(CachedScriptVarAndViewInfo.ScriptVariable, CachedScriptVarAndViewInfo.MetaData, NewNameText, OutErrorText);
}

void FNiagaraGraphPinParameterNameViewModel::OnParameterRenamed(const FText& NewNameText, ETextCommit::Type SelectionType) const
{
	UNiagaraNode* Node = Cast<UNiagaraNode>(OwningPin->GetOwningNode());
	if (Node)
	{
		if (CachedScriptVarAndViewInfo.MetaData.GetIsUsingLegacyNameString())
		{
			Node->CommitEditablePinName(NewNameText, OwningPin);
		}
		else
		{
			FName OldFullName = CachedScriptVarAndViewInfo.ScriptVariable.GetName();
			FName OldParameterName;
			CachedScriptVarAndViewInfo.MetaData.GetParameterName(OldParameterName);

			FString OldParamNameStr = OldParameterName.ToString();
			FString NewFullNameStr = OldFullName.ToString();
			NewFullNameStr.RemoveFromEnd(OldParamNameStr);
			NewFullNameStr += NewNameText.ToString();

			FText NewFullNameText = FText::FromString(NewFullNameStr);

			Node->CommitEditablePinName(NewFullNameText, OwningPin);
		}
	}
	//ParameterPanelViewModel->RenamePin(OwningPin, NewNameText);
}
