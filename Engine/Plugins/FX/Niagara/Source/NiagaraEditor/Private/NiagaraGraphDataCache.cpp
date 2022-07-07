// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraGraphDataCache.h"

#include "NiagaraNodeFunctionCall.h"

void FNiagaraGraphDataCache::GetStackFunctionInputPins(UNiagaraNodeFunctionCall& FunctionCallNode, TConstArrayView<FNiagaraVariable> StaticVars, TArray<const UEdGraphPin*>& OutInputPins, FCompileConstantResolver ConstantResolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions Options, bool bIgnoreDisabled)
{
	FGetStackFunctionInputPinsKey Key(FunctionCallNode, ConstantResolver, Options, bIgnoreDisabled, false);
	FCachedStackFunctionInputPins& CachedResult = GetStackFunctionInputPinsCache.FindOrAdd(Key);
	if (!CachedResult.IsValid())
	{
		FNiagaraStackGraphUtilities::GetStackFunctionInputPinsWithoutCache(
			FunctionCallNode,
			StaticVars,
			CachedResult.InputPins,
			ConstantResolver,
			Options,
			bIgnoreDisabled,
			false /*bFilterForCompilation*/);
	}

	OutInputPins = CachedResult.InputPins;
}

void FNiagaraGraphDataCache::GetStackFunctionInputPins(UNiagaraNodeFunctionCall& FunctionCallNode, TConstArrayView<FNiagaraVariable> StaticVars, TArray<const UEdGraphPin*>& OutInputPins, TSet<const UEdGraphPin*>& OutHiddenPins, FCompileConstantResolver ConstantResolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions Options, bool bIgnoreDisabled)
{
	FGetStackFunctionInputPinsKey Key(FunctionCallNode, ConstantResolver, Options, bIgnoreDisabled, false);
	FGetStackFunctionInputPinsKey CompilationKey(FunctionCallNode, ConstantResolver, Options, bIgnoreDisabled, true);

	if (!GetStackFunctionInputPinsCache.Contains(Key))
	{
		GetStackFunctionInputPinsCache.Add(Key);
	}

	if (!GetStackFunctionInputPinsCache.Contains(CompilationKey))
	{
		GetStackFunctionInputPinsCache.Add(CompilationKey);
	}

	FCachedStackFunctionInputPins& CachedResult = GetStackFunctionInputPinsCache.FindChecked(Key);
	FCachedStackFunctionInputPins& CompilationCachedResult = GetStackFunctionInputPinsCache.FindChecked(CompilationKey);

	if (!CachedResult.IsValid())
	{
		FNiagaraStackGraphUtilities::GetStackFunctionInputPinsWithoutCache(
			FunctionCallNode,
			StaticVars,
			CachedResult.InputPins,
			ConstantResolver,
			Options,
			bIgnoreDisabled,
			false /*bFilterForCompilation*/);
	}

	if (!CompilationCachedResult.IsValid())
	{
		FNiagaraStackGraphUtilities::GetStackFunctionInputPinsWithoutCache(
			FunctionCallNode,
			StaticVars,
			CompilationCachedResult.InputPins,
			ConstantResolver,
			Options,
			bIgnoreDisabled,
			true /*bFilterForCompilation*/);
	}

	OutInputPins = CachedResult.InputPins;

	// generate hidden pins
	auto PinsMatch = [](const UEdGraphPin* Lhs, const UEdGraphPin* Rhs) -> bool
	{
		return Lhs->GetFName() == Rhs->GetFName()
			&& Lhs->PinType.PinCategory == Rhs->PinType.PinCategory
			&& Lhs->PinType.PinSubCategoryObject == Rhs->PinType.PinSubCategoryObject;
	};

	for (const UEdGraphPin* InputPin : CachedResult.InputPins)
	{
		if (!CompilationCachedResult.InputPins.ContainsByPredicate([&](const UEdGraphPin* CompilationPin) { return PinsMatch(InputPin, CompilationPin); }))
		{
			OutHiddenPins.Add(InputPin);
		}
	}
}

FNiagaraGraphDataCache::FGetStackFunctionInputPinsKey::FGetStackFunctionInputPinsKey(UNiagaraNodeFunctionCall& InFunctionCallNode, const FCompileConstantResolver& InConstantResolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions InOptions, bool bInIgnoreDisabled, bool bInFilterForCompilation)
{
	FunctionCallNodeKey = FObjectKey(&InFunctionCallNode);

	ConstantResolverSystemKey = FObjectKey(InConstantResolver.GetSystem());
	ConstantResolverEmitterKey = FObjectKey(InConstantResolver.GetEmitter());
	ConstantResolverTranslator = InConstantResolver.GetTranslator();
	ConstantResolverUsage = InConstantResolver.GetUsage();
	ConstantResolverDebugState = InConstantResolver.GetDebugState();

	Options = InOptions;
	bIgnoreDisabled = bInIgnoreDisabled;
	bFilterForCompilation = bInFilterForCompilation;

	Hash = GetTypeHash(FunctionCallNodeKey);
	Hash = HashCombine(Hash, GetTypeHash(ConstantResolverSystemKey));
	Hash = HashCombine(Hash, GetTypeHash(ConstantResolverEmitterKey));
	Hash = HashCombine(Hash, GetTypeHash(ConstantResolverTranslator));
	Hash = HashCombine(Hash, GetTypeHash(ConstantResolverUsage));
	Hash = HashCombine(Hash, GetTypeHash(ConstantResolverDebugState));

	Hash = HashCombine(Hash, GetTypeHash(Options));
	Hash = HashCombine(Hash, GetTypeHash(bIgnoreDisabled));
	Hash = HashCombine(Hash, GetTypeHash(bFilterForCompilation));
}

bool FNiagaraGraphDataCache::FCachedStackFunctionInputPins::IsValid() const
{
	return
		SourceGraphWeak.IsValid() &&
		FunctionCallNodeWeak.IsValid() &&
		CalledGraphWeak.IsValid() &&

		SourceGraphWeak->GetChangeID() == LastSourceGraphChangeId &&
		FunctionCallNodeWeak->GetCalledGraph() == CalledGraphWeak.Get() &&
		(CalledGraphWeak.IsValid() == false || CalledGraphWeak->GetChangeID() == LastCalledGraphChangeId);
}
