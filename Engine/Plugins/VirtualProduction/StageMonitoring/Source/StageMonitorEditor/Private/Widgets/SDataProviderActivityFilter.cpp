// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDataProviderActivityFilter.h"

#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IStageMonitor.h"
#include "IStageMonitorModule.h"
#include "StageMessages.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "SDataProviderActivityFilter"



FDataProviderActivityFilter::FDataProviderActivityFilter(TWeakPtr<IStageMonitorSession> InSession)
	: Session(MoveTemp(InSession))
{
	//Default to filter out period message types
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		UScriptStruct* PeriodicMessageStruct = FStageProviderPeriodicMessage::StaticStruct();
		const bool bIsPeriodMessageStruct = PeriodicMessageStruct && Struct->IsChildOf(PeriodicMessageStruct) && (Struct != PeriodicMessageStruct);
		if (bIsPeriodMessageStruct)
		{
			RestrictedTypes.Add(Struct);
		}
	}
}

bool FDataProviderActivityFilter::DoesItPass(TSharedPtr<FStageDataEntry>& Entry) const
{
	TSharedPtr<IStageMonitorSession> SessionPtr = Session.Pin();
	if (!SessionPtr.IsValid() || !Entry.IsValid() || !Entry->Data.IsValid() || Entry->Data->GetStructMemory() == nullptr)
	{
		return false;
	}

	if (RestrictedTypes.Contains(Cast<UScriptStruct>(Entry->Data->GetStruct())))
	{
		return false;
	}

	const FStageDataBaseMessage* Message = reinterpret_cast<const FStageDataBaseMessage*>(Entry->Data->GetStructMemory());
	const FGuid ProviderIdentifier = Message->Identifier;
	if (RestrictedProviders.ContainsByPredicate([ProviderIdentifier](const FStageSessionProviderEntry& Other) { return Other.Identifier == ProviderIdentifier; }))
	{
		return false;
	}

	//If we're not filtering based on critical sources, we're done here
	if (!bEnableCriticalStateFilter)
	{
		return true;
	}

	const FStageProviderMessage* ProviderMessage = reinterpret_cast<const FStageProviderMessage*>(Entry->Data->GetStructMemory());
	const TArray<FName> Sources = SessionPtr->GetCriticalStateSources(ProviderMessage->FrameTime.AsSeconds());
	for (const FName& Source : Sources)
	{
		if (RestrictedSources.Contains(Source))
		{
			return true;
		}
	}
	
	return false;
}


void SDataProviderActivityFilter::Construct(const FArguments& InArgs, const TWeakPtr<IStageMonitorSession>& InSession)
{
	AttachToMonitorSession(InSession);

	CurrentFilter = MakeUnique<FDataProviderActivityFilter>(Session);

	OnActivityFilterChanged = InArgs._OnActivityFilterChanged;
	
	ChildSlot
	[
		SNew(SHorizontalBox)
		// Data type filter
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SComboButton)
			.ComboButtonStyle(FEditorStyle::Get(), "GenericFilters.ComboButtonStyle")
			.ForegroundColor(FLinearColor::White)
			.ToolTipText(LOCTEXT("AddFilterToolTip", "Configure activity filter."))
			.OnGetMenuContent(this, &SDataProviderActivityFilter::MakeAddFilterMenu)
			.HasDownArrow(true)
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
					.Text(FText::FromString(FString(TEXT("\xf0b0"))) /*fa-filter*/)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2, 0, 0, 0)
				[
					SNew(STextBlock)
					.TextStyle(FEditorStyle::Get(), "GenericFilters.TextStyle")
					.Text(LOCTEXT("Filters", "Filters"))
				]
			]
		]
		// Search (to do)
		+ SHorizontalBox::Slot()
		.Padding(4, 1, 0, 0)
		.FillWidth(1.0f)
		[
			SNew(STextBlock)
		]
	];
}

void SDataProviderActivityFilter::RefreshMonitorSession(const TWeakPtr<IStageMonitorSession>& NewSession)
{
	AttachToMonitorSession(NewSession);
}

