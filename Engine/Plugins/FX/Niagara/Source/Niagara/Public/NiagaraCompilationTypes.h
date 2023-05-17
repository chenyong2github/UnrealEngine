// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"

#include "NiagaraScript.h"
#include "NiagaraTypes.h"

#include "NiagaraCompilationTypes.generated.h"

class UNiagaraSystem;

using FNiagaraCompilationTaskHandle = int32;

USTRUCT()
struct FNiagaraScriptAsyncCompileData
{
	GENERATED_USTRUCT_BODY()

	FNiagaraVMExecutableDataId CompileId;
	TSharedPtr<struct FNiagaraVMExecutableData> ExeData;
	FString UniqueEmitterName;
	bool bFromDerivedDataCache = false;

	UPROPERTY()
	TMap<FName, TObjectPtr<UNiagaraDataInterface>> NamedDataInterfaces;
};

USTRUCT()
struct FNiagaraSystemAsyncCompileResults
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<TObjectPtr<UObject>> RootObjects;

	FNiagaraCompilationTaskHandle CompilationTask;

	using FCompileResultMap = TMap<UNiagaraScript*, FNiagaraScriptAsyncCompileData>;
	FCompileResultMap CompileResultMap;

	UPROPERTY()
	TArray<FNiagaraVariable> ExposedVariables;

	bool bForced = false;

	float CombinedCompileTime = 0.0f;
	float StartTime = 0.0f;
};

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
