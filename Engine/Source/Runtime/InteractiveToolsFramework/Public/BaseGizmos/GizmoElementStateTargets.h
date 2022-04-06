// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveGizmoManager.h"
#include "InteractiveToolManager.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "BaseGizmos/GizmoElementBase.h"
#include "Changes/TransformChange.h"
#include "GizmoElementStateTargets.generated.h"

/**
 * UGizmoDependentTransformChangeStateTarget is an implementation of IGizmoStateTarget that
 * opens/closes an undo transaction via GizmoManager.
 *
 * The DependentChangeSources and ExternalDependentChangeSources lists allow additional
 * FChange objects to be inserted into the transaction, provided by IToolCommandChangeSource implementations.
 */
UCLASS()
class INTERACTIVETOOLSFRAMEWORK_API UGizmoDependentTransformChangeStateTarget : public UObject, public IGizmoStateTarget
{
	GENERATED_BODY()
public:
	virtual void BeginUpdate()
	{
		if (TransactionManager)
		{
			TransactionManager->BeginUndoTransaction(ChangeDescription);
		}

		for (TUniquePtr<IToolCommandChangeSource>& Source : DependentChangeSources)
		{
			if (Source.IsValid())
			{
				Source->BeginChange();
			}
		}

		for (IToolCommandChangeSource* Source : ExternalDependentChangeSources)
		{
			if (Source)
			{
				Source->BeginChange();
			}
		}
	}

	virtual void EndUpdate()
	{
		if (TransactionManager)
		{
			for (TUniquePtr<IToolCommandChangeSource>& Source : DependentChangeSources)
			{
				if (Source.IsValid())
				{
					TUniquePtr<FToolCommandChange> Change = Source->EndChange();
					if (Change)
					{
						UObject* Target = Source->GetChangeTarget();
						TransactionManager->EmitObjectChange(Target, MoveTemp(Change), Source->GetChangeDescription());
					}
				}
			}

			for (IToolCommandChangeSource* Source : ExternalDependentChangeSources)
			{
				if (Source)
				{
					TUniquePtr<FToolCommandChange> Change = Source->EndChange();
					if (Change)
					{
						UObject* Target = Source->GetChangeTarget();
						TransactionManager->EmitObjectChange(Target, MoveTemp(Change), Source->GetChangeDescription());
					}
				}
			}

			TransactionManager->EndUndoTransaction();
		}
	}

	/**
	 * Localized text description of the transaction (will be visible in Editor on undo/redo)
	 */
	FText ChangeDescription;

	/**
	 * Pointer to the GizmoManager or ToolManager that is used to open/close the transaction
	 */
	UPROPERTY()
	TScriptInterface<IToolContextTransactionProvider> TransactionManager;

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
	 * @param DescriptionIn Localized text description of the transaction
	 * @param TransactionManagerIn pointer to the GizmoManager or ToolManager used to manage transactions
	 */
	static UGizmoDependentTransformChangeStateTarget* Construct(
		const FText& DescriptionIn,
		IToolContextTransactionProvider* TransactionManagerIn,
		UObject* Outer = (UObject*)GetTransientPackage())
	{
		UGizmoDependentTransformChangeStateTarget* NewTarget = NewObject<UGizmoDependentTransformChangeStateTarget>(Outer);
		NewTarget->ChangeDescription = DescriptionIn;

		// have to explicitly configure this because we only have IToolContextTransactionProvider pointer
		NewTarget->TransactionManager.SetInterface(TransactionManagerIn);
		NewTarget->TransactionManager.SetObject(CastChecked<UObject>(TransactionManagerIn));

		return NewTarget;
	}


};
