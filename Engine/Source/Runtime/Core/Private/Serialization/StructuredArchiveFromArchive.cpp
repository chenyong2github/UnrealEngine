// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Serialization/StructuredArchive.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"

struct FStructuredArchiveFromArchive::FImpl
{
	explicit FImpl(FArchive& Ar)
		: Formatter(Ar)
		, StructuredArchive(Formatter)
		, Slot(StructuredArchive.Open())
	{
	}

	FBinaryArchiveFormatter Formatter;
	FStructuredArchive StructuredArchive;
	FStructuredArchive::FSlot Slot;
};

FStructuredArchiveFromArchive::FStructuredArchiveFromArchive(FArchive& Ar)
	: Pimpl(Ar)
{
}

FStructuredArchiveFromArchive::~FStructuredArchiveFromArchive() = default;

FStructuredArchive::FSlot FStructuredArchiveFromArchive::GetSlot()
{
	return Pimpl->Slot;
}
