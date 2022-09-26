// Copyright Epic Games, Inc. All Rights Reserved.	

#include "SCustomizableObjectEditorTransformViewportToolbar.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "CustomizableObjectEditorViewportLODCommands.h"
#include "SCustomizableObjectEditorViewport.h"
#include "CustomizableObjectEditorViewportClient.h"

#define LOCTEXT_NAMESPACE "SCustomizableObjectEditorTransformViewportToolbar"

void SCustomizableObjectEditorTransformViewportToolbar::Construct( const FArguments& InArgs )
{
	Viewport = InArgs._Viewport;
	ViewportTapBody = InArgs._ViewportTapBody;

	const FCustomizableObjectEditorViewportLODCommands& ViewportLODMenuCommands = FCustomizableObjectEditorViewportLODCommands::Get();
	UICommandList = MakeShareable(new FUICommandList);

	SViewportToolBar::Construct(SViewportToolBar::FArguments());
}

#undef LOCTEXT_NAMESPACE 
