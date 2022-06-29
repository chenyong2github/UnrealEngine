// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SClassViewer;

namespace UE::MVVM
{
class SViewModelBindingListWidget;

class SMVVMSelectViewModel : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnValueChanged, const UClass*);

	SLATE_BEGIN_ARGS(SMVVMSelectViewModel) {}
		SLATE_EVENT(FSimpleDelegate, OnCancel)
		SLATE_EVENT(FOnValueChanged, OnViewModelCommitted)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs);

private:
	void HandleClassPicked(UClass* ClassPicked);
	FReply HandleAccepted();
	FReply HandleCancel();
	bool HandelIsSelectionEnabled() const;

private:
	TSharedPtr<SClassViewer> ClassViewer;
	TSharedPtr<SViewModelBindingListWidget> ViewModelBindingListWidget;
	TWeakObjectPtr<const UClass> SelectedClass;

	FSimpleDelegate OnCancel;
	FOnValueChanged OnViewModelCommitted;
};

} //namespace