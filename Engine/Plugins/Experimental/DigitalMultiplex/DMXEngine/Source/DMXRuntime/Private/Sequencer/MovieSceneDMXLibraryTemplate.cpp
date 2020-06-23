// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneDMXLibraryTemplate.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolCommon.h"
#include "Interfaces/IDMXProtocol.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityController.h"
#include "MovieSceneExecutionToken.h"

DECLARE_LOG_CATEGORY_CLASS(MovieSceneDMXLibraryTemplateLog, Log, All);

struct FPreAnimatedDMXLibraryToken : IMovieScenePreAnimatedToken
{
	FGuid EntityID;

	FPreAnimatedDMXLibraryToken(const FGuid& InEntityID)
		: EntityID(InEntityID)
	{}

	// Restore the animated Fixture Patch to the default values of the Functions
	// from its default Active Mode
	virtual void RestoreState(UObject& Object, IMovieScenePlayer& Player) override
	{
		UDMXLibrary* DMXLibrary = CastChecked<UDMXLibrary>(&Object);

		// Recover the Patch from its GUID
		UDMXEntityFixturePatch* Patch = Cast<UDMXEntityFixturePatch>(DMXLibrary->FindEntity(EntityID));
		if (Patch == nullptr)
		{
			return;
		}

		// Get a valid parent Fixture Type
		UDMXEntityFixtureType* FixtureType = Patch->ParentFixtureTypeTemplate;
		if (FixtureType == nullptr)
		{
			return;
		}

		// Check if the current active mode is accessible
		if (FixtureType->Modes.Num() <= Patch->ActiveMode)
		{
			return;
		}

		// Get the Controllers affecting this Fixture Patch's universe
		const TArray<UDMXEntityController*>&& Controllers = Patch->GetRelevantControllers();
		if (Controllers.Num() == 0)
		{
			// No data will be sent from this Patch because it's unassigned
			return;
		}

		const FDMXFixtureMode& Mode = FixtureType->Modes[Patch->ActiveMode];
		const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;

		// Cache the FragmentMap to send through the controllers
		IDMXFragmentMap FragmentMap;
		FragmentMap.Reserve(Functions.Num());

		const int32 PatchChannelOffset = Patch->GetStartingChannel() - 1;
		for (const FDMXFixtureFunction& Function : Functions)
		{
			// Is this function in the Mode and Universe's ranges?
			if (!UDMXEntityFixtureType::IsFunctionInModeRange(Function, Mode, PatchChannelOffset))
			{
				// Functions are ordered by channel. If this one is over the
				// valid range, the next ones will be as well.
				break;
			}

			const int32 FunctionStartChannel = Function.Channel + PatchChannelOffset;
			
			// Get each individual channel value from the Function
			uint8 FunctionValue[4] = { 0 };
			UDMXEntityFixtureType::FunctionValueToBytes(Function, Function.DefaultValue, FunctionValue);

			// Write each channel (byte) to the fragment map
			const int32 FunctionEndChannel = UDMXEntityFixtureType::GetFunctionLastChannel(Function) + PatchChannelOffset;

			int32 ByteIndex = 0;
			int32 ChannelIndex = FunctionStartChannel;
			for ( ; ChannelIndex <= FunctionEndChannel && ByteIndex < 4; ++ByteIndex, ++ChannelIndex)
			{
				FragmentMap.Add(ChannelIndex, FunctionValue[ByteIndex]);
			}
		}

		// Send the fragment map through each Controller that affects this Patch
		for (const UDMXEntityController* Controller : Controllers)
		{
			if (Controller != nullptr && Controller->DeviceProtocol.IsValid())
			{
				IDMXProtocolPtr Protocol = Controller->DeviceProtocol;
				Protocol->SendDMXFragment(Patch->UniverseID + Controller->RemoteOffset, FragmentMap);
			}
		}
	}
};

struct FPreAnimatedDMXLibraryTokenProducer : IMovieScenePreAnimatedTokenProducer
{
	FGuid EntityID;

	FPreAnimatedDMXLibraryTokenProducer(const FGuid& InEntityID)
		: EntityID(InEntityID)
	{}

	virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const override
	{
		UDMXLibrary* DMXLibrary = CastChecked<UDMXLibrary>(&Object);
		return FPreAnimatedDMXLibraryToken(EntityID);
	}
};

struct FDMXLibraryExecutionToken : IMovieSceneExecutionToken
{
	FDMXLibraryExecutionToken(const UMovieSceneDMXLibrarySection* InSection) : Section(InSection) {}
	
	FDMXLibraryExecutionToken(FDMXLibraryExecutionToken&&) = default;
	FDMXLibraryExecutionToken& operator=(FDMXLibraryExecutionToken&&) = default;

	// Non-copyable
	FDMXLibraryExecutionToken(const FDMXLibraryExecutionToken&) = delete;
	FDMXLibraryExecutionToken& operator=(const FDMXLibraryExecutionToken&) = delete;

	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
	{
		if (Section == nullptr || !Section->IsValidLowLevelFast())
		{
			return;
		}

		//UE_LOG(MovieSceneDMXLibraryTemplateLog, Warning, TEXT("Execute: %d"), __threadid());
		
		// Keeps record of the animated Patches GUIDs
		static TMovieSceneAnimTypeIDContainer<FGuid> AnimTypeIDsByGUID;

		// Keeps all values from Fixture Functions that will be sent using the controllers.
		// A Protocol points to Universe IDs. Each Universe ID points to a FragmentMap.
		TMap<IDMXProtocolPtr, TMap<uint16, IDMXFragmentMap>> DMXFragmentMaps;

		// Store channels evaluated values
		TArray<float> ChannelsValues;
		const FFrameTime Time = Context.GetTime();

		// Add the Patches' function channels values to the Fragment Maps so that we can send
		// them later all at once for each affected universe on each protocol
		for (const FDMXFixturePatchChannels& PatchChannels : Section->GetFixturePatchChannels())
		{
			UDMXEntityFixturePatch* Patch = PatchChannels.Reference.GetFixturePatch();
			if (Patch == nullptr || !Patch->IsValidLowLevelFast())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: A Fixture Patch is null."), __FUNCTION__);
				continue;
			}

			if (!PatchChannels.FunctionChannels.Num())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: Patch %s has no function channels."), __FUNCTION__, *Patch->GetDisplayName());
				continue;
			}

