// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Graph/SControlRigGraphPinNameListValueWidget.h"
#include "Rigs/RigHierarchyContainer.h"
#include "ControlRigBlueprint.h"

class SControlRigGizmoNameList : public SBox
{
public:

	DECLARE_DELEGATE_RetVal( const TArray<TSharedPtr<FString>>&, FOnGetNameListContent );

	SLATE_BEGIN_ARGS(SControlRigGizmoNameList){}

		SLATE_EVENT(FOnGetNameListContent, OnGetNameListContent)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FRigControl* InControl, UControlRigBlueprint* InBlueprint);

protected:

	const TArray<TSharedPtr<FString>>& GetNameList() const;
	FText GetNameListText() const;
	virtual void SetNameListText(const FText& NewTypeInValue, ETextCommit::Type CommitInfo);

	TSharedRef<SWidget> MakeNameListItemWidget(TSharedPtr<FString> InItem);
	void OnNameListChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnNameListComboBox();

	FOnGetNameListContent OnGetNameListContent;
	TSharedPtr<SControlRigGraphPinNameListValueWidget> NameListComboBox;
	TArray<TSharedPtr<FString>> EmptyList;

	FRigElementKey ControlKey;
	UControlRigBlueprint* Blueprint;
};
