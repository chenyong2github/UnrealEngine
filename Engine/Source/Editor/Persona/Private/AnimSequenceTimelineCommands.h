// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/Commands.h"

/**
 * Defines commands for the anim sequence timeline editor
 */
class FAnimSequenceTimelineCommands : public TCommands<FAnimSequenceTimelineCommands>
{
public:
	FAnimSequenceTimelineCommands()
		: TCommands<FAnimSequenceTimelineCommands>
		(
			TEXT("AnimSequenceCurveEditor"),
			NSLOCTEXT("Contexts", "AnimSequenceTimelineEditor", "Anim Sequence Timeline Editor"),
			NAME_None,
			FEditorStyle::GetStyleSetName()
		)
	{
	}

	TSharedPtr<FUICommandInfo> EditSelectedCurves;

	TSharedPtr<FUICommandInfo> RemoveSelectedCurves;

	TSharedPtr<FUICommandInfo> AddNotifyTrack;

	TSharedPtr<FUICommandInfo> InsertNotifyTrack;

	TSharedPtr<FUICommandInfo> RemoveNotifyTrack;

	TSharedPtr<FUICommandInfo> AddCurve;

	TSharedPtr<FUICommandInfo> EditCurve;

	TSharedPtr<FUICommandInfo> ShowCurveKeys;

	TSharedPtr<FUICommandInfo> AddMetadata;

	TSharedPtr<FUICommandInfo> ConvertCurveToMetaData;

	TSharedPtr<FUICommandInfo> ConvertMetaDataToCurve;

	TSharedPtr<FUICommandInfo> RemoveCurve;

	TSharedPtr<FUICommandInfo> RemoveAllCurves;

	TSharedPtr<FUICommandInfo> DisplaySeconds;

	TSharedPtr<FUICommandInfo> DisplayFrames;

	TSharedPtr<FUICommandInfo> DisplayPercentage;

	TSharedPtr<FUICommandInfo> DisplaySecondaryFormat;

	TSharedPtr<FUICommandInfo> SnapToFrames;

	TSharedPtr<FUICommandInfo> SnapToNotifies;

	TSharedPtr<FUICommandInfo> SnapToMontageSections;

	TSharedPtr<FUICommandInfo> SnapToCompositeSegments;

public:
	virtual void RegisterCommands() override;
};
