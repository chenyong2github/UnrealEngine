// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneDMXLibraryTemplate.h"

#include "DMXRuntimeLog.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolCommon.h"
#include "DMXStats.h"
#include "DMXSubsystem.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXOutputPort.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"

#include "MovieSceneExecutionToken.h"


DECLARE_LOG_CATEGORY_CLASS(MovieSceneDMXLibraryTemplateLog, Log, All);

DECLARE_CYCLE_STAT(TEXT("Sequencer create execution token"), STAT_DMXSequencerCreateExecutionTokens, STATGROUP_DMX);
DECLARE_CYCLE_STAT(TEXT("Sequencer execute execution token"), STAT_DMXSequencerExecuteExecutionToken, STATGROUP_DMX);

namespace
{
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

			const FDMXFixtureMode& Mode = FixtureType->Modes[FixturePatch->ActiveMode];
			const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;

			// Cache the data to send through the ports
			TMap<int32, uint8> ChannelToValueMap;
			ChannelToValueMap.Reserve(Functions.Num());

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


				// Get each individual channel value from the Function
				TArray<uint8, TFixedAllocator<4>> FunctionValues;
				UDMXEntityFixtureType::FunctionValueToBytes(Function, Function.DefaultValue, FunctionValues.GetData());

				// Write each channel (byte) to the channel to value map
				const int32 FunctionStartChannel = Function.Channel + PatchChannelOffset;
				const int32 FunctionEndChannel = UDMXEntityFixtureType::GetFunctionLastChannel(Function) + PatchChannelOffset;

				for (int32 Channel = FunctionStartChannel; Channel <= FunctionEndChannel; Channel++)
				{
					int32 ByteIndex = FunctionEndChannel - FunctionStartChannel;
					check(ByteIndex < FunctionValues.Num());

					ChannelToValueMap.Add(Channel, FunctionValues[ByteIndex]);
				}
			}

			if (FixtureType->bFixtureMatrixEnabled)
			{
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
							const FDMXFixtureCellAttribute* CellAttributePtr = FixtureMatrix.CellAttributes.FindByPredicate([&AttributeNameChannelKvp](const FDMXFixtureCellAttribute& TestedCellAttribute) {
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

							TArray<uint8, TFixedAllocator<4>> ValueBytes;
							UDMXEntityFixtureType::IntToBytes(CellAttribute.DataType, CellAttribute.bUseLSBMode, DefaultValue, ValueBytes.GetData());

							int32 ByteIndex = 0;
							for (int32 ChannelIndex = FirstChannelAddress; ChannelIndex <= LastChannelAddress && ByteIndex < 4; ++ChannelIndex, ++ByteIndex)
							{
								ChannelToValueMap.Add(ChannelIndex, ValueBytes[ByteIndex]);
							}
						}
					}
				}
			}

			// TODO
			for (const FDMXOutputPortSharedRef& OutputPort : DMXLibrary->GetOutputPorts())
			{
				OutputPort->SendDMX(FixturePatch->UniverseID, ChannelToValueMap);
			}
		}
	};

	struct FPreAnimatedDMXLibraryTokenProducer
		: IMovieScenePreAnimatedTokenProducer
	{
		const FGuid EntityID;

		FPreAnimatedDMXLibraryTokenProducer(const FGuid& InEntityID)
			: EntityID(InEntityID)
		{}

		virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const override
		{
			UDMXLibrary* DMXLibrary = CastChecked<UDMXLibrary>(&Object);
			return FPreAnimatedDMXLibraryToken(EntityID);
		}
	};
}

/** 
 * Token executed each tick during playback 
 */
