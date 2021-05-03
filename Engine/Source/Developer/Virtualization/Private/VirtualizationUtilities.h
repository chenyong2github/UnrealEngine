// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"
#include "Containers/UnrealString.h"

namespace UE::Virtualization
{
	class FPayloadId;
} // namespace UE::Virtualization

namespace UE::Virtualization::Utils
{

/** 
 * Converts a FPayloadId into a file path.
 *
 * This utility will take a FPayloadId and return a file path that is
 * 3 directories deep. The first six characters of the id will be used to
 * create the directory names, with each directory using two characters.
 * The remaining thirty four characters will be used as the file name.
 * Lastly the extension '.payload' will be applied to complete the path/
 * Example: FPayloadId 0139d6d5d477e32dfd2abd3c5bc8ea8507e8eef8 becomes
 *			01/39/d6/d5d477e32dfd2abd3c5bc8ea8507e8eef8.payload'
 * 
 * @param	Id The payload identifier used to create the file path.
 * @param	OutPath Will be reset and then assigned the resulting file path.
 *			The string builder must have a capacity of at least 52 characters 
			to avoid reallocation.
 */
void PayloadIdToPath(const FPayloadId& Id, FStringBuilderBase& OutPath);

/** 
 * Converts a FPayloadId into a file path.
 * 
 * See above for further details
 * 
 * @param	Id The payload identifier used to create the file path.
 * @return	The resulting file path.
 */
FString PayloadIdToPath(const FPayloadId& Id);

} // namespace UE::Virtualization::Utils
