// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformEditorStyle.h"

#include "AudioWidgetsSlateTypes.h"
#include "Styling/SlateStyleRegistry.h"
#include "WaveformEditorWidgetsSettings.h"

static FLazyName PlayheadOverlayStyleName("WaveformEditorPlayheadOverlay.Style");
static FLazyName ValueGridOverlayStyleName("WaveformEditorValueGrid.Style");
static FLazyName WaveformEditorRulerStyleName("WaveformEditorRuler.Style");
static FLazyName WaveformViewerStyleName("WaveformViewer.Style");


FName FWaveformEditorStyle::StyleName("WaveformEditorStyle");
TUniquePtr<FWaveformEditorStyle> FWaveformEditorStyle::StyleInstance = nullptr;

FWaveformEditorStyle::FWaveformEditorStyle()
	: FSlateStyleSet(StyleName)
{
}

FWaveformEditorStyle::~FWaveformEditorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

FWaveformEditorStyle& FWaveformEditorStyle::Get()
{
	check(StyleInstance);
	return *StyleInstance;
}

void FWaveformEditorStyle::Init()
{
	if (StyleInstance == nullptr)
	{
		StyleInstance = MakeUnique<FWaveformEditorStyle>();
	}

	const UWaveformEditorWidgetsSettings* Settings = GetWidgetsSettings();
	
	check(Settings);
	Settings->OnSettingChanged().AddStatic(&FWaveformEditorStyle::OnWidgetSettingsUpdated);

	StyleInstance->SetParentStyleName("CoreStyle");

	//Waveform Viewer style
	FSampledSequenceViewerStyle WaveViewerStyle = FSampledSequenceViewerStyle()
		.SetSequenceColor(Settings->WaveformColor)
		.SetBackgroundColor(Settings->WaveformBackgroundColor)
		.SetSequenceLineThickness(Settings->WaveformLineThickness)
		.SetSampleMarkersSize(Settings->SampleMarkersSize)
		.SetMajorGridLineColor(Settings->MajorGridColor)
		.SetMinorGridLineColor(Settings->MinorGridColor)
		.SetZeroCrossingLineColor(Settings->LoudnessGridColor)
		.SetZeroCrossingLineThickness(Settings->ZeroCrossingLineThickness);

	StyleInstance->Set(WaveformViewerStyleName, WaveViewerStyle);

	//Playhead Overlay style
	FPlayheadOverlayStyle PlayheadOverlayStyle = FPlayheadOverlayStyle().SetPlayheadColor(Settings->PlayheadColor);
	StyleInstance->Set(PlayheadOverlayStyleName, PlayheadOverlayStyle);

	//Time Ruler style 
	FFixedSampleSequenceRulerStyle TimeRulerStyle = FFixedSampleSequenceRulerStyle()
		.SetHandleColor(Settings->PlayheadColor)
		.SetTicksColor(Settings->RulerTicksColor)
		.SetTicksTextColor(Settings->RulerTextColor)
		.SetHandleColor(Settings->PlayheadColor)
		.SetFontSize(Settings->RulerFontSize);

	StyleInstance->Set(WaveformEditorRulerStyleName, TimeRulerStyle);

	//ValueGridOverlayStyle
	FSampledSequenceValueGridOverlayStyle ValueGridOverlayStyle = FSampledSequenceValueGridOverlayStyle()
		.SetGridColor(Settings->LoudnessGridColor)
		.SetGridThickness(Settings->LoudnessGridThickness)
		.SetLabelTextColor(Settings->LoudnessGridTextColor)
		.SetLabelTextFontSize(Settings->LoudnessGridTextSize);

	StyleInstance->Set(ValueGridOverlayStyleName, ValueGridOverlayStyle);

	FSlateStyleRegistry::RegisterSlateStyle(StyleInstance->Get());
}

const UWaveformEditorWidgetsSettings* FWaveformEditorStyle::GetWidgetsSettings() 
{
	const UWaveformEditorWidgetsSettings* WaveformEditorWidgetsSettings = GetDefault<UWaveformEditorWidgetsSettings>();
	check(WaveformEditorWidgetsSettings);

	return WaveformEditorWidgetsSettings;
}

void FWaveformEditorStyle::OnWidgetSettingsUpdated(const FName& PropertyName, const UWaveformEditorWidgetsSettings* Settings)
{
	TSharedRef<FSampledSequenceViewerStyle> WaveformViewerStyle = GetRegisteredWidgetStyle<FSampledSequenceViewerStyle>(WaveformViewerStyleName);
	TSharedRef<FPlayheadOverlayStyle> PlayheadOverlayStyle = GetRegisteredWidgetStyle<FPlayheadOverlayStyle>(PlayheadOverlayStyleName);
	TSharedRef<FFixedSampleSequenceRulerStyle> WaveformEditorTimeRulerStyle = GetRegisteredWidgetStyle<FFixedSampleSequenceRulerStyle>(WaveformEditorRulerStyleName);
	TSharedRef<FSampledSequenceValueGridOverlayStyle> ValueGridOverlayStyle = GetRegisteredWidgetStyle<FSampledSequenceValueGridOverlayStyle>(ValueGridOverlayStyleName);


	if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, WaveformColor))
	{
		WaveformViewerStyle->SetSequenceColor(Settings->WaveformColor);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, WaveformBackgroundColor))
	{
		WaveformViewerStyle->SetBackgroundColor(Settings->WaveformBackgroundColor);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, WaveformLineThickness))
	{
		WaveformViewerStyle->SetSequenceLineThickness(Settings->WaveformLineThickness);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, SampleMarkersSize))
	{
		WaveformViewerStyle->SetSampleMarkersSize(Settings->SampleMarkersSize);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, MajorGridColor))
	{
		WaveformViewerStyle->SetMajorGridLineColor(Settings->MajorGridColor);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, MinorGridColor))
	{
		WaveformViewerStyle->SetMinorGridLineColor(Settings->MinorGridColor);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, ZeroCrossingLineThickness))
	{
		WaveformViewerStyle->SetZeroCrossingLineThickness(Settings->ZeroCrossingLineThickness);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, PlayheadColor))
	{
		PlayheadOverlayStyle->SetPlayheadColor(Settings->PlayheadColor);
		WaveformEditorTimeRulerStyle->SetHandleColor(Settings->PlayheadColor);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, RulerBackgroundColor))
	{
		WaveformEditorTimeRulerStyle->SetBackgroundColor(Settings->RulerBackgroundColor);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, RulerTicksColor))
	{
		WaveformEditorTimeRulerStyle->SetTicksColor(Settings->RulerTicksColor);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, RulerTextColor))
	{
		WaveformEditorTimeRulerStyle->SetTicksTextColor(Settings->RulerTextColor);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, RulerFontSize))
	{
		WaveformEditorTimeRulerStyle->SetFontSize(Settings->RulerFontSize);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, LoudnessGridColor))
	{
		ValueGridOverlayStyle->SetGridColor(Settings->LoudnessGridColor);
		WaveformViewerStyle->SetZeroCrossingLineColor(Settings->LoudnessGridColor);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, LoudnessGridThickness))
	{
		ValueGridOverlayStyle->SetGridThickness(Settings->LoudnessGridThickness);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, LoudnessGridTextColor))
	{
		ValueGridOverlayStyle->SetLabelTextColor(Settings->LoudnessGridTextColor);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaveformEditorWidgetsSettings, LoudnessGridTextSize))
	{
		ValueGridOverlayStyle->SetLabelTextFontSize(Settings->LoudnessGridTextSize);
	}

}
