// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/MemoryBase.h"

/*
	A simple stomp allocator with support for mobile platforms
*/
class FMallocStomp3 final : public FMalloc
{
public:
	enum EOptions
	{
		ENone,
		EForceIgnoreAlignment,	//Ignore alignment requirements to place the allocation exactly on a boundary of a page
		//TODO: Add options to protect from buffer under run and detecting dangling pointers
	};

	FMallocStomp3(EOptions Options);

	virtual void* Malloc(SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT) override;

	virtual void* Realloc(void* Original, SIZE_T Count, uint32 Alignment = DEFAULT_ALIGNMENT) override;

	virtual void Free(void* Original) override;

	/**
	* If possible determine the size of the memory allocated at the given address
	*
	* @param Original - Pointer to memory we are checking the size of
	* @param SizeOut - If possible, this value is set to the size of the passed in pointer
	* @return true if succeeded
	*/
	virtual bool GetAllocationSize(void* Original, SIZE_T& SizeOut) override;

	/**
	 * Gets descriptive name for logging purposes.
	 *
	 * @return pointer to human-readable malloc name
	 */
	virtual const TCHAR* GetDescriptiveName() override
	{
		return TEXT("Stomp3");
	}

	virtual bool IsInternallyThreadSafe() const override
	{
		return true;
	}

private:
	uint32 Options;
};