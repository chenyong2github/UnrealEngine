// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVEditorStyle.h"

#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "EditorStyleSet.h"
#include "Styling/SlateStyleMacros.h"

FName FUVEditorStyle::StyleName("UVStyle");

FUVEditorStyle::FUVEditorStyle()
	: FSlateStyleSet(StyleName)
{
	// Used FFractureEditorStyle as a model

	const FVector2D IconSize(20.0f, 20.0f);

	SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Experimental/UVEditor/Content/Icons"));
	SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate") );

	Set("UVEditor.BeginSelectTool",             new IMAGE_BRUSH_SVG("MeshSelect", IconSize));
	Set("UVEditor.BeginLayoutTool",             new IMAGE_BRUSH_SVG("UVLayout", IconSize));
	Set("UVEditor.BeginParameterizeMeshTool",   new IMAGE_BRUSH_SVG("AutoUnwrap", IconSize));
	Set("UVEditor.BeginChannelEditTool",        new IMAGE_BRUSH_SVG("AttributeEditor", IconSize));
	Set("UVEditor.BeginSeamTool",               new IMAGE_BRUSH_SVG("ModelingUVSeamEdit", IconSize));
	Set("UVEditor.BeginRecomputeUVsTool",       new IMAGE_BRUSH_SVG("GroupUnwrap", IconSize));

	// Top toolbar icons
	Set("UVEditor.ApplyChanges", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Apply", IconSize));
	Set("UVEditor.ChannelSettings", new CORE_IMAGE_BRUSH_SVG("Starship/Common/SetDrawUVs", IconSize));
	Set("UVEditor.BackgroundSettings", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Sprite", IconSize));

	// Viewport icons
	Set("UVEditor.OrbitCamera", new CORE_IMAGE_BRUSH_SVG("Starship/EditorViewport/rotate", IconSize));
	Set("UVEditor.FlyCamera", new CORE_IMAGE_BRUSH_SVG("Starship/EditorViewport/camera", IconSize));

	FSlateStyleRegistry::RegisterSlateStyle(*this);
}

FUVEditorStyle::~FUVEditorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

FUVEditorStyle& FUVEditorStyle::Get()
{
	static FUVEditorStyle Inst;
	return Inst;
}


