// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "WidgetBlueprint.h"

#include "SynthesisEditorSettings.generated.h"


UCLASS(config = EditorSettings, defaultconfig, meta = (DisplayName = "Synthesis"))
class USynthesisEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath BitCrusherWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath ChorusWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath DynamicsProcessorWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath EnvelopeFollowerWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath EQWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (DisplayName = "Filter Widget (Source)", AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath FilterWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath FoldbackDistortionWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath MidSideSpreaderWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath PannerWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath PhaserWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath RingModulationWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath SimpleDelayWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath StereoDelayWidget;

	UPROPERTY(config, EditAnywhere, Category = "Source Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath WaveShaperWidget;

	UPROPERTY(config, EditAnywhere, Category = "Submix Effects", meta = (DisplayName = "Convolution Reverb Widget", AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath SubmixConvolutionReverbWidget;

	UPROPERTY(config, EditAnywhere, Category = "Submix Effects", meta = (DisplayName = "Delay Widget (Submix)", AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath SubmixDelayWidget;

	UPROPERTY(config, EditAnywhere, Category = "Submix Effects", meta = (DisplayName = "Filter Widget (Submix)", AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath SubmixFilterWidget;

	UPROPERTY(config, EditAnywhere, Category = "Submix Effects", meta = (DisplayName = "Flexiverb Widget", AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath SubmixFlexiverbWidget;

	UPROPERTY(config, EditAnywhere, Category = "Submix Effects", meta = (DisplayName = "Stereo Delay (Submix)", AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath SubmixStereoDelayWidget;

	UPROPERTY(config, EditAnywhere, Category = "Submix Effects", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath SubmixTapDelayWidget;

	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
};
