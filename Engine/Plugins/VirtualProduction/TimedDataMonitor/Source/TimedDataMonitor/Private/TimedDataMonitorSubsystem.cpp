// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimedDataMonitorSubsystem.h"

#include "ITimeManagementModule.h"
#include "TimedDataInputCollection.h"

#define LOCTEXT_NAMESPACE "TimedDataMonitorSubsystem"

/**
 *
 */
FTimedDataMonitorGroupIdentifier FTimedDataMonitorGroupIdentifier::NewIdentifier()
{
	FTimedDataMonitorGroupIdentifier Item;
	Item.Group = FGuid::NewGuid();
	return Item;
}


/**
 *
 */
FTimedDataMonitorInputIdentifier FTimedDataMonitorInputIdentifier::NewIdentifier()
{
	FTimedDataMonitorInputIdentifier Item;
	Item.Input = FGuid::NewGuid();
	return Item;
}


/**
 * 
 */
bool UTimedDataMonitorSubsystem::FTimeDataInputItem::HasGroup() const
{
	return GroupIdentifier.IsValidGroup();
}


void UTimedDataMonitorSubsystem::FTimeDataInputItem::ResetValue()
{
}

/**
 *
 */
//void UTimedDataMonitorSubsystem::FTimeDataInputItemGroup::UpdateValue(int32 NewBufferSize)
//{
//	++BufferSizeAverageCount;
//	BufferSizeAverageValue += ((NewBufferSize- BufferSizeAverageValue)/BufferSizeAverageCount);
//}

void UTimedDataMonitorSubsystem::FTimeDataInputItemGroup::ResetValue()
{
	InputIdentifiers.Reset();
}

/**
 * 
 */
void UTimedDataMonitorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	OtherGroupIdentifier = FTimedDataMonitorGroupIdentifier::NewIdentifier();
	bRequestSourceListRebuilt = true;
	ITimeManagementModule::Get().GetTimedDataInputCollection().OnCollectionChanged().AddUObject(this, &UTimedDataMonitorSubsystem::OnTimedDataSourceCollectionChanged);
	Super::Initialize(Collection);
}


void UTimedDataMonitorSubsystem::Deinitialize()
{
	if (ITimeManagementModule::IsAvailable())
	{
		ITimeManagementModule::Get().GetTimedDataInputCollection().OnCollectionChanged().RemoveAll(this);
	}

	bRequestSourceListRebuilt = true;
	InputMap.Reset();
	GroupMap.Reset();
	OnIdentifierListChanged_Delegate.Broadcast();
	OnIdentifierListChanged_Dynamic.Broadcast();

	Super::Deinitialize();
}


ITimedDataInput* UTimedDataMonitorSubsystem::GetTimedDataInput(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input;
	}

	return nullptr;
}


ITimedDataInputGroup* UTimedDataMonitorSubsystem::GetTimedDataInputGroup(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		return GroupItem->Group;
	}

	return nullptr;
}


TArray<FTimedDataMonitorGroupIdentifier> UTimedDataMonitorSubsystem::GetAllGroups()
{
	BuildSourcesListIfNeeded();

	TArray<FTimedDataMonitorGroupIdentifier> Result;
	GroupMap.GenerateKeyArray(Result);
	return Result;
}


TArray<FTimedDataMonitorInputIdentifier> UTimedDataMonitorSubsystem::GetAllInputs()
{
	BuildSourcesListIfNeeded();

	TArray<FTimedDataMonitorInputIdentifier> Result;
	InputMap.GenerateKeyArray(Result);
	return Result;
}


void UTimedDataMonitorSubsystem::ResetAllBufferStats()
{
	BuildSourcesListIfNeeded();

	for (auto& InputItt : InputMap)
	{
		InputItt.Value.Input->ResetBufferStats();
	}
}


bool UTimedDataMonitorSubsystem::DoesGroupExist(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	return GroupMap.Find(Identifier) != nullptr;
}


FText UTimedDataMonitorSubsystem::GetGroupDisplayName(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (Identifier == OtherGroupIdentifier)
	{
		return LOCTEXT("DefaultGroupName", "Other");
	}
	else if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		check(GroupItem->Group);
		return GroupItem->Group->GetDisplayName();
	}

	return FText::GetEmpty();
}


