// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneDMXLibraryTemplate.h"

#include "DMXRuntimeLog.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolCommon.h"
#include "DMXSubsystem.h"
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

	// Sends the default value for the patch, to reset changes in the buffers that happened during playback
	virtual void RestoreState(UObject& Object, IMovieScenePlayer& Player) override
	{
		UDMXLibrary* DMXLibrary = CastChecked<UDMXLibrary>(&Object);

		// Recover the Patch from its GUID
		UDMXEntityFixturePatch* FixturePatch = Cast<UDMXEntityFixturePatch>(DMXLibrary->FindEntity(EntityID));
		if (FixturePatch == nullptr)
		{
			return;
		}

		// Get a valid parent Fixture Type
		UDMXEntityFixtureType* FixtureType = FixturePatch->ParentFixtureTypeTemplate;
		if (FixtureType == nullptr)
		{
			return;
		}

		// Check if the current active mode is accessible
		if (FixtureType->Modes.Num() <= FixturePatch->ActiveMode)
		{
			return;
		}

		// Get the Controllers affecting this Fixture Patch's universe
		const TArray<UDMXEntityController*>&& Controllers = FixturePatch->GetRelevantControllers();
		if (Controllers.Num() == 0)
		{
			// No data will be sent from this Patch because it's unassigned
			return;
		}

		const FDMXFixtureMode& Mode = FixtureType->Modes[FixturePatch->ActiveMode];
		const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;

		// Cache the FragmentMap to send through the controllers
		IDMXFragmentMap FragmentMap;
		FragmentMap.Reserve(Functions.Num());

		const int32 PatchChannelOffset = FixturePatch->GetStartingChannel() - 1;

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
			for (; ChannelIndex <= FunctionEndChannel && ByteIndex < 4; ++ByteIndex, ++ChannelIndex)
			{
				FragmentMap.Add(ChannelIndex, FunctionValue[ByteIndex]);
			}
		}

		const FDMXFixtureMatrix& FixtureMatrix = Mode.FixtureMatrixConfig;
		if (UDMXEntityFixtureType::IsFixtureMatrixInModeRange(FixtureMatrix, Mode, PatchChannelOffset))
		{
			UDMXSubsystem* DMXSubsystem = UDMXSubsystem::GetDMXSubsystem_Pure();
			check(DMXSubsystem);

			TArray<FDMXCell> Cells;
			DMXSubsystem->GetAllMatrixCells(FixturePatch, Cells);

			for (const FDMXCell& Cell : Cells)
			{
				TMap<FDMXAttributeName, int32> AttributeNameChannelMap;
				DMXSubsystem->GetMatrixCellChannelsAbsolute(FixturePatch, Cell.Coordinate, AttributeNameChannelMap);
				
				bool bLoggedMissingAttribute = false;
				for (const TPair<FDMXAttributeName, int32>& AttributeNameChannelKvp : AttributeNameChannelMap)
				{
					const FDMXFixtureCellAttribute* CellAttributePtr = FixtureMatrix.CellAttributes.FindByPredicate([&AttributeNameChannelKvp](const FDMXFixtureCellAttribute& TestedCellAttribute){
							return TestedCellAttribute.Attribute == AttributeNameChannelKvp.Key;
						});

					if (!CellAttributePtr)
					{
						if (!bLoggedMissingAttribute)
						{
							UE_LOG(LogDMXRuntime, Warning, TEXT("%S: Function with attribute %s from %s doesn't have a counterpart Fixture Function."), __FUNCTION__, *AttributeNameChannelKvp.Key.GetName().ToString(), *FixturePatch->GetDisplayName());
							UE_LOG(LogDMXRuntime, Warning, TEXT("%S: Further attributes may be missing. Warnings ommited to avoid overflowing the log."), __FUNCTION__);
							bLoggedMissingAttribute = true;
						}

						continue;
					}

					const FDMXFixtureCellAttribute& CellAttribute = *CellAttributePtr;
					int32 FirstChannelAddress = AttributeNameChannelKvp.Value;
					int32 LastChannelAddress = AttributeNameChannelKvp.Value + UDMXEntityFixtureType::NumChannelsToOccupy(CellAttribute.DataType) - 1;

					int32 DefaultValue = CellAttribute.DefaultValue;

					uint8 ValueBytes[4] = { 0 };
					UDMXEntityFixtureType::IntToBytes(CellAttribute.DataType, CellAttribute.bUseLSBMode, DefaultValue, ValueBytes);

					int32 ByteIndex = 0;
					for (int32 ChannelIndex = FirstChannelAddress; ChannelIndex <= LastChannelAddress && ByteIndex < 4; ++ChannelIndex, ++ByteIndex)
					{
						FragmentMap.Add(ChannelIndex, ValueBytes[ByteIndex]);
					}
				}
			}
		}	

		// Send the fragment map through each Controller that affects this Patch
		for (const UDMXEntityController* Controller : Controllers)
		{
			if (Controller == nullptr || !Controller->IsValidLowLevelFast() || !Controller->DeviceProtocol.IsValid())
			{
				continue;
			}

			IDMXProtocolPtr Protocol = Controller->DeviceProtocol;

			// If sent DMX will not be received via network, input it directly
			bool bCanLoopback = Protocol->IsSendDMXEnabled() && Protocol->IsReceiveDMXEnabled();

			// If sent DMX will not be received via network, input it directly
			if (!bCanLoopback)
			{
				Protocol->InputDMXFragment(FixturePatch->UniverseID + Controller->RemoteOffset, FragmentMap);
			}

			Protocol->SendDMXFragment(FixturePatch->UniverseID + Controller->RemoteOffset, FragmentMap);
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
		
		// Keeps record of the animated Patches GUIDs
		static TMovieSceneAnimTypeIDContainer<FGuid> AnimTypeIDsByGUID;

		// Store channels evaluated values
		TArray<float> ChannelsValues;
		const FFrameTime Time = Context.GetTime();

		// Reset previous fragments
		DMXFragmentMaps.Reset();

		// Add the Patches' function channels values to the Fragment Maps so that we can send
		// them later all at once for each affected universe on each protocol
		for (const FDMXFixturePatchChannel& PatchChannel : Section->GetFixturePatchChannels())
		{
			UDMXEntityFixturePatch* FixturePatch = PatchChannel.Reference.GetFixturePatch();
			if (FixturePatch == nullptr || !FixturePatch->IsValidLowLevelFast())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: A Fixture Patch is null."), __FUNCTION__);
				continue;
			}

			if (!PatchChannel.FunctionChannels.Num())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: Patch %s has no function channels."), __FUNCTION__, *FixturePatch->GetDisplayName());
				continue;
			}

			// Verify the Patch still have a valid Parent Template
			const UDMXEntityFixtureType* FixtureType = FixturePatch->ParentFixtureTypeTemplate;
			if (FixtureType == nullptr || !FixtureType->IsValidLowLevelFast())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: Patch %s has invalid Fixture Type template."), __FUNCTION__, *FixturePatch->GetDisplayName());
				continue;
			}

			// Verify the active mode from the Patch Channels still exists in the Fixture Type template 
			if (PatchChannel.ActiveMode >= FixtureType->Modes.Num())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: Patch track %s ActiveMode is invalid."), __FUNCTION__, *FixturePatch->GetDisplayName());
				continue;
			}

			// By this point, we know data is gonna be sent. So register the Patch's PreAnimated state
			if (UDMXLibrary* Library = FixturePatch->GetParentLibrary())
			{
				const FGuid& PatchID = FixturePatch->GetID();
				Player.SavePreAnimatedState(*FixturePatch->GetParentLibrary(), AnimTypeIDsByGUID.GetAnimTypeID(PatchID), FPreAnimatedDMXLibraryTokenProducer(PatchID));
			}
			else // This should be impossible!!
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: Patch %s parent DMX Library is invalid."), __FUNCTION__, *FixturePatch->GetDisplayName());
				checkNoEntry();
			}

			UDMXSubsystem* DMXSubsystem = UDMXSubsystem::GetDMXSubsystem_Pure();
			check(DMXSubsystem);

			const FDMXFixtureMode& Mode = FixtureType->Modes[PatchChannel.ActiveMode];
			const FDMXFixtureMatrix& MatrixConfig = Mode.FixtureMatrixConfig;
			const TArray<FDMXFixtureCellAttribute>& CellAttributes = MatrixConfig.CellAttributes;

			// Channel offset for the Patch
			const int32 PatchChannelOffset = FixturePatch->GetStartingChannel() - 1;
			uint8 FunctionChannelsValues[4] = { 0 };

			// For each Function Channel, add its value to the FragmentMap
			bool bLoggedMissingFunction = false;
			for (const FDMXFixtureFunctionChannel& FunctionChannel : PatchChannel.FunctionChannels)
			{
				if (!FunctionChannel.bEnabled)
				{
					continue;
				}

				if (FunctionChannel.IsCellFunction())
				{
					TMap<FDMXAttributeName, int32> AttributeNameChannelMap;
					DMXSubsystem->GetMatrixCellChannelsAbsolute(FixturePatch, FunctionChannel.CellCoordinate, AttributeNameChannelMap);

					const FDMXFixtureCellAttribute* CellAttributePtr = CellAttributes.FindByPredicate([&FunctionChannel](const FDMXFixtureCellAttribute& CellAttribute) {
						return CellAttribute.Attribute == FunctionChannel.AttributeName;
						});

					bool bMissingFunction = !AttributeNameChannelMap.Contains(FunctionChannel.AttributeName) || !CellAttributePtr;
					if (!CellAttributePtr)
					{
						if (bMissingFunction && !bLoggedMissingFunction)
						{
							UE_LOG(MovieSceneDMXLibraryTemplateLog, Warning, TEXT("%S: Function with attribute %s from %s doesn't have a counterpart Fixture Function."), __FUNCTION__, *FunctionChannel.AttributeName.ToString(), *FixturePatch->GetDisplayName());
							UE_LOG(MovieSceneDMXLibraryTemplateLog, Warning, TEXT("%S: Further attributes may be missing. Warnings ommited to avoid overflowing the log."), __FUNCTION__);
							bLoggedMissingFunction = true;
						}

						continue;
					}

					const FDMXFixtureCellAttribute& CellAttribute = *CellAttributePtr;
					int32 FirstChannelAddress = AttributeNameChannelMap[FunctionChannel.AttributeName];
					int32 LastChannelAddress = AttributeNameChannelMap[FunctionChannel.AttributeName] + UDMXEntityFixtureType::NumChannelsToOccupy(CellAttribute.DataType) - 1;

					float ChannelValue = 0.0f;
					if (FunctionChannel.Channel.Evaluate(Time, ChannelValue))
					{
						// Round to int so if the user draws into the tracks, values are assigned to int accurately
						const uint32 FunctionValue = FMath::RoundToInt(ChannelValue);

						// Round to int so if the user draws into the tracks, values are assigned to int accurately
						TArray<uint8> ByteArr;
						DMXSubsystem->IntValueToBytes(FunctionValue, CellAttribute.DataType, ByteArr, CellAttribute.bUseLSBMode);

						WriteDMXFragment(FixturePatch, FirstChannelAddress, ByteArr);
					}					
				}
				else
				{
					const TArray<FDMXFixtureFunction>& Functions = FixtureType->Modes[PatchChannel.ActiveMode].Functions;

					const FDMXFixtureFunction* FunctionPtr = Functions.FindByPredicate([&FunctionChannel](const FDMXFixtureFunction& TestedFunction) {
						return TestedFunction.Attribute == FunctionChannel.AttributeName;
						});

					if (!FunctionPtr)
					{
						UE_LOG(MovieSceneDMXLibraryTemplateLog, Warning, TEXT("%S: Function with attribute %s from %s doesn't have a counterpart Fixture Function."), __FUNCTION__, *FunctionChannel.AttributeName.ToString(), *FixturePatch->GetDisplayName());
						break;
					}

					const FDMXFixtureFunction& Function = *FunctionPtr;
					int32 FirstChannelAddress = Function.Channel + PatchChannelOffset;
					int32 LastChannelAddress = UDMXEntityFixtureType::GetFunctionLastChannel(Function) + PatchChannelOffset;

					float ChannelValue = 0.0f;
					if (FunctionChannel.Channel.Evaluate(Time, ChannelValue))
					{
						// Round to int so if the user draws into the tracks, values are assigned to int accurately
						const uint32 FunctionValue = FMath::RoundToInt(ChannelValue);

						// Round to int so if the user draws into the tracks, values are assigned to int accurately
						TArray<uint8> ByteArr;
						DMXSubsystem->IntValueToBytes(FunctionValue, Function.DataType, ByteArr, Function.bUseLSBMode);

						WriteDMXFragment(FixturePatch, FirstChannelAddress, ByteArr);
					}
				}
			}
		} 

		// Send the Universes data from the accumulated DMXFragmentMaps
		for (const TPair<IDMXProtocolPtr, TMap<uint16, IDMXFragmentMap>>& Protocol_Universes : DMXFragmentMaps)
		{
			for (const TPair<uint16, IDMXFragmentMap>& Universe_FragmentMaps : Protocol_Universes.Value)
			{
				IDMXProtocolPtr Protocol = Protocol_Universes.Key;
				bool bCanLoopback = Protocol->IsReceiveDMXEnabled() && Protocol->IsSendDMXEnabled();

				// If sent DMX will not be looped back via network, input it directly
				if (!bCanLoopback)
				{
					Protocol->InputDMXFragment(Universe_FragmentMaps.Key, Universe_FragmentMaps.Value);
				}

				Protocol->SendDMXFragment(Universe_FragmentMaps.Key, Universe_FragmentMaps.Value);
			}
		}
	}
	
	/** Helper that writes the Bytes array to DMXFragmentMaps member */
	void WriteDMXFragment(UDMXEntityFixturePatch* Patch, int32 FirstChannelAddress, const TArray<uint8>& Bytes)
	{
		check(Bytes.Num() < 5);

		// Controllers to send data from this Patch
		TArray<UDMXEntityController*>&& Controllers = Patch->GetRelevantControllers();
		if (!Controllers.Num())
		{
			UE_LOG(MovieSceneDMXLibraryTemplateLog, Warning, TEXT("%S: Patch %s isn't affectected by any Controllers."), __FUNCTION__, *Patch->GetDisplayName());
		}

		for (const UDMXEntityController* Controller : Controllers)
		{
			IDMXProtocolPtr ControllerProtocol = Controller->DeviceProtocol;
			if (!ControllerProtocol.IsValid())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: Protocol is invalid for %s."), __FUNCTION__, *Controller->GetDisplayName());
				continue;
			}

			TMap<uint16, IDMXFragmentMap>& UniverseFragmentMapKvp = DMXFragmentMaps.FindOrAdd(ControllerProtocol);

			const uint16 UniverseID = Patch->UniverseID + Controller->RemoteOffset;
			IDMXFragmentMap& FragmentMap = UniverseFragmentMapKvp.FindOrAdd(UniverseID);

			for (int32 ByteIdx = 0; ByteIdx < Bytes.Num(); ByteIdx++)
			{
				uint8& Value = FragmentMap.FindOrAdd(FirstChannelAddress + ByteIdx);
				Value = Bytes[ByteIdx];
			}
		}
	}

	// Keeps all values from Fixture Functions that will be sent using the controllers.
	// A Protocol points to Universe IDs. Each Universe ID points to a FragmentMap.
	TMap<IDMXProtocolPtr, TMap<uint16 /** Universe */, IDMXFragmentMap /** ChannelValueKvp */>> DMXFragmentMaps;

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
