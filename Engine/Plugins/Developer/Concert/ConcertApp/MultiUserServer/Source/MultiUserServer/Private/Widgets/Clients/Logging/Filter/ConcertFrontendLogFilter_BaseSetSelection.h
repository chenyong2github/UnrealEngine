// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertFrontendLogFilter.h"
#include "ConcertLogFilter.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "UnrealMultiUserUI.BaseSetSelection"

namespace UE::MultiUserServer::Filters
{
	/**
	 * Helper implementation for filters that allow filter based on a finte set of items.
	 * Subclasses must implement:
	 * - static TSet<TSetType> GetAllOptions();
	 * - static FString GetOptionDisplayString(const TSetType&);
	 */
	template<typename TRealFilterImpl, typename TSetType>
	class TConcertLogFilter_BaseSetSelection : public FConcertLogFilter
	{
	public:

		using TItemType = TSetType;

		TConcertLogFilter_BaseSetSelection()
			: AllowedItems(TRealFilterImpl::GetAllOptions())
		{}

		void AllowAll()
		{
			TSet<TSetType> Allowed = TRealFilterImpl::GetAllOptions();
			if (Allowed.Num() != AllowedItems.Num())
			{
				AllowedItems = MoveTemp(Allowed);
				OnChanged().Broadcast();
			}
		}
		void DisallowAll()
		{
			if (AllowedItems.Num() > 0)
			{
				AllowedItems.Reset();
				OnChanged().Broadcast();
			}
		}
		void ToggleAll(const TSet<TSetType>& ToToggle)
		{
			for (const TSetType Item : ToToggle)
			{
				if (IsItemAllowed(Item))
				{
					DisallowItem(Item);
				}
				else
				{
					AllowItem(Item);
				}
			}

			if (ToToggle.Num() > 0)
			{
				OnChanged().Broadcast();
			}
		}
	
		void AllowItem(const TSetType& MessageTypeName)
		{
			if (!AllowedItems.Contains(MessageTypeName))
			{
				AllowedItems.Add(MessageTypeName);
				OnChanged().Broadcast();
			}
		}
		void DisallowItem(const TSetType& MessageTypeName)
		{
			if (AllowedItems.Contains(MessageTypeName))
			{
				AllowedItems.Remove(MessageTypeName);
				OnChanged().Broadcast();
			}
		}
	
		bool IsItemAllowed(const TSetType& MessageTypeName) const
		{
			return AllowedItems.Contains(MessageTypeName);
		}
		bool AreAllAllowed() const
		{
			return AllowedItems.Num() == TRealFilterImpl::GetAllOptions().Num();
		}
		uint8 GetNumSelected() const { return AllowedItems.Num(); }
		
	private:
	
		TSet<TSetType> AllowedItems;
	};

	template<typename TRealFilterImpl>
	class TConcertFrontendLogFilter_BaseSetSelection : public TConcertFrontendLogFilterAggregate<TRealFilterImpl>
	{
		using TItemType = typename TRealFilterImpl::TItemType;
	public:

		TConcertFrontendLogFilter_BaseSetSelection(FText FilterName)
		{
			this->ChildSlot = SNew(SHorizontalBox)
				.ToolTipText(LOCTEXT("BaseSetSelection.ToolTipText", "Select a list of allowed items\nHint: Type in menu to search"))

				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FilterName)
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SNew(SComboButton)
					.OnGetMenuContent_Raw(this, &TConcertFrontendLogFilter_BaseSetSelection::MakeSelectionMenu)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text_Lambda([this]()
						{
							return this->Implementation.AreAllAllowed()
								? LOCTEXT("BaseSetSelection.Selection.All", "All")
								: FText::FromString(FString::FromInt(this->Implementation.GetNumSelected()));
						})
					]
				];
		}

	private:

		TSharedRef<SWidget> MakeSelectionMenu()
		{
			FMenuBuilder MenuBuilder(false, nullptr);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("BaseSetSelection.SelectAll.", "Select all"),
				LOCTEXT("BaseSetSelection.SelectAll.Tooltip", "Allows all items"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this](){ this->Implementation.AllowAll(); }),
					FCanExecuteAction::CreateLambda([] { return true; }),
					FIsActionChecked()),
				NAME_None,
				EUserInterfaceActionType::Button
				);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("BaseSetSelection.DeselectAll.", "Deselect all"),
				LOCTEXT("BaseSetSelection.DeelectAll.Tooltip", "Disallows all items"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this](){ this->Implementation.DisallowAll(); }),
					FCanExecuteAction::CreateLambda([] { return true; }),
					FIsActionChecked()),
				NAME_None,
				EUserInterfaceActionType::Button
				);
	
			MenuBuilder.AddSeparator();
	
			for (const TItemType& Item : TRealFilterImpl::GetAllOptions())
			{
				MenuBuilder.AddMenuEntry(
					FText::FromString(TRealFilterImpl::GetOptionDisplayString(Item)),
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this, Item]()
						{
							if (this->Implementation.IsItemAllowed(Item))
							{
								this->Implementation.DisallowItem(Item);
							}
							else
							{
								this->Implementation.AllowItem(Item);
							}
						}),
						FCanExecuteAction::CreateLambda([] { return true; }),
						FIsActionChecked::CreateLambda([this, Item]() { return this->Implementation.IsItemAllowed(Item); })),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
				);
			}
			return MenuBuilder.MakeWidget();
		}
	};

}

#undef LOCTEXT_NAMESPACE