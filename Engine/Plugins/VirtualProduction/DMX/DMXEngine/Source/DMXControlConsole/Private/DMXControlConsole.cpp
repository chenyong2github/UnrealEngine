// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsole.h"

#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFaderGroupRow.h"
#include "DMXProtocolCommon.h"
#include "IO/DMXOutputPort.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXLibrary.h"

#include "Algo/Sort.h"


#define LOCTEXT_NAMESPACE "DMXControlConsole"

UDMXControlConsoleFaderGroupRow* UDMXControlConsole::AddFaderGroupRow(const int32 RowIndex = 0)
{
	if (!ensureMsgf(RowIndex >= 0, TEXT("Invalid index. Cannot add new fader group row to '%s' correctly."), *GetName()))
	{
		return nullptr;
	}

	UDMXControlConsoleFaderGroupRow* FaderGroupRow = NewObject<UDMXControlConsoleFaderGroupRow>(this, NAME_None, RF_Transactional);
	FaderGroupRows.Insert(FaderGroupRow, RowIndex);
	FaderGroupRow->AddFaderGroup(0);

	return FaderGroupRow;
}

void UDMXControlConsole::DeleteFaderGroupRow(const TObjectPtr<UDMXControlConsoleFaderGroupRow>& FaderGroupRow)
{
	if (!ensureMsgf(FaderGroupRow, TEXT("Invalid fader group row, cannot delete from '%s'."), *GetName()))
	{
		return;
	}
	
	if (!ensureMsgf(FaderGroupRows.Contains(FaderGroupRow), TEXT("'%s' is not owner of '%s'. Cannot delete fader group row correctly."), *GetName(), *FaderGroupRow->GetName()))
	{
		return;
	}

	FaderGroupRows.Remove(FaderGroupRow);
}

void UDMXControlConsole::GenarateFromDMXLibrary()
{
	if (!DMXLibrary.IsValid())
	{
		return;
	}

	TArray<UDMXEntityFixturePatch*> FixturePatchesInLibrary = DMXLibrary->GetEntitiesTypeCast<UDMXEntityFixturePatch>();

	// Get All Fixture Patches in use in a Fader Group
	auto FaderGroupsHasFixturePatchInUseLambda = [](const UDMXControlConsoleFaderGroup* FaderGroup)
	{
		return FaderGroup && FaderGroup->HasFixturePatch();
	};
	auto GetFixturePatchFromFaderGroupLambda = [](UDMXControlConsoleFaderGroup* FaderGroup)
	{
		return FaderGroup->GetFixturePatch();
	};
	const TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups = GetAllFaderGroups();
	TArray<UDMXEntityFixturePatch*> AllFixturePatchesInUse;
	Algo::TransformIf(AllFaderGroups, AllFixturePatchesInUse, FaderGroupsHasFixturePatchInUseLambda, GetFixturePatchFromFaderGroupLambda);

	FixturePatchesInLibrary.RemoveAll([AllFixturePatchesInUse](UDMXEntityFixturePatch* FixturePatch)
		{
			return AllFixturePatchesInUse.Contains(FixturePatch);
		});

	auto SortFixturePatchesLambda = [](const UDMXEntityFixturePatch* ItemA, const UDMXEntityFixturePatch* ItemB)
		{
			const int32 UniverseIDA = ItemA->GetUniverseID();
			const int32 UniverseIDB = ItemB->GetUniverseID();

			const int32 StartingChannelA = ItemA->GetStartingChannel();
			const int32 StartingChannelB = ItemB->GetStartingChannel();

			const int64 AbsoluteChannelA = (UniverseIDA - 1) * DMX_MAX_ADDRESS + StartingChannelA;
			const int64 AbsoluteChannelB = (UniverseIDB - 1) * DMX_MAX_ADDRESS + StartingChannelB;

			return AbsoluteChannelA < AbsoluteChannelB;
		};

	Algo::Sort(FixturePatchesInLibrary, SortFixturePatchesLambda);

	int32 CurrentUniverseID = 0;
	for (int32 FixturePatchIndex = 0; FixturePatchIndex < FixturePatchesInLibrary.Num(); ++FixturePatchIndex)
	{
		UDMXEntityFixturePatch* FixturePatch = FixturePatchesInLibrary[FixturePatchIndex];
		if (!FixturePatch)
		{
			continue;
		}

		const int32 UniverseID = FixturePatch->GetUniverseID();
		UDMXControlConsoleFaderGroupRow* FaderGroupRow;
		UDMXControlConsoleFaderGroup* RowFirstFaderGroup = nullptr;
		if (UniverseID > CurrentUniverseID)
		{
			CurrentUniverseID = UniverseID;
			FaderGroupRow = AddFaderGroupRow(FaderGroupRows.Num());
			RowFirstFaderGroup = FaderGroupRow->GetFaderGroups()[0];
		}
		else
		{
			FaderGroupRow = FaderGroupRows.Last();
		}

		if (!FaderGroupRow)
		{
			continue;
		}

		const int32 NextFaderGroupIndex = FaderGroupRow->GetFaderGroups().Num();

		UDMXControlConsoleFaderGroup* FaderGroup = FaderGroupRow->AddFaderGroup(NextFaderGroupIndex);
		if (!FaderGroup)
		{
			continue;
		}
		FaderGroup->GenerateFromFixturePatch(FixturePatch);

		if (RowFirstFaderGroup)
		{
			FaderGroupRow->DeleteFaderGroup(RowFirstFaderGroup);
		}
	}
}

