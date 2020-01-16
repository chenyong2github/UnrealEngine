// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorViewportLayoutThreePanes.h"
#include "Framework/Docking/LayoutService.h"
#include "ShowFlags.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"
#include "LevelEditor.h"

namespace ViewportLayoutThreePanesDefs
{
	/** Default splitters to equal 50/50 split */
	static const float DefaultSplitterPercentage = 0.5f;
}


// FEditorViewportLayoutThreePanes /////////////////////////////

TSharedRef<SWidget> FEditorViewportLayoutThreePanes::MakeViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FString& LayoutString)
{
	FString SpecificLayoutString = GetTypeSpecificLayoutString(LayoutString);

	FEngineShowFlags OrthoShowFlags(ESFIM_Editor);	
	ApplyViewMode(VMI_BrushWireframe, false, OrthoShowFlags);

	FEngineShowFlags PerspectiveShowFlags(ESFIM_Editor);	
	ApplyViewMode(VMI_Lit, true, PerspectiveShowFlags);

	FString ViewportKey0, ViewportKey1, ViewportKey2;
	FString ViewportType0, ViewportType1, ViewportType2;
	float PrimarySplitterPercentage = ViewportLayoutThreePanesDefs::DefaultSplitterPercentage;
	float SecondarySplitterPercentage = ViewportLayoutThreePanesDefs::DefaultSplitterPercentage;

	if (!SpecificLayoutString.IsEmpty())
	{
		// The Layout String only holds the unique ID of the Additional Layout Configs to use
		const FString& IniSection = FLayoutSaveRestore::GetAdditionalLayoutConfigIni();

		ViewportKey0 = SpecificLayoutString + TEXT(".Viewport0");
		ViewportKey1 = SpecificLayoutString + TEXT(".Viewport1");
		ViewportKey2 = SpecificLayoutString + TEXT(".Viewport2");

		GConfig->GetString(*IniSection, *(ViewportKey0 + TEXT(".TypeWithinLayout")), ViewportType0, GEditorPerProjectIni);
		GConfig->GetString(*IniSection, *(ViewportKey1 + TEXT(".TypeWithinLayout")), ViewportType1, GEditorPerProjectIni);
		GConfig->GetString(*IniSection, *(ViewportKey2 + TEXT(".TypeWithinLayout")), ViewportType2, GEditorPerProjectIni);

		FString PercentageString;
		if (GConfig->GetString(*IniSection, *(SpecificLayoutString + TEXT(".Percentage0")), PercentageString, GEditorPerProjectIni))
		{
			TTypeFromString<float>::FromString(PrimarySplitterPercentage, *PercentageString);
		}
		if (GConfig->GetString(*IniSection, *(SpecificLayoutString + TEXT(".Percentage1")), PercentageString, GEditorPerProjectIni))
		{
			TTypeFromString<float>::FromString(SecondarySplitterPercentage, *PercentageString);
		}
	}

	// Set up the viewports
	FAssetEditorViewportConstructionArgs Args;
	Args.ParentLayout = AsShared();
	Args.IsEnabled = FSlateApplication::Get().GetNormalExecutionAttribute();

	Args.bRealtime = !FPlatformMisc::IsRemoteSession();
	Args.ConfigKey = *ViewportKey0;
	Args.ViewportType = LVT_Perspective;
	TSharedRef<IEditorViewportLayoutEntity> Viewport0 =FactoryViewport(Func, *ViewportType0, Args);

	Args.bRealtime = false;
	Args.ConfigKey = *ViewportKey1;
	Args.ViewportType = LVT_OrthoXY;
	TSharedRef<IEditorViewportLayoutEntity> Viewport1 =FactoryViewport(Func, *ViewportType1, Args);

	// Front viewport
	Args.bRealtime = false;
	Args.ConfigKey = *ViewportKey2;
	Args.ViewportType = LVT_OrthoXZ;
	TSharedRef<IEditorViewportLayoutEntity> Viewport2 =FactoryViewport(Func, *ViewportType2, Args);

	Viewports.Add( *ViewportKey0, Viewport0 );
	Viewports.Add( *ViewportKey1, Viewport1 );
	Viewports.Add( *ViewportKey2, Viewport2 );

	TSharedRef<SWidget> LayoutWidget = MakeThreePanelWidget(
		Viewports,
		Viewport0->AsWidget(), Viewport1->AsWidget(), Viewport2->AsWidget(),
		PrimarySplitterPercentage, SecondarySplitterPercentage);

	return LayoutWidget;
}

// FEditorViewportLayoutThreePanesLeft /////////////////////////////

