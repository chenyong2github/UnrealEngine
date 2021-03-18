// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXProtocolCommon.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Library/DMXEntityReference.h"

#include "MovieSceneDMXLibrarySection.generated.h"

enum class EDMXFixtureSignalFormat : uint8;
class UDMXEntityFixturePatch;


USTRUCT()
struct FDMXFixtureFunctionChannel
{
	GENERATED_BODY()

	/** Attribute Name of the function */
	FName AttributeName;

	/** Function animation curve. */
	UPROPERTY()
	FMovieSceneFloatChannel Channel;

	/** 
	 * For Cell Functions the coordinate of the cell
	 * For common functions -1.
	 */
	FIntPoint CellCoordinate = FIntPoint(-1, -1);

	/** Default value to use when this Function is disabled in the track. */
	UPROPERTY()
	uint32 DefaultValue;

	/**
	 * Whether or not to display this Function in the Patch's group
	 * If false, the Function's default value is sent to DMX protocols.
	 */
	UPROPERTY()
	bool bEnabled = true;

	/** True if the function is a cell function */
	bool IsCellFunction() const { return CellCoordinate.X != -1 && CellCoordinate.Y != -1; }
};

USTRUCT()
struct FDMXFixturePatchChannel
{
	GENERATED_BODY()

	FDMXFixturePatchChannel()
		: ActiveMode(INDEX_NONE)
	{}

	/** Points to the Fixture Patch */
	UPROPERTY()
	FDMXEntityFixturePatchRef FixturePatchReference;

	/** Fixture function float channels */
	UPROPERTY()
	TArray<FDMXFixtureFunctionChannel> FunctionChannels;

	/**
	 * Allows Sequencer to animate the fixture using a mode and not have it break
	 * simply by the user changing the active mode in the DMX Library.
	 */
	UPROPERTY()
	int32 ActiveMode;

	void SetFixturePatch(UDMXEntityFixturePatch* InPatch);

	/** Makes sure the number of Float Channels matches the number of functions in the selected Patch mode */
	void UpdateNumberOfChannels(bool bResetDefaultValues = false);
};

/**
* Cached info of fixture function channels. Exists to streamline performance.
*
* Without this class, data for all tracks would have to be prepared each tick, leading to significant overhead.
*
* Besides caching values, the instance deduces how the track should be evaluated:
*
* 1. bNeedsEvaluation - These channels need update each tick
* 2. bNeedsInitialization - These channels need update only in the first tick!
* 3. Other tracks - These do not need update ever.
*
* Profiled, issues were apparent in 4.26 with a great number of sequencer channels (attributes).
*/
struct FDMXCachedFunctionChannelInfo
{
	FDMXCachedFunctionChannelInfo() = delete;

	FDMXCachedFunctionChannelInfo(const TArray<FDMXFixturePatchChannel>& FixturePatchChannels, int32 InPatchChannelIndex, int32 InFunctionChannelIndex);

	/** Returns the function channel, or nullptr if it got moved or deleted */
	const FDMXFixtureFunctionChannel* TryGetFunctionChannel(const TArray<FDMXFixturePatchChannel>& FixturePatchChannels) const;

	FORCEINLINE bool NeedsInitialization() const { return bNeedsInitialization; }

	FORCEINLINE bool NeedsEvaluation() const { return bNeedsEvaluation; }

	FORCEINLINE int32 GetUniverseID() const { return UniverseID; }

	FORCEINLINE int32 GetStartingChannel() const { return StartingChannel; }

	FORCEINLINE EDMXFixtureSignalFormat GetSignalFormat() const { return SignalFormat; }

	FORCEINLINE bool ShouldUseLSBMode() const { return bLSBMode; }

private:
	/**
	 * Gets the cell channels, but unlike subsystem's methods doesn't sort channels by pixel mapping distribution.
	 * If the cell coordinate would be sorted here, it would be sorted on receive again causing doubly sorting.
	 */
	void GetMatrixCellChannelsAbsoluteNoSorting(UDMXEntityFixturePatch* FixturePatch, const FIntPoint& CellCoordinate, TMap<FName, int32>& OutAttributeToAbsoluteChannelMap) const;

	bool bNeedsInitialization;
	bool bNeedsEvaluation;

	int32 PatchChannelIndex;
	
	int32 FunctionChannelIndex;

	int32 NumFunctionChannels;

	FName AttributeName;

	int32 UniverseID;

	int32 StartingChannel;

	EDMXFixtureSignalFormat SignalFormat;

	bool bLSBMode;
};

