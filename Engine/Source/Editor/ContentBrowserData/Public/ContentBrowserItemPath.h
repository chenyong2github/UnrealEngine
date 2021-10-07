// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ContentBrowserDataSubsystem.h"

/**
 * Hold multiple versions of a path as FNames
 * 
 * Path conversion each time Set is called
 */
class CONTENTBROWSERDATA_API FContentBrowserItemPath
{
public:
	FContentBrowserItemPath();

	FContentBrowserItemPath(const FName InVirtualPath, const FName InInternalPath)
		: VirtualPath(InVirtualPath)
		, InternalPath(InInternalPath)
	{
		check(!VirtualPath.IsNone());
		check(!InternalPath.IsNone());
	}

	FContentBrowserItemPath(const FStringView InPath, const EContentBrowserPathType InPathType);
	FContentBrowserItemPath(const TCHAR* InPath, const EContentBrowserPathType InPathType);
	FContentBrowserItemPath(const FName InPath, const EContentBrowserPathType InPathType);

	bool operator==(const FContentBrowserItemPath& Other) const 
	{ 
		return VirtualPath == Other.VirtualPath && InternalPath == Other.InternalPath;
	}

	/**
	 * Set the path being stored
	 */
	void SetPathFromString(const FStringView InPath, const EContentBrowserPathType InPathType);
	void SetPathFromName(const FName InPath, const EContentBrowserPathType InPathType);

	/**
	 * Returns virtual path as FName (eg, "/All/Plugins/PluginA/MyFile").
	 */
	FName GetVirtualPathName() const;

	/**
	 * Returns internal path if there is one (eg,. "/PluginA/MyFile").
	 */
	FName GetInternalPathName() const;

	/**
	 * Returns virtual path as newly allocated FString (eg, "/All/Plugins/PluginA/MyFile").
	 */
	FString GetVirtualPathString() const;

	/**
	 * Returns internal path as newly allocated FString if there is one or empty FString (eg,. "/PluginA/MyFile").
	 */
	FString GetInternalPathString() const;

	/**
	 * Returns true if there is an internal path
	 */
	bool HasInternalPath() const;

private:
	/** Path as virtual (eg, "/All/Plugins/PluginA/MyFile") */
	FName VirtualPath;

	/** Path as internal (eg,. "/PluginA/MyFile") */
	FName InternalPath;
};
