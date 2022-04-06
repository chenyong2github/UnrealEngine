// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/StructuredArchiveChildReader.h"

#if WITH_TEXT_ARCHIVE_SUPPORT

FStructuredArchiveChildReader::FStructuredArchiveChildReader(FStructuredArchiveSlot InSlot)
	: OwnedFormatter(nullptr)
	, Archive(nullptr)
{
	FStructuredArchiveFormatter* Formatter = &InSlot.Ar.Formatter;
	if (InSlot.GetUnderlyingArchive().IsTextFormat())
	{
		Formatter = OwnedFormatter = InSlot.Ar.Formatter.CreateSubtreeReader();
	}

	Archive = new FStructuredArchive(*Formatter);
	Root.Emplace(Archive->Open());
	InSlot.EnterRecord();
}

FStructuredArchiveChildReader::~FStructuredArchiveChildReader()
{
	Root.Reset();
	Archive->Close();
	delete Archive;
	Archive = nullptr;

	// If this is a text archive, we'll have created a subtree reader that our contained archive is using as 
	// its formatter. We need to clean it up now.
	if (OwnedFormatter)
	{
		delete OwnedFormatter;
		OwnedFormatter = nullptr;
	}
}

#endif
