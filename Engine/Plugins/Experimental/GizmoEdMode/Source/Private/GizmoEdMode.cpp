// Copyright Epic Games, Inc. All Rights Reserved.

#include "GizmoEdMode.h"
#include "EdModeInteractiveToolsContext.h"

#define LOCTEXT_NAMESPACE "FGizmoEdMode"

UGizmoEdMode::UGizmoEdMode()
	:Super()
{
	Info = FEditorModeInfo(
		FName(TEXT("GizmoMode")),
		LOCTEXT("ModeName", "Gizmo"),
		FSlateIcon(),
		false,
		600
	);
	SettingsClass = UGizmoEdModeSettings::StaticClass();
	ToolsContextClass = UEdModeInteractiveToolsContext::StaticClass();
}

void UGizmoEdMode::Enter()
{
	Super::Enter();
}

void UGizmoEdMode::Exit()
{
	Super::Exit();
}

#undef LOCTEXT_NAMESPACE