void SDataProviderActivityFilter::ToggleProviderFilter(FStageSessionProviderEntry Provider)
{
	if (CurrentFilter->RestrictedProviders.Contains(Provider))
	{
		CurrentFilter->RestrictedProviders.RemoveSingle(Provider);
	}
	else
	{
		CurrentFilter->RestrictedProviders.AddUnique(Provider);
	}

	OnActivityFilterChanged.ExecuteIfBound();
}

bool SDataProviderActivityFilter::IsProviderFiltered(FStageSessionProviderEntry Provider) const
{
	// The list contains types to filter. Inverse return value to display a more natural way of looking at filter choices
	return !CurrentFilter->RestrictedProviders.Contains(Provider);
}

void SDataProviderActivityFilter::ToggleDataTypeFilter(UScriptStruct* Type)
{
	if(CurrentFilter->RestrictedTypes.Contains(Type))
	{
		CurrentFilter->RestrictedTypes.RemoveSingle(Type);
	}
	else
	{
		CurrentFilter->RestrictedTypes.AddUnique(Type);
	}

	OnActivityFilterChanged.ExecuteIfBound();
}

bool SDataProviderActivityFilter::IsDataTypeFiltered(UScriptStruct* Type) const
{
	// The list contains types to filter. Inverse return value to display a more natural way of looking at filter choices
	return !CurrentFilter->RestrictedTypes.Contains(Type);
}

void SDataProviderActivityFilter::ToggleCriticalStateSourceFilter(FName Source)
{
	if (CurrentFilter->RestrictedSources.Contains(Source))
	{
		CurrentFilter->RestrictedSources.RemoveSingle(Source);
	}
	else
	{
		CurrentFilter->RestrictedSources.AddUnique(Source);
	}

	OnActivityFilterChanged.ExecuteIfBound();
}

bool SDataProviderActivityFilter::IsCriticalStateSourceFiltered(FName Source) const
{
	// The list contains sources to passing the filter.
	return CurrentFilter->RestrictedSources.Contains(Source);
}

void SDataProviderActivityFilter::ToggleCriticalSourceEnabledFilter()
{
	CurrentFilter->bEnableCriticalStateFilter = !CurrentFilter->bEnableCriticalStateFilter;

	OnActivityFilterChanged.ExecuteIfBound();
}

bool SDataProviderActivityFilter::IsCriticalSourceFilteringEnabled() const
{
	return CurrentFilter->bEnableCriticalStateFilter;
}

TSharedRef<SWidget> SDataProviderActivityFilter::MakeAddFilterMenu()
{
	// generate menu
	const bool bShouldCloseWindowAfterClosing = false;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterClosing, nullptr);

	MenuBuilder.BeginSection("ProviderActivityFilter", LOCTEXT("ProviderActivityFilterMenu", "Provider Activity Filters"));
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("FilterMessageType", "Message Type"),
			LOCTEXT("FilterMessageTypeToolTip", "Filters based on the type of the message"),
			FNewMenuDelegate::CreateSP(this, &SDataProviderActivityFilter::CreateMessageTypeFilterMenu),
			FUIAction(),
			NAME_None,
			EUserInterfaceActionType::None
		);

		MenuBuilder.AddSubMenu(
			LOCTEXT("FilterProvider", "Provider"),
			LOCTEXT("FilterProviderToolTip", "Filters based on the provider"),
			FNewMenuDelegate::CreateSP(this, &SDataProviderActivityFilter::CreateProviderFilterMenu),
			FUIAction(),
			NAME_None,
			EUserInterfaceActionType::None
		);

		MenuBuilder.AddSubMenu(
			LOCTEXT("FilterCriticalSources", "Critical state source"),
			LOCTEXT("FilterCriticalSourcesTooltip", "Filters based on critical state sources"),
			FNewMenuDelegate::CreateSP(this, &SDataProviderActivityFilter::CreateCriticalStateSourceFilterMenu),
			FUIAction
			(
				FExecuteAction::CreateSP(this, &SDataProviderActivityFilter::ToggleCriticalSourceEnabledFilter)
				, FCanExecuteAction()
				, FIsActionChecked::CreateSP(this, &SDataProviderActivityFilter::IsCriticalSourceFilteringEnabled)
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SDataProviderActivityFilter::CreateMessageTypeFilterMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("AvailableProviderMessageTypes", LOCTEXT("AvailableDataTypes", "Provider Message Types"));
	{
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			UScriptStruct* Struct = *It;
			const UScriptStruct* BasePeriodicMessage = FStageProviderPeriodicMessage::StaticStruct();
			const UScriptStruct* BaseEventMessage = FStageProviderEventMessage::StaticStruct();
			const bool bIsValidMessageStruct = (BasePeriodicMessage && Struct->IsChildOf(BasePeriodicMessage) && (Struct != BasePeriodicMessage))
												|| (BaseEventMessage && Struct->IsChildOf(BaseEventMessage) && (Struct != BaseEventMessage));
											 
			if (bIsValidMessageStruct)
			{
				MenuBuilder.AddMenuEntry
				(
					Struct->GetDisplayNameText(), //Label
					Struct->GetDisplayNameText(), //Tooltip
					FSlateIcon(),
					FUIAction
					(
						FExecuteAction::CreateSP(this, &SDataProviderActivityFilter::ToggleDataTypeFilter, Struct)
						, FCanExecuteAction()
						, FIsActionChecked::CreateSP(this, &SDataProviderActivityFilter::IsDataTypeFiltered, Struct)
					),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
				);
			}
		}
	}
	MenuBuilder.EndSection();
}

