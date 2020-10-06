// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneSpawnable.h"
#include "UObject/UObjectAnnotation.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "IMovieScenePlayer.h"
#include "Misc/StringBuilder.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"

struct FIsSpawnable
{
	FIsSpawnable() : bIsSpawnable(false) {}
	explicit FIsSpawnable(bool bInIsSpawnable) : bIsSpawnable(bInIsSpawnable) {}

	bool IsDefault() const { return !bIsSpawnable; }

	bool bIsSpawnable;
};

static FUObjectAnnotationSparse<FIsSpawnable,true> SpawnablesAnnotation;

bool FMovieSceneSpawnable::IsSpawnableTemplate(const UObject& InObject)
{
	return !SpawnablesAnnotation.GetAnnotation(&InObject).IsDefault();
}

void FMovieSceneSpawnable::MarkSpawnableTemplate(const UObject& InObject)
{
	SpawnablesAnnotation.AddAnnotation(&InObject, FIsSpawnable(true));
}

void FMovieSceneSpawnable::CopyObjectTemplate(UObject& InSourceObject, UMovieSceneSequence& MovieSceneSequence)
{
	const FName ObjectName = ObjectTemplate ? ObjectTemplate->GetFName() : InSourceObject.GetFName();

	if (ObjectTemplate)
	{
		ObjectTemplate->Rename(*MakeUniqueObjectName(MovieSceneSequence.GetMovieScene(), ObjectTemplate->GetClass(), "ExpiredSpawnable").ToString());
		ObjectTemplate->MarkPendingKill();
		ObjectTemplate = nullptr;
	}

	ObjectTemplate = MovieSceneSequence.MakeSpawnableTemplateFromInstance(InSourceObject, ObjectName);

	check(ObjectTemplate);

	MarkSpawnableTemplate(*ObjectTemplate);
	MovieSceneSequence.MarkPackageDirty();
}

FName FMovieSceneSpawnable::GetNetAddressableName(IMovieScenePlayer& Player, FMovieSceneSequenceID SequenceID) const
{
	UObject* PlayerObject = Player.AsUObject();
	if (!PlayerObject)
	{
		return NAME_None;
	}

	TStringBuilder<128> AddressableName;

	// Spawnable name
	AddressableName.Append(*Name, Name.Len());

	// SequenceID
	AddressableName.Appendf(TEXT("_0x%08X"), SequenceID.GetInternalValue());

	// Spawnable GUID
	AddressableName.Appendf(TEXT("_%08X%08X%08X%08X"), Guid.A, Guid.B, Guid.C, Guid.D);

	// Actor / player Name
	if (AActor* OuterActor = PlayerObject->GetTypedOuter<AActor>())
	{
		AddressableName.Append('_');
		OuterActor->GetFName().AppendString(AddressableName);
	}
	else
	{
		AddressableName.Append('_');
		PlayerObject->GetFName().AppendString(AddressableName);
	}

	return FName(AddressableName.Len(), AddressableName.GetData());
}

void FMovieSceneSpawnable::AutoSetNetAddressableName()
{
	bNetAddressableName = false;

	AActor* Actor = Cast<AActor>(ObjectTemplate);
	if (Actor && Actor->FindComponentByClass<UStaticMeshComponent>() != nullptr)
	{
		bNetAddressableName = true;
	}
}