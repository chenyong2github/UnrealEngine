// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementRegistry.h"
#include "TypedElementFrameworkTests.generated.h"

/**
 * Test interfaces
 */
UCLASS()
class UTestTypedElementInterfaceA : public UTypedElementInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Testing")
	virtual FText GetDisplayName(const FTypedElementHandle& InElementHandle) { return FText(); }
	
	UFUNCTION(BlueprintCallable, Category="TypedElementFramework|Testing")
	virtual bool SetDisplayName(const FTypedElementHandle& InElementHandle, FText InNewName, bool bNotify = true) { return false; }
};

template <>
struct TTypedElement<UTestTypedElementInterfaceA> : public TTypedElementBase<UTestTypedElementInterfaceA>
{
	FText GetDisplayName() const { return InterfacePtr->GetDisplayName(*this); }
	bool SetDisplayName(FText InNewName, bool bNotify = true) const { return InterfacePtr->SetDisplayName(*this, InNewName, bNotify); }
};

/**
 * Test dummy type
 */
struct FTestTypedElementData
{
	UE_DECLARE_TYPED_ELEMENT_DATA_RTTI(FTestTypedElementData);
	
	FName InternalElementId;
};

UCLASS()
class UTestTypedElementInterfaceA_ImplTyped : public UTestTypedElementInterfaceA
{
	GENERATED_BODY()

public:
	virtual FText GetDisplayName(const FTypedElementHandle& InElementHandle) override;
	virtual bool SetDisplayName(const FTypedElementHandle& InElementHandle, FText InNewName, bool bNotify) override;
};

/**
 * Test untyped
 */
UCLASS()
class UTestTypedElementInterfaceA_ImplUntyped : public UTestTypedElementInterfaceA
{
	GENERATED_BODY()

public:
	virtual FText GetDisplayName(const FTypedElementHandle& InElementHandle) override;
	virtual bool SetDisplayName(const FTypedElementHandle& InElementHandle, FText InNewName, bool bNotify) override;
};
