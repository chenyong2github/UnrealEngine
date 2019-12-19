// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Animation/AnimNodeBase.h"
#include "AnimNode_CustomProperty.generated.h"

/** 
 * Custom property node that you'd like to expand pin by reflecting internal instance (we call TargetInstance here)
 * 
 *  Used by sub anim instance or control rig node 
 *	where you have internal instance and would like to reflect to AnimNode as a pin
 * 
 *  To make pin working, you need storage inside of AnimInstance (SourceProperties/SourcePropertyNames)
 *  So this creates storage inside of AnimInstance with the unique custom property name
 *	and it copies to the actually TargetInstance here to allow the information be transferred in runtime (DestProperties/DestPropertyNames)
 * 
 *  TargetInstance - UObject derived instance that has certain dest properties
 *  Source - AnimInstance's copy properties that is used to store the data 
 */
USTRUCT()
struct ENGINE_API FAnimNode_CustomProperty : public FAnimNode_Base
{
	GENERATED_BODY()

public:

	FAnimNode_CustomProperty();

	/* Set Target Instance */
	void SetTargetInstance(UObject* InInstance);

	/* Get Target Instance by type for convenience */
	template<class T>
	T* GetTargetInstance() const
	{
		if (TargetInstance && !TargetInstance->IsPendingKill())
		{
			return Cast<T>(TargetInstance);
		}

		return nullptr;
	}

protected:
	/** List of source properties to use, 1-1 with Dest names below, built by the compiler */
	UPROPERTY()
	TArray<FName> SourcePropertyNames;

	/** List of destination properties to use, 1-1 with Source names above, built by the compiler */
	UPROPERTY()
	TArray<FName> DestPropertyNames;

	/** This is the actual instance allocated at runtime that will run. Set by child class. */
	UPROPERTY(Transient)
	UObject* TargetInstance;

	/** List of properties on the calling Source Instances instance to push from  */
	TArray<FProperty*> SourceProperties;

	/** List of properties on the TargetInstance to push to, built from name list when initialised */
	TArray<FProperty*> DestProperties;

#if WITH_EDITOR
	bool bReinitializeProperties;
#endif // WITH_EDITOR

	virtual bool HasPreUpdate() const override
	{
#if WITH_EDITOR
		return true;
#else
		return false;
#endif
	}

	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;

	/* Initialize property links from the source instance, in this case AnimInstance 
	 * Compiler creates those properties during compile time */
	void InitializeProperties(const UObject* InSourceInstance, UClass* InTargetClass);

	/* Propagate the Source Instances' properties to Target Instance*/
	void PropagateInputProperties(const UObject* InSourceInstance);

	/** Get Target Class */
	virtual UClass* GetTargetClass() const PURE_VIRTUAL(FAnimNode_CustomProperty::GetTargetClass, return nullptr;);

	friend class UAnimGraphNode_CustomProperty;
};
