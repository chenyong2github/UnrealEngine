// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"

class UNiagaraSystem;

struct FNiagaraCompilationOptions
{
	UNiagaraSystem* System = nullptr;
	bool bForced = false;
};

struct FNiagaraQueryCompilationOptions
{
	UNiagaraSystem* System = nullptr;
	double MaxWaitDuration = 0.125;
	bool bWait = false;
};

class FNiagaraActiveCompilation : public FGCObject
{
public:
	static TUniquePtr<FNiagaraActiveCompilation> CreateCompilation();

	virtual bool Launch(const FNiagaraCompilationOptions& Options) = 0;
	virtual void Abort() = 0;
	virtual bool QueryCompileComplete(const FNiagaraQueryCompilationOptions& Options) = 0;
	virtual bool Validate(const FNiagaraQueryCompilationOptions& Options) const = 0;
	virtual void Apply(const FNiagaraQueryCompilationOptions& Options) = 0;
	virtual void ReportResults(const FNiagaraQueryCompilationOptions& Options) const = 0;

	void Invalidate()
	{
		bShouldApply = false;
	}

	bool ShouldApply() const
	{
		return bShouldApply;
	}

	bool WasForced() const
	{
		return bForced;
	}

protected:
	bool bForced = false;

private:
	bool bShouldApply = true;
};

