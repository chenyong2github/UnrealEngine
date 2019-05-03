// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Serialization/StructuredArchive.h"
#include "Concepts/Insertable.h"
#include "Templates/Models.h"

class CORE_API FStructuredArchiveFromArchive
{
public:

	FStructuredArchiveFromArchive(FArchive& Ar)
		: Formatter(Ar)
		, StructuredArchive(Formatter)
		, Slot(StructuredArchive.Open())
	{
	}

	FStructuredArchive::FSlot GetSlot() { return Slot; }

private:

	FBinaryArchiveFormatter Formatter;
	FStructuredArchive StructuredArchive;
	FStructuredArchive::FSlot Slot;
};

/**
 * Adapter operator which allows a type to stream to an FArchive when it already supports streaming to an FStructuredArchive::FSlot.
 *
 * @param  Ar   The archive to read from or write to.
 * @param  Obj  The object to read or write.
 *
 * @return  A reference to the same archive as Ar.
 */
template <typename T>
typename TEnableIf<
	!TModels<CInsertable<FArchive&>, T>::Value && TModels<CInsertable<FStructuredArchive::FSlot>, T>::Value,
	FArchive&
>::Type operator<<(FArchive& Ar, T& Obj)
{
	FStructuredArchiveFromArchive ArAdapt(Ar);
	ArAdapt.GetSlot() << Obj;
	return Ar;
}
