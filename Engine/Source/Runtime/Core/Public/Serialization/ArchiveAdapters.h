// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Concepts/Insertable.h"
#include "Templates/Models.h"
//#include "ArchiveFromStructuredArchive.h"
//#include "StructuredArchiveFromArchive.h"

/**
 * Adapter operator which allows a type to stream to an FStructuredArchive::FSlot when it already supports streaming to an FArchive.
 *
 * @param  Slot  The slot to read from or write to.
 * @param  Obj   The object to read or write.
 */
template <typename T>
typename TEnableIf<
	TModels<CInsertable<FArchive&>, T>::Value &&
	!TModels<CInsertable<FStructuredArchive::FSlot>, T>::Value
>::Type operator<<(FStructuredArchive::FSlot Slot, T& Obj)
{
#if WITH_TEXT_ARCHIVE_SUPPORT
	FArchiveFromStructuredArchive Ar(Slot);
#else
	FArchive& Ar = Slot.GetUnderlyingArchive();
#endif
	Ar << Obj;
}

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