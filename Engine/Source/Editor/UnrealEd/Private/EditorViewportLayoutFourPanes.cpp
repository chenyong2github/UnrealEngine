// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorViewportLayoutFourPanes.h"
#include "Framework/Docking/LayoutService.h"
#include "Editor.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"

namespace ViewportLayoutFourPanesDefs
{
	/** Default main splitter to equal 50/50 split */
	static const float DefaultPrimarySplitterPercentage = 0.5f;

	/** Default secondary splitter to equal three-way split */
	static const float DefaultSecondarySplitterPercentage = 0.333f;
}

// FEditorViewportLayoutFourPanes /////////////////////////////

TSharedRef<SWidget> FEditorViewportLayoutFourPanes::MakeViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FString& LayoutString)
{
	FString SpecificLayoutString = GetTypeSpecificLayoutString(LayoutString);

	FString ViewportKey0, ViewportKey1, ViewportKey2, ViewportKey3;
	FString ViewportType0 = TEXT("Default"), ViewportType1 = TEXT("Default"), ViewportType2 = TEXT("Default"), ViewportType3 = TEXT("Default");

	float PrimarySplitterPercentage = ViewportLayoutFourPanesDefs::DefaultPrimarySplitterPercentage;
	float SecondarySplitterPercentage0 = ViewportLayoutFourPanesDefs::DefaultSecondarySplitterPercentage;
	float SecondarySplitterPercentage1 = ViewportLayoutFourPanesDefs::DefaultSecondarySplitterPercentage;

	if (!SpecificLayoutString.IsEmpty())
	{
		// The Layout String only holds the unique ID of the Additional Layout Configs to use
		const FString& IniSection = FLayoutSaveRestore::GetAdditionalLayoutConfigIni();

		ViewportKey0 = SpecificLayoutString + TEXT(".Viewport0");
		ViewportKey1 = SpecificLayoutString + TEXT(".Viewport1");
		ViewportKey2 = SpecificLayoutString + TEXT(".Viewport2");
		ViewportKey3 = SpecificLayoutString + TEXT(".Viewport3");

		GConfig->GetString(*IniSection, *(ViewportKey0 + TEXT(".TypeWithinLayout")), ViewportType0, GEditorPerProjectIni);
		GConfig->GetString(*IniSection, *(ViewportKey1 + TEXT(".TypeWithinLayout")), ViewportType1, GEditorPerProjectIni);
		GConfig->GetString(*IniSection, *(ViewportKey2 + TEXT(".TypeWithinLayout")), ViewportType2, GEditorPerProjectIni);
		GConfig->GetString(*IniSection, *(ViewportKey3 + TEXT(".TypeWithinLayout")), ViewportType3, GEditorPerProjectIni);

		FString PercentageString;
		if (GConfig->GetString(*IniSection, *(SpecificLayoutString + TEXT(".Percentage0")), PercentageString, GEditorPerProjectIni))
		{
			TTypeFromString<float>::FromString(PrimarySplitterPercentage, *PercentageString);
		}
		if (GConfig->GetString(*IniSection, *(SpecificLayoutString + TEXT(".Percentage1")), PercentageString, GEditorPerProjectIni))
		{
			TTypeFromString<float>::FromString(SecondarySplitterPercentage0, *PercentageString);
		}
		if (GConfig->GetString(*IniSection, *(SpecificLayoutString + TEXT(".Percentage2")), PercentageString, GEditorPerProjectIni))
		{
			TTypeFromString<float>::FromString(SecondarySplitterPercentage1, *PercentageString);
		}
	}

	// Set up the viewports
	FAssetEditorViewportConstructionArgs Args;
	Args.ParentLayout = AsShared();
	Args.IsEnabled = FSlateApplication::Get().GetNormalExecutionAttribute();

	Args.bRealtime = !FPlatformMisc::IsRemoteSession();
	Args.ConfigKey = *ViewportKey0;
	Args.ViewportType = LVT_Perspective;
	TSharedRef<IEditorViewportLayoutEntity> Viewport0 = FactoryViewport(Func, *ViewportType0, Args);

	Args.bRealtime = false;
	Args.ConfigKey = *ViewportKey1;
	Args.ViewportType = LVT_OrthoXY;
	TSharedRef<IEditorViewportLayoutEntity> Viewport1 = FactoryViewport(Func, *ViewportType1, Args);

	// Front viewport
	Args.bRealtime = false;
	Args.ConfigKey = *ViewportKey2;
	Args.ViewportType = LVT_OrthoXZ;
	TSharedRef<IEditorViewportLayoutEntity> Viewport2 = FactoryViewport(Func, *ViewportType2, Args);

	// Top Viewport
	Args.bRealtime = false;
	Args.ConfigKey = *ViewportKey3;
	Args.ViewportType = LVT_OrthoYZ;
	TSharedRef<IEditorViewportLayoutEntity> Viewport3 = FactoryViewport(Func, *ViewportType2, Args);

	Viewports.Add( *ViewportKey0, Viewport0 );
	Viewports.Add( *ViewportKey1, Viewport1 );
	Viewports.Add( *ViewportKey2, Viewport2 );
	Viewports.Add( *ViewportKey3, Viewport3 );

	TSharedRef<SWidget> LayoutWidget = MakeFourPanelWidget(
		Viewports,
		Viewport0->AsWidget(), Viewport1->AsWidget(), Viewport2->AsWidget(), Viewport3->AsWidget(),
		PrimarySplitterPercentage, SecondarySplitterPercentage0, SecondarySplitterPercentage1);

	return LayoutWidget;
}


