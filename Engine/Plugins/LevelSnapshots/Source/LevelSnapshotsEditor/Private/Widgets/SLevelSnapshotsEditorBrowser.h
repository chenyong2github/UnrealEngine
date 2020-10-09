// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FAssetData;
class UWorld;

class SLevelSnapshotsEditorBrowser : public SCompoundWidget
{
public:
	~SLevelSnapshotsEditorBrowser();

	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorBrowser)
	{}

	/** Attribute for retrieving the current context */
	SLATE_ATTRIBUTE(UWorld*, Value)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void OnAssetSelected(const FAssetData& InAssetData);
	bool OnShouldFilterAsset(const FAssetData& InAssetData);

	TAttribute<UWorld*> ValueAttribute;
};
