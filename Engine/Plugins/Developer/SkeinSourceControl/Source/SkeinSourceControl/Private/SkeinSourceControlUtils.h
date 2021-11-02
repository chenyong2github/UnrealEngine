// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SkeinSourceControlState.h"

namespace SkeinSourceControlUtils
{

/**
 * Determines the path to the Skein executable (if any)
 * @returns an empty string if the Skein binary couldn't be found
 */
FString FindSkeinBinaryPath();

/**
 * Determines the path to the Skein project root (if any)
 * @returns an empty string if the directory is not part of a Skein project
 */
FString FindSkeinProjectRoot(const FString& InPath);

/**
 * Finds out if the Skein environment is available.
 * In practice this checks if the Skein CLI application is installed at the expected location.
 * @returns true if the Skein environment can be used for source control operations.
 */
bool IsSkeinAvailable();

/**
  * Finds out if the given directory is part of a Skein project
  *
  * @param	 InPath				The directory to check
  * @param	 OutProjectRoot		The Skein project root directory found (if any)
  * @param	 OutProjectName		The Skein project name found (if any)
  * @returns true if a Skein project was found.
  */
bool IsSkeinProjectFound(const FString& InPath, FString& OutProjectRoot, FString& OutProjectName);

/**
 * Run a Skein command - output is a string TArray
 *
 * @param	InCommand			The Skein command - e.g. add
 * @param	InSkeinBinaryPath	The path to the Skein binary
 * @param	InSkeinProjectRoot	The Skein project root from where to run the command
 * @param	InParameters		The parameters to the Skein command
 * @param	InFiles				The files to be operated on
 * @param	OutResults			The results as an array per-line
 * @param	OutErrors			The errors as an array per-line
 * @returns true if the command succeeded and returned no errors
 */
bool RunCommand(const FString& InCommand, const FString& InSkeinBinaryPath, const FString& InSkeinProjectRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, TArray<FString>& OutResults, TArray<FString>& OutErrors);
	
/**
 * Run a Skein "status" command and parse it
 *
 * @param	InSkeinBinaryPath	The path to the Skein binary
 * @param	InSkeinProjectRoot	The Skein project root from where to run the command
 * @param	InFiles				The files to be operated on
 * @param	OutErrors			Any errors as an array per-line
 * @param   OutStates           The state of each of the input files
 * @returns true if the command succeeded and returned no errors
 */
bool RunUpdateStatus(const FString& InSkeinBinaryPath, const FString& InSkeinProjectRoot, const TArray<FString>& InFiles, TArray<FString>& OutErrors, TArray<FSkeinSourceControlState>& OutStates);
}