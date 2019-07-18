// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SGraphPin.h"
#include "SGraphPinBoneNameValueWidget.h"

class SGraphPinCurveName : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinCurveName) {}
	SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	//~ Begin SGraphPin Interface
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override;
	//~ End SGraphPin Interface

	FText GetCurveNameText() const;
	virtual void SetCurveNameText(const FText& NewTypeInValue, ETextCommit::Type CommitInfo);

	TSharedRef<SWidget> MakeCurveNameItemWidget(TSharedPtr<FString> InItem);
	void OnCurveNameChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnCurveNameComboBox();

	TSharedPtr<SGraphPinBoneNameValueWidget> CurveNameComboBox;

};
