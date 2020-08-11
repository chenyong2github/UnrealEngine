// Copyright Epic Games, Inc. All Rights Reserved.

#include "StageMonitorEditorStyle.h"

#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"



FStageMonitorEditorStyle::FStageMonitorEditorStyle()
	: FSlateStyleSet("StageMonitorEditorStyle")
{
	SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	// ListView
	{
		const FVector2D Icon8x8(8.0f, 8.0f);
		FTableRowStyle CriticalStateRow(FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row"));
		CriticalStateRow.SetEvenRowBackgroundBrush(FSlateColorBrush(FLinearColor(0.2f, 0.0f, 0.0f, 0.1f)))
			.SetOddRowBackgroundBrush(FSlateColorBrush(FLinearColor(0.2f, 0.0f, 0.0f, 0.1f)));

		Set("TableView.CriticalStateRow", CriticalStateRow);
	}
	
	FSlateStyleRegistry::RegisterSlateStyle(*this);

#undef IMAGE_BRUSH
}

FStageMonitorEditorStyle::~FStageMonitorEditorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

FStageMonitorEditorStyle& FStageMonitorEditorStyle::Get()
{
	static FStageMonitorEditorStyle Inst;
	return Inst;
}


