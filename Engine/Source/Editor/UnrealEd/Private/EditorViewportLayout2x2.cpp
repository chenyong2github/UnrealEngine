// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorViewportLayout2x2.h"
#include "Framework/Docking/LayoutService.h"
#include "Editor.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
// #include "LevelEditor.h"

namespace ViewportLayout2x2Defs
{
	/** Default 2x2 splitters to equal 50/50 splits */
	static const FVector2D DefaultSplitterPercentages(0.5f, 0.5f);
}

// FLevelViewportLayout2x2 //////////////////////////////////////////

TSharedRef<SWidget> FEditorViewportLayout2x2::MakeViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FString& LayoutString)
{
// 	FString SpecificLayoutString = GetTypeSpecificLayoutString(LayoutString);

	FString TopLeftKey, BottomLeftKey, TopRightKey, BottomRightKey;

	FString TopLeftType = TEXT("Default"), BottomLeftType = TEXT("Default"), TopRightType = TEXT("Default"), BottomRightType = TEXT("Default");

	TArray<FVector2D> SplitterPercentages;
	
	// Set up the viewports
	FAssetEditorViewportConstructionArgs Args;
 	Args.ParentLayout = AsShared();

	// Left viewport
	Args.bRealtime = false;
	Args.ConfigKey = *TopLeftKey;
	Args.ViewportType = LVT_OrthoYZ;
	TSharedPtr< IEditorViewportLayoutEntity > ViewportTL = FactoryViewport(Func, *TopLeftType, Args);

	// Persp viewport
	Args.bRealtime = !FPlatformMisc::IsRemoteSession();
	Args.ConfigKey = *BottomLeftKey;
	Args.ViewportType = LVT_Perspective;
	TSharedPtr< IEditorViewportLayoutEntity > ViewportBL = FactoryViewport(Func, *BottomLeftType, Args);

	// Front viewport
	Args.bRealtime = false;
	Args.ConfigKey = *TopRightKey;
	Args.ViewportType = LVT_OrthoXZ;
	TSharedPtr< IEditorViewportLayoutEntity > ViewportTR = FactoryViewport(Func, *TopRightType, Args);

	// Top Viewport
	Args.bRealtime = false;
	Args.ConfigKey = *BottomRightKey;
	Args.ViewportType = LVT_OrthoXY;
	TSharedPtr< IEditorViewportLayoutEntity > ViewportBR = FactoryViewport(Func, *BottomRightType, Args);

	Viewports.Add( *TopLeftKey, ViewportTL );
	Viewports.Add( *BottomLeftKey, ViewportBL );
	Viewports.Add( *TopRightKey, ViewportTR );
	Viewports.Add( *BottomRightKey, ViewportBR );

	// Set up the splitter
	SplitterWidget = 
	SNew( SSplitter2x2 )
	.TopLeft()
	[
		ViewportTL->AsWidget()
	]
	.BottomLeft()
	[
		ViewportBL->AsWidget()
	]
	.TopRight()
	[
		ViewportTR->AsWidget()
	]
	.BottomRight()
	[
		ViewportBR->AsWidget()
	];
	
	if (SplitterPercentages.Num() > 0)
	{
		SplitterWidget->SetSplitterPercentages(SplitterPercentages);
	}


	return SplitterWidget.ToSharedRef();
}

