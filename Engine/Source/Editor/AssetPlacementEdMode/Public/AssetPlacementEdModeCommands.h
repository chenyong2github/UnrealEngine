// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class FAssetPlacementEdModeCommands : public TCommands<FAssetPlacementEdModeCommands>
{
public:
	FAssetPlacementEdModeCommands();

	virtual void RegisterCommands() override;
	static TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> GetCommands();

	TSharedPtr<FUICommandInfo> Select;
	TSharedPtr<FUICommandInfo> SelectAll;
	TSharedPtr<FUICommandInfo> Deselect;
	TSharedPtr<FUICommandInfo> SelectInvalid;
	TSharedPtr<FUICommandInfo> LassoSelect;
	TSharedPtr<FUICommandInfo> Paint;
	TSharedPtr<FUICommandInfo> Reapply;
	TSharedPtr<FUICommandInfo> PlaceSingle;
	TSharedPtr<FUICommandInfo> Fill;
	TSharedPtr<FUICommandInfo> Erase;
	TSharedPtr<FUICommandInfo> Delete;
	TSharedPtr<FUICommandInfo> MoveToCurrentLevel;

protected:
	TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> Commands;
};
