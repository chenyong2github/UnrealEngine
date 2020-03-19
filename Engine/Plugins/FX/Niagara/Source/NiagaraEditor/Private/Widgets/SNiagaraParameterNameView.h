// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Types/SlateEnums.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

struct FNiagaraScriptVariableAndViewInfo;
struct FSlateFontInfo;
struct FCreateWidgetForActionData;
class INiagaraParameterNameViewModel;

struct FCreateParameterNameViewInfo
{
	FCreateParameterNameViewInfo() = delete;

	FCreateParameterNameViewInfo(const FNiagaraScriptVariableAndViewInfo& InScriptVarAndViewInfo, const FSlateFontInfo& InNameFont, bool bInIsReadOnly)
		: ScriptVarAndViewInfo(InScriptVarAndViewInfo)
		, NameFont(InNameFont)
		, bIsReadOnly(bInIsReadOnly)
	{};

	FCreateParameterNameViewInfo(const FNiagaraScriptVariableAndViewInfo& InScriptVarAndViewInfo, const FSlateFontInfo& InNameFont, bool bInIsReadOnly, const FCreateWidgetForActionData* const InCreateData)
		: ScriptVarAndViewInfo(InScriptVarAndViewInfo)
		, NameFont(InNameFont)
		, bIsReadOnly(bInIsReadOnly)
		, CreateData(InCreateData)
	{};

	const FNiagaraScriptVariableAndViewInfo& ScriptVarAndViewInfo;
	const FSlateFontInfo& NameFont;
	bool bIsReadOnly;
	TOptional<const FCreateWidgetForActionData* const> CreateData;
};

/** A widget for viewing and editing FNiagaraVariable names by separating their names into a namespace scope SEnumComboBox and name SInlineEditableTextBox. */
class SNiagaraParameterNameView : public SCompoundWidget
{
public:

	/** Scope ComboBox Delegates */
	DECLARE_DELEGATE_RetVal(int32, FOnGetScopeValue);
	DECLARE_DELEGATE_TwoParams(FOnScopeValueChanged, int32, ESelectInfo::Type);
	DECLARE_DELEGATE_RetVal_OneParam(FText, FOnGetToolTipForScopeValue, int32 /* Value */)
	DECLARE_DELEGATE_RetVal_OneParam(bool, FOnIsScopeValueEnabled, int32 /* Value */)

	/** Name InlineEditableTextBox Delegates */
	DECLARE_DELEGATE_RetVal(FText, FOnGetParameterNameText);
	DECLARE_DELEGATE_RetVal_TwoParams(bool, FOnVerifyParameterNameChanged, const FText&, FText&)
	DECLARE_DELEGATE_TwoParams(FOnParameterRenamed, const FText&, const struct FNiagaraScriptVarAndViewInfoAction&)


	SLATE_BEGIN_ARGS(SNiagaraParameterNameView)
	{}
		/** Scope ComboBox Delegates */
		SLATE_EVENT(FOnGetScopeValue, OnGetScopeValue)
		SLATE_EVENT(FOnScopeValueChanged, OnScopeValueChanged)
		SLATE_EVENT(FOnGetToolTipForScopeValue, OnGetToolTipForScopeValue)
		SLATE_EVENT(FOnIsScopeValueEnabled, OnIsScopeValueEnabled)

		/** Name InlineEditableTextBox Delegates */
		SLATE_EVENT(FOnGetParameterNameText, OnGetParameterNameText)
		SLATE_EVENT(FOnVerifyParameterNameChanged, OnVerifyParameterNameChanged)
		SLATE_EVENT(FOnParameterRenamed, OnParameterRenamed)
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, const TSharedPtr<class INiagaraParameterNameViewModel>& InParameterNameViewModel);
	void SetPendingRename(bool bInPendingRename) { bPendingRename = bInPendingRename; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

protected:
	TSharedPtr<INiagaraParameterNameViewModel> ParameterNameViewModel;

	/** Scope ComboBox Delegates */
	FOnGetScopeValue OnGetScopeValue;
	FOnScopeValueChanged OnScopeValueChanged;
	FOnGetToolTipForScopeValue OnGetToolTipForScopeValue;
	FOnIsScopeValueEnabled OnIsScopeValueEnabled;

	/** Name InlineEditableTextBox Delegates */
	FOnGetParameterNameText OnGetParameterNameText;
	FOnVerifyParameterNameChanged OnVerifyParameterNameChanged;
	FOnParameterRenamed OnParameterRenamed;

	int32 GetScopeValue() const;
	FText GetTooltipForScopeValue(int32 Value) const;
	bool GetScopeValueIsEnabled(int32 Value) const;
	FText GetParameterNameText() const;

	TSharedPtr<SWidget> ScopeSlotWidget;
	TSharedPtr<SInlineEditableTextBlock> NameSlotWidget;
	bool bPendingRename = false;
};
