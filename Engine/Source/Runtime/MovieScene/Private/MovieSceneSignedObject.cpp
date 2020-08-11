// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneSignedObject.h"
#include "Templates/Casts.h"
#include "MovieSceneSequence.h"
#include "UObject/Package.h"

UMovieSceneSignedObject::UMovieSceneSignedObject(const FObjectInitializer& Init)
	: Super(Init)
{
}

void UMovieSceneSignedObject::PostInitProperties()
{
	Super::PostInitProperties();

	// Always seed newly created objects with a new signature
	// (CDO and archetypes always have a zero GUID)
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad | RF_LoadCompleted) && Signature == GetDefault<UMovieSceneSignedObject>()->Signature)
	{
		Signature = FGuid::NewGuid();
	}
}

void UMovieSceneSignedObject::PostLoad()
{
	Super::PostLoad();
}

void UMovieSceneSignedObject::MarkAsChanged()
{
	using namespace UE::MovieScene;

	Signature = FGuid::NewGuid();

	OnSignatureChangedEvent.Broadcast();
	
	UObject* Outer = GetOuter();
	while (Outer)
	{
		UMovieSceneSignedObject* TypedOuter = Cast<UMovieSceneSignedObject>(Outer);
		if (TypedOuter)
		{
			TypedOuter->MarkAsChanged();
			break;
		}
		Outer = Outer->GetOuter();
	}
}

#if WITH_EDITOR
bool UMovieSceneSignedObject::Modify(bool bAlwaysMarkDirty)
{
	bool bModified = Super::Modify(bAlwaysMarkDirty);
	if ( bAlwaysMarkDirty )
	{
		MarkAsChanged();
	}
	return bModified;
}

void UMovieSceneSignedObject::PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MarkAsChanged();
}

void UMovieSceneSignedObject::PostEditUndo()
{
	Super::PostEditUndo();
	MarkAsChanged();
}

void UMovieSceneSignedObject::PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> TransactionAnnotation)
{
	Super::PostEditUndo(TransactionAnnotation);
	MarkAsChanged();
}
#endif

