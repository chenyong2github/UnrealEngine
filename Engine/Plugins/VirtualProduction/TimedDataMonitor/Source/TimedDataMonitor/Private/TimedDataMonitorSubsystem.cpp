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
	UnGroupedInputs.Reset();
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


bool UTimedDataMonitorSubsystem::DoesGroupExist(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	return GroupMap.Find(Identifier) != nullptr;
}


FText UTimedDataMonitorSubsystem::GetGroupDisplayName(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
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


bool UTimedDataMonitorSubsystem::IsGroupEnabled(const FTimedDataMonitorGroupIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItemGroup* GroupItem = GroupMap.Find(Identifier))
	{
		for (const FTimedDataMonitorInputIdentifier& Input : GroupItem->InputIdentifiers)
		{
			if (!InputMap[Input].bEnabled)
			{
				return false;
			}
		}
		return GroupItem->InputIdentifiers.Num() > 0;
	}

	return false;
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


FFrameRate UTimedDataMonitorSubsystem::GetInputFrameRate(const FTimedDataMonitorInputIdentifier& Identifier)
{
	BuildSourcesListIfNeeded();

	if (FTimeDataInputItem* SourceItem = InputMap.Find(Identifier))
	{
		return SourceItem->Input->GetFrameRate();
	}

	return ITimedDataInput::UnknowedFrameRate;
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
			UnGroupedInputs.Reset();
		}
		else
		{
			bRequestSourceListRebuilt = false;

			TMap<ITimedDataInputGroup*, FTimedDataMonitorGroupIdentifier> ReverseGroupMap;
			// Build ReverseGroupMap
			for (const auto& Itt : GroupMap)
			{
				ReverseGroupMap.Add(Itt.Value.Group, Itt.Key);
			}

			// Regenerate the list of group
			{
				TArray<FTimedDataMonitorGroupIdentifier> PreviousGroupList;
				GroupMap.GenerateKeyArray(PreviousGroupList);
				const TArray<ITimedDataInputGroup*>& TimedDataGoups = ITimeManagementModule::Get().GetTimedDataInputCollection().GetGroups();
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

				// Remove old group
				check(PreviousGroupList.Num() == 0);
				for (const FTimedDataMonitorGroupIdentifier& Old : PreviousGroupList)
				{
					GroupMap.Remove(Old);
				}
			}

			// Regenerate the list of inputs
			{
				TArray<FTimedDataMonitorInputIdentifier> PreviousInputList;
				InputMap.GenerateKeyArray(PreviousInputList);
				const TArray<ITimedDataInput*>& TimedDataInputs = ITimeManagementModule::Get().GetTimedDataInputCollection().GetInputs();
				for (ITimedDataInput* TimedDataInput : TimedDataInputs)
				{
					if (TimedDataInput == nullptr)
					{
						continue;
					}

					FTimedDataMonitorGroupIdentifier GroupIdentifier;
					if (ITimedDataInputGroup* Group = TimedDataInput->GetGroup())
					{
						if (FTimedDataMonitorGroupIdentifier* FoundGroupIdentifier = ReverseGroupMap.Find(Group))
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

				// Remove old input
				check(PreviousInputList.Num() == 0);
				for (const FTimedDataMonitorInputIdentifier& Old : PreviousInputList)
				{
					InputMap.Remove(Old);
				}
			}

			// generate group's input list
			UnGroupedInputs.Reset();
			for (const auto& Itt : InputMap)
			{
				FTimeDataInputItemGroup* FoundGroup = Itt.Value.HasGroup() ? GroupMap.Find(Itt.Value.GroupIdentifier) : nullptr;
				if (FoundGroup)
				{
					FoundGroup->InputIdentifiers.Add(Itt.Key);
				}
				else
				{
					UnGroupedInputs.Add(Itt.Key);
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
		GroupMap.Remove(Id);
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
