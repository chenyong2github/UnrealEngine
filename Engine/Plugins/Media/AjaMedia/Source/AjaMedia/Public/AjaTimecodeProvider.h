// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/TimecodeProvider.h"

#include "AjaMediaSource.h"
#include "AjaDeviceProvider.h"
#include "MediaIOCoreDefinitions.h"
#include "Tickable.h"

#include "AjaTimecodeProvider.generated.h"

namespace AJA
{
	class AJASyncChannel;
}

class UEngine;

/**
 * Class to fetch a timecode via an AJA card.
 * When the signal is lost in the editor (not in PIE), the TimecodeProvider will try to re-synchronize every second.
 */
UCLASS(Blueprintable, editinlinenew, meta=(DisplayName="AJA SDI Input", MediaIOCustomLayout="AJA"))
class AJAMEDIA_API UAjaTimecodeProvider : public UTimecodeProvider, public FTickableGameObject
{
	GENERATED_UCLASS_BODY()

public:
	//~ UTimecodeProvider interface
	virtual FQualifiedFrameTime GetQualifiedFrameTime() const override;
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

private:
	struct FAJACallback;
	friend FAJACallback;

	void ReleaseResources();

public:
	/**
	 * Shoud we read the timecode from an input source or the reference. The device may be able to read LTC or VITC.
	 */
	UPROPERTY(EditAnywhere, Category="Timecode")
	bool bUseReferenceIn;

	/**
	 * It read the timecode from the reference.
	 * @note The device has support LTC from the reference pin.
	 */
	UPROPERTY(EditAnywhere, Category="Timecode", meta=(EditCondition="bUseReferenceIn"))
	FAjaMediaTimecodeReference ReferenceConfiguration;

	/**
     * It read the timecode from an input source.
	 */
	UPROPERTY(EditAnywhere, Category="Timecode", meta=(EditCondition="!bUseReferenceIn"))
	FAjaMediaTimecodeConfiguration VideoConfiguration;

private:
	/** AJA Port to capture the Sync */
	AJA::AJASyncChannel* SyncChannel;
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
