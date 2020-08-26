// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"

class STextBlock;
class SWidget;
class UEdMode;

namespace UE
{
namespace MotionTrailEditor
{

class FMotionTrailEditorModeToolkit : public FModeToolkit
{
public:

	FMotionTrailEditorModeToolkit();
	
	/** FModeToolkit interface */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual UEdMode* GetScriptableEditorMode() const override;
	TSharedPtr<SWidget> GetInlineContent() const override;

	void SetTimingStats(const TArray<TMap<FString, FTimespan>>& HierarchyStats);

private:
	TSharedPtr<STextBlock> TimingStatsTextWidget;
};

} // namespace MovieScene
} // namespace UE
