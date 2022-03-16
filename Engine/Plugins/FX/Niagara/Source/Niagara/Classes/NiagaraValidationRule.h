// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "NiagaraValidationRule.generated.h"

class FNiagaraSystemViewModel;

UENUM()
enum class ENiagaraValidationSeverity
{
	/** Just an info message for the user. */
	Info,
	/** Could be a potential problem, for example bad performance. */
	Warning,
	/** A problem that must be fixed for the content to be valid. */
	Error,
};

DECLARE_DELEGATE(FNiagaraValidationFixDelegate);

/** Delegate wrapper to automatically fix content that fails validation checks. */
struct FNiagaraValidationFix
{
	FNiagaraValidationFix() {}
	FNiagaraValidationFix(FText InDescription, FNiagaraValidationFixDelegate InFixDelegate)
	: Description(InDescription)
	, FixDelegate(InFixDelegate)
	{
	}
	
	FText Description;
	FNiagaraValidationFixDelegate FixDelegate;
};

struct FNiagaraValidationResult
{
	FNiagaraValidationResult() {}
	FNiagaraValidationResult(ENiagaraValidationSeverity InSeverity, FText InSummaryText, FText InDescription, TWeakObjectPtr<UObject> InSourceObject)
		: Severity(InSeverity), SummaryText(InSummaryText), Description(InDescription), SourceObject(InSourceObject) {}

	ENiagaraValidationSeverity Severity = ENiagaraValidationSeverity::Info;

	FText SummaryText;
	FText Description;
	TWeakObjectPtr<UObject> SourceObject;
	TArray<FNiagaraValidationFix> Fixes;
	TArray<FNiagaraValidationFix> Links;
};

/**
Base class for system validation logic. 
These allow Niagara systems to be inspected for content validation either at save time or from a commandlet.
*/
UCLASS(abstract, EditInlineNew)
class NIAGARA_API UNiagaraValidationRule : public UObject
{
	GENERATED_BODY()
public:

#if WITH_EDITOR
	virtual void CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel, TArray<FNiagaraValidationResult>& OutResults) const;
#endif
};

