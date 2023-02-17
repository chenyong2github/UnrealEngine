// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "ChooserPropertyAccess.generated.h"

#if WITH_EDITOR
struct FBindingChainElement;
#endif

UINTERFACE(MinimalAPI)
class UHasContextClass : public UInterface
{
	GENERATED_BODY()
};

class IHasContextClass
{
	GENERATED_BODY()
public:
	virtual UClass* GetContextClass() { return nullptr; };
};

USTRUCT()
struct FChooserPropertyBinding 
{
	GENERATED_BODY()
	
	UPROPERTY()
	TArray<FName> PropertyBindingChain;
};

namespace UE::Chooser
{
	CHOOSER_API bool ResolvePropertyChain(const void*& Container, UStruct*& StructType, const TArray<FName>& PropertyBindingChain);
#if WITH_EDITOR
	CHOOSER_API void CopyPropertyChain(const TArray<FBindingChainElement>& InBindingChain, TArray<FName>& OutPropertyBindingChain);
#endif
}