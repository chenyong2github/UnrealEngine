// Copyright Epic Games, Inc. All Rights Reserved.

#include "ReferenceViewer/ReferenceViewerSettings.h"

bool UReferenceViewerSettings::IsSearchDepthLimited() const
{
	return bLimitSearchDepth;
}

bool UReferenceViewerSettings::IsSearchBreadthLimited() const
{
	return bLimitSearchBreadth;
}

bool UReferenceViewerSettings::IsShowSoftReferences() const
{
	return bIsShowSoftReferences;
}

bool UReferenceViewerSettings::IsShowHardReferences() const
{
	return bIsShowHardReferences;
}

bool UReferenceViewerSettings::IsShowFilteredPackagesOnly() const
{
	return bIsShowFilteredPackagesOnly;
}

bool UReferenceViewerSettings::IsCompactMode() const
{
	return bIsCompactMode;
}

bool UReferenceViewerSettings::IsShowDuplicates() const
{
	return bIsShowDuplicates;
}

bool UReferenceViewerSettings::IsShowEditorOnlyReferences() const
{
	return bIsShowEditorOnlyReferences;
}

bool UReferenceViewerSettings::IsShowManagementReferences() const
{
	return bIsShowManagementReferences;
}

bool UReferenceViewerSettings::IsShowSearchableNames() const
{
	return bIsShowSearchableNames;
}

bool UReferenceViewerSettings::IsShowNativePackages() const
{
	return bIsShowNativePackages;
}

bool UReferenceViewerSettings::IsShowReferencers() const
{
	return bIsShowReferencers;
}

bool UReferenceViewerSettings::IsShowDependencies() const
{
	return bIsShowDependencies;
}

void UReferenceViewerSettings::SetSearchDepthLimitEnabled(bool newEnabled)
{
	bLimitSearchDepth = newEnabled;
	SaveConfig();
}

void UReferenceViewerSettings::SetSearchBreadthLimitEnabled(bool newEnabled)
{
	bLimitSearchBreadth = newEnabled;
	SaveConfig();
}

void UReferenceViewerSettings::SetShowSoftReferencesEnabled(bool newEnabled)
{
	bIsShowSoftReferences = newEnabled;
	SaveConfig();
}

void UReferenceViewerSettings::SetShowHardReferencesEnabled(bool newEnabled)
{
	bIsShowHardReferences = newEnabled;
	SaveConfig();
}

void UReferenceViewerSettings::SetShowFilteredPackagesOnlyEnabled(bool newEnabled)
{
	bIsShowFilteredPackagesOnly = newEnabled;
	SaveConfig();
}

void UReferenceViewerSettings::SetCompactModeEnabled(bool newEnabled)
{
	bIsCompactMode = newEnabled;
	SaveConfig();
}

void UReferenceViewerSettings::SetShowDuplicatesEnabled(bool newEnabled)
{
	bIsShowDuplicates = newEnabled;
	SaveConfig();
}

void UReferenceViewerSettings::SetShowEditorOnlyReferencesEnabled(bool newEnabled)
{
	bIsShowEditorOnlyReferences = newEnabled;
	SaveConfig();
}

void UReferenceViewerSettings::SetShowManagementReferencesEnabled(bool newEnabled)
{
	bIsShowManagementReferences = newEnabled;
	SaveConfig();
}

void UReferenceViewerSettings::SetShowSearchableNames(bool newEnabled)
{
	bIsShowSearchableNames = newEnabled;
	SaveConfig();
}

void UReferenceViewerSettings::SetShowNativePackages(bool newEnabled)
{
	bIsShowNativePackages = newEnabled;
	SaveConfig();
}

void UReferenceViewerSettings::SetShowReferencers(const bool newEnabled)
{
	bIsShowReferencers = newEnabled;
	SaveConfig();
}

void UReferenceViewerSettings::SetShowDependencies(const bool newEnabled)
{
	bIsShowDependencies = newEnabled;
	SaveConfig();
}

int32 UReferenceViewerSettings::GetSearchDependencyDepthLimit() const
{
	return MaxSearchDependencyDepth;
}

void UReferenceViewerSettings::SetSearchDependencyDepthLimit(int32 NewDepthLimit)
{
	MaxSearchDependencyDepth = FMath::Max(NewDepthLimit, 0);
	SaveConfig();
}

int32 UReferenceViewerSettings::GetSearchReferencerDepthLimit() const
{
	return MaxSearchReferencerDepth;
}

void UReferenceViewerSettings::SetSearchReferencerDepthLimit(int32 NewDepthLimit)
{
	MaxSearchReferencerDepth = FMath::Max(NewDepthLimit, 0);
	SaveConfig();
}

int32 UReferenceViewerSettings::GetSearchBreadthLimit() const
{
	return MaxSearchBreadth;
}

void UReferenceViewerSettings::SetSearchBreadthLimit(int32 NewBreadthLimit)
{
	MaxSearchBreadth = FMath::Max(NewBreadthLimit, 0);
	SaveConfig();
}

bool UReferenceViewerSettings::GetEnableCollectionFilter() const
{
	return bEnableCollectionFilter;
}

void UReferenceViewerSettings::SetEnableCollectionFilter(bool bEnabled)
{
	bEnableCollectionFilter = bEnabled;
	SaveConfig();
}

bool UReferenceViewerSettings::IsShowPath() const
{
	return bIsShowPath;
}

void UReferenceViewerSettings::SetShowPathEnabled(bool bEnabled)
{
	bIsShowPath = bEnabled;
	SaveConfig();
}

