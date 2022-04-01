// Copyright Epic Games, Inc. All Rights Reserved.

#include "Styling/MVVMEditorStyle.h"
#include "Styling/SlateStyleMacros.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Styling/StyleColors.h"
#include "Interfaces/IPluginManager.h"


TUniquePtr<FMVVMEditorStyle, FMVVMEditorStyle::FCustomDeleter> FMVVMEditorStyle::Instance;


FMVVMEditorStyle::FMVVMEditorStyle()
	: FSlateStyleSet("MVVMEditorStyle")
{
	const FVector2D Icon10x10(10.0f, 10.0f);
	const FVector2D Icon14x14(14.0f, 14.0f);
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon24x24(24.0f, 24.0f);
	const FVector2D Icon32x32(32.0f, 32.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);

	TSharedPtr<IPlugin> MVVMPlugin = IPluginManager::Get().FindPlugin("ModelViewViewModel");
	if (ensure(MVVMPlugin))
	{
		SetContentRoot(MVVMPlugin->GetContentDir() / TEXT("Editor"));
	}

	// Class Icons
	{
		Set("ClassIcon.MVVMBlueprintView", new IMAGE_BRUSH("Slate/PH_MVVMBlueprintView_16x", Icon16x16));
		Set("ClassIcon.MVVMBlueprintViewModel", new IMAGE_BRUSH("Slate/PH_MVVMBlueprintViewModel_16x", Icon16x16));
	}

	{
		Set("BlueprintView.TabIcon", new IMAGE_BRUSH_SVG("Slate/ViewModel", Icon16x16));
	}

	// ViewModelSelectionWidget Icons
	{
		Set("ViewModelSelection.AddIcon", new IMAGE_BRUSH("Slate/MVVMViewModelSelection_Add_16x", Icon16x16));
		Set("ViewModelSelection.RemoveIcon", new IMAGE_BRUSH("Slate/MVVMViewModelSelection_Remove_16x", Icon16x16));
		Set("ViewModelSelection.TagIcon", new IMAGE_BRUSH("Slate/MVVMViewModelSelection_Tag_16x", Icon16x16));
	}

	{
		Set("PropertyPath.ContextBorder", new BOX_BRUSH("Common/LightGroupBorder", FMargin(4.0f / 16.0f)));

		Set("PropertyPath.ContextText", FTextBlockStyle(FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		.SetFont(DEFAULT_FONT("Bold", 8))
		.SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.9f))
		.SetShadowOffset(FVector2D(1, 1))
		.SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.7f)));
	}

	FSlateStyleRegistry::RegisterSlateStyle(*this);
}


FMVVMEditorStyle::~FMVVMEditorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}


void FMVVMEditorStyle::CreateInstance()
{
	Instance.Reset(new FMVVMEditorStyle);
}


void FMVVMEditorStyle::DestroyInstance()
{
	Instance.Reset();
}
