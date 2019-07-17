// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Types/PaintArgs.h"
#include "Layout/ArrangedWidget.h"
#include "Input/HittestGrid.h"
#include "Widgets/SWindow.h"

FPaintArgs::FPaintArgs(const SWidget* PaintParent, FHittestGrid& InRootHittestGrid, FHittestGrid& InCurrentHitTestGrid, FVector2D InWindowOffset, double InCurrentTime, float InDeltaTime)
	: RootGrid(InRootHittestGrid)
	, CurrentGrid(InCurrentHitTestGrid)
	, WindowOffset(InWindowOffset)
	, PaintParentPtr(PaintParent)
	, CurrentTime(InCurrentTime)
	, DeltaTime(InDeltaTime)
	, bInheritedHittestability(true)
{
}

FPaintArgs::FPaintArgs(const SWidget* PaintParent, FHittestGrid& InRootHittestGrid, FVector2D InWindowOffset, double InCurrentTime, float InDeltaTime)
	: FPaintArgs(PaintParent, InRootHittestGrid, InRootHittestGrid, InWindowOffset, InCurrentTime, InDeltaTime)
{

}

FPaintArgs FPaintArgs::InsertCustomHitTestPath(const SWidget* Widget, TSharedRef<ICustomHitTestPath> CustomHitTestPath) const
{
	TSharedRef<SWidget> SafeWidget = const_cast<SWidget*>(Widget)->AsShared();

	const_cast<FHittestGrid&>(CurrentGrid).InsertCustomHitTestPath(SafeWidget, CustomHitTestPath);
	return *this;
}