void UDMXControlConsole::SendDMX()
{
	bIsSendingDMX = true;
}

void UDMXControlConsole::StopDMX()
{
	bIsSendingDMX = false;
}

void UDMXControlConsole::UpdateOutputPorts(const TArray<FDMXOutputPortSharedRef> InOutputPorts)
{
	if (InOutputPorts.IsEmpty())
	{
		return;
	}

	OutputPorts = InOutputPorts;
}

void UDMXControlConsole::Reset()
{
	ClearFaderGroupRows();
}

void UDMXControlConsole::PostInitProperties()
{
	Super::PostInitProperties();

	bIsSendingDMX = false;
}

void UDMXControlConsole::Tick(float InDeltaTime)
{
	if (!IsSendingDMX())
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroup*> FaderGroups = GetAllFaderGroups();
	for (const UDMXControlConsoleFaderGroup* FaderGroup : FaderGroups)
	{
		if (!FaderGroup)
		{
			continue;
		}

		UDMXEntityFixturePatch* FixturePatch = FaderGroup->GetFixturePatch();
		if (FixturePatch)
		{
			const TMap<FDMXAttributeName, int32> AttributeMap = FaderGroup->GetAttributeMap();
			FixturePatch->SendDMX(AttributeMap);
		}
		else
		{
			const TMap<int32, TMap<int32, uint8>> UniverseToFragmentMap = FaderGroup->GetUniverseToFragmentMap();
			for (const TTuple<int32, TMap<int32, uint8>>& UniverseToFragement : UniverseToFragmentMap)
			{
				for (const FDMXOutputPortSharedRef& OutputPort : OutputPorts)
				{
					OutputPort->SendDMX(UniverseToFragement.Key, UniverseToFragement.Value);
				}
			}
		}
	}
}

TStatId UDMXControlConsole::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDMXControlConsole, STATGROUP_Tickables);
}

ETickableTickType UDMXControlConsole::GetTickableTickType() const
{
	return ETickableTickType::Always;
}

void UDMXControlConsole::ClearFaderGroupRows()
{
	FaderGroupRows.Reset();
}

TArray<UDMXControlConsoleFaderGroup*> UDMXControlConsole::GetAllFaderGroups() const
{
	TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups;

	for (const TObjectPtr<UDMXControlConsoleFaderGroupRow>& FaderGroupRow : FaderGroupRows)
	{
		if (!FaderGroupRow)
		{
			continue;
		}

		const TArray<UDMXControlConsoleFaderGroup*> FaderGroups = FaderGroupRow->GetFaderGroups();
		if (FaderGroups.IsEmpty())
		{
			continue;
		}

		AllFaderGroups.Append(FaderGroups);
	}

	return AllFaderGroups;
}

#undef LOCTEXT_NAMESPACE
