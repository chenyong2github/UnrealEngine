// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "IO/IoHash.h"

namespace UE { class FPackageTrailer; }

namespace UE::Virtualization
{

/** Parse all of the active mount points and find all packages */
TArray<FString> FindAllPackages();

/** Finds all of the packages under a given directory including its subdirectories */
TArray<FString> FindPackagesInDirectory(const FString& DirectoryToSearch);

/**
 * Finds all of the packages under a the directory given by the provided command line.
 * If no commandline switch can be found then the function will return all available 
 * packages.
 * Valid commandline switches:
 * '-PackageDir=...'
 * '-PackageFolder=...'
 * 
 * @param CmdlineParams A string containing the command line
 * @return An array with the file path for each package found
 */
TArray<FString> DiscoverPackages(const FString& CmdlineParams);

/** Returns a combined list of unique virtualized payload ids from the given list of packages */
TArray<FIoHash> FindVirtualizedPayloads(const TArray<FString>& PackageNames);

/** 
 * Load and parse the package trailers for the given packages. We return a map of all of the packages that contain
 * virtualized payloads and a unique set of all the virtualized payloads referenced. Note that packages can reference
 * the same payload if they re-use assets.
 * 
 * @param PackagePaths	A list of packages to check
 * @param OutPackages	A map of package paths to package trailers
 * @param OutPayloads	A unique set of all the virtualized payloads referenced by the packages
 */
void FindVirtualizedPayloadsAndTrailers(const TArray<FString>& PackagePaths, TMap<FString, UE::FPackageTrailer>& OutPackages, TSet<FIoHash>& OutPayloads);

} //namespace UE::Virtualization
