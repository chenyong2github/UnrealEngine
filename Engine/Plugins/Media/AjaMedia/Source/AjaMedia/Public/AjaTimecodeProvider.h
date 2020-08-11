// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenlockedTimecodeProvider.h"
#include "AjaMediaSource.h"
#include "AjaDeviceProvider.h"
#include "MediaIOCoreDefinitions.h"
#include "Tickable.h"

#include "AjaTimecodeProvider.generated.h"

namespace AJA
{
	class AJATimecodeChannel;
}

class UEngine;

/**
 * Class to fetch a timecode via an AJA card.
 * When the signal is lost in the editor (not in PIE), the TimecodeProvider will try to re-synchronize every second.
 */
UCLASS(Blueprintable, editinlinenew, meta=(DisplayName="AJA SDI Input", MediaIOCustomLayout="AJA"))
class AJAMEDIA_API UAjaTimecodeProvider : public UGenlockedTimecodeProvider, public FTickableGameObject
{
	GENERATED_UCLASS_BODY()

public:
	//~ UTimecodeProvider interface
	virtual bool FetchTimecode(FQualifiedFrameTime& OutFrameTime) override;
	virtual ETimecodeProviderSynchronizationState GetSynchronizationState() const override { return State; }
	virtual bool Initialize(class UEngine* InEngine) override;
	virtual void Shutdown(class UEngine* InEngine) override;

	//~ FTickableGameObject interface
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAjaTimecodeProvider, STATGROUP_Tickables); }

	//~ UObject interface
	virtual void BeginDestroy() override;
	virtual void Serialize(FArchive& Ar) override;

private:
	struct FAJACallback;
	friend FAJACallback;

	void ReleaseResources();

public:

	/**
	 * Should we read the timecode from a dedicated LTC pin or an SDI input.
	 */
	UPROPERTY(EditAnywhere, Category="Timecode")
	bool bUseDedicatedPin;

	/**
	 * Read LTC timecode from reference pin. Will fail if device doesn't support that feature.
	 */
	UPROPERTY(EditAnywhere, Category="Timecode", meta = (EditCondition = "bUseDedicatedPin"))
	bool bUseReferenceIn;

	/**
	 * Where to read LTC timecode from with which FrameRate expected
	 */
	UPROPERTY(EditAnywhere, Category="Timecode", meta=(EditCondition="bUseDedicatedPin"))
	FAjaMediaTimecodeReference LTCConfiguration;

	/**
     * It read the timecode from an input source.
	 */
	UPROPERTY(EditAnywhere, Category="Timecode", meta=(EditCondition="!bUseDedicatedPin"))
	FAjaMediaTimecodeConfiguration VideoConfiguration;

private:
	/** AJA channel associated with reading LTC timecode */
	AJA::AJATimecodeChannel* TimecodeChannel;
	FAJACallback* SyncCallback;

#if WITH_EDITORONLY_DATA
	/** Engine used to initialize the Provider */
	UPROPERTY(Transient)
	UEngine* InitializedEngine;

	/** The time the last attempt to auto synchronize was triggered. */
	double LastAutoSynchronizeInEditorAppTime;
#endif

	/** The current SynchronizationState of the TimecodeProvider*/
	ETimecodeProviderSynchronizationState State;
};
