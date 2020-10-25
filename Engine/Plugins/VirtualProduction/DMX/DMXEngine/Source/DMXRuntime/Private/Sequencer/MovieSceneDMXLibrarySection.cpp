// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneDMXLibrarySection.h"

#include "DMXProtocolCommon.h"
#include "DMXRuntimeLog.h"
#include "DMXSubsystem.h"
#include "Interfaces/IDMXProtocol.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneChannelEditorData.h"
#include "Evaluation/Blending/MovieSceneBlendType.h"

DECLARE_LOG_CATEGORY_CLASS(MovieSceneDMXLibrarySectionLog, Log, All);

void FDMXFixturePatchChannel::SetFixturePatch(UDMXEntityFixturePatch* InPatch, int32 InActiveMode /*= INDEX_NONE*/)
{
	if (InPatch != nullptr && InPatch->IsValidLowLevelFast())
	{
		ActiveMode = InActiveMode == INDEX_NONE ? InPatch->ActiveMode : InActiveMode;
	}

	Reference.SetEntity(InPatch);
	UpdateNumberOfChannels();
}

void FDMXFixturePatchChannel::UpdateNumberOfChannels(bool bResetDefaultValues /*= false*/)
{
	// Test if the patch is still being what was recorded
	bool bValidPatchChannel = true;

	UDMXEntityFixturePatch* Patch = Reference.GetFixturePatch();

	if (Patch == nullptr || !Patch->IsValidLowLevelFast())
	{
		UE_LOG(LogDMXRuntime, Warning, TEXT("Patch removed from DMX Library. Corresponding DMX Channel removed from Sequencer."));
		bValidPatchChannel = false;
	}

	UDMXEntityFixtureType* FixtureType = nullptr;
	if (Patch)
	{
		FixtureType = Patch->ParentFixtureTypeTemplate;
		if (FixtureType == nullptr)
		{
			UE_LOG(LogDMXRuntime, Warning, TEXT("%s has no valid Parent Fixture Type set. Corresponding DMX Channel removed from Sequencer."), *Patch->GetDisplayName());
			bValidPatchChannel = false;
		}
		else if (!FixtureType->Modes.IsValidIndex(ActiveMode))
		{
			UE_LOG(LogDMXRuntime, Warning, TEXT("Recorded Mode '%s' no longer exists in '%s'. Channel Removed from Sequencer."), *FixtureType->Modes[ActiveMode].ModeName, *FixtureType->GetDisplayName());
			bValidPatchChannel = false;
		}
	}

	if (!bValidPatchChannel)
	{
		FunctionChannels.Empty();
		ActiveMode = INDEX_NONE;
		return;
	}

	const FDMXFixtureMode& Mode = FixtureType->Modes[ActiveMode];
	if (Patch->ActiveMode != ActiveMode)
	{
		UE_LOG(LogDMXRuntime, Warning, TEXT("Active Mode of '%s' changed. Its channel in Sequencer uses recorded Mode '%s'."), *FixtureType->GetDisplayName(), *FixtureType->Modes[ActiveMode].ModeName);
		UE_LOG(LogDMXRuntime, Warning, TEXT("Only channels with matching attribute will be displayed and played, potentially resulting in empty channels."));
	}

	const int32 PatchChannelOffset = Patch->GetStartingChannel() - 1;

	int32 IdxFunctionChannel = 0;
	for (const FDMXFixtureFunction& Function : Mode.Functions)
	{
		// Add channels for functions that are in mode range and have an Attribute set

		if (UDMXEntityFixtureType::IsFunctionInModeRange(Function, Mode, PatchChannelOffset) &&
			FDMXAttributeName::IsValid(Function.Attribute.GetName()))
		{
			bool bNewChannel = false;
			if (!FunctionChannels.IsValidIndex(IdxFunctionChannel))
			{
				FunctionChannels.Add(FDMXFixtureFunctionChannel());
				bNewChannel = true;
			}					

			FunctionChannels[IdxFunctionChannel].AttributeName = Function.Attribute.GetName();

			if (bResetDefaultValues || bNewChannel)
			{
				const int64& FunctionDefaultValue = Function.DefaultValue;
				FunctionChannels[IdxFunctionChannel].DefaultValue = (int32)FunctionDefaultValue;
				FunctionChannels[IdxFunctionChannel].Channel.Reset();
				FunctionChannels[IdxFunctionChannel].Channel.SetDefault((float)FunctionDefaultValue);
			}

			++IdxFunctionChannel;
		}
	}

	if (UDMXEntityFixtureType::IsFixtureMatrixInModeRange(Mode.FixtureMatrixConfig, Mode, PatchChannelOffset))
	{
		UDMXSubsystem* DMXSubsystem = UDMXSubsystem::GetDMXSubsystem_Pure();
		check(DMXSubsystem);

		int32 NumXCells = Mode.FixtureMatrixConfig.XCells;
		int32 NumYCells = Mode.FixtureMatrixConfig.YCells;

		for (int32 IdxCellX = 0; IdxCellX < NumXCells; IdxCellX++)
		{
			for (int32 IdxCellY = 0; IdxCellY < NumYCells; IdxCellY++)
			{
				FIntPoint CellCoordinates = FIntPoint(IdxCellX, IdxCellY);

				for (const FDMXFixtureCellAttribute& CellAttribute : Mode.FixtureMatrixConfig.CellAttributes)
				{
					bool bNewChannel = false;
					if (!FunctionChannels.IsValidIndex(IdxFunctionChannel))
					{
						FunctionChannels.Add(FDMXFixtureFunctionChannel());
						bNewChannel = true;
					}

					FunctionChannels[IdxFunctionChannel].AttributeName = CellAttribute.Attribute.GetName();
					FunctionChannels[IdxFunctionChannel].CellCoordinate = CellCoordinates;

					if (bResetDefaultValues || bNewChannel)
					{
						const int64& FunctionDefaultValue = CellAttribute.DefaultValue;
						FunctionChannels[IdxFunctionChannel].DefaultValue = (int32)FunctionDefaultValue;
						FunctionChannels[IdxFunctionChannel].Channel.Reset();
						FunctionChannels[IdxFunctionChannel].Channel.SetDefault((float)FunctionDefaultValue);
					}

					++IdxFunctionChannel;
				}
			}
		}
	}

	if(FunctionChannels.Num() > IdxFunctionChannel + 1)
	{
		FunctionChannels.SetNum(IdxFunctionChannel + 1);
	}
}

