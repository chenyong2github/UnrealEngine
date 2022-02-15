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

USTRUCT()
struct FNiagaraValidationResult
{
	GENERATED_USTRUCT_BODY()

	FNiagaraValidationResult() {};
	FNiagaraValidationResult(ENiagaraValidationSeverity InSeverity, FText InSummaryText, FText InDescription, TWeakObjectPtr<UObject> InSourceObject)
		: Severity(InSeverity), SummaryText(InSummaryText), Description(InDescription), SourceObject(InSourceObject) {}

	UPROPERTY()
	ENiagaraValidationSeverity Severity = ENiagaraValidationSeverity::Info;
	
	UPROPERTY()
	FText SummaryText;

	UPROPERTY()
	FText Description;

	UPROPERTY()
	TWeakObjectPtr<UObject> SourceObject;
};

/**
Base class for system validation logic. 
These allow Niagara systems to be inspected for content validation either at save time or from a commandlet.
*/
UCLASS(abstract)
class NIAGARA_API UNiagaraValidationRule : public UObject
{
	GENERATED_BODY()
public:

#if WITH_EDITOR
	virtual TArray<FNiagaraValidationResult> CheckValidity(TSharedPtr<FNiagaraSystemViewModel> ViewModel) const;
#endif
};

