// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraGraphDataCache.h"

#include "NiagaraNodeFunctionCall.h"

void FNiagaraGraphDataCache::GetStackFunctionInputPins(UNiagaraNodeFunctionCall& FunctionCallNode, TArray<const UEdGraphPin*>& OutInputPins, TSet<const UEdGraphPin*>& OutHiddenPins, FCompileConstantResolver ConstantResolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions Options, bool bIgnoreDisabled)
{
	FGetStackFunctionInputPinsKey Key(FunctionCallNode, ConstantResolver, Options, bIgnoreDisabled);

	FCachedStackFunctionInputPins& CachedResult = GetStackFunctionInputPinsCache.FindOrAdd(Key);
	if (CachedResult.IsValid())
	{
		OutInputPins = CachedResult.InputPins;
		OutHiddenPins = CachedResult.HiddenPins;
		return;
	}

	OutInputPins.Empty();
	OutHiddenPins.Empty();
	FNiagaraStackGraphUtilities::GetStackFunctionInputPinsWithoutCache(FunctionCallNode, OutInputPins, OutHiddenPins, ConstantResolver, Options, bIgnoreDisabled);

	CachedResult.SourceGraphWeak = FunctionCallNode.GetNiagaraGraph();
	CachedResult.LastSourceGraphChangeId = FunctionCallNode.GetNiagaraGraph()->GetChangeID();
	CachedResult.FunctionCallNodeWeak = &FunctionCallNode;
	CachedResult.CalledGraphWeak = FunctionCallNode.GetCalledGraph();
	CachedResult.LastCalledGraphChangeId = FunctionCallNode.GetCalledGraph() != nullptr ? FunctionCallNode.GetCalledGraph()->GetChangeID() : FGuid();
	CachedResult.InputPins = OutInputPins;
	CachedResult.HiddenPins = OutHiddenPins;
}

FNiagaraGraphDataCache::FGetStackFunctionInputPinsKey::FGetStackFunctionInputPinsKey(UNiagaraNodeFunctionCall& InFunctionCallNode, const FCompileConstantResolver& InConstantResolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions InOptions, bool bInIgnoreDisabled)
{
	FunctionCallNodeKey = FObjectKey(&InFunctionCallNode);

	ConstantResolverSystemKey = FObjectKey(InConstantResolver.GetSystem());
	ConstantResolverEmitterKey = FObjectKey(InConstantResolver.GetEmitter());
	ConstantResolverTranslator = InConstantResolver.GetTranslator();
	ConstantResolverUsage = InConstantResolver.GetUsage();
	ConstantResolverDebugState = InConstantResolver.GetDebugState();

	Options = InOptions;
	bIgnoreDisabled = bInIgnoreDisabled;

	Hash = GetTypeHash(FunctionCallNodeKey);
	Hash = HashCombine(Hash, GetTypeHash(ConstantResolverSystemKey));
	Hash = HashCombine(Hash, GetTypeHash(ConstantResolverEmitterKey));
	Hash = HashCombine(Hash, GetTypeHash(ConstantResolverTranslator));
	Hash = HashCombine(Hash, GetTypeHash(ConstantResolverUsage));
	Hash = HashCombine(Hash, GetTypeHash(ConstantResolverDebugState));

	Hash = HashCombine(Hash, GetTypeHash(Options));
	Hash = HashCombine(Hash, GetTypeHash(bIgnoreDisabled));
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
