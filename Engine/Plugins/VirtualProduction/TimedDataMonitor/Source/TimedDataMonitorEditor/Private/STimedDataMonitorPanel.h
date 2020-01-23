// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "SlateOptMacros.h"


class FWorkspaceItem;
class STimedDataInputListView;


class STimedDataMonitorPanel : public SCompoundWidget
{
public:
	static void RegisterNomadTabSpawner(TSharedRef<FWorkspaceItem> InWorkspaceItem);
	static void UnregisterNomadTabSpawner();
	static TSharedPtr<STimedDataMonitorPanel> GetPanelInstance();

private:
	using Super = SCompoundWidget;
	static TWeakPtr<STimedDataMonitorPanel> WidgetInstance;

public:
	SLATE_BEGIN_ARGS(STimedDataMonitorPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<STimedDataInputListView> TimedDataSourceList;

	static FDelegateHandle LevelEditorTabManagerChangedHandle;

};