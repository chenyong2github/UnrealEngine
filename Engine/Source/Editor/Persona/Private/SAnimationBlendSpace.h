// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SAnimationBlendSpaceBase.h"
#include "AnimationBlendSpaceHelpers.h"

class SBlendSpaceEditor : public SBlendSpaceEditorBase
{
public:
	SLATE_BEGIN_ARGS(SBlendSpaceEditor)
		: _BlendSpace(nullptr)		
		, _DisplayScrubBar(true)
		, _StatusBarName(TEXT("AssetEditor.AnimationEditor.MainMenu"))
		{}

	SLATE_ARGUMENT(UBlendSpace*, BlendSpace)

	SLATE_ARGUMENT(bool, DisplayScrubBar)

	SLATE_EVENT(FOnBlendSpaceSampleDoubleClicked, OnBlendSpaceSampleDoubleClicked)

	SLATE_EVENT(FOnBlendSpaceSampleAdded, OnBlendSpaceSampleAdded)

	SLATE_EVENT(FOnBlendSpaceSampleRemoved, OnBlendSpaceSampleRemoved)

	SLATE_EVENT(FOnBlendSpaceSampleReplaced, OnBlendSpaceSampleReplaced)

	SLATE_EVENT(FOnGetBlendSpaceSampleName, OnGetBlendSpaceSampleName)

	SLATE_EVENT(FOnExtendBlendSpaceSampleTooltip, OnExtendSampleTooltip)

	SLATE_EVENT(FOnSetBlendSpacePreviewPosition, OnSetPreviewPosition)

	SLATE_ATTRIBUTE(FVector, PreviewPosition)

	SLATE_ATTRIBUTE(FVector, PreviewFilteredPosition)

	SLATE_ARGUMENT(FName, StatusBarName)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void Construct(const FArguments& InArgs, const TSharedRef<class IPersonaPreviewScene>& InPreviewScene);

protected:
	virtual void ResampleData() override;

	/**
	* Triangle Generator
	 */
	FDelaunayTriangleGenerator Generator;
	/**
	 * Blend Space Grid to represent data
	 */
	FBlendSpaceGrid	BlendSpaceGrid;
};
