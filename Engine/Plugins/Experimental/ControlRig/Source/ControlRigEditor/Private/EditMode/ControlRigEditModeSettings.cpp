// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigEditModeSettings.h"
#include "EditorModeManager.h"
#include "ControlRigEditMode.h"
#include "Sequencer/ControlRigSequence.h"
#include "Rigs/RigControlHierarchy.h"

#include "Components/SkeletalMeshComponent.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "MovieSceneCommonHelpers.h"


void UControlRigEditModeSettings::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

}

void UControlRigEditModeSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
#if WITH_EDITOR
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UControlRigEditModeSettings, GizmoScale))
	{
		FEditorModeTools& Tools = GLevelEditorModeTools();
		Tools.SetWidgetScale(GizmoScale);
	}
#endif

}

#if WITH_EDITOR
void UControlRigEditModeSettings::PostEditUndo()
{
	FEditorModeTools& Tools = GLevelEditorModeTools();
	Tools.SetWidgetScale(GizmoScale);
}
#endif

