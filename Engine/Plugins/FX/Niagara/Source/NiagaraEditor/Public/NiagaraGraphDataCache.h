// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "NiagaraEditorUtilities.h"
#include "UObject/ObjectKey.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

class FHlslNiagaraTranslator;
class UEdGraphPin;
class UNiagaraGraph;
class UNiagaraNodeFunctionCall;

class FNiagaraGraphDataCache
{
public:
	void GetStackFunctionInputPins(UNiagaraNodeFunctionCall& FunctionCallNode, TArray<const UEdGraphPin*>& OutInputPins, TSet<const UEdGraphPin*>& OutHiddenPins, FCompileConstantResolver ConstantResolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions Options, bool bIgnoreDisabled);
	
private:
	struct FGetStackFunctionInputPinsKey
	{
		FGetStackFunctionInputPinsKey(UNiagaraNodeFunctionCall& InFunctionCallNode, const FCompileConstantResolver& InConstantResolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions InOptions, bool bInIgnoreDisabled);

		FORCEINLINE bool operator == (const FGetStackFunctionInputPinsKey& Other) const
		{
			return
				FunctionCallNodeKey == Other.FunctionCallNodeKey &&
				ConstantResolverSystemKey == Other.ConstantResolverSystemKey &&
				ConstantResolverEmitterKey == Other.ConstantResolverEmitterKey &&
				ConstantResolverTranslator == Other.ConstantResolverTranslator &&
				ConstantResolverUsage == Other.ConstantResolverUsage &&
				ConstantResolverDebugState == Other.ConstantResolverDebugState &&
				Options == Other.Options &&
				bIgnoreDisabled == Other.bIgnoreDisabled;
		}

		friend uint32 GetTypeHash(const FGetStackFunctionInputPinsKey& Key)
		{
			return Key.Hash;
		}
		
	private:
		FObjectKey FunctionCallNodeKey;

		FObjectKey ConstantResolverSystemKey;
		FObjectKey ConstantResolverEmitterKey;
		const FHlslNiagaraTranslator* ConstantResolverTranslator;
		ENiagaraScriptUsage ConstantResolverUsage;
		ENiagaraFunctionDebugState ConstantResolverDebugState;

		FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions Options;
		bool bIgnoreDisabled;

		int32 Hash;
	};

	struct FCachedStackFunctionInputPins
	{
		TWeakObjectPtr<UNiagaraGraph> SourceGraphWeak;
		FGuid LastSourceGraphChangeId;
		TWeakObjectPtr<UNiagaraNodeFunctionCall> FunctionCallNodeWeak;
		TWeakObjectPtr<UNiagaraGraph> CalledGraphWeak;
		FGuid LastCalledGraphChangeId;
		TArray<const UEdGraphPin*> InputPins;
		TSet<const UEdGraphPin*> HiddenPins;
		bool IsValid() const;
	};

	TMap<FGetStackFunctionInputPinsKey, FCachedStackFunctionInputPins> GetStackFunctionInputPinsCache;
};