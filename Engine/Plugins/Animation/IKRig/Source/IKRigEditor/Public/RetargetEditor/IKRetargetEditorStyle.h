// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Styling/AppStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateStyleMacros.h"
#include "Styling/SlateTypes.h"

class FIKRetargetEditorStyle final	: public FSlateStyleSet
{
public:
	
	FIKRetargetEditorStyle() : FSlateStyleSet("IKRetargetEditorStyle")
	{
		const FVector2D Icon16x16(16.0f, 16.0f);
		
		const FString IKRigPluginContentDir = FPaths::EnginePluginsDir() / TEXT("Animation/IKRig/Content");
		SetContentRoot(IKRigPluginContentDir);
		Set("IKRetarget.Tree.Bone", new IMAGE_BRUSH("Slate/Bone_16x", Icon16x16));

		const FString EngineEditorSlateDir = FPaths::EngineContentDir() / TEXT("Editor/Slate");
		SetContentRoot(EngineEditorSlateDir);
		Set( "IKRetarget.Viewport.Border", new BOX_BRUSH( "Old/Window/ViewportDebugBorder", 0.8f, FLinearColor(1.0f,1.0f,1.0f,1.0f) ) );
		
		FSlateStyleRegistry::RegisterSlateStyle(*this);
	}

	static FIKRetargetEditorStyle& Get()
	{
		static FIKRetargetEditorStyle Inst;
		return Inst;
	}
	
	~FIKRetargetEditorStyle()
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*this);
	}
};

#undef IMAGE_BRUSH