void UTimedDataMonitorSubsystem::GetGroupDataBufferSize(const FTimedDataMonitorGroupIdentifier& Identifier, int32& OutMinBufferSize, int32& OutMaxBufferSize)
{
	BuildSourcesListIfNeeded();

	int32 MinValue = TNumericLimits<int32>::Max();
	int32 MaxValue = TNumericLimits<int32>::Min();
	bool bHasElement = false;
	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		for (const FTimedDataMonitorInputIdentifier& InputIdentifier : GroupItem->InputIdentifiers)
		{
			const FTimeDataInputItem& InputItem = InputMap[InputIdentifier];
			if (InputItem.bEnabled)
			{
				int32 BufferSize = InputItem.Input->GetDataBufferSize();
				MinValue = FMath::Min(BufferSize, MinValue);
				MaxValue = FMath::Max(BufferSize, MaxValue);
				bHasElement = true;
			}
		}
	}

	OutMinBufferSize = bHasElement ? MinValue : 0; 
	OutMaxBufferSize = bHasElement ? MaxValue : 0;
}


void UTimedDataMonitorSubsystem::SetGroupDataBufferSize(const FTimedDataMonitorGroupIdentifier& Identifier, int32 BufferSize)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		for (const FTimedDataMonitorInputIdentifier& InputIdentifier : GroupItem->InputIdentifiers)
		{
			const FTimeDataInputItem& InputItem = InputMap[InputIdentifier];
			if (InputItem.bEnabled)
			{
				InputItem.Input->SetDataBufferSize(BufferSize);
			}
		}
	}
}


ETimedDataInputState UTimedDataMonitorSubsystem::GetGroupState(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	ETimedDataInputState WorstState = ETimedDataInputState::Connected;
	bool bHasAtLeastOneItem = false; 
	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		for (const FTimedDataMonitorInputIdentifier& InputIdentifier : GroupItem->InputIdentifiers)
		{
			const FTimeDataInputItem& InputItem = InputMap[InputIdentifier];
			if (InputItem.bEnabled)
			{
				bHasAtLeastOneItem = true;
				ETimedDataInputState InputState = InputItem.Input->GetState();
				if (InputState == ETimedDataInputState::Disconnected)
				{
					WorstState = ETimedDataInputState::Disconnected;
					break;
				}
				else if (InputState == ETimedDataInputState::Unresponsive)
				{
					WorstState = ETimedDataInputState::Unresponsive;
				}
			}
		}
	}

	return bHasAtLeastOneItem ? WorstState : ETimedDataInputState::Disconnected;
}


void UTimedDataMonitorSubsystem::ResetGroupBufferStats(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		for (const FTimedDataMonitorInputIdentifier& InputIdentifier : GroupItem->InputIdentifiers)
		{
			const FTimeDataInputItem& InputItem = InputMap[InputIdentifier];
			InputItem.Input->ResetBufferStats();
		}
	}
}


ETimedDataMonitorGroupEnabled UTimedDataMonitorSubsystem::GetGroupEnabled(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		int32 bCountEnabled = 0;
		int32 bCountDisabled = 0;
		for (const FTimedDataMonitorInputIdentifier& Input : GroupItem->InputIdentifiers)
		{
			if (InputMap[Input].bEnabled)
			{
				++bCountEnabled;
				if (bCountDisabled > 0)
				{
					return ETimedDataMonitorGroupEnabled::MultipleValues;
				}
			}
			else
			{
				++bCountDisabled;
				if (bCountEnabled > 0)
				{
					return ETimedDataMonitorGroupEnabled::MultipleValues;
				}
			}
		}
		return bCountEnabled > 0 ? ETimedDataMonitorGroupEnabled::Enabled : ETimedDataMonitorGroupEnabled::Disabled;
	}

	return ETimedDataMonitorGroupEnabled::Disabled;
}


void UTimedDataMonitorSubsystem::SetGroupEnabled(const FTimedDataMonitorGroupIdentifier& Identifier, bool bInEnabled)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		for (const FTimedDataMonitorInputIdentifier& InputId : GroupItem->InputIdentifiers)
		{
			InputMap[InputId].bEnabled = bInEnabled;
		}
	}
}


bool UTimedDataMonitorSubsystem::DoesInputExist(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	return InputMap.Find(Identifier) != nullptr;
}


FText UTimedDataMonitorSubsystem::GetInputDisplayName(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetDisplayName();
	}

	return FText::GetEmpty();
}


FTimedDataMonitorGroupIdentifier UTimedDataMonitorSubsystem::GetInputGroup(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->GroupIdentifier;
	}

	return FTimedDataMonitorGroupIdentifier();
}


