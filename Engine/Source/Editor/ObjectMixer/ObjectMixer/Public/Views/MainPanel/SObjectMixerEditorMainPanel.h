// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class FObjectMixerEditorMainPanel;

class OBJECTMIXEREDITOR_API SObjectMixerEditorMainPanel final : public SCompoundWidget
{

public:

	SLATE_BEGIN_ARGS(SObjectMixerEditorMainPanel)
	{}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FObjectMixerEditorMainPanel>& InMainPanel);

	virtual ~SObjectMixerEditorMainPanel() override;

private:

	/** A reference to the struct that controls this widget */
	TWeakPtr<FObjectMixerEditorMainPanel> MainPanel;
};