TSharedRef<SWidget> FEditorViewportLayoutThreePanesLeft::MakeThreePanelWidget(
	TMap< FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
	const TSharedRef<SWidget>& Viewport0, const TSharedRef<SWidget>& Viewport1, const TSharedRef<SWidget>& Viewport2,
	float PrimarySplitterPercentage, float SecondarySplitterPercentage)
{
	TSharedRef<SWidget> Widget = 
		SAssignNew( PrimarySplitterWidget, SSplitter )
		.Orientation(EOrientation::Orient_Horizontal)
		+SSplitter::Slot()
		.Value(PrimarySplitterPercentage)
		[
			Viewport0
		]
		+SSplitter::Slot()
		.Value(1.0f - PrimarySplitterPercentage)
		[
			SAssignNew( SecondarySplitterWidget, SSplitter )
			.Orientation(EOrientation::Orient_Vertical)
			+SSplitter::Slot()
			.Value(SecondarySplitterPercentage)
			[
				Viewport1
			]
			+SSplitter::Slot()
			.Value(1.0f - SecondarySplitterPercentage)
			[
				Viewport2
			]
		];

	return Widget;
}


// FEditorViewportLayoutThreePanesRight /////////////////////////////

TSharedRef<SWidget> FEditorViewportLayoutThreePanesRight::MakeThreePanelWidget(
	TMap< FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
	const TSharedRef<SWidget>& Viewport0, const TSharedRef<SWidget>& Viewport1, const TSharedRef<SWidget>& Viewport2,
	float PrimarySplitterPercentage, float SecondarySplitterPercentage)
{
	TSharedRef<SWidget> Widget = 
		SAssignNew( PrimarySplitterWidget, SSplitter )
		.Orientation(EOrientation::Orient_Horizontal)
		+SSplitter::Slot()
		.Value(PrimarySplitterPercentage)
		[
			SAssignNew( SecondarySplitterWidget, SSplitter )
			.Orientation(EOrientation::Orient_Vertical)
			+SSplitter::Slot()
			.Value(SecondarySplitterPercentage)
			[
				Viewport1
			]
			+SSplitter::Slot()
			.Value(1.0f - SecondarySplitterPercentage)
			[
				Viewport2
			]
		]
		+SSplitter::Slot()
		.Value(1.0f - PrimarySplitterPercentage)
		[
			Viewport0
		];

	return Widget;
}


// FEditorViewportLayoutThreePanesTop /////////////////////////////

TSharedRef<SWidget> FEditorViewportLayoutThreePanesTop::MakeThreePanelWidget(
	TMap< FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
	const TSharedRef<SWidget>& Viewport0, const TSharedRef<SWidget>& Viewport1, const TSharedRef<SWidget>& Viewport2,
	float PrimarySplitterPercentage, float SecondarySplitterPercentage)
{
	TSharedRef<SWidget> Widget = 
		SAssignNew( PrimarySplitterWidget, SSplitter )
		.Orientation(EOrientation::Orient_Vertical)
		+SSplitter::Slot()
		.Value(PrimarySplitterPercentage)
		[
			Viewport0
		]
		+SSplitter::Slot()
		.Value(1.0f - PrimarySplitterPercentage)
		[
			SAssignNew( SecondarySplitterWidget, SSplitter )
			.Orientation(EOrientation::Orient_Horizontal)
			+SSplitter::Slot()
			.Value(SecondarySplitterPercentage)
			[
				Viewport1
			]
			+SSplitter::Slot()
			.Value(1.0f - SecondarySplitterPercentage)
			[
				Viewport2
			]
		];

	return Widget;
}


// FEditorViewportLayoutThreePanesBottom /////////////////////////////

TSharedRef<SWidget> FEditorViewportLayoutThreePanesBottom::MakeThreePanelWidget(
	TMap< FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
	const TSharedRef<SWidget>& Viewport0, const TSharedRef<SWidget>& Viewport1, const TSharedRef<SWidget>& Viewport2,
	float PrimarySplitterPercentage, float SecondarySplitterPercentage)
{
	TSharedRef<SWidget> Widget = 
		SAssignNew( PrimarySplitterWidget, SSplitter )
		.Orientation(EOrientation::Orient_Vertical)
		+SSplitter::Slot()
		.Value(PrimarySplitterPercentage)
		[
			SAssignNew( SecondarySplitterWidget, SSplitter )
			.Orientation(EOrientation::Orient_Horizontal)
			+SSplitter::Slot()
			.Value(SecondarySplitterPercentage)
			[
				Viewport1
			]
			+SSplitter::Slot()
			.Value(1.0f - SecondarySplitterPercentage)
			[
				Viewport2
			]
		]
		+SSplitter::Slot()
		.Value(1.0f - PrimarySplitterPercentage)
		[
			Viewport0
		];

	return Widget;
}
