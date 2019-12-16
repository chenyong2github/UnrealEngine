// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
 
#pragma once
 
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "IPropertyUtilities.h"
#include "EditorUndoClient.h"
 
class UEdGraphPin;

/** This customization sets up a custom details panel for the static switch Variable in the niagara module graph. */
class FNiagaraScriptVariableDetails : public IDetailCustomization, public FEditorUndoClient
{
public:
 
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();
 
	~FNiagaraScriptVariableDetails();
	FNiagaraScriptVariableDetails();
 
	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;
	
	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
	// End of FEditorUndoClient
 
private:
	void OnBeginValueChanged();
	void OnEndValueChanged();
	void OnValueChanged();
	void OnStaticSwitchValueChanged();

	UEdGraphPin* GetDefaultPin();

	TWeakObjectPtr<class UNiagaraScriptVariable> Variable;
	TSharedPtr<class INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> TypeUtilityValue;
	TSharedPtr<class INiagaraEditorTypeUtilities, ESPMode::ThreadSafe> TypeUtilityStaticSwitchValue;
	TSharedPtr<class SNiagaraParameterEditor> ParameterEditorValue;
	TSharedPtr<class SNiagaraParameterEditor> ParameterEditorStaticSwitchValue;
};