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

DECLARE_MULTICAST_DELEGATE_OneParam(FContextClassChanged, UClass*)

class IHasContextClass
{
	GENERATED_BODY()
public:
	FContextClassChanged OnContextClassChanged;
	virtual UClass* GetContextClass() { return nullptr; };
};

USTRUCT()
struct FChooserPropertyBinding 
{
	GENERATED_BODY()
	
	UPROPERTY()
	TArray<FName> PropertyBindingChain;
};

USTRUCT()
struct FChooserEnumPropertyBinding : public FChooserPropertyBinding
{
	GENERATED_BODY()
	
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<const UEnum> Enum = nullptr;
#endif
};

USTRUCT()
struct FChooserObjectPropertyBinding : public FChooserPropertyBinding
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UClass> AllowedClass = nullptr;
#endif
};

namespace UE::Chooser
{
	CHOOSER_API bool ResolvePropertyChain(const void*& Container, UStruct*& StructType, const TArray<FName>& PropertyBindingChain);

#if WITH_EDITOR
	CHOOSER_API void CopyPropertyChain(const TArray<FBindingChainElement>& InBindingChain, TArray<FName>& OutPropertyBindingChain);
#endif
}