// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "NiagaraSystem.h"

class IDetailLayoutBuilder;

/*-----------------------------------------------------------------------------
FNiagaraEmitterDetails
-----------------------------------------------------------------------------*/

class FNiagaraEmitterDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance(UNiagaraSystem* System);

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailLayout) override;

private:
	FNiagaraEmitterDetails(UNiagaraSystem* InSystem) : System(InSystem) { }

	UNiagaraSystem* System;
};

