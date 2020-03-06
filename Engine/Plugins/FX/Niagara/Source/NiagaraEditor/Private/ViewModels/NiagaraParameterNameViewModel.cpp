// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraParameterNameViewModel.h"
#include "EditorWidgets/Public/SEnumCombobox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "SGraphActionMenu.h"
#include "NiagaraActions.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "NiagaraNode.h"
#include "NiagaraParameterPanelViewModel.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEditorModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "EdGraph/EdGraphPin.h"
#include "NiagaraEditorCommon.h"

///////////////////////////////////////////////////////////////////////////////
/// Parameter Panel Entry Parameter Name ViewModel							///
///////////////////////////////////////////////////////////////////////////////

FNiagaraParameterPanelEntryParameterNameViewModel::FNiagaraParameterPanelEntryParameterNameViewModel(FCreateWidgetForActionData* const InCreateData, const FNiagaraScriptVariableAndViewInfo& InScriptVarAndViewInfo)
	: CreateData(InCreateData)
	, CachedScriptVarAndViewInfo(InScriptVarAndViewInfo)
{};

TSharedRef<SWidget> FNiagaraParameterPanelEntryParameterNameViewModel::CreateScopeSlotWidget() const
{
	if (CachedScriptVarAndViewInfo.MetaData.GetIsUsingLegacyNameString())
	{
		return SNullWidget::NullWidget;
	}

	ENiagaraParameterScope CachedScope;
	CachedScriptVarAndViewInfo.MetaData.GetScope(CachedScope);
	UEnum* ParameterScopeEnum = FNiagaraTypeDefinition::GetParameterScopeEnum();
	TSharedPtr< SWidget > ScopeComboBoxWidget;
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");

	bool bEnableScopeSlotWidget = true;
	if (CachedScriptVarAndViewInfo.MetaData.GetUsage() != ENiagaraScriptParameterUsage::Input)
	{
		bEnableScopeSlotWidget = false;
	}
	else if (CachedScope == ENiagaraParameterScope::Local)
	{
		bEnableScopeSlotWidget = false;
	}
	else if (CachedScriptVarAndViewInfo.MetaData.GetIsStaticSwitch())
	{
		bEnableScopeSlotWidget = false;
	}

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

TSharedRef<SWidget> FNiagaraParameterPanelEntryParameterNameViewModel::CreateTextSlotWidget() const
{
	TSharedPtr< SWidget > DisplayWidget;
	const FName FontType = FName("Italic");
	FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle(FontType, 10); //@todo(ng) get correct font

	SAssignNew(DisplayWidget, SInlineEditableTextBlock)
		.Text(this, &FNiagaraParameterPanelEntryParameterNameViewModel::GetParameterNameText)
		.Font(NameFont)
		.HighlightText(CreateData->HighlightText)
		.OnTextCommitted(this, &FNiagaraParameterPanelEntryParameterNameViewModel::OnParameterRenamed)
		//.OnVerifyTextChanged(InArgs._OnVerifyParameterNameChanged) //@todo(ng) impl
		.IsSelected(CreateData->IsRowSelectedDelegate)
		.IsReadOnly(bIsReadOnly);

	//CreateData->OnRenameRequest->BindSP(static_cast<SInlineEditableTextBlock*>(DisplayWidget.Get()), &SInlineEditableTextBlock::EnterEditingMode); //@Todo(ng) figure out a way to make this work

	return DisplayWidget.ToSharedRef();
}

int32 FNiagaraParameterPanelEntryParameterNameViewModel::GetScopeValue() const
{
	ENiagaraParameterScope CachedScope;
	if (ensureMsgf(CachedScriptVarAndViewInfo.MetaData.GetScope(CachedScope), TEXT("Failed to get scope value for param as override namespace is set! This method should not be bound!")))
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

void FNiagaraParameterPanelEntryParameterNameViewModel::OnParameterRenamed(const FText& NewNameText, ETextCommit::Type TextCommitType) const
{
	OnParameterRenamedDelegate.ExecuteIfBound(CachedScriptVarAndViewInfo.ScriptVariable, CachedScriptVarAndViewInfo.MetaData, NewNameText);
}

///////////////////////////////////////////////////////////////////////////////
/// EdGraphPin Parameter Name ViewModel										///
///////////////////////////////////////////////////////////////////////////////

FNiagaraGraphPinParameterNameViewModel::FNiagaraGraphPinParameterNameViewModel(
	const UEdGraphPin* InOwningPin
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

	bool bEnableScopeSlotWidget = OwningPin->Direction == EEdGraphPinDirection::EGPD_Output;

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

TSharedRef<SWidget> FNiagaraGraphPinParameterNameViewModel::CreateTextSlotWidget() const
{
	TSharedPtr< SWidget > DisplayWidget;
	const FName FontType = FName("Italic");
	FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle(FontType, 10); //@todo(ng) get correct style

	SAssignNew(DisplayWidget, SInlineEditableTextBlock)
		.Text(this, &FNiagaraGraphPinParameterNameViewModel::GetParameterNameText)
		.Font(NameFont)
		.OnTextCommitted(this, &FNiagaraGraphPinParameterNameViewModel::OnParameterRenamed)
		//.OnVerifyTextChanged(InArgs._OnVerifyParameterNameChanged) //@todo(ng) impl
		.IsReadOnly(bIsReadOnly);

	return DisplayWidget.ToSharedRef();
}

int32 FNiagaraGraphPinParameterNameViewModel::GetScopeValue() const
{
	ENiagaraParameterScope CachedScope;
	if (ensureMsgf(CachedScriptVarAndViewInfo.MetaData.GetScope(CachedScope), TEXT("Failed to get scope value for param as override namespace is set! This method should not be bound!")))
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

void FNiagaraGraphPinParameterNameViewModel::OnParameterRenamed(const FText& NewNameText, ETextCommit::Type SelectionType) const
{
	ParameterPanelViewModel->RenamePin(OwningPin, NewNameText);
}
