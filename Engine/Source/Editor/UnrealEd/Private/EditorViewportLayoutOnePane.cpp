// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorViewportLayoutOnePane.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Docking/LayoutService.h"
#include "ShowFlags.h"
#include "Editor.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"


TSharedRef<SWidget> FEditorViewportLayoutOnePane::MakeViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FString& LayoutString)
{
	FString ViewportKey, ViewportType;

	// Set up the viewport
	FAssetEditorViewportConstructionArgs Args;
 	Args.ParentLayout = AsShared();
	Args.bRealtime = !FPlatformMisc::IsRemoteSession();
 	Args.ViewportType = LVT_Perspective;
	TSharedRef<IEditorViewportLayoutEntity> Viewport = FactoryViewport(Func, *ViewportType, Args);

	ViewportBox =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			Viewport->AsWidget()
		];

	Viewports.Add( *ViewportKey, Viewport );

	return ViewportBox.ToSharedRef();
}
