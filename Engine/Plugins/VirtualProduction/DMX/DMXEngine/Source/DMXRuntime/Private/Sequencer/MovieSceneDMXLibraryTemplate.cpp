// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/MovieSceneDMXLibraryTemplate.h"

#include "DMXRuntimeLog.h"
#include "DMXProtocolTypes.h"
#include "DMXProtocolCommon.h"
#include "DMXStats.h"
#include "DMXSubsystem.h"
#include "Interfaces/IDMXProtocol.h"
#include "Library/DMXLibrary.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXEntityFixtureType.h"
#include "Library/DMXEntityController.h"

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

			// Get the Controllers affecting this Fixture Patch's universe
			const TArray<UDMXEntityController*>&& Controllers = FixturePatch->GetRelevantControllers();

			bool bNoControllerAssigned = Controllers.Num() == 0;
			if (bNoControllerAssigned)
			{
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


				// Get each individual channel value from the Function
				TArray<uint8, TFixedAllocator<4>> FunctionValues;
				UDMXEntityFixtureType::FunctionValueToBytes(Function, Function.DefaultValue, FunctionValues.GetData());
			
				// Write each channel (byte) to the fragment map
				const int32 FunctionStartChannel = Function.Channel + PatchChannelOffset;
				const int32 FunctionEndChannel = UDMXEntityFixtureType::GetFunctionLastChannel(Function) + PatchChannelOffset;

				for (int32 Channel = FunctionStartChannel; Channel <= FunctionEndChannel; Channel++)
				{
					int32 ByteIndex = FunctionEndChannel - FunctionStartChannel;
					check(ByteIndex < FunctionValues.Num());

					FragmentMap.Add(Channel, FunctionValues[ByteIndex]);
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
								FragmentMap.Add(ChannelIndex, ValueBytes[ByteIndex]);
							}
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
				const bool bCanLoopback = Protocol->IsSendDMXEnabled() && Protocol->IsReceiveDMXEnabled();
				if (!bCanLoopback)
				{
					Protocol->InputDMXFragment(FixturePatch->UniverseID + Controller->RemoteOffset, FragmentMap);
				}

				Protocol->SendDMXFragment(FixturePatch->UniverseID + Controller->RemoteOffset, FragmentMap);
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

	/**
	* Cached info of fixture function channels. Exists to streamline performance.
	*
	* Without this class, data for all tracks would have to be prepared each tick, leading to significant overhead.
	*
	* Besides caching values, the instance deduces how the track should be evaluated:
	*
	* 1. bNeedsEvalutation - These channels need update each tick
	* 2. bNeedsInitialization - These channels need update only in the first tick!
	* 3. Other tracks - These do not need update ever.
	*
	* Profiled, issues were appereant in 4.26 with a great number of sequencer channels (attributes).
	*/
	struct FDMXCachedFunctionChannelInfo
	{
		FDMXCachedFunctionChannelInfo(const FDMXFixturePatchChannel& InPatchChannel, const FDMXFixtureFunctionChannel& InFunctionChannel)
		{
			// Valid patch
			UDMXEntityFixturePatch* FixturePatch = InPatchChannel.Reference.GetFixturePatch();
			if (FixturePatch == nullptr || !FixturePatch->IsValidLowLevelFast())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: A Fixture Patch is null."), __FUNCTION__);
				return;
			}

			// Enabled
			if (!InFunctionChannel.bEnabled)
			{
				return;
			}

			// Valid controller
			UDMXEntityController* Controller = FixturePatch->GetFirstRelevantController();
			if (!Controller)
			{
				return;
			}

			// Valid protocol
			Protocol = Controller->DeviceProtocol;
			if (!Protocol.IsValid())
			{
				return;
			}

			// Try to access the active mode
			const UDMXEntityFixtureType* FixtureType = FixturePatch->ParentFixtureTypeTemplate;
			if (FixtureType == nullptr || !FixtureType->IsValidLowLevelFast())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: Patch %s has invalid Fixture Type template."), __FUNCTION__, *FixturePatch->GetDisplayName());
				return;
			}

			if (InPatchChannel.ActiveMode >= FixtureType->Modes.Num())
			{
				UE_LOG(MovieSceneDMXLibraryTemplateLog, Error, TEXT("%S: Patch track %s ActiveMode is invalid."), __FUNCTION__, *FixturePatch->GetDisplayName());
				return;
			}

			const FDMXFixtureMode& Mode = FixtureType->Modes[InPatchChannel.ActiveMode];

			// Cache the universe ID
			UniverseID = FixturePatch->UniverseID + Controller->RemoteOffset;
			if (UniverseID < Protocol->GetMinUniverseID())
			{
				return;
			}

			// Cache Fuction properties
			if (InFunctionChannel.IsCellFunction())
			{
				const FDMXFixtureMatrix& MatrixConfig = Mode.FixtureMatrixConfig;
				const TArray<FDMXFixtureCellAttribute>& CellAttributes = MatrixConfig.CellAttributes;
				
				TMap<FDMXAttributeName, int32> AttributeNameChannelMap;
				GetMatrixCellChannelsAbsoluteNoSorting(FixturePatch, InFunctionChannel.CellCoordinate, AttributeNameChannelMap);

				const FDMXFixtureCellAttribute* CellAttributePtr = CellAttributes.FindByPredicate([&InFunctionChannel](const FDMXFixtureCellAttribute& CellAttribute) {
					return CellAttribute.Attribute == InFunctionChannel.AttributeName;
					});

				const bool bMissingFunction = !AttributeNameChannelMap.Contains(InFunctionChannel.AttributeName) || !CellAttributePtr;
				if (!CellAttributePtr && bMissingFunction)
				{
					UE_LOG(MovieSceneDMXLibraryTemplateLog, Warning, TEXT("%S: Function with attribute %s from %s doesn't have a counterpart Fixture Function."), __FUNCTION__, *InFunctionChannel.AttributeName.ToString(), *FixturePatch->GetDisplayName());
					UE_LOG(MovieSceneDMXLibraryTemplateLog, Warning, TEXT("%S: Further attributes may be missing. Warnings ommited to avoid overflowing the log."), __FUNCTION__);

					return;
				}

				const FDMXFixtureCellAttribute& CellAttribute = *CellAttributePtr;
				StartingChannel = AttributeNameChannelMap[InFunctionChannel.AttributeName];
				SignalFormat = CellAttribute.DataType;
				bLSBMode = CellAttribute.bUseLSBMode;
			}
			else
			{
				const TArray<FDMXFixtureFunction>& Functions = Mode.Functions;

				const FDMXFixtureFunction* FunctionPtr = Functions.FindByPredicate([&InFunctionChannel](const FDMXFixtureFunction& TestedFunction) {
					return TestedFunction.Attribute == InFunctionChannel.AttributeName;
					});

				if (!FunctionPtr)
				{
					UE_LOG(MovieSceneDMXLibraryTemplateLog, Warning, TEXT("%S: Function with attribute %s from %s doesn't have a counterpart Fixture Function."), __FUNCTION__, *InFunctionChannel.AttributeName.ToString(), *FixturePatch->GetDisplayName());

					return;
				}

				const FDMXFixtureFunction& Function = *FunctionPtr;

				const int32 PatchChannelOffset = FixturePatch->GetStartingChannel() - 1;
				StartingChannel = Function.Channel + PatchChannelOffset;
				SignalFormat = Function.DataType;
				bLSBMode = Function.bUseLSBMode;
			}

			// UNSAFE, TODO 4.27: Cached pointer to a struct. 
			FunctionChannelPtr = &InFunctionChannel;

			// Now that we know it's fully valid, define how it should be processed.
			bNeedsInitialization = InFunctionChannel.Channel.GetNumKeys() > 0;
			bNeedsEvaluation = InFunctionChannel.Channel.GetNumKeys() > 1;

			check(FunctionChannelPtr);
		}
		
	private:
		/** 
		 * Gets the cell channels, but unlike subsystem's methods doesn't sort channels by pixel mapping distribution.
		 * If the cell coordinate would be sorted here, it would be sorted on receive again causing doubly sorting.
		 */
		void GetMatrixCellChannelsAbsoluteNoSorting(UDMXEntityFixturePatch* FixturePatch, const FIntPoint& CellCoordinate, TMap<FDMXAttributeName, int32>& OutAttributeToAbsoluteChannelMap) const
		{
			if (!FixturePatch ||
				!FixturePatch->ParentFixtureTypeTemplate ||
				!FixturePatch->ParentFixtureTypeTemplate->bFixtureMatrixEnabled)
			{
				return;
			}

			FDMXFixtureMatrix MatrixProperties;
			if (!FixturePatch->GetMatrixProperties(MatrixProperties))
			{
				return;
			}

			TMap<const FDMXFixtureCellAttribute*, int32> AttributeToRelativeChannelOffsetMap;
			int32 CellDataSize = 0;
			int32 AttributeChannelOffset = 0;
			for (const FDMXFixtureCellAttribute& CellAttribute : MatrixProperties.CellAttributes)
			{
				AttributeToRelativeChannelOffsetMap.Add(&CellAttribute, AttributeChannelOffset);
				const int32 AttributeSize = UDMXEntityFixtureType::NumChannelsToOccupy(CellAttribute.DataType);

				CellDataSize += AttributeSize;
				AttributeChannelOffset += UDMXEntityFixtureType::NumChannelsToOccupy(CellAttribute.DataType);
			}
			
			const int32 FixtureMatrixAbsoluteStartingChannel = FixturePatch->GetStartingChannel() + MatrixProperties.FirstCellChannel - 1;
			const int32 CellChannelOffset = (CellCoordinate.Y * MatrixProperties.XCells + CellCoordinate.X) * CellDataSize;
			const int32 AbsoluteCellStartingChannel = FixtureMatrixAbsoluteStartingChannel + CellChannelOffset;

			for (const TTuple<const FDMXFixtureCellAttribute*, int32>& AttributeToRelativeChannelOffsetKvp : AttributeToRelativeChannelOffsetMap)
			{
				const FDMXAttributeName AttributeName = AttributeToRelativeChannelOffsetKvp.Key->Attribute;
				const int32 AbsoluteChannel = AbsoluteCellStartingChannel + AttributeToRelativeChannelOffsetKvp.Value;

				check(AbsoluteChannel > 0 && AbsoluteChannel <= DMX_UNIVERSE_SIZE);
				OutAttributeToAbsoluteChannelMap.Add(AttributeName, AbsoluteChannel);
			}
		}

	public:
		FORCEINLINE const FDMXFixtureFunctionChannel& GetFunctionChannel() const { return *FunctionChannelPtr; }

		FORCEINLINE const IDMXProtocolPtr& GetProtocol() const { return Protocol; }

		FORCEINLINE bool NeedsInitialization() const { return bNeedsInitialization; }

		FORCEINLINE bool NeedsEvaluation() const { return bNeedsEvaluation; }

		FORCEINLINE int32 GetUniverseID() const { return UniverseID; }

		FORCEINLINE int32 GetStartingChannel() const { return StartingChannel; }

		FORCEINLINE EDMXFixtureSignalFormat GetSignalFormat() const { return SignalFormat; }

		FORCEINLINE bool ShouldUseLSBMode() const { return bLSBMode; }

	private:
		bool bNeedsInitialization = false;
		bool bNeedsEvaluation = false;

		const FDMXFixtureFunctionChannel* FunctionChannelPtr = nullptr;

		int32 UniverseID = -1;

		int32 StartingChannel = -1;

		EDMXFixtureSignalFormat SignalFormat;

		bool bLSBMode = false;

		IDMXProtocolPtr Protocol;
	};

	/** Arrays of those channels that actually need initialization and channels that need evaluation each tick. */
	struct FDMXCachedSectionInfo
	{
		TArray<FDMXCachedFunctionChannelInfo> ChannelsToInitialize;
		TArray<FDMXCachedFunctionChannelInfo> ChannelsToEvaluate;
	};

	/**
	 * Global map of sections with cached info
	 * TODO 4.27: Required to be declared globally to avoid altering public headers for 4.26.1 sequencer performance hotfix
	 */
	TMap<const UMovieSceneDMXLibrarySection*, FDMXCachedSectionInfo> SectionsWithCachedInfo;

	/** 
	 * Token executed each tick during playback 
	 */
	struct FDMXLibraryExecutionToken 
		: IMovieSceneExecutionToken
	{
		/** Constructor, called at begin of playback */
		FDMXLibraryExecutionToken(const UMovieSceneDMXLibrarySection* InSection, const TArray<FDMXCachedFunctionChannelInfo>* InChannelsToEvaluate, const TArray<FDMXCachedFunctionChannelInfo>* InChannelsToInitializeOnly)
			: Section(InSection) 
			, ChannelsToEvaluate(InChannelsToEvaluate)
			, ChannelsToInitializeOnly(InChannelsToInitializeOnly)
		{}

		FDMXLibraryExecutionToken(FDMXLibraryExecutionToken&&) = default;
		FDMXLibraryExecutionToken& operator=(FDMXLibraryExecutionToken&&) = default;

		// Non-copyable
		FDMXLibraryExecutionToken(const FDMXLibraryExecutionToken&) = delete;
		FDMXLibraryExecutionToken& operator=(const FDMXLibraryExecutionToken&) = delete;

	private:
		// Keeps all values from Fixture Functions that will be sent using the controllers.
		// A Protocol points to Universe IDs. Each Universe ID points to a FragmentMap.
		TMap<IDMXProtocolPtr, TMap<uint16 /** Universe */, IDMXFragmentMap /** ChannelValueKvp */>> DMXFragmentMaps;

		const UMovieSceneDMXLibrarySection* Section;

		const TArray<FDMXCachedFunctionChannelInfo>* ChannelsToEvaluate;

		const TArray<FDMXCachedFunctionChannelInfo>* ChannelsToInitializeOnly;

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

				for (const FDMXCachedFunctionChannelInfo& InfoForChannelToInitialize : *ChannelsToInitializeOnly)
				{
					const FDMXFixtureFunctionChannel& FixtureFunctionChannel = InfoForChannelToInitialize.GetFunctionChannel();

					float ChannelValue = 0.0f;
					if (FixtureFunctionChannel.Channel.Evaluate(Time, ChannelValue))
					{
						// Round to int so if the user draws into the tracks, values are assigned to int accurately
						const uint32 FunctionValue = FMath::RoundToInt(ChannelValue);

						TMap<uint16, IDMXFragmentMap>& UniverseFragmentMapKvp = DMXFragmentMaps.FindOrAdd(InfoForChannelToInitialize.GetProtocol());

						IDMXFragmentMap& FragmentMap = UniverseFragmentMapKvp.FindOrAdd(InfoForChannelToInitialize.GetUniverseID());

						// Round to int so if the user draws into the tracks, values are assigned to int accurately
						TArray<uint8> ByteArr;
						DMXSubsystem->IntValueToBytes(FunctionValue, InfoForChannelToInitialize.GetSignalFormat(), ByteArr, InfoForChannelToInitialize.ShouldUseLSBMode());

						for (int32 ByteIdx = 0; ByteIdx < ByteArr.Num(); ByteIdx++)
						{
							uint8& Value = FragmentMap.FindOrAdd(InfoForChannelToInitialize.GetStartingChannel() + ByteIdx);
							Value = ByteArr[ByteIdx];
						}
					}
				}
			}
			else
			{
				// Reset previous fragments
				DMXFragmentMaps.Reset();
			}

			for (const FDMXCachedFunctionChannelInfo& InfoForChannelToEvaluate : *ChannelsToEvaluate)
			{
				const FDMXFixtureFunctionChannel& FixtureFunctionChannel = InfoForChannelToEvaluate.GetFunctionChannel();

				float ChannelValue = 0.0f;
				if (FixtureFunctionChannel.Channel.Evaluate(Time, ChannelValue))
				{
					// Round to int so if the user draws into the tracks, values are assigned to int accurately
					const uint32 FunctionValue = FMath::RoundToInt(ChannelValue);

					TMap<uint16, IDMXFragmentMap>& UniverseFragmentMapKvp = DMXFragmentMaps.FindOrAdd(InfoForChannelToEvaluate.GetProtocol());

					IDMXFragmentMap& FragmentMap = UniverseFragmentMapKvp.FindOrAdd(InfoForChannelToEvaluate.GetUniverseID());

					// Round to int so if the user draws into the tracks, values are assigned to int accurately
					TArray<uint8> ByteArr;
					DMXSubsystem->IntValueToBytes(FunctionValue, InfoForChannelToEvaluate.GetSignalFormat(), ByteArr, InfoForChannelToEvaluate.ShouldUseLSBMode());

					for (int32 ByteIdx = 0; ByteIdx < ByteArr.Num(); ByteIdx++)
					{
						uint8& Value = FragmentMap.FindOrAdd(InfoForChannelToEvaluate.GetStartingChannel() + ByteIdx);
						Value = ByteArr[ByteIdx];
					}
				}
			}

			// Send the Universes data from the accumulated DMXFragmentMaps
			for (const TPair<IDMXProtocolPtr, TMap<uint16, IDMXFragmentMap>>& Protocol_Universes : DMXFragmentMaps)
			{
				for (const TPair<uint16, IDMXFragmentMap>& Universe_FragmentMaps : Protocol_Universes.Value)
				{
					IDMXProtocolPtr Protocol = Protocol_Universes.Key;
					const bool bCanLoopback = Protocol->IsReceiveDMXEnabled() && Protocol->IsSendDMXEnabled();

					// If sent DMX will not be looped back via network, input it directly
					if (!bCanLoopback)
					{
						Protocol->InputDMXFragment(Universe_FragmentMaps.Key, Universe_FragmentMaps.Value);
					}

					Protocol->SendDMXFragment(Universe_FragmentMaps.Key, Universe_FragmentMaps.Value);
				}
			}
		}
	};

}

