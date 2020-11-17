// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Memory/MemoryView.h"
#include "Misc/EnumClassFlags.h"
#include "Serialization/CompactBinary.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Flags for validating compact binary data. */
enum class ECbValidateMode : uint32
{
	/**
	 * Validate that the value can be read and stays inside the bounds of the memory view.
	 *
	 * This is the minimum level of validation required to be able to safely read a field, array,
	 * or object without the risk of crashing or reading out of bounds.
	 */
	Default                 = 0,

	/**
	 * Validate that object fields have unique non-empty names and array fields have no names.
	 *
	 * Name validation failures typically do not inhibit reading the input, but duplicated fields
	 * cannot be looked up by name other than the first, and converting to other data formats can
	 * fail in the presence of naming issues.
	 */
	Names                   = 1 << 0,

	/**
	 * Validate that fields are serialized in the canonical format.
	 *
	 * Format validation failures typically do not inhibit reading the input. Values that fail in
	 * this mode require more memory than in the canonical format, and comparisons of such values
	 * for equality are not reliable. Examples of failures include uniform arrays or objects that
	 * were not encoded uniformly, variable-length integers that could be encoded in fewer bytes,
	 * or 64-bit floats that could be encoded in 32 bits without loss of precision.
	 */
	Format                  = 1 << 1,

	/**
	 * Validate that there is no padding after the value before the end of the memory view.
	 *
	 * Padding validation failures have no impact on the ability to read the input, but are using
	 * more memory than necessary.
	 */
	Padding                 = 1 << 2,

	/** Perform all validation described above. */
	All                     = Default | Names | Format | Padding,
};

ENUM_CLASS_FLAGS(ECbValidateMode);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Flags for compact binary validation errors. Multiple flags may be combined. */
enum class ECbValidateError : uint32
{
	/** The input had no validation errors. */
	None                    = 0,

	// Mode: Default

	/** The input cannot be read without reading out of bounds. */
	OutOfBounds             = 1 << 0,
	/** The input has a field with an unrecognized or invalid type. */
	InvalidType             = 1 << 1,

	// Mode: Names

	/** An object had more than one field with the same name. */
	DuplicateName           = 1 << 2,
	/** An object had a field with no name. */
	MissingName             = 1 << 3,
	/** An array field had a name. */
	ArrayName               = 1 << 4,

	// Mode: Format

	/** A name or string payload is not valid UTF-8. */
	InvalidString           = 1 << 5,
	/** A size or integer payload can be encoded in fewer bytes. */
	InvalidInteger          = 1 << 6,
	/** A float64 payload can be encoded as a float32 without loss of precision. */
	InvalidFloat            = 1 << 7,
	/** An object has the same type for every field but is not uniform. */
	NonUniformObject        = 1 << 8,
	/** An array has the same type for every field and non-empty payloads but is not uniform. */
	NonUniformArray         = 1 << 9,

	// Mode: Padding

	/** A value did not use the entire memory view given for validation. */
	Padding                 = 1 << 10,
};

ENUM_CLASS_FLAGS(ECbValidateError);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Validate the compact binary data for one field in the view as specified by the mode flags.
 *
 * Only one top-level field is processed from the view, and validation recurses into any array or
 * object within that field. To validate multiple consecutive top-level fields, call the function
 * once for each top-level field. If the given view might contain multiple top-level fields, then
 * either exclude the Padding flag from the Mode or use MeasureCompactBinary to break up the view
 * into its constituent fields before validating.
 *
 * @param View A memory view containing at least one top-level field.
 * @param Mode A combination of the flags for the types of validation to perform.
 * @param Type HasFieldType means that View contains the type. Otherwise, use the given type.
 * @return None on success, otherwise the flags for the types of errors that were detected.
 */
CORE_API ECbValidateError ValidateCompactBinary(FConstMemoryView View, ECbValidateMode Mode, ECbFieldType Type = ECbFieldType::HasFieldType);

/**
 * Validate the compact binary data for every field in the view as specified by the mode flags.
 *
 * This function expects the entire view to contain fields. Any trailing region of the view which
 * does not contain a valid field will produce an OutOfBounds or InvalidType error instead of the
 * Padding error that would be produced by the single field validation function.
 *
 * \see \ref ValidateCompactBinary for more detail.
 */
CORE_API ECbValidateError ValidateCompactBinaryRange(FConstMemoryView View, ECbValidateMode Mode);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
