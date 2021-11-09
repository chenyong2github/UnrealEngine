// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SViewportToolBar.h"

class FExtender;
class FUICommandList;
class SUVEditor2DViewport;

/**
 * Toolbar that shows up at the top of the 2d viewport (has gizmo controls)
 */
class SUVEditor2DViewportToolBar : public SViewportToolBar
{
public:
	SLATE_BEGIN_ARGS(SUVEditor2DViewportToolBar) {}
		SLATE_ARGUMENT(TSharedPtr<FUICommandList>, CommandList)
		SLATE_ARGUMENT(TSharedPtr<FExtender>, Extenders)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> MakeSelectionToolBar(const TSharedPtr<FExtender> InExtenders);
	TSharedRef<SWidget> MakeGizmoToolBar(const TSharedPtr<FExtender> InExtenders);

	/** Command list */
	TSharedPtr<FUICommandList> CommandList;
};
