// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/DynamicEntryBoxBase.h"
#include "CommonBoundActionButton.h"
#include "CommonBoundActionBar.generated.h"

class IWidgetCompilerLog;
class IConsoleVariable;

UCLASS()
class COMMONUI_API UCommonBoundActionBar : public UDynamicEntryBoxBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = CommonBoundActionBar)
	void SetDisplayOwningPlayerActionsOnly(bool bShouldOnlyDisplayOwningPlayerActions);

protected:
	virtual void OnWidgetRebuilt() override;
	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	void ValidateCompiledDefaults(IWidgetCompilerLog& CompileLog) const override;
#endif

private:
	void HandleBoundActionsUpdated(bool bFromOwningPlayer);
	void HandleDeferredDisplayUpdate();
	void HandlePlayerAdded(int32 PlayerIdx);
	
	void MonitorPlayerActions(const ULocalPlayer* NewPlayer);

	UPROPERTY(EditAnywhere, Category = EntryLayout)
	TSubclassOf<UCommonBoundActionButton> ActionButtonClass;

	UPROPERTY(EditAnywhere, Category = Display)
	bool bDisplayOwningPlayerActionsOnly = true;

	bool bIsRefreshQueued = false;
};