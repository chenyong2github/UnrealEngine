// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FAssetData;
class UWorld;
struct FLevelSnapshotsEditorViewBuilder;

class SLevelSnapshotsEditorBrowser : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorBrowser)
	{}

	/** Attribute for retrieving the current context */
	SLATE_ATTRIBUTE(FSoftObjectPath, OwningWorldPath)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder);

private:
	void OnAssetSelected(const FAssetData& InAssetData);
	bool OnShouldFilterAsset(const FAssetData& InAssetData);

	TAttribute<FSoftObjectPath> OwningWorldPathAttribute;

	TWeakPtr<FLevelSnapshotsEditorViewBuilder> BuilderPtr;
};
