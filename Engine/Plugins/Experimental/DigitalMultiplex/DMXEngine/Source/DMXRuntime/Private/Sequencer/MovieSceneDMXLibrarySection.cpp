// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneDMXLibrarySection.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "DMXProtocolCommon.h"
#include "Interfaces/IDMXProtocol.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneChannelEditorData.h"
#include "Evaluation/Blending/MovieSceneBlendType.h"

DECLARE_LOG_CATEGORY_CLASS(MovieSceneDMXLibrarySectionLog, Log, All);

void FDMXFixturePatchChannels::SetFixturePatch(UDMXEntityFixturePatch* InPatch, int32 InActiveMode /*= INDEX_NONE*/)
{
	if (InPatch != nullptr && InPatch->IsValidLowLevelFast())
	{
		ActiveMode = InActiveMode == INDEX_NONE ? InPatch->ActiveMode : InActiveMode;
	}

	Reference.SetEntity(InPatch);
	UpdateNumberOfChannels();
}

void FDMXFixturePatchChannels::UpdateNumberOfChannels(bool bResetDefaultValues /*= false*/)
{
	const UDMXEntityFixturePatch* Patch = Reference.GetFixturePatch();
	if (Patch == nullptr || !Patch->IsValidLowLevelFast())
	{
		FunctionChannels.Empty(0);
		ActiveMode = INDEX_NONE;
		return;
	}

	if (UDMXEntityFixtureType* FixtureType = Patch->ParentFixtureTypeTemplate)
	{
		if (FixtureType->Modes.Num() > ActiveMode)
		{
			const FDMXFixtureMode& Mode = FixtureType->Modes[ActiveMode];
			const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;

			// Count only functions in the Mode's range and in the Universe's range
			int32 NumValidFunctions = 0;
			const int32 PatchChannelOffset = Patch->GetStartingChannel() - 1;
			for (const FDMXFixtureFunction& Function : Functions)
			{
				if (UDMXEntityFixtureType::IsFunctionInModeRange(Function, Mode, PatchChannelOffset))
				{
					++NumValidFunctions;
				}
			}

			// Shrink the Function Channels array if there are more channels than Fixture Functions
			if (FunctionChannels.Num() > NumValidFunctions)
			{
				FunctionChannels.SetNum(Functions.Num());
			}

			// Reset values of existing functions, if required.
			if (bResetDefaultValues)
			{
				int32 FunctionIndex = 0;
				for (FDMXFixtureFunctionChannel& FunctionChannel : FunctionChannels)
				{
					const int64& FunctionDefaultValue = Functions[FunctionIndex++].DefaultValue;
					FunctionChannel.DefaultValue = (int32)FunctionDefaultValue;
					FunctionChannel.Channel.Reset();
					FunctionChannel.Channel.SetDefault((float)FunctionDefaultValue);
				}
			}

			// Add a Function Channel for each additional Fixture Function
			while (FunctionChannels.Num() < NumValidFunctions)
			{
				const int32& NewChannelIndex = FunctionChannels.Emplace(FDMXFixtureFunctionChannel());
				FDMXFixtureFunctionChannel& FunctionChannel = FunctionChannels[NewChannelIndex];

				const int64& FunctionDefaultValue = Functions[NewChannelIndex].DefaultValue;
				FunctionChannel.Channel.SetDefault((float)FunctionDefaultValue);
				FunctionChannel.DefaultValue = (int32)FunctionDefaultValue;
			}
		}
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

void UMovieSceneDMXLibrarySection::AddFixturePatch(UDMXEntityFixturePatch* InPatch, int32 ActiveMode /*= INDEX_NONE*/)
{
	if (InPatch == nullptr || !InPatch->IsValidLowLevelFast())
	{
		return;
	}

	FDMXFixturePatchChannels NewPatchChannels;
	NewPatchChannels.SetFixturePatch(InPatch, ActiveMode);
	Patches.Add(NewPatchChannels);

	UpdateChannelProxy();
}

void UMovieSceneDMXLibrarySection::RemoveFixturePatch(UDMXEntityFixturePatch* InPatch)
{
	int32 PatchIndex = Patches.IndexOfByPredicate([InPatch](const FDMXFixturePatchChannels& PatchChannels)
		{
			return PatchChannels.Reference.GetFixturePatch() == InPatch;
		});

	if (PatchIndex != INDEX_NONE)
	{
		Patches.RemoveAt(PatchIndex);

		UpdateChannelProxy();
	}
}

void UMovieSceneDMXLibrarySection::RemoveFixturePatch(const FName& InPatchName)
{
	// Search for the Fixture Patch
	const FString&& TargetPatchName = InPatchName.ToString();

	for (const FDMXFixturePatchChannels& PatchChannels : Patches)
	{
		if (UDMXEntityFixturePatch* Patch = PatchChannels.Reference.GetFixturePatch())
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
	int32 PatchIndex = Patches.IndexOfByPredicate([InPatch](const FDMXFixturePatchChannels& PatchChannels)
		{
			return PatchChannels.Reference.GetFixturePatch() == InPatch;
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

	// Find the PatchChannels object that represents the passed in Patch
	for (FDMXFixturePatchChannels& PatchChannels : Patches)
	{
		UDMXEntityFixturePatch* Patch = PatchChannels.Reference.GetFixturePatch();
		if (Patch == InPatch)
		{
			PatchChannels.ActiveMode = InActiveMode;
			const bool bResetFunctionChannelsValues = true;
			UpdateChannelProxy(bResetFunctionChannelsValues);

			break;
		}
	}
}

/**
 * Utility method to send a single DMX Function's default value when disabling
 * a function channel
 */
void SendDefaultFunctionValueToDMX(const UDMXEntityFixturePatch* InPatch, int32 ModeIndex, int32 FunctionIndex)
{
	// No safety checks are done here because this function is only called locally
	// in this file for functions already verified to exist in the fixture patch
	
	const int32 PatchChannelOffset = InPatch->GetStartingChannel() - 1;

	// Add function default value bytes (channels) to a fragment map
	const FDMXFixtureFunction& DMXFunction = InPatch->ParentFixtureTypeTemplate->Modes[ModeIndex].Functions[FunctionIndex];
	const int32 FunctionChannelStart = DMXFunction.Channel + PatchChannelOffset;
	const int32 FunctionChannelEnd = UDMXEntityFixtureType::GetFunctionLastChannel(DMXFunction) + PatchChannelOffset;

	uint8 ValueBytes[4] = { 0 };
	UDMXEntityFixtureType::FunctionValueToBytes(DMXFunction, DMXFunction.DefaultValue, ValueBytes);

	IDMXFragmentMap FragmentMap;
	int32 ByteIndex = 0;
	for (int32 ChannelIndex = FunctionChannelStart; ChannelIndex <= FunctionChannelEnd && ByteIndex < 4; ++ChannelIndex, ++ByteIndex)
	{
		FragmentMap.Add(ChannelIndex, ValueBytes[ByteIndex]);
	}

	// Send the fragment map through each controller affecting the Fixture Patch
	const TArray<UDMXEntityController*>&& Controllers = InPatch->GetRelevantControllers();
	for (const UDMXEntityController* Controller : Controllers)
	{
		if (Controller == nullptr || !Controller->IsValidLowLevelFast() || !Controller->DeviceProtocol.IsValid())
		{
			continue;
		}

		IDMXProtocolPtr DMXProtocol = Controller->DeviceProtocol;
		DMXProtocol->SendDMXFragment(InPatch->UniverseID + Controller->RemoteOffset, FragmentMap);
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

	// Find the PatchChannels object that represents the passed in Patch
	for (FDMXFixturePatchChannels& PatchChannels : Patches)
	{
		UDMXEntityFixturePatch* Patch = PatchChannels.Reference.GetFixturePatch();
		if (Patch == InPatch)
		{
			// Make sure the Channel Index is valid
			const int32 NumFunctionsInMode = FixtureType->Modes[PatchChannels.ActiveMode].Functions.Num();
			if (InChannelIndex < 0 || InChannelIndex >= NumFunctionsInMode)
			{
				return;
			}

			// The channel index is valid, but the PatchChannels could be out of sync in channels count
			if (PatchChannels.FunctionChannels.Num() != NumFunctionsInMode)
			{
				PatchChannels.UpdateNumberOfChannels();
			}

			FDMXFixtureFunctionChannel& FunctionChannel = PatchChannels.FunctionChannels[InChannelIndex];
			FunctionChannel.bEnabled = !FunctionChannel.bEnabled;

			// If disabling the function, send its default value to the fixture to undo
			// non-default animated value changes
			if (!FunctionChannel.bEnabled)
			{
				SendDefaultFunctionValueToDMX(InPatch, PatchChannels.ActiveMode, InChannelIndex);
			}

			UpdateChannelProxy();

			break;
		}
	}
}

void UMovieSceneDMXLibrarySection::ToggleFixturePatchChannel(const FName& InPatchName, const FName& InChannelName)
{
	// Search for the Fixture Patch
	const FString&& TargetPatchName = InPatchName.ToString();

	for (FDMXFixturePatchChannels& PatchChannels : Patches)
	{
		if (UDMXEntityFixturePatch* Patch = PatchChannels.Reference.GetFixturePatch())
		{
			if (Patch->GetDisplayName().Equals(TargetPatchName))
			{
				const UDMXEntityFixtureType* FixtureType = Patch->ParentFixtureTypeTemplate;
				if (FixtureType == nullptr || !FixtureType->IsValidLowLevelFast())
				{
					return;
				}

				if (FixtureType->Modes.Num() <= PatchChannels.ActiveMode)
				{
					return;
				}

				// Search for the Function index
				const FString&& TargetFunctionName = InChannelName.ToString();
				int32 FunctionIndex = 0;
				for (const FDMXFixtureFunction& Function : FixtureType->Modes[PatchChannels.ActiveMode].Functions)
				{
					if (Function.FunctionName.Equals(TargetFunctionName))
					{
						// PatchChannels could be out of sync in channels count
						if (PatchChannels.FunctionChannels.Num() <= FunctionIndex)
						{
							PatchChannels.UpdateNumberOfChannels();
						}

						FDMXFixtureFunctionChannel& FunctionChannel = PatchChannels.FunctionChannels[FunctionIndex];
						FunctionChannel.bEnabled = !FunctionChannel.bEnabled;

						// If disabling the function, send its default value to the fixture to undo
						// non-default animated value changes
						if (!FunctionChannel.bEnabled)
						{
							SendDefaultFunctionValueToDMX(Patch, PatchChannels.ActiveMode, FunctionIndex);
						}
						
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

	// Find the PatchChannels object that represents the passed in Patch
	for (const FDMXFixturePatchChannels& PatchChannels : Patches)
	{
		UDMXEntityFixturePatch* Patch = PatchChannels.Reference.GetFixturePatch();
		if (Patch == InPatch)
		{
			// Make sure the Channel Index is valid
			const int32 NumFunctionsInMode = FixtureType->Modes[PatchChannels.ActiveMode].Functions.Num();
			if (InChannelIndex < 0 || InChannelIndex >= NumFunctionsInMode)
			{
				return false;
			}

			// The channel index is valid, but the PatchChannels could be out of sync in channels count
			if (PatchChannels.FunctionChannels.Num() != NumFunctionsInMode)
			{
				// In this case, the channel can't be seen in the track, so consider it as disabled
				return false;
			}

			return PatchChannels.FunctionChannels[InChannelIndex].bEnabled;
		}
	}

	return false;
}

TArray<UDMXEntityFixturePatch*> UMovieSceneDMXLibrarySection::GetFixturePatches() const
{
	TArray<UDMXEntityFixturePatch*> Result;
	Result.Reserve(Patches.Num());

	for (const FDMXFixturePatchChannels& PatchRef : Patches)
	{
		// Add only valid patches
		if (UDMXEntityFixturePatch* Patch = PatchRef.Reference.GetFixturePatch())
		{
			if (Patch->IsValidLowLevelFast())
			{
				Result.Add(Patch);
			}
		}
	}

	return Result;
}

void UMovieSceneDMXLibrarySection::ForEachPatchFunctionChannels(TFunctionRef<void(UDMXEntityFixturePatch*, TArray<FDMXFixtureFunctionChannel>&)> InPredicate)
{
	for (FDMXFixturePatchChannels& PatchChannels : Patches)
	{
		if (UDMXEntityFixturePatch* Patch = PatchChannels.Reference.GetFixturePatch())
		{
			int32 FunctionIndex = 0;
			InPredicate(Patch, PatchChannels.FunctionChannels);
		}
	}
}

void UMovieSceneDMXLibrarySection::UpdateChannelProxy(bool bResetDefaultChannelValues /*= false*/)
{
	FMovieSceneChannelProxyData Channels;
	TArray<int32> InvalidPatchChannelsIndexes;

	int32 PatchChannelsIndex = 0; // Safer because the ranged for ensures the array length isn't changed
	for (FDMXFixturePatchChannels& PatchChannels : Patches)
	{
		PatchChannels.UpdateNumberOfChannels(bResetDefaultChannelValues);

		const UDMXEntityFixturePatch* Patch = PatchChannels.Reference.GetFixturePatch();
		if (Patch == nullptr || !Patch->IsValidLowLevelFast())
		{
			UE_LOG(MovieSceneDMXLibrarySectionLog, Warning, TEXT("%S: Removing a null Patch"), __FUNCTION__);
			InvalidPatchChannelsIndexes.Add(PatchChannelsIndex);
			continue;
		}

		// If the Patch is null, invalid, doesn't have Modes or the selected mode doesn't have any functions,
		// FunctionChannels will be empty
		if (PatchChannels.FunctionChannels.Num() == 0)
		{
			// With no Function Channels to be displayed, the Patch group won't be displayed.
			// This will give users the impression that the Patch isn't added, but it is,
			// which prevents the user from adding it again. So, to mitigate that, we remove
			// the Patch from the track section.
			UE_LOG(MovieSceneDMXLibrarySectionLog, Warning, TEXT("%S: Removing empty Patch %s"), __FUNCTION__, *Patch->GetDisplayName());
			InvalidPatchChannelsIndexes.Add(PatchChannelsIndex);
			continue;
		}

		const TArray<FDMXFixtureFunction>& Functions = Patch->ParentFixtureTypeTemplate->Modes[PatchChannels.ActiveMode].Functions;
		TArray<FDMXFixtureFunctionChannel>& PatchFloatChannels = PatchChannels.FunctionChannels;

		// To use as a group for its channels
		const FString&& PatchName = Patch->GetDisplayName();
		const FText&& PatchNameText = FText::FromString(PatchName);

		// Add a channel proxy entry for each Function channel
		// We use the length of PatchFloatChannels because it's possible some Fixture Functions
		// are outside the valid range for the Mode's channels or the Universe's channels.
		// And that's already been accounted for when generating the Function channels
		int32 SortOrder = 0;
		for (int32 FunctionIndex = 0; FunctionIndex < PatchFloatChannels.Num(); ++FunctionIndex)
		{
			if (!PatchFloatChannels[FunctionIndex].bEnabled)
			{
				continue;
			}

#if WITH_EDITOR
			const FDMXFixtureFunction& Function = Functions[FunctionIndex];

			const FText ChannelDisplayName(FText::FromName(Function.Attribute.GetName()));
			const FName ChannelPropertyName(*FString::Printf(TEXT("%s.%s"), *PatchName, *Function.Attribute.GetName().ToString()));

			FMovieSceneChannelMetaData MetaData;
			MetaData.SetIdentifiers(ChannelPropertyName, ChannelDisplayName, PatchNameText);
			MetaData.SortOrder = SortOrder++;
			MetaData.bCanCollapseToTrack = false;

			Channels.Add(PatchFloatChannels[FunctionIndex].Channel, MetaData, TMovieSceneExternalValue<float>());
#else
			Channels.Add(PatchFloatChannels[FunctionIndex].Channel);
#endif // WITH_EDITOR
		}

		++PatchChannelsIndex;
	}

	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MoveTemp(Channels));

	// Remove Patches that can't be seen by users because they don't have any functions
	// or represent an invalid Patch object
	for (const int32& InvalidPatchChannelsIndex : InvalidPatchChannelsIndexes)
	{
		Patches.RemoveAt(InvalidPatchChannelsIndex);
	}
}
