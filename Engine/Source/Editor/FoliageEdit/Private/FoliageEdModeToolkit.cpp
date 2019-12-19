// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FoliageEdModeToolkit.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "SFoliageEdit.h"
#include "Classes/EditorStyleSettings.h"

#include "FoliageEditActions.h"
#include "FoliagePaletteCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "FoliageEditMode"

namespace 
{
	static const FName FoliageName(TEXT("Foliage")); 
	const TArray<FName> FoliagePaletteNames = { FoliageName };
}

void FFoliageEdModeToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager)
{

}

void FFoliageEdModeToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager)
{

}

void FFoliageEdModeToolkit::Init(const TSharedPtr< class IToolkitHost >& InitToolkitHost)
{
	FoliageEdWidget = SNew(SFoliageEdit);

	FModeToolkit::Init(InitToolkitHost);
}

FName FFoliageEdModeToolkit::GetToolkitFName() const
{
	return FName("FoliageEditMode");
}

FText FFoliageEdModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT( "ToolkitName", "Foliage" );
}

class FEdMode* FFoliageEdModeToolkit::GetEditorMode() const
{
	return GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_Foliage);
}

TSharedPtr<SWidget> FFoliageEdModeToolkit::GetInlineContent() const
{
	return FoliageEdWidget;
}

void FFoliageEdModeToolkit::RefreshFullList()
{
	FoliageEdWidget->RefreshFullList();
}

void FFoliageEdModeToolkit::NotifyFoliageTypeMeshChanged(class UFoliageType* FoliageType)
{
	FoliageEdWidget->NotifyFoliageTypeMeshChanged(FoliageType);
}

void FFoliageEdModeToolkit::GetToolPaletteNames(TArray<FName>& InPaletteName) const
{
	if (!GetDefault<UEditorStyleSettings>()->bEnableLegacyEditorModeUI)
	{
		InPaletteName = FoliagePaletteNames;
	}
}

FText FFoliageEdModeToolkit::GetToolPaletteDisplayName(FName PaletteName) const
{
	if (PaletteName == FoliageName)
	{
		return LOCTEXT("Foliage", "Foliage");
	}
	return FText();
}

void FFoliageEdModeToolkit::BuildToolPalette(FName PaletteName, class FToolBarBuilder& ToolBarBuilder)
{
	if (PaletteName == FoliageName)
	{
		FoliageEdWidget->CustomizeToolBarPalette(ToolBarBuilder);
	}
}

void FFoliageEdModeToolkit::OnToolPaletteChanged(FName PaletteName) 
{

}




#undef LOCTEXT_NAMESPACE
