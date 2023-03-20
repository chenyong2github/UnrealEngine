// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "Binding/States/WidgetStateBitfield.h"

#include "WidgetStateRegistration.generated.h"

class UWidget;
class UWidgetStateSettings;

/**
 * Derive from to add a new widget binary state
 * 
 * Technically these can be created in BP, but for now we don't want to encourage
 * that workflow as it involves requring overrides for the virtuals which is technical.
 */
UCLASS(Transient)
class UMG_API UWidgetBinaryStateRegistration : public UObject
{
	GENERATED_BODY()

public:
	UWidgetBinaryStateRegistration() = default;
	virtual ~UWidgetBinaryStateRegistration() = default;

	/** Called once during WidgetStateSettings initialization to get this widget state's name */
	virtual FName GetStateName() const;

	/** Called on widget registration to correctly initialize widget state based on the current widget */
	virtual bool GetRegisteredWidgetState(const UWidget* InWidget) const;

protected:
	friend UWidgetStateSettings;

	/** Called to give CDO chance to initialize any static state bitfields that might be declared for convenience */
	virtual void InitializeStaticBitfields() const;
};

UCLASS(Transient)
class UMG_API UWidgetHoveredStateRegistration : public UWidgetBinaryStateRegistration
{
	GENERATED_BODY()

public:

	/** Post-load initialized bit corresponding to this binary state */
	static inline FWidgetStateBitfield Bit;

	static const inline FName StateName = FName("Hovered");

	//~ Begin UWidgetBinaryStateRegistration Interface.
	virtual FName GetStateName() const override;
	virtual bool GetRegisteredWidgetState(const UWidget* InWidget) const override;
	//~ End UWidgetBinaryStateRegistration Interface

protected:
	friend UWidgetStateSettings;

	//~ Begin UWidgetBinaryStateRegistration Interface.
	virtual void InitializeStaticBitfields() const override;
	//~ End UWidgetBinaryStateRegistration Interface
};

UCLASS(Transient)
class UMG_API UWidgetPressedStateRegistration : public UWidgetBinaryStateRegistration
{
	GENERATED_BODY()

public:

	/** Post-load initialized bit corresponding to this binary state */
	static inline FWidgetStateBitfield Bit;

	static const inline FName StateName = FName("Pressed");

	//~ Begin UWidgetBinaryStateRegistration Interface.
	virtual FName GetStateName() const override;
	virtual bool GetRegisteredWidgetState(const UWidget* InWidget) const override;
	//~ End UWidgetBinaryStateRegistration Interface

protected:
	friend UWidgetStateSettings;

	//~ Begin UWidgetBinaryStateRegistration Interface.
	virtual void InitializeStaticBitfields() const override;
	//~ End UWidgetBinaryStateRegistration Interface
};

UCLASS(Transient)
class UMG_API UWidgetDisabledStateRegistration : public UWidgetBinaryStateRegistration
{
	GENERATED_BODY()

public:

	/** Post-load initialized bit corresponding to this binary state */
	static inline FWidgetStateBitfield Bit;

	static const inline FName StateName = FName("Disabled");

	//~ Begin UWidgetBinaryStateRegistration Interface.
	virtual FName GetStateName() const override;
	virtual bool GetRegisteredWidgetState(const UWidget* InWidget) const override;
	//~ End UWidgetBinaryStateRegistration Interface

protected:
	friend UWidgetStateSettings;

	//~ Begin UWidgetBinaryStateRegistration Interface.
	virtual void InitializeStaticBitfields() const override;
	//~ End UWidgetBinaryStateRegistration Interface
};

UCLASS(Transient)
class UMG_API UWidgetSelectedStateRegistration : public UWidgetBinaryStateRegistration
{
	GENERATED_BODY()

public:

	/** Post-load initialized bit corresponding to this binary state */
	static inline FWidgetStateBitfield Bit;

	static const inline FName StateName = FName("Selected");

	//~ Begin UWidgetBinaryStateRegistration Interface.
	virtual FName GetStateName() const override;
	virtual bool GetRegisteredWidgetState(const UWidget* InWidget) const override;
	//~ End UWidgetBinaryStateRegistration Interface

protected:
	friend UWidgetStateSettings;

	//~ Begin UWidgetBinaryStateRegistration Interface.
	virtual void InitializeStaticBitfields() const override;
	//~ End UWidgetBinaryStateRegistration Interface
};

/**
 * Derive from to add a new Enum binary state
 */
UCLASS(Transient)
class UMG_API UWidgetEnumStateRegistration : public UObject
{
	GENERATED_BODY()

public:
	UWidgetEnumStateRegistration() = default;
	virtual ~UWidgetEnumStateRegistration() = default;

	/** Called once during WidgetStateSettings initialization to get this widget state's name */
	virtual FName GetStateName() const;

	/** Called on widget registration to determine if this widget uses the given state */
	virtual bool GetRegisteredWidgetUsesState(const UWidget* InWidget) const;

	/** Called on widget registration to correctly initialize widget state based on the current widget */
	virtual uint8 GetRegisteredWidgetState(const UWidget* InWidget) const;

protected:
	friend UWidgetStateSettings;

	/** Called to give CDO chance to initialize any static state bitfields that might be declared for convenience */
	virtual void InitializeStaticBitfields() const;
};
