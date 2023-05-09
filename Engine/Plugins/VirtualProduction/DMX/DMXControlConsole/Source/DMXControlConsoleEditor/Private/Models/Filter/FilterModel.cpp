// Copyright Epic Games, Inc. All Rights Reserved.

#include "FilterModel.h"

#include "DMXControlConsoleData.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXEditorUtils.h"
#include "FilterModelFaderGroup.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Models/DMXControlConsoleEditorModel.h"

#include "Algo/Find.h"
#include "Algo/Transform.h"
#include "Internationalization/Regex.h"


namespace UE::DMXControlConsoleEditor::FilterModel::Private
{
	TArray<FString> ParseStringIntoArray(const FString& InString)
	{
		constexpr int32 NumDelimiters = 4;
		static const TCHAR* AttributeNameParamDelimiters[] =
		{
			TEXT("."),
			TEXT(","),
			TEXT(":"),
			TEXT(";")
		};

		TArray<FString> Substrings;
		constexpr bool bCullEmpty = false;
		InString.ParseIntoArray(Substrings, AttributeNameParamDelimiters, NumDelimiters, bCullEmpty);

		// Cull whitespace and empty
		TArray<FString> Result;
		for (FString& Substring : Substrings)
		{
			Substring.TrimStartAndEndInline();
			if (!Substring.IsEmpty())
			{
				Result.Add(Substring);
			}
		}

		return Result;
	}

	void FGlobalFilter::Parse(const FString& InString)
	{
		Reset();

		String = InString.TrimStartAndEnd();

		// Parse universes
		const FRegexPattern UniversePattern(TEXT("(universe|uni\\s*[0-9, -]*)"));
		FRegexMatcher UniverseRegex(UniversePattern, String);
		FString UniverseSubstring;
		if (UniverseRegex.FindNext())
		{
			UniverseSubstring = UniverseRegex.GetCaptureGroup(1);
			Universes = FDMXEditorUtils::ParseUniverses(UniverseSubstring);
		}

		// Parse fixture IDs. Ignore universes.
		const FString StringWithoutUniverses = String.Replace(*UniverseSubstring, TEXT(""));
		FixtureIDs = FDMXEditorUtils::ParseFixtureIDs(StringWithoutUniverses);

		// Parse address. Ignore universes.
		int32 Address;
		if (FDMXEditorUtils::ParseAddress(StringWithoutUniverses, Address))
		{
			AbsoluteAddress = Address;
		}

		// Parse names. Ignore universes, digits.
		FString StringNoDigits;
		for (const TCHAR& Char : StringWithoutUniverses.GetCharArray())
		{
			if (!FChar::IsDigit(Char))
			{
				StringNoDigits += Char;
			}
		}
		Names = ParseStringIntoArray(StringNoDigits);
	}

	void FGlobalFilter::Reset()
	{
		String.Reset();
		Universes.Reset();
		FixtureIDs.Reset();
		AbsoluteAddress.Reset();
		Names.Reset();
	}

	TOptional<int32> FGlobalFilter::GetUniverse() const
	{
		if (AbsoluteAddress.IsSet())
		{
			return AbsoluteAddress.GetValue() / DMX_UNIVERSE_SIZE;
		}
		return TOptional<int32>();
	}

	TOptional<int32> FGlobalFilter::GetChannel() const
	{
		if (AbsoluteAddress.IsSet())
		{
			return AbsoluteAddress.GetValue() % DMX_UNIVERSE_SIZE;
		}
		return TOptional<int32>();
	}


	void FFilterModel::Initialize()
	{
		InitializeInternal();

		// Listen to loading control consoles in editor 
		UDMXControlConsoleEditorModel* EditorModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
		EditorModel->GetOnConsoleLoaded().AddSP(AsShared(), &FFilterModel::InitializeInternal);
	}

	FFilterModel& FFilterModel::Get()
	{
		UDMXControlConsoleEditorModel* EditorModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
		check(EditorModel->FilterModel.IsValid());
		return *EditorModel->FilterModel;
	}