/** A DMX Fixture Patch section */
UCLASS()
class DMXRUNTIME_API UMovieSceneDMXLibrarySection
	: public UMovieSceneSection
{
public:

	GENERATED_BODY()

	UMovieSceneDMXLibrarySection();

public:

	// Begin UObject interface
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostEditImport() override;
	// ~End UObject interface

public:
	/** Refreshes the channels. Useful e.g. when underlying DMX Library changes */
	void RefreshChannels();

	/** Adds a single patch to the section */
	UFUNCTION(BlueprintCallable, Category = "Movie Scene")
	void AddFixturePatch(UDMXEntityFixturePatch* InPatch);

	/** Adds all patches to the secion */
	UFUNCTION(BlueprintCallable, Category = "Movie Scene")
	void AddFixturePatches(const TArray<FDMXEntityFixturePatchRef>& InFixturePatchRefs);

	/** Remove all Functions of a Fixture Patch */
	UFUNCTION(BlueprintCallable, Category = "Movie Scene")
	void RemoveFixturePatch(UDMXEntityFixturePatch* InPatch);

	/** Remove all Functions of a Fixture Patch, searching it by name */
	void RemoveFixturePatch(const FName& InPatchName);
	
	/** Check if this Section animates a Fixture Patch's Functions */
	UFUNCTION(BlueprintPure, Category = "Movie Scene")
	bool ContainsFixturePatch(UDMXEntityFixturePatch* InPatch) const;

	/** Sets the active mode for a Fixture Patch */
	UFUNCTION(BlueprintCallable, Category = "Movie Scene")
	void SetFixturePatchActiveMode(UDMXEntityFixturePatch* InPatch, int32 InActiveMode);

	/**
	 * Toggle the visibility and evaluation of a Fixture Patch's Function.
	 * When invisible, the Function won't send its data to DMX Protocol.
	 */
	UFUNCTION(BlueprintCallable, Category = "Movie Scene")
	void ToggleFixturePatchChannel(UDMXEntityFixturePatch* InPatch, int32 InChannelIndex);

	/**
	 * Toggle the visibility and evaluation of a Fixture Patch's Function searching
	 * both the Patch and Function by name.
	 * When invisible, the Function won't send its data to DMX Protocol.
	 */
	void ToggleFixturePatchChannel(const FName& InPatchName, const FName& InChannelName);

	/** Returns whether a Fixture Patch's Function curve channel is currently enabled */
	UFUNCTION(BlueprintPure, Category = "Movie Scene")
	bool GetFixturePatchChannelEnabled(UDMXEntityFixturePatch* InPatch, int32 InChannelIndex) const;

	/** Get a list of the Fixture Patches being animated by this Section */
	UFUNCTION(BlueprintPure, Category = "Movie Scene")
	TArray<UDMXEntityFixturePatch*> GetFixturePatches() const;

	/** Returns the channel for specified patch or nullptr if the patch is not in use */
	FDMXFixturePatchChannel* GetPatchChannel(UDMXEntityFixturePatch* Patch);

	/** Get the list of animated Fixture Patches and their curve channels */
	UFUNCTION(BlueprintPure, Category = "Movie Scene")
	int32 GetNumPatches() const { return FixturePatchChannels.Num(); }

	const TArray<FDMXFixturePatchChannel>& GetFixturePatchChannels() const { return FixturePatchChannels; }

	TArray<FDMXFixturePatchChannel>& GetMutableFixturePatchChannels() { return FixturePatchChannels; }

	/**
	 * Used only by the Take Recorder to prevent Track evaluation from sending
	 * DMX data while recording it.
	 */
	void SetIsRecording(bool bNewState) { bIsRecording = bNewState; }
	
	/** 
	 * Checked in evaluation to prevent sending DMX data while recording it with
	 * the Take Recorder.
	 */
	bool GetIsRecording() const { return bIsRecording; }

	/** Precaches data for playback */
	void RebuildPlaybackCache() const;

	/** Returns the precached channels that need initialization only (one time evaluation) */
	const TArray<FDMXCachedFunctionChannelInfo>& GetChannelsToInitializeOnly() const { return CachedChannelsToInitialize; }

	/** Returns the precached channels that need continous evaluation */
	const TArray<FDMXCachedFunctionChannelInfo>& GetChannelsToEvaluate() const { return CachedChannelsToEvaluate; }

	/** Returns the precached output ports */
	const TSet<FDMXOutputPortSharedRef>& GetCachedOutputPorts() const { return CachedOutputPorts; };

protected:
	/** Update the displayed Patches and Function channels in the section */
	void UpdateChannelProxy(bool bResetDefaultChannelValues = false);
	
protected:
	/** The Fixture Patches being controlled by this section and their respective chosen mode */
	UPROPERTY()
	TArray<FDMXFixturePatchChannel> FixturePatchChannels;

	/**
	 * When recording DMX data into this track, this is set to true to prevent
	 * track evaluation from sending data to DMX simultaneously.
	 */
	bool bIsRecording;

	// Cache
private:	
	/** Cached channel info for functions that need initialization (one time evaluation) only */
	mutable TArray<FDMXCachedFunctionChannelInfo> CachedChannelsToInitialize;

	/** Cached channel info for functions that need continous evaluation */
	mutable TArray<FDMXCachedFunctionChannelInfo> CachedChannelsToEvaluate;

	/** The output ports which should be used during playback */
	mutable TSet<FDMXOutputPortSharedRef> CachedOutputPorts;
};
