// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/DelegateCombinations.h"
#include "Internationalization/Text.h"
#include "Textures/SlateIcon.h"
#include "UObject/NameTypes.h"

/** The shorthand identifier used for editor modes */
typedef FName FEditorModeID;

struct FEditorModeInfo
{
	/** Default constructor */
	UNREALED_API FEditorModeInfo();

	/** Helper constructor */
	UNREALED_API FEditorModeInfo(
		FEditorModeID InID,
		FText InName = FText(),
		FSlateIcon InIconBrush = FSlateIcon(),
		bool bInIsVisible = false,
		int32 InPriorityOrder = MAX_int32
		);

	/** The mode ID */
	FEditorModeID ID;

	/** Name of the toolbar this mode uses and can be used by external systems to customize that mode toolbar */
	FName ToolbarCustomizationName;

	/** Name for the editor to display */
	FText Name;

	/** The mode icon */
	FSlateIcon IconBrush;

	/** Whether or not the mode should be visible in the mode menu */
	bool bVisible;

	/** The priority of this mode which will determine its default order and shift+X command assignment */
	int32 PriorityOrder;
};

DECLARE_MULTICAST_DELEGATE(FRegisteredModesChangedEvent);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnModeRegistered, FEditorModeID);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnModeUnregistered, FEditorModeID);
