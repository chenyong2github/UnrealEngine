// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Library/DMXEntityReference.h"

#include "MovieSceneDMXLibrarySection.generated.h"

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

	/** Points to the Fixture Patch */
	UPROPERTY()
	FDMXEntityFixturePatchRef Reference;

	/** Fixture function float channels */
	UPROPERTY()
	TArray<FDMXFixtureFunctionChannel> FunctionChannels;

	/**
	 * Allows Sequencer to animate the fixture using a mode and not have it break
	 * simply by the user changing the active mode in the DMX Library.
	 */
	UPROPERTY()
	int32 ActiveMode;

	void SetFixturePatch(UDMXEntityFixturePatch* InPatch, int32 InActiveMode = -1);

	/** Makes sure the number of Float Channels matches the number of functions in the selected Patch mode */
	void UpdateNumberOfChannels(bool bResetDefaultValues = false);
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

	/** Add a Fixture Patch's Functions as curve channels to be animated */
	UFUNCTION(BlueprintCallable, Category = "Movie Scene")
	void AddFixturePatch(UDMXEntityFixturePatch* InPatch, int32 ActiveMode = -1);

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

	/**
	 * Iterate over each Patch's Function Channels array.
	 * Use it to edit the animation curves for each Patch.
	 */
	void ForEachPatchFunctionChannels(TFunctionRef<void(UDMXEntityFixturePatch*, TArray<FDMXFixtureFunctionChannel>&)> InPredicate);

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
};
