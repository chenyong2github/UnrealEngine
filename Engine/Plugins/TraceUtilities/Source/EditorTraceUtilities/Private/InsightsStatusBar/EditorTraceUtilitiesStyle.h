// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/StyleColors.h"

class FEditorTraceUtilitiesStyle final
	: public FSlateStyleSet
{
public:
	FEditorTraceUtilitiesStyle()
		: FSlateStyleSet("EditorTraceUtilitiesStyle")
	{
		const FVector2D Icon10x10(10.0f, 10.0f);
		const FVector2D Icon16x16(16.0f, 16.0f);
		const FVector2D Icon20x20(20.0f, 20.0f);
		const FVector2D Icon24x24(24.0f, 24.0f);
		const FVector2D Icon32x32(32.0f, 32.0f);
		const FVector2D Icon40x40(40.0f, 40.0f);

		SetContentRoot(FPaths::EnginePluginsDir() / TEXT("/TraceUtilities/Content/Slate"));
		SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

		Set("Icons.OpenLiveSession", new CORE_IMAGE_BRUSH_SVG("Starship/Insights/Session", Icon16x16));

		Set("Icons.Trace", new IMAGE_BRUSH_SVG("Trace", Icon16x16));
		Set("Icons.RecordTrace", new IMAGE_BRUSH_SVG("RecordTrace", Icon16x16));
		Set("Icons.RecordTraceOutline", new IMAGE_BRUSH_SVG("RecordTraceOutline", Icon16x16));
		Set("Icons.RecordTraceWithOutline", new IMAGE_BRUSH_SVG("RecordTraceWithOutline", Icon16x16));
		Set("Icons.RecordTraceStop", new IMAGE_BRUSH_SVG("RecordTraceStop", Icon16x16, FStyleColors::Error));
		Set("Icons.StartTrace", new IMAGE_BRUSH_SVG("StartTrace", Icon16x16));
		Set("Icons.TraceSnapshot", new IMAGE_BRUSH_SVG("TraceSnapshot", Icon16x16));

		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}

	static FEditorTraceUtilitiesStyle& Get()
	{
		static FEditorTraceUtilitiesStyle Inst;
		return Inst;
	}
	
	~FEditorTraceUtilitiesStyle()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	}
};
