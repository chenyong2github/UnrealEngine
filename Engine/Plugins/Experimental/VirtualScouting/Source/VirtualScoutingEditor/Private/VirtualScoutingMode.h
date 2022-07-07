// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VREditorMode.h"
#include "VirtualScoutingMode.generated.h"


UCLASS()
class UVirtualScoutingMode : public UVREditorMode
{
	GENERATED_BODY()

public:
	//~ Begin UVREditorMode interface
	virtual bool NeedsSyntheticDpad() override;

	virtual void Enter() override;
	//~ End UVREditorMode interface
};
