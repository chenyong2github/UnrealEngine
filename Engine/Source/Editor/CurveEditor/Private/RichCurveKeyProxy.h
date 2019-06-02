// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "CurveEditorKeyProxy.h"
#include "Curves/RichCurve.h"

#include "RichCurveKeyProxy.generated.h"

UCLASS()
class URichCurveKeyProxy : public UObject, public ICurveEditorKeyProxy
{
public:
	GENERATED_BODY()

	/**
	 * Initialize this key proxy object by caching the underlying key object, and retrieving the time/value each tick
	 */
	void Initialize(FKeyHandle InKeyHandle, FRichCurve* InRichCurve, TWeakObjectPtr<UObject> InWeakOwner)
	{
		KeyHandle  = InKeyHandle;
		RichCurve  = InRichCurve;
		WeakOwner  = InWeakOwner;

		UObject* Owner = WeakOwner.Get();
		if (Owner && RichCurve->IsKeyHandleValid(KeyHandle))
		{
			Value = RichCurve->GetKey(KeyHandle);
		}
	}

private:

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);

		UObject* Owner = WeakOwner.Get();
		if (Owner && RichCurve->IsKeyHandleValid(KeyHandle))
		{
			Owner->Modify();

			FRichCurveKey& ActualKey = RichCurve->GetKey(KeyHandle);

			const float PreviousTime = ActualKey.Time;
			const float NewTime      = Value.Time;

			// Copy the key properties
			ActualKey = Value;
			ActualKey.Time = PreviousTime;
			if (PreviousTime != NewTime)
			{
				RichCurve->SetKeyTime(KeyHandle, NewTime);
			}
		}
	}

	virtual void UpdateValuesFromRawData() override
	{
		UObject* Owner = WeakOwner.Get();
		if (Owner && RichCurve->IsKeyHandleValid(KeyHandle))
		{
			Value = RichCurve->GetKey(KeyHandle);
		}
	}

private:

	/** User-facing value of the key, applied to the actual key on PostEditChange, and updated every tick */
	UPROPERTY(EditAnywhere, Category="Key", meta=(ShowOnlyInnerProperties))
	FRichCurveKey Value;

private:

	/** Cached key handle that this key proxy relates to */
	FKeyHandle KeyHandle;
	/** Cached curve in which the key resides */
	FRichCurve* RichCurve;
	/** Cached owner in which the raw curve resides */
	TWeakObjectPtr<UObject> WeakOwner;
};