// FEditorViewportLayoutFourPanesLeft /////////////////////////////

TSharedRef<SWidget> FEditorViewportLayoutFourPanesLeft::MakeFourPanelWidget(
	TMap<FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
	TSharedRef<SWidget> Viewport0, TSharedRef<SWidget> Viewport1, TSharedRef<SWidget> Viewport2, TSharedRef<SWidget> Viewport3,
	float PrimarySplitterPercentage, float SecondarySplitterPercentage0, float SecondarySplitterPercentage1)
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
			.Value(SecondarySplitterPercentage0)
			[
				Viewport1
			]
			+SSplitter::Slot()
			.Value(SecondarySplitterPercentage1)
			[
				Viewport2
			]
			+SSplitter::Slot()
			.Value(1.0f - SecondarySplitterPercentage0 - SecondarySplitterPercentage1)
			[
				Viewport3
			]
		];

	return Widget;
}


// FEditorViewportLayoutFourPanesRight /////////////////////////////

TSharedRef<SWidget> FEditorViewportLayoutFourPanesRight::MakeFourPanelWidget(
	TMap<FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
	TSharedRef<SWidget> Viewport0, TSharedRef<SWidget> Viewport1, TSharedRef<SWidget> Viewport2, TSharedRef<SWidget> Viewport3,
	float PrimarySplitterPercentage, float SecondarySplitterPercentage0, float SecondarySplitterPercentage1)
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
			.Value(SecondarySplitterPercentage0)
			[
				Viewport1
			]
			+SSplitter::Slot()
			.Value(SecondarySplitterPercentage1)
			[
				Viewport2
			]
			+SSplitter::Slot()
			.Value(1.0f - SecondarySplitterPercentage0 - SecondarySplitterPercentage1)
			[
				Viewport3
			]
		]
		+SSplitter::Slot()
		.Value(1.0f - PrimarySplitterPercentage)
		[
			Viewport0
		];

	return Widget;
}


// FEditorViewportLayoutFourPanesTop /////////////////////////////

TSharedRef<SWidget> FEditorViewportLayoutFourPanesTop::MakeFourPanelWidget(
	TMap<FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
	TSharedRef<SWidget> Viewport0, TSharedRef<SWidget> Viewport1, TSharedRef<SWidget> Viewport2, TSharedRef<SWidget> Viewport3,
	float PrimarySplitterPercentage, float SecondarySplitterPercentage0, float SecondarySplitterPercentage1)
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
			.Value(SecondarySplitterPercentage0)
			[
				Viewport1
			]
			+SSplitter::Slot()
			.Value(SecondarySplitterPercentage1)
			[
				Viewport2
			]
			+SSplitter::Slot()
			.Value(1.0f - SecondarySplitterPercentage0 - SecondarySplitterPercentage1)
			[
				Viewport3
			]
		];

	return Widget;
}


// FEditorViewportLayoutFourPanesBottom /////////////////////////////

TSharedRef<SWidget> FEditorViewportLayoutFourPanesBottom::MakeFourPanelWidget(
	TMap<FName, TSharedPtr< IEditorViewportLayoutEntity >>& ViewportWidgets,
	TSharedRef<SWidget> Viewport0, TSharedRef<SWidget> Viewport1, TSharedRef<SWidget> Viewport2, TSharedRef<SWidget> Viewport3,
	float PrimarySplitterPercentage, float SecondarySplitterPercentage0, float SecondarySplitterPercentage1)
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
			.Value(SecondarySplitterPercentage0)
			[
				Viewport1
			]
			+SSplitter::Slot()
			.Value(SecondarySplitterPercentage1)
			[
				Viewport2
			]
			+SSplitter::Slot()
			.Value(1.0f - SecondarySplitterPercentage0 - SecondarySplitterPercentage1)
			[
				Viewport3
			]
		]
		+SSplitter::Slot()
		.Value(1.0f - PrimarySplitterPercentage)
		[
			Viewport0
		];

	return Widget;
}
