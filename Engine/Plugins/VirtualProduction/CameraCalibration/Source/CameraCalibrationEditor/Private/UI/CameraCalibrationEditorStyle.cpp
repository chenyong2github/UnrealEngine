// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/CameraCalibrationEditorStyle.h"

#include "Brushes/SlateImageBrush.h"
#include "EditorStyleSet.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

namespace CameraCalibrationEditorStyle
{
	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);
	const FVector2D Icon64x64(64.0f, 64.0f);

	const FName NAME_StyleName(TEXT("CameraCalibrationStyle"));

	static TUniquePtr<FSlateStyleSet> StyleInstance;
}

#define IMAGE_BRUSH(RelativePath, ...) FSlateImageBrush(CameraCalibrationEditorStyle::StyleInstance->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)

void FCameraCalibrationEditorStyle::Register()
{
	CameraCalibrationEditorStyle::StyleInstance = MakeUnique<FSlateStyleSet>(CameraCalibrationEditorStyle::NAME_StyleName);
	CameraCalibrationEditorStyle::StyleInstance->SetContentRoot(FPaths::EnginePluginsDir() / TEXT("VirtualProduction/CameraCalibration/Content/Editor/Icons/"));

	CameraCalibrationEditorStyle::StyleInstance->Set("ClassThumbnail.LensFile", new IMAGE_BRUSH("LensFileIcon_64x", CameraCalibrationEditorStyle::Icon64x64));
	CameraCalibrationEditorStyle::StyleInstance->Set("ClassIcon.LensFile", new IMAGE_BRUSH("LensFileIcon_20x", CameraCalibrationEditorStyle::Icon20x20));

	FTextBlockStyle ButtonTextStyle = FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("ContentBrowser.TopBar.Font");
	FLinearColor ButtonTextColor = ButtonTextStyle.ColorAndOpacity.GetSpecifiedColor();
	ButtonTextColor.A /= 2;
	ButtonTextStyle.ColorAndOpacity = ButtonTextColor;
	ButtonTextStyle.ShadowColorAndOpacity.A /= 2;
	CameraCalibrationEditorStyle::StyleInstance->Set("CameraCalibration.Button.TextStyle", ButtonTextStyle);

	FButtonStyle RemoveButtonStyle = FEditorStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton");
	RemoveButtonStyle.Normal = FSlateNoResource();
	RemoveButtonStyle.NormalPadding = FMargin(0, 1.5f);
	RemoveButtonStyle.PressedPadding = FMargin(0, 1.5f);
	CameraCalibrationEditorStyle::StyleInstance->Set("CameraCalibration.RemoveButton", RemoveButtonStyle);

	FSlateStyleRegistry::RegisterSlateStyle(*CameraCalibrationEditorStyle::StyleInstance.Get());
}

void FCameraCalibrationEditorStyle::Unregister()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*CameraCalibrationEditorStyle::StyleInstance.Get());
	CameraCalibrationEditorStyle::StyleInstance.Reset();
}

FName FCameraCalibrationEditorStyle::GetStyleSetName()
{
	return CameraCalibrationEditorStyle::NAME_StyleName;
}

const ISlateStyle& FCameraCalibrationEditorStyle::Get()
{
	check(CameraCalibrationEditorStyle::StyleInstance.IsValid());
	return *CameraCalibrationEditorStyle::StyleInstance.Get();
}

#undef IMAGE_BRUSH
