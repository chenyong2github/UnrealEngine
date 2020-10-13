// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraStackObject.h"

class INiagaraStackObjectIssueGenerator
{
public:
	virtual ~INiagaraStackObjectIssueGenerator(){}
	virtual void GenerateIssues(void* Object, UNiagaraStackObject* StackObject, TArray<UNiagaraStackEntry::FStackIssue>& NewIssues) = 0;
};

class FNiagaraPlatformSetIssueGenerator : public INiagaraStackObjectIssueGenerator
{
public:
	virtual void GenerateIssues(void* Object, UNiagaraStackObject* StackObject, TArray<UNiagaraStackEntry::FStackIssue>& NewIssues)override;
};