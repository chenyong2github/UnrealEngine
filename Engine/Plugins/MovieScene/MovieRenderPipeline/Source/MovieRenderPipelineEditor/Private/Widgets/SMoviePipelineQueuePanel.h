// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/GCObject.h"

class UMoviePipelineConfigBase;
class SMoviePipelineQueueEditor;

/**
 * Outermost widget that is used for setting up a new movie render pipeline queue. Operates on a transient object that is internally owned and maintained 
 */
class SMoviePipelineQueuePanel : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SMoviePipelineQueuePanel)
		: _BasePreset(nullptr)

		{}

		/*~ All following arguments are mutually-exclusive */
		/*-------------------------------------------------*/
		/** A preset asset to base the pipeline off */
		SLATE_ARGUMENT(UMoviePipelineConfigBase*, BasePreset)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	// ~FGCObject Interface

	FReply OnRenderLocalRequested();
	bool IsRenderLocalEnabled() const;
	FReply OnRenderRemoteRequested();
	bool IsRenderRemoteEnabled() const;

private:
	/** Allocates a transient preset so that the user can use the pipeline without saving it to an asset first. */
	//UMoviePipelineConfigBase* AllocateTransientPreset();


private:
	/** The main movie pipeline queue editor widget */
	TSharedPtr<SMoviePipelineQueueEditor> PipelineQueueEditorWidget;

	/** The transient preset that we use - kept alive by AddReferencedObjects */
	// UMoviePipelineConfigBase* TransientPreset;
};