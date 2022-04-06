// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Formatters
class FBinaryArchiveFormatter;
class FStructuredArchiveFormatter;

// Archives
class FStructuredArchive;
class FStructuredArchiveChildReader;

// Slots
class FStructuredArchiveSlot;
class FStructuredArchiveRecord;
class FStructuredArchiveArray;
class FStructuredArchiveStream;
class FStructuredArchiveMap;

/** Typedef for which formatter type to support */
#if WITH_TEXT_ARCHIVE_SUPPORT
	using FArchiveFormatterType = FStructuredArchiveFormatter;
#else
	using FArchiveFormatterType = FBinaryArchiveFormatter;
#endif
