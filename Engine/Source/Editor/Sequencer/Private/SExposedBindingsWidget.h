// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ISequencer;
class FMenuBuilder;

class SExposedBindingsWidget : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SExposedBindingsWidget){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<ISequencer> InWeakSequencer);

private:

	void Reconstruct();

	void MakeSubMenu(FMenuBuilder& MenuBuilder, FName ExposedName);

	void OnNewTextCommitted(const FText& InNewText, ETextCommit::Type CommitType);

	void ExposeAsName(FName InNewName);

	FReply RemoveExposedName(FName InNameToRemove);

	FText GetSubMenuLabel(FName ExposedName) const;

private:

	/** The sequencer UI instance that is currently open */
	TWeakPtr<ISequencer> WeakSequencer;
};