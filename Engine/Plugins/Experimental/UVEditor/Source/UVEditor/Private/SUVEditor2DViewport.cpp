// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUVEditor2DViewport.h"

#include "SUVEditor2DViewportToolBar.h"
#include "UVEditor2DViewportClient.h"

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

TSharedPtr<SWidget> SUVEditor2DViewport::MakeViewportToolbar()
{
	return SNew(SUVEditor2DViewportToolBar)
		.CommandList(CommandList);
}

bool SUVEditor2DViewport::IsWidgetModeActive(UE::Widget::EWidgetMode Mode) const
{
	return static_cast<FUVEditor2DViewportClient*>(Client.Get())->AreWidgetButtonsEnabled() 
		&& Client->GetWidgetMode() == Mode;
}

#undef LOCTEXT_NAMESPACE
