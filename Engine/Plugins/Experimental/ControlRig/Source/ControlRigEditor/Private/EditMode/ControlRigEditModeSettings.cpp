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

}