	void FFilterModel::SetGlobalFilter(const FString& NewFilter)
	{
		UDMXControlConsoleData* ControlConsoleData = WeakControlConsoleData.Get();
		if (ControlConsoleData && GlobalFilter.String != NewFilter)
		{
			GlobalFilter.Parse(NewFilter);
			UpdateNameFilterMode();

			ApplyFilter();

			ControlConsoleData->FilterString = NewFilter;

			OnFilterChanged.Broadcast();
		}
	}

	void FFilterModel::SetFaderGroupFilter(UDMXControlConsoleFaderGroup* FaderGroup, const FString& InString)
	{
		const TSharedRef<FFilterModelFaderGroup>* FaderGroupModelPtr = Algo::FindBy(FaderGroupModels, FaderGroup,
			[](const TSharedRef<FFilterModelFaderGroup>& Model)
			{
				return Model->GetFaderGroup();
			});
		if (FaderGroupModelPtr)
		{
			(*FaderGroupModelPtr)->SetFilter(InString);
			(*FaderGroupModelPtr)->Apply(GlobalFilter, NameFilterMode);

			OnFilterChanged.Broadcast();
		}
	}

	void FFilterModel::UpdateNameFilterMode()
	{
		bool bGroupMatchesGlobalFilterNames = false;
		bool bHasFadersMatchingGlobalFilterNames = false;
		for (const TSharedRef<FFilterModelFaderGroup>& FaderGroupModel : FaderGroupModels)
		{
			bGroupMatchesGlobalFilterNames |= FaderGroupModel->MatchesGlobalFilterNames(GlobalFilter);
			bHasFadersMatchingGlobalFilterNames |= FaderGroupModel->HasFadersMatchingGlobalFilterNames(GlobalFilter);

			if (bGroupMatchesGlobalFilterNames && bHasFadersMatchingGlobalFilterNames)
			{
				break;
			}
		}

		if (bGroupMatchesGlobalFilterNames && bHasFadersMatchingGlobalFilterNames)
		{
			NameFilterMode = ENameFilterMode::MatchFaderAndFaderGroupNames;
		}
		else if (bHasFadersMatchingGlobalFilterNames)
		{
			NameFilterMode = ENameFilterMode::MatchFaderNames;
		}
		else 
		{
			NameFilterMode = ENameFilterMode::MatchFaderGroupNames;
		}
	}

	void FFilterModel::InitializeInternal()
	{
		UDMXControlConsoleEditorModel* EditorModel = GetMutableDefault<UDMXControlConsoleEditorModel>();
		UDMXControlConsole* EditorConsole = EditorModel->GetEditorConsole();
		WeakControlConsoleData = EditorConsole ? EditorConsole->GetControlConsoleData() : nullptr;

		if (WeakControlConsoleData.IsValid())
		{
			UpdateFaderGroupModels();

			GlobalFilter.Parse(WeakControlConsoleData->FilterString);
			ApplyFilter();
		}
	}

	void  FFilterModel::UpdateFaderGroupModels()
	{
		FaderGroupModels.Reset();

		const TArray<UDMXControlConsoleFaderGroup*> FaderGroups = WeakControlConsoleData.IsValid() ? WeakControlConsoleData->GetAllFaderGroups() : TArray<UDMXControlConsoleFaderGroup*>{};
		Algo::Transform(FaderGroups, FaderGroupModels, [](UDMXControlConsoleFaderGroup* FaderGroup)
			{
				return MakeShared<FFilterModelFaderGroup>(FaderGroup);;
			});
	}

	void  FFilterModel::ApplyFilter()
	{
		TArray<UDMXControlConsoleFaderBase*> MatchingFaders;
		for (const TSharedRef<FFilterModelFaderGroup>& FaderGroupModel : FaderGroupModels)
		{
			FaderGroupModel->Apply(GlobalFilter, NameFilterMode);
		}
	}
}
