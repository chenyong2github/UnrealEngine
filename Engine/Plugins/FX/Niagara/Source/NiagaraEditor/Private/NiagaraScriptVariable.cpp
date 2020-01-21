// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptVariable.h"
#include "NiagaraCommon.h"
#include "NiagaraEditorUtilities.h"

UNiagaraScriptVariable::UNiagaraScriptVariable(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultMode(ENiagaraDefaultMode::Value)
{
	
}

void UNiagaraScriptVariable::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

#if WITH_EDITOR
	if (UNiagaraGraph* Graph = Cast<UNiagaraGraph>(GetOuter()))
	{
		Graph->NotifyGraphNeedsRecompile();
	}
#endif	//#if WITH_EDITOR
}

void UNiagaraScriptVariable::PostLoad()
{
	Super::PostLoad();

	if (GIsEditor)
	{
		SetFlags(RF_Transactional);
	}
}

bool UNiagaraScriptVariable::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	if (!FNiagaraEditorUtilities::NestedPropertiesAppendCompileHash(static_cast<const void*>(this), StaticClass(), EFieldIteratorFlags::ExcludeSuper, StaticClass()->GetName(), InVisitor))
	{
		return false;
	}
	return true;
}