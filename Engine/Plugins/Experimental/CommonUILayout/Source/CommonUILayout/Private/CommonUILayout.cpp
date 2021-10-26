// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonUILayout.h"

#if WITH_EDITOR
void UCommonUILayout::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ValueSet && PropertyChangedEvent.Property)
	{
		const FName& PropertyName = PropertyChangedEvent.Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(FCommonUILayoutWidget, ZOrder))
		{
			const int32 ChangedIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(UCommonUILayout, Widgets).ToString());
			if (Widgets.IsValidIndex(ChangedIndex))
			{
				Widgets[ChangedIndex].CustomZOrder = (int32)Widgets[ChangedIndex].ZOrder;
			}
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(FCommonUILayoutWidget, CustomZOrder))
		{
			// FIXME: ClampMin & ClampMax doesn't support non-numeric value
			//        We want CustomZOrder to be between ECommonUILayoutZOrder::CustomMin & ECommonUILayoutZOrder::CustomMax
			const int32 ChangedIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(UCommonUILayout, Widgets).ToString());
			if (Widgets.IsValidIndex(ChangedIndex))
			{
				Widgets[ChangedIndex].CustomZOrder = FMath::Clamp(Widgets[ChangedIndex].CustomZOrder, (int32)ECommonUILayoutZOrder::CustomMin, (int32)ECommonUILayoutZOrder::CustomMax);
			}
		}
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR
