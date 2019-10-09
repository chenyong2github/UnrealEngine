// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SessionMonitorCommon.h"

/**
 * Gets the current process path
 * @param Filename	If specified, it will contain the name of the executable on return
 * @return The executable's directory
 */
std::string GetProcessPath(std::string* Filename = nullptr);

/**
 * Gets the extension of a file name
 * @param FullFilename	File name to get the extension from
 * @param Basename	If specified, it will contain the filename without extension
 */
std::string GetExtension(const std::string& FullFilename, std::string* Basename);

/**
* Given a full file path, it split it into folder and file
*/
std::pair<std::string, std::string> GetFolderAndFile(const std::string& FullFilename);

/**
 * Get current working directory
 */
std::string GetCWD();


/**
 * Canonicalizes a path (converts relative paths to absolute). It also converts '/' characters
 to '\' for consistency.
 @param Dst Where you'll get the resulting path
 @param Path The Path to canonicalize
 @Param Root
	Root path to use when Path is a relative path. If not specified it will assume the current
	working directory
 */
bool FullPath(std::string& Dst, const std::string& Path, std::string Root = "");


//! Gets the description of the last win32 error
std::string Win32ErrorMsg(const char* FuncName = nullptr);

//! Helper to make it easier to log a boost address
std::string AddrToString(const boost::asio::ip::tcp::endpoint& Addr);
