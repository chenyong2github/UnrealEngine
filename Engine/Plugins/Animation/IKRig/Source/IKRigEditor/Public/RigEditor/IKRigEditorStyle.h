// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorStyleSet.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateTypes.h"

class FIKRigEditorStyle	: public FSlateStyleSet
{
public:
	
	FIKRigEditorStyle() : FSlateStyleSet("IKRigEditorStyle")
	{
		SetContentRoot(FPaths::EnginePluginsDir() / TEXT("Animation/IKRig/Content"));
		const FVector2D Icon10x10(10.0f, 10.0f);
		const FVector2D Icon14x14(14.0f, 14.0f);
		const FVector2D Icon16x16(16.0f, 16.0f);
		const FVector2D Icon20x20(20.0f, 20.0f);
		const FVector2D Icon24x24(24.0f, 24.0f);
		const FVector2D Icon32x32(32.0f, 32.0f);
		const FVector2D Icon40x40(40.0f, 40.0f);
		
		Set("IKRig.Tree.Bone", new IMAGE_BRUSH("Slate/Bone_16x", Icon16x16));
		Set("IKRig.Tree.BoneWithSettings", new IMAGE_BRUSH("Slate/BoneWithSettings_16x", Icon16x16));
		Set("IKRig.Tree.Goal", new IMAGE_BRUSH("Slate/Goal_16x", Icon16x16));
		Set("IKRig.Tree.Effector", new IMAGE_BRUSH("Slate/Effector_16x", Icon16x16));
		
		Set("IKRig.TabIcon", new IMAGE_BRUSH("Slate/Tab_16x", Icon16x16));
		
		Set("IKRig.Solver", new IMAGE_BRUSH("Slate/Solver_16x", Icon16x16));
		Set("IKRig.DragSolver", new IMAGE_BRUSH("Slate/DragSolver", FVector2D(6, 15)));

		Set("IKRig.Reset", new IMAGE_BRUSH("Slate/Reset", Icon40x40));
		Set("IKRig.Reset.Small", new IMAGE_BRUSH("Slate/Reset", Icon20x20));
		
		FTextBlockStyle NormalText = FEditorStyle::GetWidgetStyle<FTextBlockStyle>("SkeletonTree.NormalFont");
		Set( "IKRig.Tree.AffectedBoneText", FTextBlockStyle(NormalText).SetColorAndOpacity( FLinearColor::Green ));
		
		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}

	static FIKRigEditorStyle& Get()
	{
		static FIKRigEditorStyle Inst;
		return Inst;
	}
	
	~FIKRigEditorStyle()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	}
};

#undef IMAGE_BRUSH