FMovieSceneDMXLibraryTemplate::FMovieSceneDMXLibraryTemplate(const UMovieSceneDMXLibrarySection& InSection)
	: Section(&InSection)
{
	// By removing the section, we force the section's cache to be updated when a new template is created.
	// That alike changes in editor are considered (otherwise existing cached data would reused, and changes ingored).
	if (SectionsWithCachedInfo.Contains(&InSection))
	{
		SectionsWithCachedInfo.Remove(&InSection);
	}
}

void FMovieSceneDMXLibraryTemplate::Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const
{
	SCOPE_CYCLE_COUNTER(STAT_DMXSequencerCreateExecutionTokens);

	check(Section && Section->IsValidLowLevelFast());

	// Don't evaluate while recording to prevent conflicts between
	// sent DMX data and incoming recorded data
	if (Section->GetIsRecording())
	{
		return;
	}

	// Reuse cached info or initialize that if no chached section info exists
	FDMXCachedSectionInfo* CachedSectionInfoPtr = SectionsWithCachedInfo.Find(Section);

	if (!CachedSectionInfoPtr)
	{
		CachedSectionInfoPtr = &SectionsWithCachedInfo.Add(Section);

		// Cache channel data to streamline performance
		for (const FDMXFixturePatchChannel& PatchChannel : Section->GetFixturePatchChannels())
		{
			for (const FDMXFixtureFunctionChannel& FunctionChannel : PatchChannel.FunctionChannels)
			{
				FDMXCachedFunctionChannelInfo CachedFunctionChannelInfo = FDMXCachedFunctionChannelInfo(PatchChannel, FunctionChannel);
				if (CachedFunctionChannelInfo.NeedsEvaluation())
				{
					CachedSectionInfoPtr->ChannelsToEvaluate.Add(CachedFunctionChannelInfo);
				}
				else if (CachedFunctionChannelInfo.NeedsInitialization())
				{
					CachedSectionInfoPtr->ChannelsToInitialize.Add(CachedFunctionChannelInfo);
				}
			}
		}
	}
	check(CachedSectionInfoPtr);

	FDMXLibraryExecutionToken ExecutionToken(Section, &CachedSectionInfoPtr->ChannelsToEvaluate, &CachedSectionInfoPtr->ChannelsToInitialize);
	ExecutionTokens.Add(MoveTemp(ExecutionToken));
}
