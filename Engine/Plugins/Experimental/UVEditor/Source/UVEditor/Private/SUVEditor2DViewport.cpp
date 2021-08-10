// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUVEditor2DViewport.h"

#define LOCTEXT_NAMESPACE "SUVEditor2DViewport"

void SUVEditor2DViewport::AddOverlayWidget(TSharedRef<SWidget> OverlaidWidget)
{
	ViewportOverlay->AddSlot()
	[
		OverlaidWidget
	];
}

void SUVEditor2DViewport::RemoveOverlayWidget(TSharedRef<SWidget> OverlaidWidget)
{
	ViewportOverlay->RemoveSlot(OverlaidWidget);
}

#undef LOCTEXT_NAMESPACE
