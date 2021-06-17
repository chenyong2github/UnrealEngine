// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterConfiguratorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateBoxBrush.h"
#include "Brushes/SlateBorderBrush.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FDisplayClusterConfiguratorStyle::StyleInstance = nullptr;
TArray<FDisplayClusterConfiguratorStyle::FCornerColor> FDisplayClusterConfiguratorStyle::CornerColors;

void FDisplayClusterConfiguratorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FDisplayClusterConfiguratorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FDisplayClusterConfiguratorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("DisplayClusterConfiguratorStyle"));
	return StyleSetName;
}

const FLinearColor& FDisplayClusterConfiguratorStyle::GetColor(FName PropertyName, const ANSICHAR* Specifier)
{
	return StyleInstance->GetColor(PropertyName, Specifier);
}

const FLinearColor& FDisplayClusterConfiguratorStyle::GetCornerColor(uint32 Index)
{
	return CornerColors[Index % CornerColors.Num()].Color;
}

const FMargin& FDisplayClusterConfiguratorStyle::GetMargin(FName PropertyName, const ANSICHAR* Specifier)
{
	return StyleInstance->GetMargin(PropertyName, Specifier);
}

const FSlateBrush* FDisplayClusterConfiguratorStyle::GetBrush(FName PropertyName, const ANSICHAR* Specifier)
{
	return StyleInstance->GetBrush(PropertyName, Specifier);
}

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )

