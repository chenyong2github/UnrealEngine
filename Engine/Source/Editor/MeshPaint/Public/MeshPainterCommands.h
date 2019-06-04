// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

/** Base set of mesh painter commands */
class MESHPAINT_API FMeshPainterCommands : public TCommands<FMeshPainterCommands>
{
public:
	FMeshPainterCommands();

	/**
	* Initialize commands
	*/
	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> IncreaseBrushRadius;
	TSharedPtr<FUICommandInfo> DecreaseBrushRadius;

	TSharedPtr<FUICommandInfo> IncreaseBrushStrength;
	TSharedPtr<FUICommandInfo> DecreaseBrushStrength;

	TSharedPtr<FUICommandInfo> IncreaseBrushFalloff;
	TSharedPtr<FUICommandInfo> DecreaseBrushFalloff;

	TArray<TSharedPtr<FUICommandInfo>> Commands;
};