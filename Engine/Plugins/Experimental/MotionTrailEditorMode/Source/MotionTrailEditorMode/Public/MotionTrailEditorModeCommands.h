// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/Commands/UICommandInfo.h"
#include "Framework/Commands/Commands.h"
#include "EditorStyleSet.h"

namespace UE
{
namespace MotionTrailEditor
{

class FMotionTrailEditorModeCommands : public TCommands<FMotionTrailEditorModeCommands>
{
public:
	FMotionTrailEditorModeCommands()
		: TCommands<FMotionTrailEditorModeCommands>(
			"MotionTrail",
			NSLOCTEXT("MotionTrailEditorMode", "MotionTrailEditingModeCommands", "Motion Trail Editing Mode"),
			NAME_None,
			FEditorStyle::GetStyleSetName()
		)
	{}

	/**
	* Initialize commands
	*/
	virtual void RegisterCommands() override;

	static void RegisterDynamic(const FName InName, const TArray<TSharedPtr<FUICommandInfo>>& InCommands);
	static void UnRegisterDynamic(const FName InName);

	static const TMap<FName, TArray<TSharedPtr<FUICommandInfo>>>& GetCommands()
	{
		return FMotionTrailEditorModeCommands::Get().Commands;
	}

public:
	TSharedPtr<FUICommandInfo> Default;

	TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> Commands;
};

} // namespace MovieScene
} // namespace UE
