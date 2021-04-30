// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEditorCommon.h"
#include "Templates/SharedPointer.h"
#include "Types/SlateEnums.h"
#include "Misc/Attribute.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

struct FCreateWidgetForActionData;
class FNiagaraScriptToolkitParameterPanelViewModel;
class SWidget;
struct FNiagaraScriptVariableAndViewInfo;
class UEdGraphPin;

/** Interface for view models to the parameter name view widget. */
class INiagaraParameterNameViewModel : public TSharedFromThis<INiagaraParameterNameViewModel>
{
public:
	virtual ~INiagaraParameterNameViewModel() { }

	virtual TSharedRef<SWidget> CreateScopeSlotWidget() const = 0;
	virtual TSharedRef<SInlineEditableTextBlock> CreateTextSlotWidget() const = 0;

	virtual int32 GetScopeValue() const = 0;
	virtual void OnScopeValueChanged(int32 NewScopeValue, ESelectInfo::Type SelectionType) const = 0;
	virtual FText GetToolTipForScopeValue(int32 ScopeValue) const = 0;
	virtual bool GetScopeValueIsEnabled(int32 ScopeValue) const = 0;

	virtual FText GetParameterNameText() const = 0;
	virtual bool VerifyParameterNameChanged(const FText& NewNameText, FText& OutErrorText) const = 0;
	virtual void OnParameterRenamed(const FText& NewNameText, ETextCommit::Type SelectionType) const = 0;
};

class FNiagaraParameterPanelEntryParameterNameViewModel : public INiagaraParameterNameViewModel
{
public:
	DECLARE_DELEGATE_ThreeParams(FOnScopeSelectionChanged, const FNiagaraVariable&, const FNiagaraVariableMetaData&, const ENiagaraParameterScope)
	DECLARE_DELEGATE_ThreeParams(FOnParameterRenamed, const FNiagaraVariable&, const FNiagaraVariableMetaData&, const FText&)
	DECLARE_DELEGATE_RetVal_FourParams(bool, FOnVerifyParameterRenamed, const FNiagaraVariable&, const FNiagaraVariableMetaData&, TOptional<const FText> , FText&)

	FNiagaraParameterPanelEntryParameterNameViewModel(const FNiagaraScriptVariableAndViewInfo& InScriptVarAndViewInfo);

	/** Begin INiagaraParameterNameViewModel Interface */
	virtual TSharedRef<SWidget> CreateScopeSlotWidget() const override;
	virtual TSharedRef<SInlineEditableTextBlock> CreateTextSlotWidget() const override;

	virtual int32 GetScopeValue() const override;
	virtual void OnScopeValueChanged(int32 NewScopeValue, ESelectInfo::Type SelectionType) const override;
	virtual FText GetToolTipForScopeValue(int32 ScopeValue) const override;
	virtual bool GetScopeValueIsEnabled(int32 ScopeValue) const override;

	virtual FText GetParameterNameText() const override;
	virtual bool VerifyParameterNameChanged(const FText& NewNameText, FText& OutErrorText) const override;
	virtual void OnParameterRenamed(const FText& NewNameText, ETextCommit::Type SelectionType) const override;
	/** End INiagaraParameterNameViewModel Interface */

	FOnScopeSelectionChanged& GetOnScopeSelectionChangedDelegate() { return OnScopeSelectionChangedDelegate; };
	FOnParameterRenamed& GetOnParameterRenamedDelegate() { return OnParameterRenamedDelegate; };
	FOnVerifyParameterRenamed& GetOnVerifyParameterRenamedDelegate() { return OnVerifyParameterRenamedDelegate; };

private:
	FOnScopeSelectionChanged OnScopeSelectionChangedDelegate;
	FOnParameterRenamed OnParameterRenamedDelegate;
	FOnVerifyParameterRenamed OnVerifyParameterRenamedDelegate;

	//FCreateWidgetForActionData* const CreateData;
	const FNiagaraScriptVariableAndViewInfo CachedScriptVarAndViewInfo;

	TAttribute<bool> bIsReadOnly; //@todo(ng) debug
};

class FNiagaraGraphPinParameterNameViewModel : public INiagaraParameterNameViewModel
{
public:
	FNiagaraGraphPinParameterNameViewModel(
		  UEdGraphPin* InOwningPin
		, const FNiagaraScriptVariableAndViewInfo& InScriptVarAndViewInfo
		, const FNiagaraScriptToolkitParameterPanelViewModel* InParameterPanelViewModel
	);

	/** Begin INiagaraParameterNameViewModel Interface */
	virtual TSharedRef<SWidget> CreateScopeSlotWidget() const override;
	virtual TSharedRef<SInlineEditableTextBlock> CreateTextSlotWidget() const override;

	virtual int32 GetScopeValue() const override;
	virtual void OnScopeValueChanged(int32 NewScopeValue, ESelectInfo::Type SelectionType) const override;
	virtual FText GetToolTipForScopeValue(int32 ScopeValue) const override;
	virtual bool GetScopeValueIsEnabled(int32 ScopeValue) const override;

	virtual FText GetParameterNameText() const override;
	virtual bool VerifyParameterNameChanged(const FText& NewNameText, FText& OutErrorText) const override;
	virtual void OnParameterRenamed(const FText& NewNameText, ETextCommit::Type SelectionType) const override;
	/** End INiagaraParameterNameViewModel Interface */

private:
	UEdGraphPin* OwningPin;
	const FNiagaraScriptVariableAndViewInfo CachedScriptVarAndViewInfo;
	const FNiagaraScriptToolkitParameterPanelViewModel* ParameterPanelViewModel;

	TAttribute<bool> bIsReadOnly; //@todo(ng) debug
};
