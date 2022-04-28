// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LevelSequencePlayer.h"
#include "MovieSceneBindingOwnerInterface.h"
#include "MovieSceneBindingOverrides.h"
#include "MovieSceneSequenceTickManager.h"
#include "TimeOfDayActor.generated.h"

UCLASS(prioritizeCategories=(General, TimeOfDay, RuntimeDayCycle, Binding), hideCategories=(Rendering, Physics, HLOD, Activation, Input, Collision, Actor, Lod, Cooking, DataLayers, Replication, WorldPartition))
class TIMEOFDAY_API ATimeOfDayActor
	: public AActor
	, public IMovieSceneSequenceActor
	, public IMovieScenePlaybackClient
	, public IMovieSceneBindingOwnerInterface
{
	GENERATED_BODY()

public:
	ATimeOfDayActor(const FObjectInitializer& Init);

	/** Access this actor's sequence player, or None if it is not yet initialized */
	UFUNCTION(BlueprintGetter)
	ULevelSequencePlayer* GetSequencePlayer() const;

	/**
	 * Get the level sequence being played by this actor.
	 *
	 * @return Level sequence, or nullptr if not assigned or if it cannot be loaded.
	 * @see SetSequence
	 */
	UFUNCTION(BlueprintCallable, Category="Sequencer|Player")
	ULevelSequence* GetDaySequence() const;

	/**
	 * Set the level sequence being played by this actor.
	 *
	 * @param InSequence The sequence object to set.
	 * @see GetSequence
	 */
	UFUNCTION(BlueprintCallable, Category="Sequencer|Player")
	void SetDaySequence(ULevelSequence* InSequence);

	/** Set whether or not to replicate playback for this actor */
	UFUNCTION(BlueprintSetter)
	void SetReplicatePlayback(bool ReplicatePlayback);

#if WITH_EDITOR
	virtual bool GetReferencedContentObjects(TArray<UObject*>& Objects) const override;
#endif //WITH_EDITOR

protected:
	//~ Begin UObject interface
	virtual bool ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags *RepFlags) override;
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	//~ End UObject interface
	
	//~ Begin AActor interface
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void RewindForReplay() override;
#if WITH_EDITOR
	virtual bool CanChangeIsSpatiallyLoadedFlag() const override { return false; }
#endif
	//~ End AActor interface

	//~ Begin IMovieSceneSequenceActor interface
	virtual void TickFromSequenceTickManager(float DeltaSeconds) override;
	//~ End IMovieSceneSequenceActor interface

	//~ Begin IMovieScenePlaybackClient interface
	virtual bool RetrieveBindingOverrides(const FGuid& InBindingId, FMovieSceneSequenceID InSequenceID, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const override;
	virtual UObject* GetInstanceData() const override;
	virtual bool GetIsReplicatedPlayback() const override;
	//~ End IMovieScenePlaybackClient interface

	//~ Begin IMovieSceneBindingOwnerInterface
#if WITH_EDITOR
	virtual TSharedPtr<FStructOnScope> GetObjectPickerProxy(TSharedPtr<IPropertyHandle> PropertyHandle) override;
	virtual void UpdateObjectFromProxy(FStructOnScope& Proxy, IPropertyHandle& ObjectPropertyHandle) override;
	virtual UMovieSceneSequence* RetrieveOwnedSequence() const override
	{
		return DaySequenceAsset;
	}
#endif
	//~ End IMovieSceneBindingOwnerInterface

	/** Initialize SequencePlayer with transient master sequence */
	void InitializePlayer();

	void UpdateDaySequence(ULevelSequence* SequenceAsset);

	/** Compute PlaybackSettings from day cycle properties */
	FMovieSceneSequencePlaybackSettings GetPlaybackSettings(const ULevelSequence* Sequence) const;

public:
	UPROPERTY(Instanced, transient, replicated, BlueprintReadOnly, BlueprintGetter=GetSequencePlayer, Category="Playback", meta=(ExposeFunctionCategories="Sequencer|Player"))
	TObjectPtr<ULevelSequencePlayer> SequencePlayer;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="General", meta=(AllowedClasses="LevelSequence"))
	TObjectPtr<ULevelSequence> DaySequenceAsset;

	/** Mapping of actors to override the sequence bindings with */
	UPROPERTY(Instanced, BlueprintReadOnly, Category="General")
	TObjectPtr<UMovieSceneBindingOverrides> BindingOverrides;
	
	/** If true, playback of this level sequence on the server will be synchronized across other clients */
	UPROPERTY(EditAnywhere, DisplayName="Replicate Playback", BlueprintReadWrite, BlueprintSetter=SetReplicatePlayback, Category=Replication)
	uint8 bReplicatePlayback : 1;

protected:
	UPROPERTY(Transient)
	TObjectPtr<ULevelSequence> MasterSequence;

#if WITH_EDITORONLY_DATA
	/** Sets the time of day to preview in the editor. Does not affect the start time at runtime */
	UPROPERTY(EditAnywhere, Category = TimeOfDay)
	FTimecode TimeOfDayPreview;
#endif

	/** Whether or not to run a day cycle. If this is unchecked the day cycle will remain fixed at the time specified by the Initial Time setting */
	UPROPERTY(EditAnywhere, Category = RuntimeDayCycle)
	bool bRunDayCycle;

	/** How long a single day cycle is */
	UPROPERTY(EditAnywhere, Category = RuntimeDayCycle)
	FTimecode DayLength;

	/** How long does it take for a day cycle to complete in world time. If this is the same value as day duration that means real world time is used */
	UPROPERTY(EditAnywhere, Category = RuntimeDayCycle)
	FTimecode TimePerCycle;

	/** The initial time that the day cycle will start at */
	UPROPERTY(EditAnywhere, Category = RuntimeDayCycle)
	FTimecode InitialTimeOfDay;
};


