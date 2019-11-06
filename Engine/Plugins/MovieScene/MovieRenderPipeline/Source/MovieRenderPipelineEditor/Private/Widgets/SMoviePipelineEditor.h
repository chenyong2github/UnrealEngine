// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectKey.h"

struct FAssetData;
struct IMoviePipelineSettingTreeItem;

class SScrollBox;
class SMoviePipelineSettings;
class UMoviePipelineConfigBase;
class IDetailsView;
class UMoviePipelineSetting;

template<class> class TSubclassOf;

/**
 * Widget used to edit a Movie Render Pipeline Shot Config.
 */
class SMoviePipelineEditor : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMoviePipelineEditor)
		: _MoviePipeline(nullptr)
		{}

		SLATE_ATTRIBUTE(UMoviePipelineConfigBase*, MoviePipeline)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Construct a button that can add sources to this widget's preset
	 */
	TSharedRef<SWidget> MakeAddSettingButton();

private:

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:

	/**
	 * Check to see whether the movie pipeline ptr has changed, and propagate that change if necessary
	 */
	void CheckForNewMoviePipeline();

	/**
	 * Update the details panel for the current selection
	 */
	void UpdateDetails();

private:

	TSharedRef<SWidget> OnGenerateSettingsMenu();
     
	void AddSettingFromClass(TSubclassOf<UMoviePipelineSetting> SettingClass);
	bool CanAddSettingFromClass(TSubclassOf<UMoviePipelineSetting> SettingClass);
	
	void OnSettingsSelectionChanged(TSharedPtr<IMoviePipelineSettingTreeItem>, ESelectInfo::Type) { bRequestDetailsRefresh = true; }

private:

	bool bRequestDetailsRefresh;
	TAttribute<UMoviePipelineConfigBase*> MoviePipelineAttribute;
	TWeakObjectPtr<UMoviePipelineConfigBase> CachedMoviePipeline;
    
	TSharedPtr<SMoviePipelineSettings> SettingsWidget;
	TSharedPtr<SScrollBox> DetailsBox;
	TMap<FObjectKey, TSharedPtr<IDetailsView>> ClassToDetailsView;
};