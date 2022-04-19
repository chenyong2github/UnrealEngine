// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SCommonEditorViewportToolbarBase.h"

class SPoseSearchDatabaseViewport;

class SPoseSearchDatabaseViewportToolBar : public SCommonEditorViewportToolbarBase
{
public:
	SLATE_BEGIN_ARGS(SPoseSearchDatabaseViewportToolBar)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<SPoseSearchDatabaseViewport> InViewport);

	// ~SCommonEditorViewportToolbarBase interface
	virtual TSharedRef<SWidget> GenerateShowMenu() const override;
	// ~End of SCommonEditorViewportToolbarBase interface
};
