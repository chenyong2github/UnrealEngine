// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "Misc/Optional.h"

/** This customization sets up a custom details panel for the function call node in the niagara module graph. */
class FNiagaraFunctionCallNodeDetails : public IDetailCustomization
{
public:

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();
	
	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;
	
private:
	TWeakObjectPtr<class UNiagaraNodeFunctionCall> Node;
};
