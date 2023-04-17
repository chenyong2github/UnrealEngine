// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraTypes.h"
#include "Widgets/SCompoundWidget.h"

/**
 * 
 */
class NIAGARAEDITOR_API SNiagaraHierarchyAssignment : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraHierarchyAssignment)
	{
	}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs, class UNiagaraNodeAssignment& InAssignmentNode);
};
