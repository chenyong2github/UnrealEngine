// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Views/STreeView.h"

class UPoseSearchFeatureChannel;

namespace UE::PoseSearch
{
	class FDatabaseViewModel;
	typedef TSharedPtr<class FChannelItem> FChannelItemPtr;
	typedef STreeView<FChannelItemPtr> SChannelItemsTreeView;

	class SDatabaseDataDetails : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS( SDatabaseDataDetails ) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& Args, TSharedRef<FDatabaseViewModel> InEditorViewModel);
		void Reconstruct();

	private:
		static void RebuildChannelItemsTreeRecursively(TArray<FChannelItemPtr>& ChannelItems, TConstArrayView<TObjectPtr<UPoseSearchFeatureChannel>> Channels);

		TSharedPtr<SChannelItemsTreeView> ChannelItemsTreeView;
		TArray<FChannelItemPtr> ChannelItems;
		TWeakPtr<FDatabaseViewModel> EditorViewModel;
	};
} // namespace UE::PoseSearch