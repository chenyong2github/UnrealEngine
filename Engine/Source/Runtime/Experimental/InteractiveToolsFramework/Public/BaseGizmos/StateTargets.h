// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveGizmoManager.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "StateTargets.generated.h"


/**
 * UGizmoNilStateTarget is an implementation of IGizmoStateTarget that does nothing
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoNilStateTarget : public UObject, public IGizmoStateTarget
{
	GENERATED_BODY()
public:
	virtual void BeginUpdate() final
	{
	}

	virtual void EndUpdate() final
	{
	}

};



/**
 * UGizmoLambdaStateTarget is an implementation of IGizmoStateTarget that forwards
 * calls to its interface functions to external TFunctions
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoLambdaStateTarget : public UObject, public IGizmoStateTarget
{
	GENERATED_BODY()
public:
	virtual void BeginUpdate()
	{
		if (BeginUpdateFunction)
		{
			BeginUpdateFunction();
		}
	}

	virtual void EndUpdate()
	{
		if (EndUpdateFunction)
		{
			EndUpdateFunction();
		}
	}

	TUniqueFunction<void(void)> BeginUpdateFunction = TUniqueFunction<void(void)>();
	TUniqueFunction<void(void)> EndUpdateFunction = TUniqueFunction<void(void)>();
};



/**
 * UGizmoObjectModifyStateTarget is an implementation of IGizmoStateTarget that 
 * opens and closes change transactions on a target UObject via a GizmoManager.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoObjectModifyStateTarget : public UObject, public IGizmoStateTarget
{
	GENERATED_BODY()
public:
	virtual void BeginUpdate()
	{
		if (GizmoManager.IsValid())
		{
			GizmoManager->BeginUndoTransaction(TransactionDescription);
		}
		if (ModifyObject.IsValid())
		{
			ModifyObject->Modify();
		}
	}

	virtual void EndUpdate()
	{
		if (GizmoManager.IsValid())
		{
			GizmoManager->EndUndoTransaction();
		}
	}


	/**
	 * The object that will be changed, ie have Modify() called on it on BeginUpdate()
	 */
	TWeakObjectPtr<UObject> ModifyObject;

	/**
	 * Localized text description of the transaction (will be visible in Editor on undo/redo)
	 */
	FText TransactionDescription;

	/**
	 * Pointer to the GizmoManager that is used to open/close the transaction
	 */
	TWeakObjectPtr<UInteractiveGizmoManager> GizmoManager;


public:
	/**
	 * Create and initialize an standard instance of UGizmoObjectModifyStateTarget
	 * @param ModifyObjectIn the object this StateTarget will call Modify() on
	 * @param DescriptionIn Localized text description of the transaction
	 * @param GizmoManagerIn pointer to the GizmoManager used to manage transactions
	 */
	static UGizmoObjectModifyStateTarget* Construct(
		UObject* ModifyObjectIn,
		const FText& DescriptionIn,
		UInteractiveGizmoManager* GizmoManagerIn,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoObjectModifyStateTarget* NewTarget = NewObject<UGizmoObjectModifyStateTarget>(Outer);
		NewTarget->ModifyObject = ModifyObjectIn;
		NewTarget->TransactionDescription = DescriptionIn;
		NewTarget->GizmoManager = GizmoManagerIn;
		return NewTarget;
	}
};
