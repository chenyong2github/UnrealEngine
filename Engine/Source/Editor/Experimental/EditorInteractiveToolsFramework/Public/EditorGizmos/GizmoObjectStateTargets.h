// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveGizmoManager.h"
#include "InteractiveToolManager.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "Changes/TransformChange.h"
#include "EditorGizmos/GizmoBaseObject.h"
#include "GizmoObjectStateTargets.generated.h"

/**
 * UGizmoObjectTransformChangeStateTarget is an implementation of IGizmoStateTarget that
 * emits an FComponentWorldTransformChange on a target USceneComponent. This StateTarget
 * also opens/closes an undo transaction via GizmoManager.
 *
 * The DependentChangeSources and ExternalDependentChangeSources lists allow additional
 * FChange objects to be inserted into the transaction, provided by IToolCommandChangeSource implementations.
 */
UCLASS()
class EDITORINTERACTIVETOOLSFRAMEWORK_API UGizmoObjectTransformChangeStateTarget : public UObject, public IGizmoStateTarget
{
	GENERATED_BODY()
public:
	virtual void BeginUpdate()
	{
		if (TargetObject.IsValid())
		{
			if (TransactionManager)
			{
				TransactionManager->BeginUndoTransaction(ChangeDescription);
			}

			InitialTransform = TargetObject->GetLocalToWorldTransform();

			for (TUniquePtr<IToolCommandChangeSource>& Source : DependentChangeSources)
			{
				Source->BeginChange();
			}

			for (IToolCommandChangeSource* Source : ExternalDependentChangeSources)
			{
				Source->BeginChange();
			}
		}
	}

	virtual void EndUpdate()
	{
		if (TargetObject.IsValid())
		{
			FinalTransform = TargetObject->GetLocalToWorldTransform();

			if (TransactionManager)
			{
				TUniquePtr<FComponentWorldTransformChange> TransformChange
					= MakeUnique<FComponentWorldTransformChange>(InitialTransform, FinalTransform);
				TransactionManager->EmitObjectChange(TargetObject.Get(), MoveTemp(TransformChange), ChangeDescription);

				for (TUniquePtr<IToolCommandChangeSource>& Source : DependentChangeSources)
				{
					TUniquePtr<FToolCommandChange> Change = Source->EndChange();
					if (Change)
					{
						UObject* Target = Source->GetChangeTarget();
						TransactionManager->EmitObjectChange(Target, MoveTemp(Change), Source->GetChangeDescription());
					}
				}

				for (IToolCommandChangeSource* Source : ExternalDependentChangeSources)
				{
					TUniquePtr<FToolCommandChange> Change = Source->EndChange();
					if (Change)
					{
						UObject* Target = Source->GetChangeTarget();
						TransactionManager->EmitObjectChange(Target, MoveTemp(Change), Source->GetChangeDescription());
					}
				}

				TransactionManager->EndUndoTransaction();
			}
		}
	}


	/**
	 * The object that will be changed, ie have Modify() called on it on BeginUpdate()
	 */
	TWeakObjectPtr<UGizmoBaseObject> TargetObject;

	/**
	 * Localized text description of the transaction (will be visible in Editor on undo/redo)
	 */
	FText ChangeDescription;

	/**
	 * Pointer to the GizmoManager or ToolManager that is used to open/close the transaction
	 */
	UPROPERTY()
	TScriptInterface<IToolContextTransactionProvider> TransactionManager;

	/** Start Transform, saved on BeginUpdate() */
	FTransform InitialTransform;
	/** End Transform, saved on EndUpdate() */
	FTransform FinalTransform;


	/**
	 * Dependent-change generators. This will be told about update start/end, and any generated changes will also be emitted.
	 * This allows (eg) TransformProxy change events to be collected at the same time as changes to a gizmo target component.
	 */
	TArray<TUniquePtr<IToolCommandChangeSource>> DependentChangeSources;

	/**
	 * Dependent-change generators that are not owned by this class, otherwise handled identically to DependentChangeSources
	 */
	TArray<IToolCommandChangeSource*> ExternalDependentChangeSources;

public:

	/**
	 * Create and initialize an standard instance of UGizmoObjectTransformChangeStateTarget
	 * @param TargetComponentIn the USceneComponent this StateTarget will track transform changes on
	 * @param DescriptionIn Localized text description of the transaction
	 * @param TransactionManagerIn pointer to the GizmoManager or ToolManager used to manage transactions
	 */
	static UGizmoObjectTransformChangeStateTarget* Construct(
		UGizmoBaseObject* TargetObjectIn,
		const FText& DescriptionIn,
		IToolContextTransactionProvider* TransactionManagerIn,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoObjectTransformChangeStateTarget* NewTarget = NewObject<UGizmoObjectTransformChangeStateTarget>(Outer);
		NewTarget->TargetObject = TargetObjectIn;
		NewTarget->ChangeDescription = DescriptionIn;

		// have to explicitly configure this because we only have IToolContextTransactionProvider pointer
		NewTarget->TransactionManager.SetInterface(TransactionManagerIn);
		NewTarget->TransactionManager.SetObject(CastChecked<UObject>(TransactionManagerIn));

		return NewTarget;
	}


};
