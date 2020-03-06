// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEditorCommon.h"
#include "Templates/SharedPointer.h"
#include "Types/SlateEnums.h"
#include "Misc/Attribute.h"

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
	virtual TSharedRef<SWidget> CreateTextSlotWidget() const = 0;

	virtual int32 GetScopeValue() const = 0;
	virtual void OnScopeValueChanged(int32 NewScopeValue, ESelectInfo::Type SelectionType) const = 0;
	virtual FText GetToolTipForScopeValue(int32 ScopeValue) const = 0;
	virtual bool GetScopeValueIsEnabled(int32 ScopeValue) const = 0;

	virtual FText GetParameterNameText() const = 0;
	//bool VerifyParameterNameChanged(const FText& NewNameText, FText&) //@todo(ng) impl
	virtual void OnParameterRenamed(const FText& NewNameText, ETextCommit::Type SelectionType) const = 0;
};

class FNiagaraParameterPanelEntryParameterNameViewModel : public INiagaraParameterNameViewModel
{
public:
	DECLARE_DELEGATE_ThreeParams(FOnScopeSelectionChanged, const FNiagaraVariable&, const FNiagaraVariableMetaData&, const ENiagaraParameterScope)
	DECLARE_DELEGATE_ThreeParams(FOnParameterRenamed, const FNiagaraVariable&, const FNiagaraVariableMetaData&, const FText&)

	FNiagaraParameterPanelEntryParameterNameViewModel(FCreateWidgetForActionData* const InCreateData, const FNiagaraScriptVariableAndViewInfo& InScriptVarAndViewInfo);

	/** Begin INiagaraParameterNameViewModel Interface */
	virtual TSharedRef<SWidget> CreateScopeSlotWidget() const override;
	virtual TSharedRef<SWidget> CreateTextSlotWidget() const override;

	virtual int32 GetScopeValue() const override;
	virtual void OnScopeValueChanged(int32 NewScopeValue, ESelectInfo::Type SelectionType) const override;
	virtual FText GetToolTipForScopeValue(int32 ScopeValue) const override;
	virtual bool GetScopeValueIsEnabled(int32 ScopeValue) const override;

	virtual FText GetParameterNameText() const override;
	//bool VerifyParameterNameChanged(const FText& NewNameText, FText&) //@todo(ng) impl
	virtual void OnParameterRenamed(const FText& NewNameText, ETextCommit::Type SelectionType) const override;
	/** End INiagaraParameterNameViewModel Interface */

	FOnScopeSelectionChanged& GetOnScopeSelectionChangedDelegate() { return OnScopeSelectionChangedDelegate; };
	FOnParameterRenamed& GetOnParameterRenamedDelegate() { return OnParameterRenamedDelegate; };

private:
	FOnScopeSelectionChanged OnScopeSelectionChangedDelegate;
	FOnParameterRenamed OnParameterRenamedDelegate;

	FCreateWidgetForActionData* const CreateData;
	const FNiagaraScriptVariableAndViewInfo CachedScriptVarAndViewInfo;

	TAttribute<bool> bIsReadOnly; //@todo(ng) debug
};

class FNiagaraGraphPinParameterNameViewModel : public INiagaraParameterNameViewModel
{
public:
	FNiagaraGraphPinParameterNameViewModel(
		  const UEdGraphPin* InOwningPin
		, const FNiagaraScriptVariableAndViewInfo& InScriptVarAndViewInfo
		, const FNiagaraScriptToolkitParameterPanelViewModel* InParameterPanelViewModel
	);

	/** Begin INiagaraParameterNameViewModel Interface */
	virtual TSharedRef<SWidget> CreateScopeSlotWidget() const override;
	virtual TSharedRef<SWidget> CreateTextSlotWidget() const override;

	virtual int32 GetScopeValue() const override;
	virtual void OnScopeValueChanged(int32 NewScopeValue, ESelectInfo::Type SelectionType) const override;
	virtual FText GetToolTipForScopeValue(int32 ScopeValue) const override;
	virtual bool GetScopeValueIsEnabled(int32 ScopeValue) const override;

	virtual FText GetParameterNameText() const override;
	//bool VerifyParameterNameChanged(const FText& NewNameText, FText&) //@todo(ng) impl
	virtual void OnParameterRenamed(const FText& NewNameText, ETextCommit::Type SelectionType) const override;
	/** End INiagaraParameterNameViewModel Interface */

private:
	const UEdGraphPin* OwningPin;
	const FNiagaraScriptVariableAndViewInfo CachedScriptVarAndViewInfo;
	const FNiagaraScriptToolkitParameterPanelViewModel* ParameterPanelViewModel;

	TAttribute<bool> bIsReadOnly; //@todo(ng) debug
};
