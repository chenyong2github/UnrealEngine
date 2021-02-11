// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SGraphPin.h"
#include "SGraphPinComboBox.h"

namespace Metasound
{
	namespace Frontend
	{
		struct IEnumDataTypeInterface;
	}
}

class SMetasoundEnumPin : public SGraphPin
{
	SLATE_BEGIN_ARGS(SMetasoundEnumPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

	static TSharedPtr<const Metasound::Frontend::IEnumDataTypeInterface> FindEnumInterfaceFromPin(UEdGraphPin* InPin);

protected:	
	TSharedPtr<class SPinComboBox>	ComboBox;

	TSharedRef<SWidget>	GetDefaultValueWidget();
	
	FString OnGetText() const;

	void GenerateComboBoxIndexes(TArray<TSharedPtr<int32>>& OutComboBoxIndexes);

	void ComboBoxSelectionChanged(TSharedPtr<int32> NewSelection, ESelectInfo::Type SelectInfo);

	FText OnGetFriendlyName(int32 EnumIndex);

	FText OnGetTooltip(int32 EnumIndex);
};