ETimedDataInputEvaluationType UTimedDataMonitorSubsystem::GetInputEvaluationType(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetEvaluationType();
	}

	return ETimedDataInputEvaluationType::None;
}


void UTimedDataMonitorSubsystem::SetInputEvaluationType(const FTimedDataMonitorInputIdentifier& Identifier, ETimedDataInputEvaluationType Evaluation)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->SetEvaluationType(Evaluation);
	}
}


float UTimedDataMonitorSubsystem::GetInputEvaluationOffsetInSeconds(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return (float)SourceItem->Input->GetEvaluationOffsetInSeconds();
	}

	return 0.f;
}


void UTimedDataMonitorSubsystem::SetInputEvaluationOffsetInSeconds(const FTimedDataMonitorInputIdentifier& Identifier, float Offset)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->SetEvaluationOffsetInSeconds(Offset);
	}
}


ETimedDataInputState UTimedDataMonitorSubsystem::GetInputState(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetState();
	}

	return ETimedDataInputState::Disconnected;
}


FFrameRate UTimedDataMonitorSubsystem::GetInputFrameRate(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetFrameRate();
	}

	return ITimedDataInput::UnknowedFrameRate;
}


FTimedDataInputSampleTime UTimedDataMonitorSubsystem::GetInputNewestDataTime(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetNewestDataTime();
	}

	return FTimedDataInputSampleTime();
}


int32 UTimedDataMonitorSubsystem::GetInputDataBufferSize(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetDataBufferSize();
	}

	return 0;
}


void UTimedDataMonitorSubsystem::SetInputDataBufferSize(const FTimedDataMonitorInputIdentifier& Identifier, int32 BufferSize)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->SetDataBufferSize(BufferSize);
	}
}


bool UTimedDataMonitorSubsystem::IsInputEnabled(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->bEnabled;
	}

	return false;
}


void UTimedDataMonitorSubsystem::ResetInputBufferStats(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		SourceItem->Input->ResetBufferStats();
	}
}


void UTimedDataMonitorSubsystem::SetInputEnabled(const FTimedDataMonitorInputIdentifier& Identifier, bool bInEnabled)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		SourceItem->bEnabled = bInEnabled;
	}
}


