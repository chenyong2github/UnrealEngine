// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SGraphPin.h"
#include "RigVMModel/RigVMPin.h"
#include "SControlRigGraphPinNameListValueWidget.h"

class SControlRigGraphPinNameList : public SGraphPin
{
public:

	DECLARE_DELEGATE_RetVal_OneParam( const TArray<TSharedPtr<FString>>&, FOnGetNameListContent, URigVMPin*);

	SLATE_BEGIN_ARGS(SControlRigGraphPinNameList){}

		SLATE_ARGUMENT(URigVMPin*, ModelPin)
		SLATE_EVENT(FOnGetNameListContent, OnGetNameListContent)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	//~ Begin SGraphPin Interface
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override;
	//~ End SGraphPin Interface

	const TArray<TSharedPtr<FString>>& GetNameList() const;
	FText GetNameListText() const;
	virtual void SetNameListText(const FText& NewTypeInValue, ETextCommit::Type CommitInfo);

	TSharedRef<SWidget> MakeNameListItemWidget(TSharedPtr<FString> InItem);
	void OnNameListChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnNameListComboBox();

	FOnGetNameListContent OnGetNameListContent;
	URigVMPin* ModelPin;
	TSharedPtr<SControlRigGraphPinNameListValueWidget> NameListComboBox;
	TArray<TSharedPtr<FString>> EmptyList;
	TArray<TSharedPtr<FString>> CurrentList;
};
