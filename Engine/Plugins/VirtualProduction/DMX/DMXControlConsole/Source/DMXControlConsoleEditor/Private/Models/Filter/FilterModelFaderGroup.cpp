// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterModelFaderGroup.h"

#include "Algo/Find.h"
#include "Algo/Transform.h"
#include "DMXControlConsoleData.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "FilterModel.h"
#include "FilterModelFader.h"


namespace UE::DMXControlConsoleEditor::FilterModel::Private
{
	void FFaderGroupFilter::Parse(const FString& InString)
	{
		Reset();

		String = InString.TrimStartAndEnd();
		Names = ParseStringIntoArray(InString);
	}

	void FFaderGroupFilter::Reset()
	{
		String.Reset();
		Names.Reset();
	}

	FFilterModelFaderGroup::FFilterModelFaderGroup(UDMXControlConsoleFaderGroup* InFaderGroup)
		: WeakFaderGroup(InFaderGroup)
	{
		if (WeakFaderGroup.IsValid())
		{
			SetFilter(WeakFaderGroup->FilterString);
			UpdateFaderModels();
		}
	}

	UDMXControlConsoleFaderGroup* FFilterModelFaderGroup::GetFaderGroup() const
	{
		return WeakFaderGroup.Get();
	}

	void FFilterModelFaderGroup::SetFilter(const FString& InString)
	{
		UDMXControlConsoleFaderGroup* FaderGroup = WeakFaderGroup.Get();
		if (FaderGroup && FaderGroupFilter.String != InString)
		{
			FaderGroupFilter.Parse(InString);

			FaderGroup->Modify();
			FaderGroup->FilterString = FaderGroupFilter.String;
		}
	}

	bool FFilterModelFaderGroup::MatchesGlobalFilterNames(const FGlobalFilter& GlobalFilter) const
	{
		if (UDMXControlConsoleFaderGroup* FaderGroup = WeakFaderGroup.Get())
		{
			const FString* MatchingStringPtr = Algo::FindByPredicate(GlobalFilter.Names, [FaderGroup](const FString& Name)
				{
					return FaderGroup->GetFaderGroupName().Contains(Name);
				});
			
			return MatchingStringPtr != nullptr;
		}
		return false;
	}

	bool FFilterModelFaderGroup::HasFadersMatchingGlobalFilterNames(const FGlobalFilter& GlobalFilter) const
	{
		const TSharedRef<FFilterModelFader>* MatchingFaderModelPtr = Algo::FindByPredicate(FaderModels,
			[GlobalFilter](const TSharedRef<FFilterModelFader>& FaderModel)
			{
				return FaderModel->MatchesAnyName(GlobalFilter.Names);
			});

		return MatchingFaderModelPtr != nullptr;
	}

	void FFilterModelFaderGroup::Apply(const FGlobalFilter& GlobalFilter, ENameFilterMode NameFilterMode)
	{
		UDMXControlConsoleFaderGroup* FaderGroup = WeakFaderGroup.Get();
		if (!FaderGroup)
		{
			return;
		}

		// Reset visibility
		FaderGroup->SetIsMatchingFilter(false);
		const TArray<UDMXControlConsoleFaderBase*> Faders = FaderGroup->GetAllFaders();
		for (UDMXControlConsoleFaderBase* Fader : Faders)
		{
			Fader->SetIsMatchingFilter(false);
		}

		// Apply filter
		const bool bRequiresMatchingFaderGroupName = NameFilterMode == ENameFilterMode::MatchFaderGroupNames || NameFilterMode == ENameFilterMode::MatchFaderAndFaderGroupNames;
		if (bRequiresMatchingFaderGroupName && !IsMatchingGlobalFilterNames(GlobalFilter))
		{
			return;
		}
		else if (bRequiresMatchingFaderGroupName && FaderModels.IsEmpty())
		{
			FaderGroup->SetIsMatchingFilter(true);
		}

		for (const TSharedRef<FFilterModelFader>& FaderModel : FaderModels)
		{
			UDMXControlConsoleFaderBase* Fader = FaderModel->GetFader();
			if (!Fader || !FaderModel->MatchesGlobalFilter(GlobalFilter, NameFilterMode))
			{
				continue;
			}

			FaderGroup->SetIsMatchingFilter(true);
			if (FaderModel->MatchesFaderGroupFilter(FaderGroupFilter))
			{
				Fader->SetIsMatchingFilter(true);
			}
		}
	}

	void FFilterModelFaderGroup::UpdateFaderModels()
	{
		FaderModels.Reset();

		const TArray<UDMXControlConsoleFaderBase*> Faders = WeakFaderGroup.IsValid() ? WeakFaderGroup->GetAllFaders() : TArray<UDMXControlConsoleFaderBase*>{};
		Algo::Transform(Faders, FaderModels, [](UDMXControlConsoleFaderBase* Fader)
			{
				return MakeShared<FFilterModelFader>(Fader);
			});
	}

	bool FFilterModelFaderGroup::IsMatchingGlobalFilterNames(const FGlobalFilter& GlobalFilter) const
	{
		if (UDMXControlConsoleFaderGroup* FaderGroup = WeakFaderGroup.Get())
		{
			const bool bMatchesFaderGroupName =
				GlobalFilter.Names.IsEmpty() ||
				Algo::FindByPredicate(GlobalFilter.Names, [FaderGroup](const FString& Name)
					{
						return FaderGroup->GetFaderGroupName().Contains(Name);
					}) != nullptr;
			return bMatchesFaderGroupName;
		}

		return false;
	}
}