UMovieSceneDMXLibrarySection::UMovieSceneDMXLibrarySection()
	: bIsRecording(false)
{
	BlendType = EMovieSceneBlendType::Absolute;
	UpdateChannelProxy();
}

void UMovieSceneDMXLibrarySection::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		UpdateChannelProxy();
	}
}

void UMovieSceneDMXLibrarySection::PostEditImport()
{
	Super::PostEditImport();

	UpdateChannelProxy();
}

void UMovieSceneDMXLibrarySection::RefreshChannels()
{
	UpdateChannelProxy();
}

void UMovieSceneDMXLibrarySection::AddFixturePatch(UDMXEntityFixturePatch* InPatch, int32 ActiveMode /*= INDEX_NONE*/)
{
	if (InPatch == nullptr || !InPatch->IsValidLowLevelFast())
	{
		return;
	}

	FDMXFixturePatchChannel NewPatchChannel;
	NewPatchChannel.SetFixturePatch(InPatch, ActiveMode);
	FixturePatchChannels.Add(NewPatchChannel);

	UpdateChannelProxy();
}

void UMovieSceneDMXLibrarySection::RemoveFixturePatch(UDMXEntityFixturePatch* InPatch)
{
	int32 PatchIndex = FixturePatchChannels.IndexOfByPredicate([InPatch](const FDMXFixturePatchChannel& PatchChannel)
		{
			return PatchChannel.Reference.GetFixturePatch() == InPatch;
		});

	if (PatchIndex != INDEX_NONE)
	{
		FixturePatchChannels.RemoveAt(PatchIndex);

		UpdateChannelProxy();
	}
}

