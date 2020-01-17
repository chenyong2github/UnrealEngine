// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorViewportLayoutTwoPanes.h"
#include "Framework/Docking/LayoutService.h"
#include "Editor.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Framework/Application/SlateApplication.h"


namespace ViewportLayoutTwoPanesDefs
{
	/** Default splitters to equal 50/50 split */
	static const float DefaultSplitterPercentage = 0.5f;
}


template <EOrientation TOrientation>
TSharedRef<SWidget> TEditorViewportLayoutTwoPanes<TOrientation>::MakeViewportLayout(TFunction<TSharedRef<SEditorViewport>(void)> &Func, const FString& LayoutString)
{
	FString SpecificLayoutString = GetTypeSpecificLayoutString(LayoutString);

	FString ViewportKey0, ViewportKey1;
	FString ViewportType0, ViewportType1;
	float SplitterPercentage = ViewportLayoutTwoPanesDefs::DefaultSplitterPercentage;

	if (!SpecificLayoutString.IsEmpty())
	{
		// The Layout String only holds the unique ID of the Additional Layout Configs to use
		const FString& IniSection = FLayoutSaveRestore::GetAdditionalLayoutConfigIni();

		ViewportKey0 = SpecificLayoutString + TEXT(".Viewport0");
		ViewportKey1 = SpecificLayoutString + TEXT(".Viewport1");

		GConfig->GetString(*IniSection, *(ViewportKey0 + TEXT(".TypeWithinLayout")), ViewportType0, GEditorPerProjectIni);
		GConfig->GetString(*IniSection, *(ViewportKey1 + TEXT(".TypeWithinLayout")), ViewportType1, GEditorPerProjectIni);

		FString PercentageString;
		if (GConfig->GetString(*IniSection, *(SpecificLayoutString + TEXT(".Percentage")), PercentageString, GEditorPerProjectIni))
		{
			TTypeFromString<float>::FromString(SplitterPercentage, *PercentageString);
		}
	}
	// Set up the viewports
	FAssetEditorViewportConstructionArgs Args;
	Args.ParentLayout = AsShared();
	Args.IsEnabled = FSlateApplication::Get().GetNormalExecutionAttribute();

	Args.bRealtime = false;
	Args.ConfigKey = *ViewportKey0;
	Args.ViewportType = LVT_OrthoXY;
	TSharedRef<IEditorViewportLayoutEntity> Viewport0 = FactoryViewport(Func, *ViewportType0, Args);

	Args.bRealtime = !FPlatformMisc::IsRemoteSession();
	Args.ConfigKey = *ViewportKey1;
	Args.ViewportType = LVT_Perspective;
	TSharedRef<IEditorViewportLayoutEntity> Viewport1 = FactoryViewport(Func, *ViewportType1, Args);

	Viewports.Add(*ViewportKey0, Viewport0);
	Viewports.Add(*ViewportKey1, Viewport1);

	SplitterWidget =
		SNew(SSplitter)
		.Orientation(TOrientation)
		+ SSplitter::Slot()
		.Value(SplitterPercentage)
		[
			Viewport0->AsWidget()
		]
	+ SSplitter::Slot()
		.Value(1.0f - SplitterPercentage)
		[
			Viewport1->AsWidget()
		];

	return SplitterWidget.ToSharedRef();
}


/**
* Function avoids linker errors on the template class functions in this cpp file.
* It avoids the need to put the contents of this file into the header.
* It doesn't get called.
*/
void EditorViewportLayoutTwoPanes_LinkErrorFixFunc()
{
	check(0);
	FEditorViewportLayoutTwoPanesVert DummyVert;
	FEditorViewportLayoutTwoPanesHoriz DummyHoriz;
}
