// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UEdMode.h"
#include "UObject/Object.h"
#include "GizmoEdMode.generated.h"

UCLASS()
class GIZMOEDMODE_API UGizmoEdModeSettings : public UObject
{
	GENERATED_BODY()
};

UCLASS()
class GIZMOEDMODE_API UGizmoEdMode : public UEdMode
{
	GENERATED_BODY()
public:
	UGizmoEdMode();
private:
	void Enter() override;
	void Exit() override;
};
