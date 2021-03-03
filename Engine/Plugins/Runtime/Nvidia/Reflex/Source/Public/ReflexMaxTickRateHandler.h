// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Performance/MaxTickRateHandlerModule.h"

class FReflexMaxTickRateHandler : public IMaxTickRateHandlerModule
{
public:
	virtual ~FReflexMaxTickRateHandler() {}

	virtual void Initialize() override;
	virtual void SetEnabled(bool bInEnabled) override { bEnabled = bInEnabled; }
	virtual bool GetEnabled() override { return bEnabled; }

	// Used to provide a generic customization interface for custom tick rate handlers
	virtual void SetFlags(uint32 Flags);
	virtual uint32 GetFlags();

	virtual bool HandleMaxTickRate(float DesiredMaxTickRate) override;

	bool bEnabled = false;
	bool bWasEnabled = false;
	bool bProperDriverVersion = false;
	float MinimumInterval = -1.0f;
	bool bUltraLowLatency = true;
	bool bGPUBoost = true;

	uint32 CustomFlags = 0;
	uint32 LastCustomFlags = 0;
};