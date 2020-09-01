// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tools/Modes.h"
#include "EditorStyleSet.h"

FEditorModeInfo::FEditorModeInfo()
	: ID(NAME_None)
	, bVisible(false)
	, PriorityOrder(MAX_int32)
{
}

FEditorModeInfo::FEditorModeInfo(
	FEditorModeID InID,
	FText InName,
	FSlateIcon InIconBrush,
	bool InIsVisible,
	int32 InPriorityOrder
	)
	: ID(InID)
	, ToolbarCustomizationName(*(InID.ToString() + TEXT("Toolbar")))
	, Name(InName)
	, IconBrush(InIconBrush)
	, bVisible(InIsVisible)
	, PriorityOrder(InPriorityOrder)
{
	if (!InIconBrush.IsSet())
	{
		IconBrush = FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.EditorModes");
	}
}