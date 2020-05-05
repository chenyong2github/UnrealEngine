// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "MovieSceneSignedObject.generated.h"

class ITransactionObjectAnnotation;

UCLASS()
class MOVIESCENE_API UMovieSceneSignedObject : public UObject
{
public:
	GENERATED_BODY()

	UMovieSceneSignedObject(const FObjectInitializer& Init);

	/**
	 * 
	 */
	void MarkAsChanged();

	/**
	 * 
	 */
	const FGuid& GetSignature() const
	{
		return Signature;
	}

	/** Event that is triggered whenever this object's signature has changed */
	DECLARE_EVENT(UMovieSceneSignedObject, FOnSignatureChanged)
	FOnSignatureChanged& OnSignatureChanged() { return OnSignatureChangedEvent; }

public:

	virtual void PostInitProperties() override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual bool Modify(bool bAlwaysMarkDirty = true) override;
	virtual void PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditUndo() override;
	virtual void PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> TransactionAnnotation) override;
#endif

private:

	/** Unique generation signature */
	UPROPERTY()
	FGuid Signature;

	/** Event that is triggered whenever this object's signature has changed */
	FOnSignatureChanged OnSignatureChangedEvent;
};
