// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SGraphPin.h"
#include "SGraphPinBoneNameValueWidget.h"

class SGraphPinBoneName : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGraphPinBoneName) {}
	SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	//~ Begin SGraphPin Interface
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override;
	//~ End SGraphPin Interface

	FText GetBoneNameText() const;
	virtual void SetBoneNameText(const FText& NewTypeInValue, ETextCommit::Type CommitInfo);

	TSharedRef<SWidget> MakeBoneNameItemWidget(TSharedPtr<FString> InItem);
	void OnBoneNameChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnBoneNameComboBox();

	TSharedPtr<SGraphPinBoneNameValueWidget> BoneNameComboBox;

};
