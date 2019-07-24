// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEditorModule.h"
#include "Modules/ModuleInterface.h"
#include "Delegates/IDelegateInstance.h"
#include "UObject/ObjectKey.h"
#include "Templates/SharedPointer.h"

class FNiagaraStackCurveEditorOptions
{
public:
	FNiagaraStackCurveEditorOptions();

	float GetViewMinInput() const;
	float GetViewMaxInput() const;
	void SetInputViewRange(float InViewMinInput, float InViewMaxInput);

	float GetViewMinOutput() const;
	float GetViewMaxOutput() const;
	void SetOutputViewRange(float InViewMinOutput, float InViewMaxOutput);

	bool GetAreCurvesVisible() const;
	void SetAreCurvesVisible(bool bInAreCurvesVisible);

	float GetTimelineLength() const;

	float GetHeight() const;
	void SetHeight(float InHeight);

private:
	float ViewMinInput;
	float ViewMaxInput;
	float ViewMinOutput;
	float ViewMaxOutput;
	bool bAreCurvesVisible;
	float Height;
};

class UObject;

/** A module containing widgets for editing niagara data. */
class FNiagaraEditorWidgetsModule : public IModuleInterface
{
private:
	class FNiagaraEditorWidgetProvider : public INiagaraEditorWidgetProvider
	{
	public:
		virtual TSharedRef<SWidget> CreateStackView(UNiagaraStackViewModel& StackViewModel) override;
		virtual TSharedRef<SWidget> CreateSystemOverview(TSharedRef<FNiagaraSystemViewModel> SystemViewModel) override;
	};

public:
	// IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TSharedRef<FNiagaraStackCurveEditorOptions> GetOrCreateStackCurveEditorOptionsForObject(UObject* Object, bool bDefaultAreCurvesVisible, float DefaultHeight);

private:
	void ReinitializeStyle();

private:
	TMap<FObjectKey, TSharedRef<FNiagaraStackCurveEditorOptions>> ObjectToStackCurveEditorOptionsMap;

	TSharedPtr<FNiagaraEditorWidgetProvider> WidgetProvider;

	IConsoleCommand* ReinitializeStyleCommand;
};