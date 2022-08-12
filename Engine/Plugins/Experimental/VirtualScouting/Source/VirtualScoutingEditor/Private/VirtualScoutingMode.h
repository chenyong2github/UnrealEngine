// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VREditorMode.h"
#include "VirtualScoutingMode.generated.h"


UCLASS()
class UVirtualScoutingMode : public UVREditorMode
{
	GENERATED_BODY()

public:
	UVirtualScoutingMode(const FObjectInitializer& ObjectInitializer);

	//~ Begin UVREditorMode interface
	virtual bool NeedsSyntheticDpad() override;

	virtual bool ShouldDisplayExperimentalWarningOnEntry() const override { return false; }

	virtual void Enter() override;
	//~ End UVREditorMode interface

protected:
	bool ValidateSettings();
};
