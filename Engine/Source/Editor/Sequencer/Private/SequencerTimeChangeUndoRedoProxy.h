// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "UObject/ObjectMacros.h"
#include "EventHandlers/ISignedObjectEventHandler.h"
#include "Delegates/IDelegateInstance.h"
#include "Misc/QualifiedFrameTime.h"
#include "SequencerTimeChangeUndoRedoProxy.generated.h"

class FSequencer;

UCLASS()
class  USequencerTimeChangeUndoRedoProxy : public UObject, public UE::MovieScene::ISignedObjectEventHandler
{
public:
	GENERATED_BODY()
	USequencerTimeChangeUndoRedoProxy() :bTimeWasSet(false), WeakSequencer(nullptr) {  };
	~USequencerTimeChangeUndoRedoProxy();

	void SetSequencer(TSharedRef<FSequencer> InSequencer);
	void OnActivateSequenceChanged(FMovieSceneSequenceIDRef ID);

	/*~ UObject */
	virtual void PostEditUndo() override;

	/*~ ISignedObjectEventHandler Interface */
	virtual void  OnModifiedIndirectly(UMovieSceneSignedObject*) override;

private:
	UPROPERTY(Transient)
	FQualifiedFrameTime Time;
	
	//no TOptional UPROPERTY so use this instead
	UPROPERTY(Transient)
	bool bTimeWasSet = false;
	
	FDelegateHandle OnActivateSequenceChangedHandle;
	TWeakPtr<FSequencer> WeakSequencer;

	UE::MovieScene::TNonIntrusiveEventHandler<UE::MovieScene::ISignedObjectEventHandler> MovieSceneModified;
};

