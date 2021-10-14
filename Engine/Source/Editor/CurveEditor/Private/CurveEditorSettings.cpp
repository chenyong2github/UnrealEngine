// Copyright Epic Games, Inc. All Rights Reserved.

#include "CurveEditorSettings.h"

UCurveEditorSettings::UCurveEditorSettings()
{
	bAutoFrameCurveEditor = true;
	FrameInputPadding = 50;
	FrameOutputPadding = 50;
	bShowCurveEditorCurveToolTips = true;
	TangentVisibility = ECurveEditorTangentVisibility::SelectedKeys;
	ZoomPosition = ECurveEditorZoomPosition::CurrentTime;
}

bool UCurveEditorSettings::GetAutoFrameCurveEditor() const
{
	return bAutoFrameCurveEditor;
}

void UCurveEditorSettings::SetAutoFrameCurveEditor(bool InbAutoFrameCurveEditor)
{
	if (bAutoFrameCurveEditor != InbAutoFrameCurveEditor)
	{
		bAutoFrameCurveEditor = InbAutoFrameCurveEditor;
		SaveConfig();
	}
}

int32 UCurveEditorSettings::GetFrameInputPadding() const
{
	return FrameInputPadding;
}

void UCurveEditorSettings::SetFrameInputPadding(int32 InFrameInputPadding)
{
	if (FrameInputPadding != InFrameInputPadding)
	{
		FrameInputPadding = InFrameInputPadding;
		SaveConfig();
	}
}

int32 UCurveEditorSettings::GetFrameOutputPadding() const
{
	return FrameOutputPadding;
}

void UCurveEditorSettings::SetFrameOutputPadding(int32 InFrameOutputPadding)
{
	if (FrameOutputPadding != InFrameOutputPadding)
	{
		FrameOutputPadding = InFrameOutputPadding;
		SaveConfig();
	}
}

bool UCurveEditorSettings::GetShowCurveEditorCurveToolTips() const
{
	return bShowCurveEditorCurveToolTips;
}

void UCurveEditorSettings::SetShowCurveEditorCurveToolTips(bool InbShowCurveEditorCurveToolTips)
{
	if (bShowCurveEditorCurveToolTips != InbShowCurveEditorCurveToolTips)
	{
		bShowCurveEditorCurveToolTips = InbShowCurveEditorCurveToolTips;
		SaveConfig();
	}
}

ECurveEditorTangentVisibility UCurveEditorSettings::GetTangentVisibility() const
{
	return TangentVisibility;
}

void UCurveEditorSettings::SetTangentVisibility(ECurveEditorTangentVisibility InTangentVisibility)
{
	if (TangentVisibility != InTangentVisibility)
	{
		TangentVisibility = InTangentVisibility;
		SaveConfig();
	}
}

ECurveEditorZoomPosition UCurveEditorSettings::GetZoomPosition() const
{
	return ZoomPosition;
}

void UCurveEditorSettings::SetZoomPosition(ECurveEditorZoomPosition InZoomPosition)
{
	if (ZoomPosition != InZoomPosition)
	{
		ZoomPosition = InZoomPosition;
		SaveConfig();
	}
}

TOptional<FLinearColor> UCurveEditorSettings::GetCustomColor(UClass* InClass, const FString& InPropertyName) const
{
	TOptional<FLinearColor> Color;
	for (const FCustomColorForChannel& CustomColor : CustomColors)
	{
		UClass* Class = CustomColor.Object.LoadSynchronous();
		if (Class == InClass && CustomColor.PropertyName == InPropertyName)
		{
			Color = CustomColor.Color;
			break;
		}
	}
	return Color;
}
void UCurveEditorSettings::SetCustomColor(UClass* InClass, const FString& InPropertyName, FLinearColor InColor)
{
	TOptional<FLinearColor> Color;
	for (FCustomColorForChannel& CustomColor : CustomColors)
	{
		UClass* Class = CustomColor.Object.LoadSynchronous();
		if (Class == InClass && CustomColor.PropertyName == InPropertyName)
		{
			CustomColor.Color = InColor;
			SaveConfig(); 
			return;
		}
	}
	FCustomColorForChannel NewColor;
	NewColor.Object = InClass;
	NewColor.PropertyName = InPropertyName;
	NewColor.Color = InColor;
	CustomColors.Add(NewColor);
	SaveConfig();
}

void UCurveEditorSettings::DeleteCustomColor(UClass* InClass, const FString& InPropertyName)
{
	TOptional<FLinearColor> Color;
	for (int32 Index = 0;Index < CustomColors.Num(); ++Index)
	{
		FCustomColorForChannel& CustomColor = CustomColors[Index];
		UClass* Class = CustomColor.Object.LoadSynchronous();
		if (Class == InClass && CustomColor.PropertyName == InPropertyName)
		{
			CustomColors.RemoveAt(Index);
			SaveConfig();
			return;
		}
	}
}

FLinearColor UCurveEditorSettings::GetNextRandomColor()
{
	static TArray<FLinearColor> IndexedColor;
	static int32 NextIndex = 0;
	if (IndexedColor.Num() == 0)
	{
		IndexedColor.Add(FLinearColor(FColor::Magenta));
		IndexedColor.Add(FLinearColor(FColor::Cyan));
		IndexedColor.Add(FLinearColor(FColor::Turquoise));
		IndexedColor.Add(FLinearColor(FColor::Orange));
		IndexedColor.Add(FLinearColor(FColor::Yellow));
		IndexedColor.Add(FLinearColor(FColor::Purple));
		IndexedColor.Add(FLinearColor(FColor::Silver));
		IndexedColor.Add(FLinearColor(FColor::Emerald));
		IndexedColor.Add(FLinearColor(FColor::White));
		IndexedColor.Add(FLinearColor(FColor::Red));
		IndexedColor.Add(FLinearColor(FColor::Green));
		IndexedColor.Add(FLinearColor(FColor::Blue));
	}
	FLinearColor Color = IndexedColor[NextIndex];
	++NextIndex;
	int32 NewIndex = (NextIndex % IndexedColor.Num());
	NextIndex = NewIndex;
	return Color;
}