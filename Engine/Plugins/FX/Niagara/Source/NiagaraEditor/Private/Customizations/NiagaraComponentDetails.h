// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "NiagaraComponent.h"

class IDetailLayoutBuilder;
class IPropertyHandle;
class FNiagaraParameterViewModelCustomDetails;
class INiagaraParameterViewModel;
class SNiagaraParameterEditor;
class FNiagaraParameterViewModelCustomDetails;
class UNiagaraSystem;

class FNiagaraComponentDetails : public IDetailCustomization
{
public:
	virtual ~FNiagaraComponentDetails() override;

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;

protected:
	void OnWorldDestroyed(class UWorld* InWorld);
	void OnPiEEnd();

	FReply OnResetSelectedSystem();
	FReply OnDebugSelectedSystem();
private:
	TWeakObjectPtr<UNiagaraComponent> Component;
	IDetailLayoutBuilder* Builder = nullptr;
};

class FNiagaraSystemUserParameterDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;

protected:
	TWeakObjectPtr<UNiagaraSystem> System;	
};
