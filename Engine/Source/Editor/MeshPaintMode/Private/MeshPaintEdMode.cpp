// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshPaintEdMode.h"
#include "EdMode.h"
#include "MeshPaintModeToolKit.h"
#include "EditorModeManager.h"

#include "PaintModePainter.h"

#define LOCTEXT_NAMESPACE "EdModeMeshPaint"

void FEdModeMeshPaint::Initialize()
{
	MeshPainter = FPaintModePainter::Get();
}

TSharedPtr<class FModeToolkit> FEdModeMeshPaint::GetToolkit()
{
	return MakeShareable(new FMeshPaintModeToolKit(this));
}

bool FEdModeMeshPaint::InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	return IMeshPaintEdMode::InputKey(InViewportClient, InViewport, InKey, InEvent);
}

bool FEdModeMeshPaint::IsEditingEnabled() const
{
	return GetWorld() ? GetWorld()->FeatureLevel >= ERHIFeatureLevel::SM5 : false;
}

#undef LOCTEXT_NAMESPACE // "FEdModeMeshPaint"
