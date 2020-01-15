// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraAnimationSequenceActions.h"
#include "CameraAnimationSequence.h"
#include "CineCameraActor.h"
#include "TemplateSequenceEditorToolkit.h"

#define LOCTEXT_NAMESPACE "AssetTypeActions"

FCameraAnimationSequenceActions::FCameraAnimationSequenceActions(const TSharedRef<ISlateStyle>& InStyle)
    : FTemplateSequenceActions(InStyle)
{
}

FText FCameraAnimationSequenceActions::GetName() const
{
    return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_CameraAnimationSequence", "Camera Animation Sequence");
}

UClass* FCameraAnimationSequenceActions::GetSupportedClass() const
{
    return UCameraAnimationSequence::StaticClass();
}

void FCameraAnimationSequenceActions::InitializeToolkitParams(FTemplateSequenceToolkitParams& ToolkitParams) const
{
    FTemplateSequenceActions::InitializeToolkitParams(ToolkitParams);

    ToolkitParams.bCanChangeBinding = false;
    ToolkitParams.InitialBindingClass = ACineCameraActor::StaticClass();
}

#undef LOCTEXT_NAMESPACE
