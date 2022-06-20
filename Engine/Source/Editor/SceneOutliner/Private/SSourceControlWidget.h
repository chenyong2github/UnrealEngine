// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ISceneOutliner.h"
#include "SceneOutlinerPublicTypes.h"
#include "Widgets/Images/SLayeredImage.h"
#include "SourceControlHelpers.h"

class SSourceControlWidget : public SLayeredImage
{
public:
	SLATE_BEGIN_ARGS(SSourceControlWidget) {}
	SLATE_END_ARGS()

	/** Construct this widget */
	void Construct(const FArguments& InArgs, TWeakPtr<ISceneOutliner> InWeakOutliner, TWeakPtr<ISceneOutlinerTreeItem> InWeakTreeItem);

	~SSourceControlWidget();

	FSourceControlStatePtr GetSourceControlState();

	FString GetPackageName() { return ExternalPackageName; }

	UPackage* GetPackage() { return ExternalPackage; }

private:

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override;

	void ConnectSourceControl();

	void DisconnectSourceControl();

	void HandleSourceControlStateChanged(EStateCacheUsage::Type CacheUsage);

	void HandleSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider);

	void UpdateSourceControlStateIcon(FSourceControlStatePtr SourceControlState);

	/** The tree item we relate to */
	TWeakPtr<ISceneOutlinerTreeItem> WeakTreeItem;

	/** Reference back to the outliner so we can set visibility of a whole selection */
	TWeakPtr<ISceneOutliner> WeakOutliner;

	/** Cache the items external package name */
	FString ExternalPackageName;
	UPackage* ExternalPackage = nullptr;

	/** Source control state changed delegate handle */
	FDelegateHandle SourceControlStateChangedDelegateHandle;

	/** Source control provider changed delegate handle */
	FDelegateHandle SourceControlProviderChangedDelegateHandle;

	/** Actor packaging mode changed delegate handle */
	FDelegateHandle ActorPackingModeChangedDelegateHandle;
};
