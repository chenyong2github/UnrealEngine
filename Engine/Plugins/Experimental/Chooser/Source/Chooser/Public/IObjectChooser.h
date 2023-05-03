// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "InstancedStruct.h"
#include "IObjectChooser.generated.h"

UINTERFACE(NotBlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class CHOOSER_API UObjectChooser : public UInterface
{
	GENERATED_BODY()
};

class CHOOSER_API IObjectChooser
{
	GENERATED_BODY()
public:
	virtual void ConvertToInstancedStruct(FInstancedStruct& OutInstancedStruct) const { }
};

struct FContextEntry
{
	const UStruct* Type = nullptr;
	void* Data = nullptr;
};

#if WITH_EDITOR
struct FChooserDebuggingInfo
{
	bool bCurrentDebugTarget = false;
};
#endif

struct FChooserEvaluationContext
{
#if WITH_EDITOR
	FChooserDebuggingInfo DebuggingInfo;
#endif

	TArray<FContextEntry, TFixedAllocator<4>> ContextData;
};

USTRUCT()
struct CHOOSER_API FObjectChooserBase
{
	GENERATED_BODY()

public:
	virtual ~FObjectChooserBase() {}

	enum class EIteratorStatus { Continue, ContinueWithOutputs, Stop };

	DECLARE_DELEGATE_RetVal_OneParam( EIteratorStatus, FObjectChooserIteratorCallback, UObject*);

	virtual UObject* ChooseObject(FChooserEvaluationContext& Context) const { return nullptr; };
	virtual EIteratorStatus ChooseMulti(FChooserEvaluationContext& ContextData, FObjectChooserIteratorCallback Callback) const
	{
		// fallback implementation just calls the single version
		if (UObject* Result = ChooseObject(ContextData))
		{
			return Callback.Execute(Result);
		}
		return EIteratorStatus::Continue;
	}
};