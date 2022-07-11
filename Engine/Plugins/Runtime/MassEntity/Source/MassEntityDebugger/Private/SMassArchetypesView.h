// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "SMassDebuggerViewBase.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/SListView.h"

struct FMassDebuggerModel;
struct FMassDebuggerArchetypeData;
struct FMassDebuggerProcessorData;


class SMassArchetypesView : public SMassDebuggerViewBase
{
public:
	SLATE_BEGIN_ARGS(SMassArchetypesView)
	{
	}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FMassDebuggerModel> DebuggerModel);

	void HandleSelectionChanged(TSharedPtr<FMassDebuggerArchetypeData> InNode, ESelectInfo::Type InSelectInfo);

protected:
	virtual void OnRefresh() override;
	virtual void OnProcessorsSelected(TConstArrayView<TSharedPtr<FMassDebuggerProcessorData>> SelectedProcessors, ESelectInfo::Type SelectInfo) override;
	virtual void OnArchetypesSelected(TConstArrayView<TSharedPtr<FMassDebuggerArchetypeData>> SelectedArchetypes, ESelectInfo::Type SelectInfo) override;

	TSharedPtr<STreeView<TSharedPtr<FMassDebuggerArchetypeData>>> ArchetypesTreeView;
	TSharedPtr<SListView<TSharedPtr<FMassDebuggerArchetypeData>>> SelectedArchetypesListWidget;

	TArray<TSharedPtr<FMassDebuggerArchetypeData>> TreeViewSource;
};
