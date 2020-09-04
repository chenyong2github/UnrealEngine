// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BOX_BRUSH(RelativePath, ...) FSlateBoxBrush(RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BORDER_BRUSH(RelativePath, ...) FSlateBorderBrush(RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define TTF_FONT(RelativePath, ...) FSlateFontInfo(RootToContentDir(RelativePath, TEXT(".ttf")), __VA_ARGS__)
#define OTF_FONT(RelativePath, ...) FSlateFontInfo(RootToContentDir(RelativePath, TEXT(".otf")), __VA_ARGS__)

class FControlRigEditorStyle
	: public FSlateStyleSet
{
public:
	FControlRigEditorStyle()
		: FSlateStyleSet("ControlRigEditorStyle")
	{
		const FVector2D Icon10x10(10.0f, 10.0f);
		const FVector2D Icon14x14(14.0f, 14.0f);
		const FVector2D Icon16x16(16.0f, 16.0f);
		const FVector2D Icon20x20(20.0f, 20.0f);
		const FVector2D Icon24x24(24.0f, 24.0f);
		const FVector2D Icon32x32(32.0f, 32.0f);
		const FVector2D Icon40x40(40.0f, 40.0f);
		SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Experimental/ControlRig/Content"));

		const FSlateColor DefaultForeground(FLinearColor(0.72f, 0.72f, 0.72f, 1.f));

		// Class Icons
		{
			Set("ClassIcon.ControlRigSequence", new IMAGE_BRUSH("Slate/ControlRigSequence_16x", Icon16x16));
			Set("ClassIcon.ControlRigBlueprint", new IMAGE_BRUSH("Slate/ControlRigBlueprint_16x", Icon16x16));
		}

		// Edit mode styles
		{
			Set("ControlRigEditMode", new IMAGE_BRUSH("Slate/ControlRigMode_40x", Icon40x40));
			Set("ControlRigEditMode.Small", new IMAGE_BRUSH("Slate/ControlRigMode_40x", Icon20x20));
		}

		// Sequencer styles
		{
			Set("ControlRig.ExportAnimSequence", new IMAGE_BRUSH("Slate/ExportAnimSequence_24x", Icon24x24));
			Set("ControlRig.ExportAnimSequence.Small", new IMAGE_BRUSH("Slate/ExportAnimSequence_24x", Icon24x24));
			Set("ControlRig.ReExportAnimSequence", new IMAGE_BRUSH("Slate/ExportAnimSequence_24x", Icon24x24));
			Set("ControlRig.ReExportAnimSequence.Small", new IMAGE_BRUSH("Slate/ExportAnimSequence_24x", Icon24x24));
			Set("ControlRig.ImportFromRigSequence", new IMAGE_BRUSH("Slate/ReImportRigSequence_16x", Icon16x16));
			Set("ControlRig.ImportFromRigSequence.Small", new IMAGE_BRUSH("Slate/ReImportRigSequence_16x", Icon16x16));
			Set("ControlRig.ReImportFromRigSequence", new IMAGE_BRUSH("Slate/ReImportRigSequence_16x", Icon16x16));
			Set("ControlRig.ReImportFromRigSequence.Small", new IMAGE_BRUSH("Slate/ReImportRigSequence_16x", Icon16x16));
		}

		// Control Rig Editor styles
		{
			Set("ControlRig.TabIcon", new IMAGE_BRUSH("Slate/ControlRigTab_16x", Icon16x16));
			Set("ControlRig.RigUnit", new IMAGE_BRUSH("Slate/ControlRigUnit_16x", Icon16x16));

			// icons for control units
			Set("ControlRig.ControlUnitOn", new IMAGE_BRUSH("Slate/ControlUnit_On", Icon32x32));
			Set("ControlRig.ControlUnitOff", new IMAGE_BRUSH("Slate/ControlUnit_Off", Icon32x32));

			Set("ControlRig.ExecuteGraph", new IMAGE_BRUSH("Slate/ExecuteGraph", Icon40x40));
			Set("ControlRig.ExecuteGraph.Small", new IMAGE_BRUSH("Slate/ExecuteGraph", Icon20x20));

			Set("ControlRig.AutoCompileGraph", new IMAGE_BRUSH("Slate/AutoCompile", Icon40x40));
			Set("ControlRig.AutoCompileGraph.Small", new IMAGE_BRUSH("Slate/AutoCompile", Icon20x20));

			Set("ControlRig.SetupMode", new IMAGE_BRUSH("Slate/SetupMode", Icon40x40));
			Set("ControlRig.SetupMode.Small", new IMAGE_BRUSH("Slate/SetupMode", Icon20x20));

			Set("ControlRig.UpdateEvent", new IMAGE_BRUSH("Slate/UpdateEvent", Icon40x40));
			Set("ControlRig.InverseEvent", new IMAGE_BRUSH("Slate/InverseEvent", Icon40x40));
			Set("ControlRig.UpdateAndInverse", new IMAGE_BRUSH("Slate/UpdateAndInverse", Icon40x40));
			Set("ControlRig.InverseAndUpdate", new IMAGE_BRUSH("Slate/InverseAndUpdate", Icon40x40));

			Set("ControlRig.Bug.Dot", new IMAGE_BRUSH("Slate/ControlRig_BugDot_32x", Icon16x16));
			Set("ControlRig.Bug.Normal", new IMAGE_BRUSH("Slate/ControlRig_Bug_28x", Icon14x14));
			Set("ControlRig.Bug.Open", new IMAGE_BRUSH("Slate/ControlRig_BugOpen_28x", Icon14x14));
			Set("ControlRig.Bug.Solid", new IMAGE_BRUSH("Slate/ControlRig_BugSolid_28x", Icon14x14));
		}

		// Graph styles
		{
			Set("ControlRig.Node.PinTree.Arrow_Collapsed_Left", new IMAGE_BRUSH("Slate/TreeArrow_Collapsed_Left", Icon10x10, DefaultForeground));
			Set("ControlRig.Node.PinTree.Arrow_Collapsed_Hovered_Left", new IMAGE_BRUSH("Slate/TreeArrow_Collapsed_Hovered_Left", Icon10x10, DefaultForeground));

			Set("ControlRig.Node.PinTree.Arrow_Expanded_Left", new IMAGE_BRUSH("Slate/TreeArrow_Expanded_Left", Icon10x10, DefaultForeground));
			Set("ControlRig.Node.PinTree.Arrow_Expanded_Hovered_Left", new IMAGE_BRUSH("Slate/TreeArrow_Expanded_Hovered_Left", Icon10x10, DefaultForeground));

			Set("ControlRig.Node.PinTree.Arrow_Collapsed_Right", new IMAGE_BRUSH("Slate/TreeArrow_Collapsed_Right", Icon10x10, DefaultForeground));
			Set("ControlRig.Node.PinTree.Arrow_Collapsed_Hovered_Right", new IMAGE_BRUSH("Slate/TreeArrow_Collapsed_Hovered_Right", Icon10x10, DefaultForeground));

			Set("ControlRig.Node.PinTree.Arrow_Expanded_Right", new IMAGE_BRUSH("Slate/TreeArrow_Expanded_Right", Icon10x10, DefaultForeground));
			Set("ControlRig.Node.PinTree.Arrow_Expanded_Hovered_Right", new IMAGE_BRUSH("Slate/TreeArrow_Expanded_Hovered_Right", Icon10x10, DefaultForeground));
		}

		// Tree styles
		{
			Set("ControlRig.Tree.BoneUser", new IMAGE_BRUSH("Slate/BoneUser_16x", Icon16x16));
			Set("ControlRig.Tree.BoneImported", new IMAGE_BRUSH("Slate/BoneImported_16x", Icon16x16));
			Set("ControlRig.Tree.Control", new IMAGE_BRUSH("Slate/Control_16x", Icon16x16));
			Set("ControlRig.Tree.Space", new IMAGE_BRUSH("Slate/Space_16x", Icon16x16));
		}

		// Font?
		{
			Set("ControlRig.Hierarchy.Menu", TTF_FONT("Fonts/Roboto-Regular", 12));
		}

		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}

	static FControlRigEditorStyle& Get()
	{
		static FControlRigEditorStyle Inst;
		return Inst;
	}
	
	~FControlRigEditorStyle()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	}
};

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef TTF_FONT
#undef OTF_FONT
