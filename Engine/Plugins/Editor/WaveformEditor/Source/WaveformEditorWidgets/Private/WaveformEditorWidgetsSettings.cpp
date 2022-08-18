// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformEditorWidgetsSettings.h"

UWaveformEditorWidgetsSettings::UWaveformEditorWidgetsSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, PlayheadColor(FLinearColor(255, 0.1, 0.2, 1.0))
	, WaveformColor(FLinearColor::White)
	, WaveformBackgroundColor(FLinearColor(0.02, 0.02, 0.02, 1.f))
	, MajorGridColor(FLinearColor::Black)
	, MinorGridColor(FLinearColor(0.f, 0.f, 0.f, 0.5))
	, RulerBackgroundColor(FLinearColor::Black)
	, RulerTicksColor(FLinearColor(1.f, 1.f, 1.f, 0.9))
	, RulerTextColor(FLinearColor(1.f, 1.f, 1.f, 0.9))
	, RulerFontSize(10.f)
{
}

FName UWaveformEditorWidgetsSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FText UWaveformEditorWidgetsSettings::GetSectionText() const
{
	return NSLOCTEXT("WaveformEditorDisplay", "WaveformEditorDisplaySettingsSection", "Waveform Editor Display");
}

FName UWaveformEditorWidgetsSettings::GetSectionName() const
{
	return TEXT("Waveform Editor Display");
}

void UWaveformEditorWidgetsSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != nullptr)
	{
		SettingsChangedDelegate.Broadcast(PropertyChangedEvent.GetPropertyName(), this);
	}
}

FOnWaveformEditorWidgetsSettingsChanged& UWaveformEditorWidgetsSettings::OnSettingChanged()
{
	return SettingsChangedDelegate;
}

FOnWaveformEditorWidgetsSettingsChanged UWaveformEditorWidgetsSettings::SettingsChangedDelegate;