struct FDMXLibraryExecutionToken 
	: IMovieSceneExecutionToken
{
	/** Constructor, called at begin of playback */
	FDMXLibraryExecutionToken(const UMovieSceneDMXLibrarySection* InSection)
		: Section(InSection) 
	{}

	FDMXLibraryExecutionToken(FDMXLibraryExecutionToken&&) = default;
	FDMXLibraryExecutionToken& operator=(FDMXLibraryExecutionToken&&) = default;

	// Non-copyable
	FDMXLibraryExecutionToken(const FDMXLibraryExecutionToken&) = delete;
	FDMXLibraryExecutionToken& operator=(const FDMXLibraryExecutionToken&) = delete;

private:
	// Keeps all values from Fixture Functions that will be sent using the ports.
	// A Protocol points to Universe IDs. Each Universe ID points to a FragmentMap.
	TMap<int32 /** Universe */, TMap<int32, uint8> /** ChannelToValueMap */> UniverseToChannelToValueMap;

	const UMovieSceneDMXLibrarySection* Section;

	bool bInitialized = false;


public:
	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player) override
	{
		SCOPE_CYCLE_COUNTER(STAT_DMXSequencerExecuteExecutionToken);

		const FFrameTime Time = Context.GetTime();

		UDMXSubsystem* DMXSubsystem = UDMXSubsystem::GetDMXSubsystem_Pure();
		check(DMXSubsystem);

		if (!bInitialized)
		{
			bInitialized = true;

			bool bIsCacheValid = true;
			for (const FDMXCachedFunctionChannelInfo& InfoForChannelToInitialize : Section->GetChannelsToInitializeOnly())
			{
				if (const FDMXFixtureFunctionChannel* FixtureFunctionChannelPtr = InfoForChannelToInitialize.TryGetFunctionChannel(Section->GetFixturePatchChannels()))
				{
					float ChannelValue = 0.0f;
					if (FixtureFunctionChannelPtr->Channel.Evaluate(Time, ChannelValue))
					{
						// Round to int so if the user draws into the tracks, values are assigned to int accurately
						const uint32 FunctionValue = FMath::RoundToInt(ChannelValue);

						// Round to int so if the user draws into the tracks, values are assigned to int accurately
						TArray<uint8> ByteArr;
						DMXSubsystem->IntValueToBytes(FunctionValue, InfoForChannelToInitialize.GetSignalFormat(), ByteArr, InfoForChannelToInitialize.ShouldUseLSBMode());

						TMap<int32, uint8>& ChannelToValueMap = UniverseToChannelToValueMap.FindOrAdd(InfoForChannelToInitialize.GetUniverseID());

						for (int32 ByteIdx = 0; ByteIdx < ByteArr.Num(); ByteIdx++)
						{
							uint8& Value = ChannelToValueMap.FindOrAdd(InfoForChannelToInitialize.GetStartingChannel() + ByteIdx);
							Value = ByteArr[ByteIdx];
						}
					}
				}
				else
				{
					bIsCacheValid = false;
					break;
				}
			}

			if(!bIsCacheValid)
			{
				Section->RebuildPlaybackCache();
			}
		}
		else
		{
			// Reset previous values
			UniverseToChannelToValueMap.Reset();
		}
		
		for (const FDMXCachedFunctionChannelInfo& InfoForChannelToEvaluate : Section->GetChannelsToEvaluate())
		{
			bool bIsCacheValid = true;
			if (const FDMXFixtureFunctionChannel* FixtureFunctionChannelPtr = InfoForChannelToEvaluate.TryGetFunctionChannel(Section->GetFixturePatchChannels()))
			{
				float ChannelValue = 0.0f;
				if (FixtureFunctionChannelPtr->Channel.Evaluate(Time, ChannelValue))
				{
					// Round to int so if the user draws into the tracks, values are assigned to int accurately
					const uint32 FunctionValue = FMath::RoundToInt(ChannelValue);

					TMap<int32, uint8>& ChannelToValueMap = UniverseToChannelToValueMap.FindOrAdd(InfoForChannelToEvaluate.GetUniverseID());

					// Round to int so if the user draws into the tracks, values are assigned to int accurately
					TArray<uint8> ByteArr;
					DMXSubsystem->IntValueToBytes(FunctionValue, InfoForChannelToEvaluate.GetSignalFormat(), ByteArr, InfoForChannelToEvaluate.ShouldUseLSBMode());

					for (int32 ByteIdx = 0; ByteIdx < ByteArr.Num(); ByteIdx++)
					{
						uint8& Value = ChannelToValueMap.FindOrAdd(InfoForChannelToEvaluate.GetStartingChannel() + ByteIdx);
						Value = ByteArr[ByteIdx];
					}
				}
				else
				{
					bIsCacheValid = false;
					break;
				}
			}

			if (!bIsCacheValid)
			{
				Section->RebuildPlaybackCache();
			}
		}

		for(const TPair<int32, TMap<int32, uint8>>& UniverseToChannelToValueMapKvp : UniverseToChannelToValueMap)
		{
			for (const FDMXOutputPortSharedRef& OutputPort : Section->GetCachedOutputPorts())
			{
				OutputPort->SendDMX(UniverseToChannelToValueMapKvp.Key, UniverseToChannelToValueMapKvp.Value);
			}
		}
	}
};

FMovieSceneDMXLibraryTemplate::FMovieSceneDMXLibraryTemplate(const UMovieSceneDMXLibrarySection& InSection)
	: Section(&InSection)
{
	check(IsValid(Section));
}

void FMovieSceneDMXLibraryTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	SCOPE_CYCLE_COUNTER(STAT_DMXSequencerCreateExecutionTokens);

	check(Section && Section->IsValidLowLevelFast());

	// Don't evaluate while recording to prevent conflicts between sent DMX data and incoming recorded data
	if (Section->GetIsRecording())
	{
		return;
	}

	FDMXLibraryExecutionToken ExecutionToken(Section);
	ExecutionTokens.Add(MoveTemp(ExecutionToken));
}
