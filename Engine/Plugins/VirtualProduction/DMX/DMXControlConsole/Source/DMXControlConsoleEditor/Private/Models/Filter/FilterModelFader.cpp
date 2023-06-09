// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterModelFader.h"

#include "Algo/Find.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "FilterModel.h"
#include "FilterModelFaderGroup.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"


namespace UE::DMXControlConsoleEditor::FilterModel::Private
{
	FFilterModelFader::FFilterModelFader(UDMXControlConsoleFaderBase* InFader)
		: WeakFader(InFader)
	{}

	bool FFilterModelFader::MatchesAnyName(const TArray<FString>& Names) const
	{
		if (UDMXControlConsoleFaderBase* Fader = WeakFader.Get())
		{
			const FString* NamePtr = Algo::FindByPredicate(Names, [Fader](const FString& Name)
				{
					return Fader->GetFaderName().Contains(Name);
				});
			return NamePtr != nullptr;
		}

		return false;
	}

	bool FFilterModelFader::MatchesGlobalFilter(const FGlobalFilter& GlobalFilter, ENameFilterMode NameFilterMode)
	{
		if (GlobalFilter.String.IsEmpty())
		{
			return true;
		}

		UDMXControlConsoleFaderBase* Fader = WeakFader.Get();
		if (!Fader)
		{
			return false;
		}

		auto MatchesUniverseLambda = [GlobalFilter, Fader, this]()
		{
			return
				GlobalFilter.Universes.IsEmpty() ||
				Algo::FindByPredicate(GlobalFilter.Universes, [Fader](int32 Universe)
					{
						return Universe == Fader->GetUniverseID();
					}) != nullptr;
		};

		const UDMXEntityFixturePatch* FixturePatch = Fader->GetOwnerFaderGroupChecked().GetFixturePatch();
		auto MatchesFixtureIDLambda = [FixturePatch, GlobalFilter, Fader, this]()
		{
			return
				GlobalFilter.FixtureIDs.IsEmpty() ||
				Algo::FindByPredicate(GlobalFilter.FixtureIDs, [Fader, FixturePatch](int32 FixtureID)
					{
						int32 FixturePatchFixtureID;
						if (FixturePatch && FixturePatch->FindFixtureID(FixturePatchFixtureID))
						{
							return FixturePatchFixtureID == FixtureID;
						}
						return true;
					}) != nullptr;
		};

		auto MatchesAddressLambda = [FixturePatch, GlobalFilter, Fader, this]()
		{
			return
				!GlobalFilter.AbsoluteAddress.IsSet() ||
				GlobalFilter.AbsoluteAddress == Fader->GetStartingAddress();
		};

		auto MatchesNameLambda = [GlobalFilter, NameFilterMode, Fader, this]()
		{
			if (NameFilterMode == ENameFilterMode::MatchFaderNames || NameFilterMode == ENameFilterMode::MatchFaderAndFaderGroupNames)
			{
				return MatchesAnyName(GlobalFilter.Names);
			}
			else
			{
				return true;
			}
		};

		return
			MatchesUniverseLambda() &&
			MatchesFixtureIDLambda() &&
			MatchesAddressLambda() &&
			MatchesNameLambda();
	}

	bool FFilterModelFader::MatchesFaderGroupFilter(const FFaderGroupFilter& FaderGroupFilter)
	{
		return 
			FaderGroupFilter.Names.IsEmpty() || 
			MatchesAnyName(FaderGroupFilter.Names);
	}

	UDMXControlConsoleFaderBase* FFilterModelFader::GetFader() const
	{
		return WeakFader.Get();
	}
}
