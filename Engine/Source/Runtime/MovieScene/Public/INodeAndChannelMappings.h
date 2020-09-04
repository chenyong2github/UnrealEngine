// Copyright Epic Games, Inc. All Rights Reserved.

/**
* Sequencer Animation Track Support interface - this is required for animation track to work
*/

#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"

#include "INodeAndChannelMappings.generated.h"


/** Enumeration specifying the control type */
enum class FFBXControlRigTypeProxyEnum : uint8
{
	Bool,
	Float,
	Vector2D,
	Position,
	Scale,
	Rotator,
	Transform,
	TransformNoScale,
	EulerTransform,
	Integer
};

struct FMovieSceneFloatChannel;
struct FMovieSceneBoolChannel;
struct FMovieSceneIntegerChannel;
struct FMovieSceneByteChannel;
class UMovieSceneTrack;
// For import data onto channels directly
struct FFBXNodeAndChannels
{
	UMovieSceneTrack* MovieSceneTrack;
	FFBXControlRigTypeProxyEnum ControlType;
	FString NodeName;
	TArray< FMovieSceneFloatChannel*> Channels;
	//will really only have one ever.
	TArray< FMovieSceneBoolChannel*> BoolChannels;
	TArray< FMovieSceneIntegerChannel*> IntegerChannels;
	TArray< FMovieSceneByteChannel*> EnumChannels;

};


UINTERFACE(meta = (CannotImplementInterfaceInBlueprint))
class MOVIESCENE_API UNodeAndChannelMappings : public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class MOVIESCENE_API INodeAndChannelMappings
{
	GENERATED_IINTERFACE_BODY()

	/** Get The Node And Mappings for this Track. Note Callee is responsible for deleting */
	virtual TArray<FFBXNodeAndChannels>*  GetNodeAndChannelMappings()  = 0;
	
	/** Get Selected Nodes */
	virtual void GetSelectedNodes(TArray<FName>& OutSelectedNodes) = 0;
};