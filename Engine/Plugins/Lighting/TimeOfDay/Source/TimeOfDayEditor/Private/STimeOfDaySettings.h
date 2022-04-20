// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

enum class ESettingsSection : uint8;

class SWidgetSwitcher;
class SSubobjectInstanceEditor;
class ATimeOfDayActor;
class IDetailsView;
class FSubobjectEditorTreeNode;

class STimeOfDaySettings : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(STimeOfDaySettings)
	{}
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);

private:
	void OnSettingsSectionChanged(ESettingsSection NewSection);
	void OnMapChanged(uint32 Flags);
	UObject* GetObjectContext() const;
	void UpdateTimeOfDayActor();
	FReply OnEditDaySequenceClicked();
	void OnSubobjectEditorTreeViewSelectionChanged(const TArray<TSharedPtr<FSubobjectEditorTreeNode> >& SelectedNodes);

	TSharedRef<SWidget> MakeEnvironmentPanel();
	TSharedRef<SWidget> MakeEditDaySequencePanel();
private:
	TSharedPtr<SWidgetSwitcher> SettingsSwitcher;
	TSharedPtr<SSubobjectInstanceEditor> SubobjectEditor;
	TWeakObjectPtr<ATimeOfDayActor> EditorTimeOfDayActor;
	TSharedPtr<IDetailsView> ComponentDetailsView;
};