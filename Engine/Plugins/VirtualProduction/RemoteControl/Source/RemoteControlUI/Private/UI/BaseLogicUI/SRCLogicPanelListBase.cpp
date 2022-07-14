// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCLogicPanelListBase.h"

#include "RCLogicModeBase.h"
#include "SlateOptMacros.h"
#include "UI/Action/Conditional/RCActionConditionalModel.h"
#include "UI/Action/RCActionModel.h"
#include "UI/Behaviour/RCBehaviourModel.h"
#include "UI/Controller/RCControllerModel.h"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SRCLogicPanelListBase::Construct(const FArguments& InArgs)
{
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

template<class T>
void SRCLogicPanelListBase::DeleteItemFromLogicPanel(TArray<TSharedPtr<T>>& ItemsSource, const TArray<TSharedPtr<T>>& SelectedItems)
{
	bool bIsDeleted = false;
	for (const TSharedPtr<T> SelectedItem : SelectedItems)
	{
		if (SelectedItem.IsValid())
		{
			// Remove Model from Data Container
			const int32 RemoveCount = RemoveModel(SelectedItem);
			if(ensure(RemoveCount > 0))
			{
				// Remove View Model from UI List
				const int32 RemoveModelItemIndex = ItemsSource.IndexOfByPredicate([SelectedItem](TSharedPtr<T> InModel)
					{
						return SelectedItem == InModel;
					});

				if (RemoveModelItemIndex > INDEX_NONE)
				{
					ItemsSource.RemoveAt(RemoveModelItemIndex);

					bIsDeleted = true;
				}
			}
		}
	}

	if (bIsDeleted)
	{
		BroadcastOnItemRemoved();
		Reset();
	}
}

template void SRCLogicPanelListBase::DeleteItemFromLogicPanel(TArray<TSharedPtr<FRCControllerModel>>& ItemsSource, const TArray<TSharedPtr<FRCControllerModel>>& SelectedItems);
template void SRCLogicPanelListBase::DeleteItemFromLogicPanel(TArray<TSharedPtr<FRCBehaviourModel>>& ItemsSource, const TArray<TSharedPtr<FRCBehaviourModel>>& SelectedItems);
template void SRCLogicPanelListBase::DeleteItemFromLogicPanel(TArray<TSharedPtr<FRCActionModel>>& ItemsSource, const TArray<TSharedPtr<FRCActionModel>>& SelectedItems);
template void SRCLogicPanelListBase::DeleteItemFromLogicPanel(TArray<TSharedPtr<FRCActionConditionalModel>>& ItemsSource, const TArray<TSharedPtr<FRCActionConditionalModel>>& SelectedItems);