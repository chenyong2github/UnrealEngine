// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditorStyle.h"

#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateStyleRegistry.h"


FOptimusEditorStyle::FOptimusEditorStyle() :
    FSlateStyleSet("OptimusEditorStyle")
{
	static const FVector2D IconSize10x10(10.0f, 10.0f);
	static const FVector2D IconSize16x12(16.0f, 12.0f);

	static const FSlateColor DefaultForeground(FLinearColor(0.72f, 0.72f, 0.72f, 1.f));

	SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Experimental/Optimus/Resources"));

	// Graph styles
	{
		Set("Optimus.Node.Pin.Resource_Connected", new IMAGE_BRUSH_SVG("Icons/Resource_Pin_Connected", IconSize16x12, DefaultForeground));
		Set("Optimus.Node.Pin.Resource_Disconnected", new IMAGE_BRUSH_SVG("Icons/Resource_Pin_Disconnected", IconSize16x12, DefaultForeground));		

		Set("Optimus.Node.Pin.Value_Connected", new IMAGE_BRUSH_SVG("Icons/Value_Pin_Connected", IconSize16x12, DefaultForeground));
		Set("Optimus.Node.Pin.Value_Disconnected", new IMAGE_BRUSH_SVG("Icons/Value_Pin_Disconnected", IconSize16x12, DefaultForeground));		

		Set("Optimus.Node.PinTree.Arrow_Collapsed_Left", new IMAGE_BRUSH_SVG("Icons/TreeArrow_Collapsed_Left", IconSize10x10, DefaultForeground));
		Set("Optimus.Node.PinTree.Arrow_Collapsed_Hovered_Left", new IMAGE_BRUSH_SVG("Icons/TreeArrow_Collapsed_Hovered_Left", IconSize10x10, DefaultForeground));

		Set("Optimus.Node.PinTree.Arrow_Expanded_Left", new IMAGE_BRUSH_SVG("Icons/TreeArrow_Expanded_Left", IconSize10x10, DefaultForeground));
		Set("Optimus.Node.PinTree.Arrow_Expanded_Hovered_Left", new IMAGE_BRUSH_SVG("Icons/TreeArrow_Expanded_Hovered_Left", IconSize10x10, DefaultForeground));

		Set("Optimus.Node.PinTree.Arrow_Collapsed_Right", new IMAGE_BRUSH_SVG("Icons/TreeArrow_Collapsed_Right", IconSize10x10, DefaultForeground));
		Set("Optimus.Node.PinTree.Arrow_Collapsed_Hovered_Right", new IMAGE_BRUSH_SVG("Icons/TreeArrow_Collapsed_Hovered_Right", IconSize10x10, DefaultForeground));

		Set("Optimus.Node.PinTree.Arrow_Expanded_Right", new IMAGE_BRUSH_SVG("Icons/TreeArrow_Expanded_Right", IconSize10x10, DefaultForeground));
		Set("Optimus.Node.PinTree.Arrow_Expanded_Hovered_Right", new IMAGE_BRUSH_SVG("Icons/TreeArrow_Expanded_Hovered_Right", IconSize10x10, DefaultForeground));
	}
}

void FOptimusEditorStyle::Register()
{
	FSlateStyleRegistry::RegisterSlateStyle(Get());
}


void FOptimusEditorStyle::Unregister()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(Get());
}


FOptimusEditorStyle& FOptimusEditorStyle::Get()
{
	static FOptimusEditorStyle Instance;
	return Instance;
}
