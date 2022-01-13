// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRetargetSources.h"
#include "Widgets/SBoxPanel.h"
#include "SRetargetSourceWindow.h"
#include "IEditableSkeleton.h"
#include "PropertyCustomizationHelpers.h"

#define LOCTEXT_NAMESPACE "SRetargetSources"

void SRetargetSources::Construct(
	const FArguments& InArgs,
	const TSharedRef<IEditableSkeleton>& InEditableSkeleton,
	const TSharedRef<IPersonaPreviewScene>& InPreviewScene,
	FSimpleMulticastDelegate& InOnPostUndo)
{
	EditableSkeletonPtr = InEditableSkeleton;
	PreviewScenePtr = InPreviewScene;

	const FString DocLink = TEXT("Shared/Editors/Persona");
	ChildSlot
	[
		SNew (SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(2, 5)
		.FillHeight(0.5)
		[
			// construct retarget source window
			SNew(SRetargetSourceWindow, InEditableSkeleton, InOnPostUndo)
		]
	];
}

#undef LOCTEXT_NAMESPACE