void UMovieSceneDMXLibrarySection::RemoveFixturePatch(const FName& InPatchName)
{
	// Search for the Fixture Patch
	const FString&& TargetPatchName = InPatchName.ToString();

	for (const FDMXFixturePatchChannel& PatchChannel : FixturePatchChannels)
	{
		if (UDMXEntityFixturePatch* Patch = PatchChannel.Reference.GetFixturePatch())
		{
			if (Patch->GetDisplayName().Equals(TargetPatchName))
			{
				RemoveFixturePatch(Patch);
				break;
			}
		}
	}
}

bool UMovieSceneDMXLibrarySection::ContainsFixturePatch(UDMXEntityFixturePatch* InPatch) const
{
	int32 PatchIndex = FixturePatchChannels.IndexOfByPredicate([InPatch](const FDMXFixturePatchChannel& PatchChannel) {
			return PatchChannel.Reference.GetFixturePatch() == InPatch;
		});

	return PatchIndex != INDEX_NONE;
}

void UMovieSceneDMXLibrarySection::SetFixturePatchActiveMode(UDMXEntityFixturePatch* InPatch, int32 InActiveMode)
{
	if (InPatch == nullptr || !InPatch->IsValidLowLevelFast())
	{
		return;
	}

	// Make sure the Mode Index is valid
	const UDMXEntityFixtureType* FixtureType = InPatch->ParentFixtureTypeTemplate;
	if (FixtureType == nullptr || !FixtureType->IsValidLowLevelFast())
	{
		return;
	}
	if (InActiveMode < 0 || InActiveMode >= FixtureType->Modes.Num())
	{
		return;
	}

	// Find the PatchChannel object that represents the passed in Patch
	for (FDMXFixturePatchChannel& PatchChannel : FixturePatchChannels)
	{
		UDMXEntityFixturePatch* Patch = PatchChannel.Reference.GetFixturePatch();
		if (Patch == InPatch)
		{
			PatchChannel.ActiveMode = InActiveMode;
			const bool bResetFunctionChannelsValues = true;
			UpdateChannelProxy(bResetFunctionChannelsValues);

			break;
		}
	}
}

void UMovieSceneDMXLibrarySection::ToggleFixturePatchChannel(UDMXEntityFixturePatch* InPatch, int32 InChannelIndex)
{
	if (InPatch == nullptr || !InPatch->IsValidLowLevelFast())
	{
		return;
	}

	const UDMXEntityFixtureType* FixtureType = InPatch->ParentFixtureTypeTemplate;
	if (FixtureType == nullptr || !FixtureType->IsValidLowLevelFast())
	{
		return;
	}

	// Find the PatchChannel object that represents the passed in Patch
	for (FDMXFixturePatchChannel& PatchChannel : FixturePatchChannels)
	{
		UDMXEntityFixturePatch* Patch = PatchChannel.Reference.GetFixturePatch();
		if (Patch == InPatch)
		{
			PatchChannel.UpdateNumberOfChannels();

			FDMXFixtureFunctionChannel& FunctionChannel = PatchChannel.FunctionChannels[InChannelIndex];
			FunctionChannel.bEnabled = !FunctionChannel.bEnabled;

			UpdateChannelProxy();

			break;
		}
	}
}

void UMovieSceneDMXLibrarySection::ToggleFixturePatchChannel(const FName& InPatchName, const FName& InChannelName)
{
	// Search for the Fixture Patch
	const FString&& TargetPatchName = InPatchName.ToString();

	for (FDMXFixturePatchChannel& PatchChannel : FixturePatchChannels)
	{
		if (UDMXEntityFixturePatch* Patch = PatchChannel.Reference.GetFixturePatch())
		{
			if (Patch->GetDisplayName().Equals(TargetPatchName))
			{
				const UDMXEntityFixtureType* FixtureType = Patch->ParentFixtureTypeTemplate;
				if (FixtureType == nullptr || !FixtureType->IsValidLowLevelFast())
				{
					return;
				}

				if (FixtureType->Modes.Num() <= PatchChannel.ActiveMode)
				{
					return;
				}

				// Search for the Function index
				const FString&& TargetFunctionName = InChannelName.ToString();
				int32 FunctionIndex = 0;
				for (const FDMXFixtureFunction& Function : FixtureType->Modes[PatchChannel.ActiveMode].Functions)
				{
					if (Function.FunctionName.Equals(TargetFunctionName))
					{
						// PatchChannel could be out of sync in channels count
						if (PatchChannel.FunctionChannels.Num() <= FunctionIndex)
						{
							PatchChannel.UpdateNumberOfChannels();
						}

						FDMXFixtureFunctionChannel& FunctionChannel = PatchChannel.FunctionChannels[FunctionIndex];
						FunctionChannel.bEnabled = !FunctionChannel.bEnabled;

						UpdateChannelProxy();
						return;
					}
					++FunctionIndex;
				}

				return;
			}
		}
	}
}

