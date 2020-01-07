// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

class FStringView;

class CORE_API FPathViews
{
public:
	/**
	 * Returns the portion of the path after the last separator.
	 *
	 * Examples: (Using '/' but '\' is valid too.)
	 * "A/B/C.D" -> "C.D"
	 * "A/B/C"   -> "C"
	 * "A/B/"    -> ""
	 * "A"       -> "A"
	 *
	 * @return The portion of the path after the last separator.
	 */
	static FStringView GetCleanFilename(const FStringView& InPath);

	/**
	 * Returns the portion of the path after the last separator and before the last dot.
	 *
	 * Examples: (Using '/' but '\' is valid too.)
	 * "A/B/C.D" -> "C"
	 * "A/B/C"   -> "C"
	 * "A/B/"    -> ""
	 * "A"       -> "A"
	 *
	 * @return The portion of the path after the last separator and before the last dot.
	 */
	static FStringView GetBaseFilename(const FStringView& InPath);

	/**
	 * Returns the portion of the path before the last dot.
	 *
	 * Examples: (Using '/' but '\' is valid too.)
	 * "A/B/C.D" -> "A/B/C"
	 * "A/B/C"   -> "A/B/C"
	 * "A/B/"    -> "A/B/"
	 * "A"       -> "A"
	 *
	 * @return The portion of the path before the last dot.
	 */
	static FStringView GetBaseFilenameWithPath(const FStringView& InPath);

	/** Returns the portion of the path before the last dot and optionally after the last separator. */
	UE_DEPRECATED(4.25, "FPathViews::GetBaseFilename(InPath, bRemovePath) has been superseded by "
		"FPathViews::GetBaseFilename(InPath) and FPathViews::GetBaseFilenameWithPath(InPath).")
	static FStringView GetBaseFilename(const FStringView& InPath, bool bRemovePath);

	/**
	 * Returns the portion of the path before the last separator.
	 *
	 * Examples: (Using '/' but '\' is valid too.)
	 * "A/B/C.D" -> "A/B"
	 * "A/B/C"   -> "A/B"
	 * "A/B/"    -> "A/B"
	 * "A"       -> ""
	 *
	 * @return The portion of the path before the last separator.
	 */
	static FStringView GetPath(const FStringView& InPath);

	/**
	 * Returns the portion of the path after the last dot following the last separator, optionally including the dot.
	 *
	 * Examples: (Using '/' but '\' is valid too.)
	 * "A/B.C.D" -> "D" (bIncludeDot=false) or ".D" (bIncludeDot=true)
	 * "A/B/C.D" -> "D" (bIncludeDot=false) or ".D" (bIncludeDot=true)
	 * "A/B/.D"  -> "D" (bIncludeDot=false) or ".D" (bIncludeDot=true)
	 * ".D"      -> "D" (bIncludeDot=false) or ".D" (bIncludeDot=true)
	 * "A/B/C"   -> ""
	 * "A.B/C"   -> ""
	 * "A.B/"    -> ""
	 * "A"       -> ""
	 *
	 * @param bIncludeDot Whether to include the leading dot in the returned view.
	 *
	 * @return The portion of the path after the last dot following the last separator, optionally including the dot.
	 */
	static FStringView GetExtension(const FStringView& InPath, bool bIncludeDot=false);

	/**
	 * Returns the last non-empty path component.
	 *
	 * Examples: (Using '/' but '\' is valid too.)
	 * "A/B/C.D" -> "C.D"
	 * "A/B/C"   -> "C"
	 * "A/B/"    -> "B"
	 * "A"       -> "A"
	 *
	 * @return The last non-empty path component.
	 */
	static FStringView GetPathLeaf(const FStringView& InPath);

	/**
	 * Splits a path into three parts, any of which may be empty: the path, the clean name, and the extension.
	 *
	 * Examples: (Using '/' but '\' is valid too.)
	 * "A/B/C.D" -> ("A/B", "C",   "D")
	 * "A/B/C"   -> ("A/B", "C",   "")
	 * "A/B/"    -> ("A/B", "",    "")
	 * "A/B/.D"  -> ("A/B", "",    "D")
	 * "A/B.C.D" -> ("A",   "B.C", "D")
	 * "A"       -> ("",    "A",   "")
	 * "A.D"     -> ("",    "A",   "D")
	 * ".D"      -> ("",    "",    "D")
	 *
	 * @param OutPath [out] Receives the path portion of the input string, excluding the trailing separator.
	 * @param OutName [out] Receives the name portion of the input string.
	 * @param OutExt  [out] Receives the extension portion of the input string, excluding the dot.
	 */
	static void Split(const FStringView& InPath, FStringView& OutPath, FStringView& OutName, FStringView& OutExt);
};