void SDataProviderActivityFilter::CreateProviderFilterMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("AvailableProviders", LOCTEXT("AvailableProviders", "Providers"));
	{
		if (TSharedPtr<IStageMonitorSession> SessionPtr = Session.Pin())
		{
			const auto CreateMenuEntry = [&MenuBuilder, this](const FStageSessionProviderEntry& Provider)
			{
				const FString ProviderName = FString::Printf(TEXT("%s"), *Provider.Descriptor.FriendlyName.ToString());
				MenuBuilder.AddMenuEntry
				(
					FText::FromString(ProviderName), //Label
					FText::GetEmpty(), //Tooltip
					FSlateIcon(),
					FUIAction
					(
						FExecuteAction::CreateSP(this, &SDataProviderActivityFilter::ToggleProviderFilter, Provider)
						, FCanExecuteAction()
						, FIsActionChecked::CreateSP(this, &SDataProviderActivityFilter::IsProviderFiltered, Provider)
					),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
				);
			};

			const TConstArrayView<FStageSessionProviderEntry> Providers = SessionPtr->GetProviders();
			for (const FStageSessionProviderEntry& Provider : Providers)
			{
				CreateMenuEntry(Provider);
			}
			
			const TConstArrayView<FStageSessionProviderEntry> ClearedProviders = SessionPtr->GetClearedProviders();
			for (const FStageSessionProviderEntry& Provider : ClearedProviders)
			{
				CreateMenuEntry(Provider);
			}
		}
	}
	MenuBuilder.EndSection();
}

void SDataProviderActivityFilter::CreateCriticalStateSourceFilterMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("CriticalStateSources", LOCTEXT("CriticalStateSources", "Critical state sources"));
	{
		if (TSharedPtr<IStageMonitorSession> SessionPtr = Session.Pin())
		{
			TArray<FName> Sources = SessionPtr->GetCriticalStateHistorySources();

			for (const FName& Source : Sources)
			{
				MenuBuilder.AddMenuEntry
				(
					FText::FromName(Source), //Label
					FText::GetEmpty(), //Tooltip
					FSlateIcon(),
					FUIAction
					(
						FExecuteAction::CreateSP(this, &SDataProviderActivityFilter::ToggleCriticalStateSourceFilter, Source)
						, FCanExecuteAction::CreateLambda([this]() { return CurrentFilter->bEnableCriticalStateFilter; })
						, FIsActionChecked::CreateSP(this, &SDataProviderActivityFilter::IsCriticalStateSourceFiltered, Source)
					),
					NAME_None,
					EUserInterfaceActionType::ToggleButton
				);
			}
		}

			}
	MenuBuilder.EndSection();
}


void SDataProviderActivityFilter::AttachToMonitorSession(const TWeakPtr<IStageMonitorSession>& NewSession)
{
	if (NewSession != Session)
	{
		Session = NewSession;
		CurrentFilter->Session = Session;
	}
}

#undef LOCTEXT_NAMESPACE
