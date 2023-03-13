// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "IAnimNextInterface.generated.h"

class IAnimNextInterface;

namespace UE::AnimNext
{
struct FParam;
struct FParamTypeHandle;
struct FContext;
}


UINTERFACE(BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class ANIMNEXTINTERFACE_API UAnimNextInterface : public UInterface
{
	GENERATED_BODY()
};

// Empty anim interface to support 'any' type
class ANIMNEXTINTERFACE_API IAnimNextInterface
{
	GENERATED_BODY()

public:
	/**
	 * Gets data using context to retrieve input parameters and result
	 * @return false if type are incompatible, or if nested calls fail, otherwise true
	 */
	bool GetData(const UE::AnimNext::FContext& Context) const;

	/**
	 * Gets data and stores the value in OutResult, checking whether types are compatible
	* @return false if nested calls fail, otherwise true
	*/
	bool GetDataChecked(const UE::AnimNext::FContext& Context) const;

	/**
	 * Gets data and stores the value in OutResult.
	 * @return false if type are incompatible, or if nested calls fail, otherwise true
	 */
	bool GetData(const UE::AnimNext::FContext& Context, UE::AnimNext::FParam& OutResult) const;

	/**
	 * Gets data and stores the value in OutResult, checking whether types are compatible
	 * @return false if nested calls fail, otherwise true
	 */
	bool GetDataChecked(const UE::AnimNext::FContext& Context, UE::AnimNext::FParam& OutResult) const;

	/** Get the handle of the return type */
	UE::AnimNext::FParamTypeHandle GetReturnTypeHandle() const;

private:
	/** Get data if the types are compatible */
	bool GetDataIfCompatibleInternal(const UE::AnimNext::FContext& InContext) const;
	
	/** Get a value from an anim interface with no dynamic or static type checking. Internal use only. */
	bool GetDataRawInternal(const UE::AnimNext::FContext& InContext) const;

protected:
	/** Get the handle of the return type */
	virtual UE::AnimNext::FParamTypeHandle GetReturnTypeHandleImpl() const = 0;

	/** Get the value for this interface. @return true if successful, false if unsuccessful. */
	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const = 0;
};

