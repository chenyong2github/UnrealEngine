// Copyright Epic Games, Inc. All Rights Reserved.

#include "IKRigSolver.h"

#if WITH_EDITOR

void UIKRigSolver::PostLoad()
{
	Super::PostLoad();
	SetFlags(RF_Transactional); // patch old solvers to enable undo/redo
}

void UIKRigSolver::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	Super::PostTransacted(TransactionEvent);
	IKRigSolverModified.Broadcast(this);
}

#endif

