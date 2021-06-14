// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SGraphPin.h"
#include "SControlRigGraphPinEditableNameValueWidget.h"

class SControlRigGraphPinParameterName : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SControlRigGraphPinParameterName) {}
	SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	//~ Begin SGraphPin Interface
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override;
	//~ End SGraphPin Interface

	FText GetParameterNameText() const;
	virtual void SetParameterNameText(const FText& NewTypeInValue, ETextCommit::Type CommitInfo);

	TSharedRef<SWidget> MakeParameterNameItemWidget(TSharedPtr<FString> InItem);
	void OnParameterNameChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnParameterNameComboBox();
	TArray<TSharedPtr<FString>>& GetParameterNames();

	TArray<TSharedPtr<FString>> ParameterNames;
	TSharedPtr<SControlRigGraphPinEditableNameValueWidget> NameComboBox;

};