			// Verify the Patch still have a valid Parent Template
			const UDMXEntityFixtureType* FixtureType = Patch->ParentFixtureTypeTemplate;
			if (FixtureType == nullptr || !FixtureType->IsValidLowLevelFast())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: Patch %s has invalid Fixture Type template."), __FUNCTION__, *Patch->GetDisplayName());
				continue;
			}

			// Verify the active mode from the Patch Channels still exists in the Fixture Type template 
			if (PatchChannels.ActiveMode >= FixtureType->Modes.Num())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: Patch track %s ActiveMode is invalid."), __FUNCTION__, *Patch->GetDisplayName());
				continue;
			}
			const TArray<FDMXFixtureFunction>& Functions = FixtureType->Modes[PatchChannels.ActiveMode].Functions;

			// Controllers to send data from this Patch
			TArray<UDMXEntityController*>&& Controllers = Patch->GetRelevantControllers();
			if (!Controllers.Num())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Warning, TEXT("%S: Patch %s isn't affectected by any Controllers."), __FUNCTION__, *Patch->GetDisplayName());
				continue;
			}

			// By this point, we know data is gonna be sent. So register the Patch's PreAnimated state
			if (UDMXLibrary* Library = Patch->GetParentLibrary())
			{
				const FGuid& PatchID = Patch->GetID();
				Player.SavePreAnimatedState(*Patch->GetParentLibrary(), AnimTypeIDsByGUID.GetAnimTypeID(PatchID), FPreAnimatedDMXLibraryTokenProducer(PatchID));
			}
			else // This should be impossible!!
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: Patch %s parent DMX Library is invalid."), __FUNCTION__, *Patch->GetDisplayName());
				checkNoEntry();
			}

			// Cache evaluated values from the Function channels' curves
			ChannelsValues.SetNum(PatchChannels.FunctionChannels.Num(), false);
			int32 ChannelIndex = 0;
			float ChannelValue = 0.0f;
			for (const FDMXFixtureFunctionChannel& FunctionChannel : PatchChannels.FunctionChannels)
			{
				// Only cache values for enabled channels
				if (FunctionChannel.bEnabled)
				{
					FunctionChannel.Channel.Evaluate(Time, ChannelValue);
					ChannelsValues[ChannelIndex] = ChannelValue;
				}
				else
				{
					ChannelsValues[ChannelIndex] = 0.0f;
				}
				++ChannelIndex;
			}

			// Channel offset for the Patch
			const int32 PatchChannelOffset = Patch->GetStartingChannel() - 1;
			uint8 FunctionChannelsValues[4] = { 0 };

			// For each Function Channel, add its value to each relevant Controller's Universe
			for (int32 FunctionIndex = 0; FunctionIndex < PatchChannels.FunctionChannels.Num(); ++FunctionIndex)
			{
				// Only send values for enabled channels
				if (!PatchChannels.FunctionChannels[FunctionIndex].bEnabled)
				{
					continue;
				}

				// Make sure a function at this index still exists
				if (FunctionIndex >= Functions.Num())
				{
					UE_LOG(MovieSceneDMXLibraryTemplateLog, Warning, TEXT("%S: Function Channel %d from %s doesn't have a counterpart Fixture Function."), __FUNCTION__, FunctionIndex, *Patch->GetDisplayName());
					break;
				}

				const FDMXFixtureFunction& Function = Functions[FunctionIndex];
				const int32 FunctionChannel = Function.Channel + PatchChannelOffset;
				const int32 FunctionLastChannel = UDMXEntityFixtureType::GetFunctionLastChannel(Function) + PatchChannelOffset;

				const uint32 FunctionValue = FMath::RoundToInt(ChannelsValues[FunctionIndex]);
				UDMXEntityFixtureType::FunctionValueToBytes(Function, FunctionValue, FunctionChannelsValues);

				for (const UDMXEntityController* Controller : Controllers)
				{
					IDMXProtocolPtr ControllerProtocol = Controller->DeviceProtocol;
					if (!ControllerProtocol.IsValid())
					{
						UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: Protocol is invalid for %s."), __FUNCTION__, *Controller->GetDisplayName());
						continue;
					}

					TMap<uint16, IDMXFragmentMap>* ProtocolPtr = DMXFragmentMaps.Find(ControllerProtocol);
					if (ProtocolPtr == nullptr)
					{
						ProtocolPtr = &DMXFragmentMaps.Add(ControllerProtocol);
					}

					const uint16 UniverseID = Patch->UniverseID + Controller->RemoteOffset;
					IDMXFragmentMap* UniverseFragmentsPtr = ProtocolPtr->Find(UniverseID);
					if (UniverseFragmentsPtr == nullptr)
					{
						UniverseFragmentsPtr = &ProtocolPtr->Add(UniverseID);
					}

					uint8 ByteIndex = 0;
					for (ChannelIndex = FunctionChannel; ChannelIndex <= FunctionLastChannel; ++ChannelIndex)
					{
						UniverseFragmentsPtr->Add(ChannelIndex, FunctionChannelsValues[ByteIndex++]);
					}
				}
			}
		} // Finished adding values to Fragment Maps

		// Send the Universes data from the accumulated DMXFragmentMaps
		for (const TPair<IDMXProtocolPtr, TMap<uint16, IDMXFragmentMap>>& Protocol_Universes : DMXFragmentMaps)
		{
			for (const TPair<uint16, IDMXFragmentMap>& Universe_FragmentMaps : Protocol_Universes.Value)
			{
				Protocol_Universes.Key->SendDMXFragment(Universe_FragmentMaps.Key, Universe_FragmentMaps.Value);
			}
		}
	}

	const UMovieSceneDMXLibrarySection* Section;
};

FMovieSceneDMXLibraryTemplate::FMovieSceneDMXLibraryTemplate(const UMovieSceneDMXLibrarySection& InSection)
	: Section(&InSection)
{
}

void FMovieSceneDMXLibraryTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	// Don't evaluate while recording to prevent conflicts between
	// sent DMX data and incoming recorded data
	if (Section->GetIsRecording())
	{
		return;
	}

	FDMXLibraryExecutionToken ExecutionToken(Section);
	ExecutionTokens.Add(MoveTemp(ExecutionToken));
}