void UTimedDataMonitorSubsystem::BuildSourcesListIfNeeded()
{
	if (bRequestSourceListRebuilt)
	{
		if (!ITimeManagementModule::IsAvailable())
		{
			GroupMap.Reset();
			InputMap.Reset();
		}
		else
		{
			bRequestSourceListRebuilt = false;

			// Build ReverseGroupMap
			TMap<ITimedDataInputGroup*, FTimedDataMonitorGroupIdentifier> ReverseGroupMap;
			for (const auto& Itt : GroupMap)
			{
				if (Itt.Value.Group)
				{
					ReverseGroupMap.Add(Itt.Value.Group, Itt.Key);
				}
			}

			const TArray<ITimedDataInputGroup*>& TimedDataGoups = ITimeManagementModule::Get().GetTimedDataInputCollection().GetGroups();
			const TArray<ITimedDataInput*>& TimedDataInputs = ITimeManagementModule::Get().GetTimedDataInputCollection().GetInputs();

			TArray<FTimedDataMonitorGroupIdentifier> PreviousGroupList;
			GroupMap.GenerateKeyArray(PreviousGroupList);

			TArray<FTimedDataMonitorInputIdentifier> PreviousInputList;
			InputMap.GenerateKeyArray(PreviousInputList);

			// Regenerate the list of group
			{
				for (ITimedDataInputGroup* TimedDataGoup : TimedDataGoups)
				{
					if(TimedDataGoup == nullptr)
					{
						continue;
					}

					FTimedDataMonitorGroupIdentifier* FoundGroupIdentifier = ReverseGroupMap.Find(TimedDataGoup);
					if (FoundGroupIdentifier)
					{
						PreviousGroupList.RemoveSingleSwap(*FoundGroupIdentifier);
						GroupMap[*FoundGroupIdentifier].ResetValue();
					}
					else
					{
						// if not found, add it to the list
						FTimeDataInputItemGroup NewGroup; 
						NewGroup.Group = TimedDataGoup;

						FTimedDataMonitorGroupIdentifier GroupIdentifier = FTimedDataMonitorGroupIdentifier::NewIdentifier();
						GroupMap.Add(GroupIdentifier, MoveTemp(NewGroup));
						ReverseGroupMap.Add(TimedDataGoup, GroupIdentifier);
					}
				}
			}

			// Look to see if there is any item that needs the Other group
			{
				for (ITimedDataInput* TimedDataInput : TimedDataInputs)
				{
					if (TimedDataInput && TimedDataInput->GetGroup() == nullptr)
					{
						PreviousGroupList.RemoveSingleSwap(OtherGroupIdentifier);
						FTimeDataInputItemGroup* FoundGroup = GroupMap.Find(OtherGroupIdentifier);
						if (FoundGroup)
						{
							FoundGroup->ResetValue();
						}
						break;
					}
				}
			}

			// Remove old groups
			{
				check(PreviousGroupList.Num() == 0);
				for (const FTimedDataMonitorGroupIdentifier& Old : PreviousGroupList)
				{
					GroupMap.Remove(Old);
				}
			}

			// Regenerate the list of inputs
			{
				for (ITimedDataInput* TimedDataInput : TimedDataInputs)
				{
					if (TimedDataInput == nullptr)
					{
						continue;
					}

					FTimedDataMonitorGroupIdentifier GroupIdentifier = OtherGroupIdentifier;
					if (ITimedDataInputGroup* Group = TimedDataInput->GetGroup())
					{
						FTimedDataMonitorGroupIdentifier* FoundGroupIdentifier = ReverseGroupMap.Find(Group);
						if (ensure(FoundGroupIdentifier))
						{
							GroupIdentifier = *FoundGroupIdentifier;
						}
					}

					// Find and remove from the Previous list
					bool bFound = false;
					for (auto& Itt : InputMap)
					{
						if (Itt.Value.Input == TimedDataInput)
						{
							bFound = true;
							PreviousInputList.RemoveSingleSwap(Itt.Key);

							Itt.Value.GroupIdentifier = GroupIdentifier;
							Itt.Value.ResetValue();
							break;
						}
					}

					// if not found, add it to the list
					if (!bFound)
					{
						FTimeDataInputItem NewInput;
						NewInput.Input = TimedDataInput;
						NewInput.GroupIdentifier = GroupIdentifier;
						
						FTimedDataMonitorInputIdentifier NewIdentifier = FTimedDataMonitorInputIdentifier::NewIdentifier();
						InputMap.Add(NewIdentifier, MoveTemp(NewInput));
					}
				}
			}

			// Remove old inputs
			{
				check(PreviousInputList.Num() == 0);
				for (const FTimedDataMonitorInputIdentifier& Old : PreviousInputList)
				{
					InputMap.Remove(Old);
				}
			}

			// generate group's input list
			for (const auto& Itt : InputMap)
			{
				FTimeDataInputItemGroup* FoundGroup = GroupMap.Find(Itt.Value.GroupIdentifier);
				if (ensure(FoundGroup))
				{
					FoundGroup->InputIdentifiers.Add(Itt.Key);
				}
			}
		}
	}
}


void UTimedDataMonitorSubsystem::OnTimedDataSourceCollectionChanged()
{
	bRequestSourceListRebuilt = true;

	// update map right away to not have dandling pointer
	const TArray<ITimedDataInputGroup*>& TimedDataGroups = ITimeManagementModule::Get().GetTimedDataInputCollection().GetGroups();
	TArray<FTimedDataMonitorGroupIdentifier, TInlineAllocator<4>> GroupToRemove;
	for (const auto& Itt : GroupMap)
	{
		if (!TimedDataGroups.Contains(Itt.Value.Group))
		{
			GroupToRemove.Add(Itt.Key);
		}
	}
	for (const FTimedDataMonitorGroupIdentifier& Id : GroupToRemove)
	{
		if (Id != OtherGroupIdentifier)
		{
			GroupMap.Remove(Id);
		}
	}

	const TArray<ITimedDataInput*>& TimedDataInputs = ITimeManagementModule::Get().GetTimedDataInputCollection().GetInputs();
	TArray<FTimedDataMonitorInputIdentifier, TInlineAllocator<4>> InputToRemove;
	for (const auto& Itt : InputMap)
	{
		if (!TimedDataInputs.Contains(Itt.Value.Input))
		{
			InputToRemove.Add(Itt.Key);
		}
	}
	for (const FTimedDataMonitorInputIdentifier& Id : InputToRemove)
	{
		InputMap.Remove(Id);
	}

	OnIdentifierListChanged_Delegate.Broadcast();
	OnIdentifierListChanged_Dynamic.Broadcast();
}


#undef LOCTEXT_NAMESPACE
