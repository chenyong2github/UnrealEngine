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
	virtual void Enter() override;
	//~ End UVREditorMode interface
};