bool UMovieSceneDMXLibrarySection::GetFixturePatchChannelEnabled(UDMXEntityFixturePatch* InPatch, int32 InChannelIndex) const
{
	if (InPatch == nullptr || !InPatch->IsValidLowLevelFast())
	{
		return false;
	}

	const UDMXEntityFixtureType* FixtureType = InPatch->ParentFixtureTypeTemplate;
	if (FixtureType == nullptr || !FixtureType->IsValidLowLevelFast())
	{
		return false;
	}

	// Find the PatchChannel object that represents the passed in Patch
	for (const FDMXFixturePatchChannel& PatchChannel : FixturePatchChannels)
	{
		UDMXEntityFixturePatch* Patch = PatchChannel.Reference.GetFixturePatch();
		if (Patch == InPatch &&
			PatchChannel.FunctionChannels.IsValidIndex(InChannelIndex))
		{
			return PatchChannel.FunctionChannels[InChannelIndex].bEnabled;
		}
	}

	return false;
}

TArray<UDMXEntityFixturePatch*> UMovieSceneDMXLibrarySection::GetFixturePatches() const
{
	TArray<UDMXEntityFixturePatch*> Result;
	Result.Reserve(FixturePatchChannels.Num());

	for (const FDMXFixturePatchChannel& PatchRef : FixturePatchChannels)
	{
		// Add only valid patches
		if (UDMXEntityFixturePatch* Patch = PatchRef.Reference.GetFixturePatch())
		{
			if (!Patch->IsValidLowLevelFast())
			{
				Result.Add(Patch);
			}
		}
	}

	return Result;
}

FDMXFixturePatchChannel* UMovieSceneDMXLibrarySection::GetPatchChannel(UDMXEntityFixturePatch* Patch)
{
	return 
		FixturePatchChannels.FindByPredicate([Patch](const FDMXFixturePatchChannel& Channel) {
			return Channel.Reference.GetFixturePatch() == Patch;
		});
}

void UMovieSceneDMXLibrarySection::ForEachPatchFunctionChannels(TFunctionRef<void(UDMXEntityFixturePatch*, TArray<FDMXFixtureFunctionChannel>&)> InPredicate)
{
	for (FDMXFixturePatchChannel& PatchChannel : FixturePatchChannels)
	{
		if (UDMXEntityFixturePatch* Patch = PatchChannel.Reference.GetFixturePatch())
		{
			int32 FunctionIndex = 0;
			InPredicate(Patch, PatchChannel.FunctionChannels);
		}
	}
}

