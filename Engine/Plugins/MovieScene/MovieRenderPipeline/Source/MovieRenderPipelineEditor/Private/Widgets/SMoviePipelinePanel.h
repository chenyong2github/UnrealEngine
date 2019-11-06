// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/GCObject.h"

class UMoviePipelineConfigBase;
class SMoviePipelineEditor;

/**
 * Outermost widget that is used for setting up a new movie render pipeline config. Operates on a transient UMovieRenderShotConfig that is internally owned and maintained 
 */
class SMoviePipelinePanel : public SCompoundWidget, public FGCObject
{
public:

	~SMoviePipelinePanel();

	SLATE_BEGIN_ARGS(SMoviePipelinePanel)
		: _BasePreset(nullptr)

		{}

		/*~ All following arguments are mutually-exclusive */
		/*-------------------------------------------------*/
		/** A preset asset to base the pipeline off */
		SLATE_ARGUMENT(UMoviePipelineConfigBase*, BasePreset)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	UMoviePipelineConfigBase* GetMoviePipeline() const;

private:
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

private:
	/** Generate the widget that is visible in the Choose Preset dropdown. */
	TSharedRef<SWidget> OnGeneratePresetsMenu();

	/** Called when the user wishes to revert their changes to the current preset. */
	FReply OnRevertChanges();

	/** Allocates a transient preset so that the user can use the pipeline without saving it to an asset first. */
	UMoviePipelineConfigBase* AllocateTransientPreset();


private:
	/** The transient preset that we use - kept alive by AddReferencedObjects */
	UMoviePipelineConfigBase* TransientPreset;

	/** The main movie pipeline editor widget */
	TSharedPtr<SMoviePipelineEditor> MoviePipelineEditorWidget;
};