const FVector2D Icon128x128(128.0f, 128.0f);
const FVector2D Icon64x64(64.0f, 64.0f);
const FVector2D Icon40x40(40.0f, 40.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon12x12(12.0f, 12.0f);
TSharedRef< FSlateStyleSet > FDisplayClusterConfiguratorStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShared<FSlateStyleSet>("DisplayClusterConfiguratorStyle");

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("nDisplay"));
	check(Plugin.IsValid());
	if (Plugin.IsValid())
	{
		Style->SetContentRoot(FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")));
	}

	Style->Set("ClassIcon.DisplayClusterRootActor", new IMAGE_BRUSH("RootActor/nDisplay_x16", Icon16x16));
	Style->Set("ClassThumbnail.DisplayClusterRootActor", new IMAGE_BRUSH("RootActor/nDisplay_x64", Icon64x64));

	// Tabs
	Style->Set("DisplayClusterConfigurator.Tabs.General", new IMAGE_BRUSH("Tabs/General_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.Tabs.Details", new IMAGE_BRUSH("Tabs/Details_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.Tabs.Log", new IMAGE_BRUSH("Tabs/Log_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.Tabs.OutputMapping", new IMAGE_BRUSH("Tabs/OutputMapping_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.Tabs.Scene", new IMAGE_BRUSH("Tabs/Scene_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.Tabs.Cluster", new IMAGE_BRUSH("Tabs/Cluster_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.Tabs.Viewport", new IMAGE_BRUSH("Tabs/Viewport_16x", Icon16x16));


	// Toolbar
	Style->Set("DisplayClusterConfigurator.Toolbar.Import", new IMAGE_BRUSH("Toolbar/Import_40x", Icon40x40));
	Style->Set("DisplayClusterConfigurator.Toolbar.SaveToFile", new IMAGE_BRUSH("Toolbar/SaveToFile_40x", Icon40x40));
	Style->Set("DisplayClusterConfigurator.Toolbar.EditConfig", new IMAGE_BRUSH("Toolbar/EditConfig_40x", Icon40x40));

	// TreeItems
	Style->Set("DisplayClusterConfigurator.TreeItems.Scene", new IMAGE_BRUSH("TreeItems/Scene_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.TreeItems.Cluster", new IMAGE_BRUSH("TreeItems/Cluster_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.TreeItems.Container", new IMAGE_BRUSH("TreeItems/Container_12x", Icon12x12));

	// Scene Components
	Style->Set("DisplayClusterConfigurator.TreeItems.SceneComponentXform", new IMAGE_BRUSH("TreeItems/SceneComponentXform_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.TreeItems.SceneComponentScreen", new IMAGE_BRUSH("TreeItems/SceneComponentScreen_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.TreeItems.SceneComponentCamera", new IMAGE_BRUSH("TreeItems/SceneComponentCamera_16x", Icon16x16));

	Style->Set("ClassIcon.DisplayClusterXformComponent", new IMAGE_BRUSH(TEXT("TreeItems/SceneComponentXform_16x"), Icon16x16));
	Style->Set("ClassThumbnail.DisplayClusterXformComponent", new IMAGE_BRUSH(TEXT("TreeItems/SceneComponentXform_16x"), Icon16x16));
	
	Style->Set("ClassIcon.DisplayClusterScreenComponent", new IMAGE_BRUSH(TEXT("TreeItems/SceneComponentScreen_16x"), Icon16x16));
	Style->Set("ClassThumbnail.DisplayClusterScreenComponent", new IMAGE_BRUSH(TEXT("TreeItems/SceneComponentScreen_16x"), Icon16x16));
	
	Style->Set("ClassIcon.DisplayClusterCameraComponent", new IMAGE_BRUSH(TEXT("Components/ViewOrigin/nDisplayViewOrigin_x16"), Icon16x16));
	Style->Set("ClassThumbnail.DisplayClusterCameraComponent", new IMAGE_BRUSH(TEXT("Components/ViewOrigin/nDisplayViewOrigin_x64"), Icon64x64));
	
	Style->Set("ClassIcon.DisplayClusterICVFXCameraComponent", new IMAGE_BRUSH(TEXT("Components/ICVFXCamera/nDisplayCamera_x16"), Icon16x16));

	// Cluster
	Style->Set("DisplayClusterConfigurator.TreeItems.Host", new IMAGE_BRUSH("TreeItems/ClusterNode_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.TreeItems.ClusterNode", new IMAGE_BRUSH("TreeItems/Viewport_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.TreeItems.Viewport", new IMAGE_BRUSH("TreeItems/SceneComponentScreen_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.TreeItems.Postprocess", new IMAGE_BRUSH("TreeItems/Postprocess_12x", Icon12x12));

	// Icons
	Style->Set("DisplayClusterConfigurator.OutputMapping.ToggleWindowInfo", new IMAGE_BRUSH("OutputMapping/ToggleWindowInfo_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.OutputMapping.ToggleWindowCornerImage", new IMAGE_BRUSH("OutputMapping/CornerImage_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.OutputMapping.ToggleOutsideViewports", new IMAGE_BRUSH("OutputMapping/ToggleOutsideViewports_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.OutputMapping.ZoomToFit", new IMAGE_BRUSH("OutputMapping/ZoomToFit_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.OutputMapping.ViewScale", new IMAGE_BRUSH("OutputMapping/ViewScale_16x", Icon16x16));
	Style->Set("DisplayClusterConfigurator.OutputMapping.ResizeAreaHandle", new IMAGE_BRUSH("OutputMapping/ResizeAreaHandle_20x", Icon20x20));

	Style->Set("DisplayClusterConfigurator.Node.Color.Regular", FLinearColor(FColor(97, 97, 97)));
	Style->Set("DisplayClusterConfigurator.Node.Color.Selected", FLinearColor(FColor(249, 165, 1)));
	Style->Set("DisplayClusterConfigurator.Node.Color.Regular.Opacity_50", FLinearColor(FColor(255, 255, 255, 255 * 0.5f)));
	Style->Set("DisplayClusterConfigurator.Node.Color.Selected.Opacity_50", FLinearColor(FColor(249, 165, 1, 255 * 0.5f)));

	Style->Set("DisplayClusterConfigurator.Node.Text.Color.Regular", FLinearColor(FColor(255, 255, 255)));
	Style->Set("DisplayClusterConfigurator.Node.Text.Color.WhiteGray", FLinearColor(FColor(226, 226, 226)));

	Style->Set("DisplayClusterConfigurator.Node.Host.Inner.Background", FLinearColor(FColor(38, 38, 38)));
	Style->Set("DisplayClusterConfigurator.Node.Window.Outer.Background", FLinearColor(FColor(63, 63, 63)));
	Style->Set("DisplayClusterConfigurator.Node.Window.Inner.Background", FLinearColor(FColor(53, 53, 53)));
	Style->Set("DisplayClusterConfigurator.Node.Window.Border.Color", FLinearColor(FColor(64, 164, 255, 255)));
	Style->Set("DisplayClusterConfigurator.Node.Window.Corner.Color", FLinearColor(FColor(64, 164, 255, 255)));
	Style->Set("DisplayClusterConfigurator.Node.Window.Corner.Color.Locked", FLinearColor(FColor(164, 164, 164, 255)));
	Style->Set("DisplayClusterConfigurator.Node.Viewport.Border.Color.Regular", FLinearColor(FColor(164, 164, 164, 255)));
	Style->Set("DisplayClusterConfigurator.Node.Viewport.Border.OutsideColor.Regular", FLinearColor(FColor(247, 129, 91, 255)));
	Style->Set("DisplayClusterConfigurator.Node.Viewport.Text.Background", FLinearColor(FColor(73, 73, 73, 255 * 0.65f)));
	Style->Set("DisplayClusterConfigurator.Node.Viewport.Text.Background.Locked", FLinearColor(FColor(164, 164, 164, 255)));

	Style->Set("DisplayClusterConfigurator.Node.Viewport.BackgroundColor.Regular", FLinearColor(FColor(155, 155, 155, 255 * 0.85f)));
	Style->Set("DisplayClusterConfigurator.Node.Viewport.BackgroundColor.Selected", FLinearColor(FColor(254, 178, 27, 255)));
	Style->Set("DisplayClusterConfigurator.Node.Viewport.BackgroundImage.Selected", FLinearColor(FColor(255, 204, 102)));
	Style->Set("DisplayClusterConfigurator.Node.Viewport.BackgroundImage.Locked", FLinearColor(FColor(128, 128, 128)));

	Style->Set("DisplayClusterConfigurator.Node.Viewport.OutsideBackgroundColor.Regular", FLinearColor(FColor(255, 87, 34, 255 * 0.85f)));
	Style->Set("DisplayClusterConfigurator.Node.Viewport.OutsideBackgroundColor.Selected", FLinearColor(FColor(255, 87, 34, 255)));

	Style->Set("DisplayClusterConfigurator.Node.Body", new BOX_BRUSH("Nodes/Node_Body", FMargin(16.f / 64.f, 16.f / 64.f, 16.f / 64.f, 16.f / 64.f)));
	Style->Set("DisplayClusterConfigurator.Node.Brush.Corner", new BOX_BRUSH("Nodes/Corner", 4.0f / 16.0f));

	// Node Text
	{
		FTextBlockStyle TilteTextBlockStyle = FEditorStyle::GetWidgetStyle< FTextBlockStyle >("NormalText");
		TilteTextBlockStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 28));
		Style->Set("DisplayClusterConfigurator.Node.Text.Regular", TilteTextBlockStyle);
	}

	{
		FTextBlockStyle TilteTextBlockStyle = FEditorStyle::GetWidgetStyle< FTextBlockStyle >("NormalText");
		TilteTextBlockStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 28));
		Style->Set("DisplayClusterConfigurator.Node.Text.Bold", TilteTextBlockStyle);
	}

	{
		FTextBlockStyle TilteTextBlockStyle = FEditorStyle::GetWidgetStyle<FTextBlockStyle>("Graph.StateNode.NodeTitle");
		TilteTextBlockStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 36));
		Style->Set("DisplayClusterConfigurator.Host.Text.Title", TilteTextBlockStyle);
	}

	{
		FTextBlockStyle TilteTextBlockStyle = FEditorStyle::GetWidgetStyle< FTextBlockStyle >("NormalText");
		TilteTextBlockStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Italic", 18));
		Style->Set("DisplayClusterConfigurator.Node.Text.Small", TilteTextBlockStyle);
	}

	// Canvas borders
	{
		Style->Set("DisplayClusterConfigurator.Selected.Canvas.Brush", new BORDER_BRUSH(TEXT("Slate/NonMarchingAnts"), FMargin(0.25f), Style->GetColor("DisplayClusterConfigurator.Node.Color.Selected.Opacity_50")));
		Style->Set("DisplayClusterConfigurator.Regular.Canvas.Brush", new BORDER_BRUSH(TEXT("Slate/NonMarchingAnts"), FMargin(0.25f), Style->GetColor("DisplayClusterConfigurator.Node.Color.Regular.Opacity_50")));
	}

	// Window borders
	{
		FSlateBrush* SelectedBrush = new FSlateBrush();
		SelectedBrush->Margin = FMargin(6.f);
		SelectedBrush->DrawAs = ESlateBrushDrawType::Border;
		SelectedBrush->TintColor = Style->GetColor("DisplayClusterConfigurator.Node.Color.Selected");

		Style->Set("DisplayClusterConfigurator.Node.Window.Border.Brush.Selected", SelectedBrush);

		FSlateBrush* RegularBrush = new FSlateBrush();
		RegularBrush->Margin = FMargin(1.f);
		RegularBrush->DrawAs = ESlateBrushDrawType::Border;
		RegularBrush->TintColor = Style->GetColor("DisplayClusterConfigurator.Node.Color.Regular");
		Style->Set("DisplayClusterConfigurator.Node.Window.Border.Brush.Regular", RegularBrush);
	}

	// Viewport borders
	{
		FSlateBrush* Brush = new FSlateBrush();
		Brush->Margin = FMargin(1.f);
		Brush->DrawAs = ESlateBrushDrawType::Border;
		Brush->TintColor = Style->GetColor("DisplayClusterConfigurator.Node.Window.Border.Color");

		Style->Set("DisplayClusterConfigurator.Node.Window.Border.Brush", Brush);

		FSlateBrush* SelectedBrush = new FSlateBrush();
		SelectedBrush->Margin = FMargin(2.f);
		SelectedBrush->DrawAs = ESlateBrushDrawType::Border;
		SelectedBrush->TintColor = Style->GetColor("DisplayClusterConfigurator.Node.Color.Selected");

		Style->Set("DisplayClusterConfigurator.Node.Viewport.Border.Brush.Selected", SelectedBrush);

		FSlateBrush* RegularBrush = new FSlateBrush();
		RegularBrush->Margin = FMargin(1.f);
		RegularBrush->DrawAs = ESlateBrushDrawType::Border;
		RegularBrush->TintColor = Style->GetColor("DisplayClusterConfigurator.Node.Viewport.Border.Color.Regular");
		Style->Set("DisplayClusterConfigurator.Node.Viewport.Border.Brush.Regular", RegularBrush);

		FSlateBrush* OutsideRegularBrush = new FSlateBrush();
		OutsideRegularBrush->Margin = FMargin(1.f);
		OutsideRegularBrush->DrawAs = ESlateBrushDrawType::Border;
		OutsideRegularBrush->TintColor = Style->GetColor("DisplayClusterConfigurator.Node.Viewport.Border.OutsideColor.Regular");
		Style->Set("DisplayClusterConfigurator.Node.Viewport.Border.OutsideBrush.Regular", OutsideRegularBrush);
	}

	// Corner Colors array
	{
		CornerColors.Add(FCornerColor("DisplayClusterConfigurator.Node.Corner.Color.0", FLinearColor(FColor(244, 67, 54, 255 * 0.8f))));
		CornerColors.Add(FCornerColor("DisplayClusterConfigurator.Node.Corner.Color.1", FLinearColor(FColor(156, 39, 176, 255 * 0.8f))));
		CornerColors.Add(FCornerColor("DisplayClusterConfigurator.Node.Corner.Color.2", FLinearColor(FColor(0, 188, 212, 255 * 0.8f))));
		CornerColors.Add(FCornerColor("DisplayClusterConfigurator.Node.Corner.Color.3", FLinearColor(FColor(139, 195, 74, 255 * 0.8f))));
		CornerColors.Add(FCornerColor("DisplayClusterConfigurator.Node.Corner.Color.4", FLinearColor(FColor(255, 235, 59, 255 * 0.8f))));
		CornerColors.Add(FCornerColor("DisplayClusterConfigurator.Node.Corner.Color.5", FLinearColor(FColor(96, 125, 139, 255 * 0.8f))));


		for (const FCornerColor& CornerColor : CornerColors)
		{
			Style->Set(CornerColor.Name, CornerColor.Color);
		}
	}

	return Style;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH

void FDisplayClusterConfiguratorStyle::ReloadTextures()
{
	FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
}

const ISlateStyle& FDisplayClusterConfiguratorStyle::Get()
{
	return *StyleInstance;
}