void UMovieSceneDMXLibrarySection::UpdateChannelProxy(bool bResetDefaultChannelValues /*= false*/)
{
	FMovieSceneChannelProxyData ChannelProxyData;
	TArray<int32> InvalidPatchChannelIndices;

	int32 PatchChannelIndex = 0; // Safer because the ranged for ensures the array length isn't changed
	for (FDMXFixturePatchChannel& PatchChannel : FixturePatchChannels)
	{
		PatchChannel.UpdateNumberOfChannels(bResetDefaultChannelValues);

		const UDMXEntityFixturePatch* Patch = PatchChannel.Reference.GetFixturePatch();
		if (Patch == nullptr || !Patch->IsValidLowLevelFast())
		{
			UE_LOG(MovieSceneDMXLibrarySectionLog, Warning, TEXT("%S: Removing a null Patch"), __FUNCTION__);
			InvalidPatchChannelIndices.Add(PatchChannelIndex);
			continue;
		}

		// If the Patch is null, invalid, doesn't have Modes or the selected mode doesn't have any functions,
		// FunctionChannels will be empty
		if (PatchChannel.FunctionChannels.Num() == 0)
		{
			// With no Function Channels to be displayed, the Patch group won't be displayed.
			// This will give users the impression that the Patch isn't added, but it is,
			// which prevents the user from adding it again. So, to mitigate that, we remove
			// the Patch from the track section.
			UE_LOG(MovieSceneDMXLibrarySectionLog, Warning, TEXT("%S: Removing empty Patch %s"), __FUNCTION__, *Patch->GetDisplayName());
			InvalidPatchChannelIndices.Add(PatchChannelIndex);
			continue;
		}

		// const TArray<FDMXFixtureFunction>& Functions = Patch->ParentFixtureTypeTemplate->Modes[PatchChannel.ActiveMode].Functions;
		UDMXEntityFixtureType* FixtureType = Patch->ParentFixtureTypeTemplate;
		check(FixtureType);

		const TArray<FDMXFixtureFunction>& Functions = FixtureType->Modes[PatchChannel.ActiveMode].Functions;
		TArray<FDMXFixtureFunctionChannel>& FunctionChannels = PatchChannel.FunctionChannels;

		// Add a channel proxy entry for each Function channel
		// We use the length of FunctionChannels because it's possible some Fixture Functions
		// are outside the valid range for the Mode's channels or the Universe's channels.
		// And that's already been accounted for when generating the Function channels
		int32 SortOrder = 0;
		for (FDMXFixtureFunctionChannel& FunctionChannel : PatchChannel.FunctionChannels)
		{
			if (!FunctionChannel.bEnabled)
			{
				continue;
			}


#if WITH_EDITOR
			FString AttributeName = FunctionChannel.AttributeName.ToString();

			FString ChannelDisplayNameString;
			if (FunctionChannel.IsCellFunction())
			{
				ChannelDisplayNameString =
					TEXT(" Cell ") +
					FString::FromInt(FunctionChannel.CellCoordinate.X) +
					TEXT("x") +
					FString::FromInt(FunctionChannel.CellCoordinate.Y);

				// Tabulator cosmetics
				if (ChannelDisplayNameString.Len() < 10)
				{
					ChannelDisplayNameString = 
						ChannelDisplayNameString +
						TCString<TCHAR>::Tab(4);
				}
				else
				{
					ChannelDisplayNameString =
						ChannelDisplayNameString +
						TCString<TCHAR>::Tab(3);
				}

				ChannelDisplayNameString = ChannelDisplayNameString + AttributeName;
			}
			else
			{
				ChannelDisplayNameString = AttributeName;
			}

			const FText ChannelDisplayName(FText::FromString(ChannelDisplayNameString));
			const FName ChannelPropertyName(*FString::Printf(TEXT("%s.%s"), *Patch->GetDisplayName(), *ChannelDisplayNameString));

			FMovieSceneChannelMetaData MetaData;
			MetaData.SetIdentifiers(ChannelPropertyName, ChannelDisplayName, FText::FromString(Patch->GetDisplayName()));
			MetaData.SortOrder = SortOrder++;
			MetaData.bCanCollapseToTrack = false;

			ChannelProxyData.Add(FunctionChannel.Channel, MetaData, TMovieSceneExternalValue<float>());
#else
			ChannelProxyData.Add(FunctionChannel.Channel);
#endif // WITH_EDITOR
		}

		++PatchChannelIndex;
	}

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(ChannelProxyData));

	// Remove Patches that can't be seen by users because they don't have any functions
	// or represent an invalid Patch object
	for (const int32& InvalidPatchChannelIndex : InvalidPatchChannelIndices)
	{
		FixturePatchChannels.RemoveAt(InvalidPatchChannelIndex);
	}
}
