// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Graph/SControlRigGraphPinNameListValueWidget.h"
#include "Rigs/RigHierarchyContainer.h"
#include "ControlRigBlueprint.h"

class SControlRigShapeNameList : public SBox
{
public:

	DECLARE_DELEGATE_RetVal( const TArray<TSharedPtr<FString>>&, FOnGetNameListContent );

	SLATE_BEGIN_ARGS(SControlRigShapeNameList){}

		SLATE_EVENT(FOnGetNameListContent, OnGetNameListContent)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FRigControlElement* ControlElement, UControlRigBlueprint* InBlueprint);
	void Construct(const FArguments& InArgs, TArray<FRigControlElement*> ControlElements, UControlRigBlueprint* InBlueprint);
	void Construct(const FArguments& InArgs, TArray<FRigControlElement> ControlElements, UControlRigBlueprint* InBlueprint);

protected:

	void ConstructCommon();

	const TArray<TSharedPtr<FString>>& GetNameList() const;
	FText GetNameListText() const;
	virtual void SetNameListText(const FText& NewTypeInValue, ETextCommit::Type CommitInfo);

	TSharedRef<SWidget> MakeNameListItemWidget(TSharedPtr<FString> InItem);
	void OnNameListChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnNameListComboBox();

	FOnGetNameListContent OnGetNameListContent;
	TSharedPtr<SControlRigGraphPinNameListValueWidget> NameListComboBox;
	TArray<TSharedPtr<FString>> EmptyList;

	TArray<FRigElementKey> ControlKeys;
	UControlRigBlueprint* Blueprint;
};
