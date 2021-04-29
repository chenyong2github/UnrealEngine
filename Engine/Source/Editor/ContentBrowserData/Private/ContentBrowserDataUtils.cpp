// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserDataUtils.h"
#include "Containers/StringView.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

int32 ContentBrowserDataUtils::CalculateFolderDepthOfPath(const FStringView InPath)
{
	int32 Depth = 0;
	if (InPath.Len() > 1)
	{
		++Depth;

		// Ignore first and final characters
		const TCHAR* Current = InPath.GetData() + 1;
		const TCHAR* End = InPath.GetData() + InPath.Len() - 1;
		for (; Current != End; ++Current)
		{
			if (*Current == TEXT('/'))
			{
				++Depth;
			}
		}
	}

	return Depth;
}

bool ContentBrowserDataUtils::PathPassesAttributeFilter(const FStringView InPath, const int32 InAlreadyCheckedDepth, const EContentBrowserItemAttributeFilter InAttributeFilter)
{
	static const FString ProjectContentRootName = TEXT("Game");
	static const FString EngineContentRootName = TEXT("Engine");
	static const FString LocalizationFolderName = TEXT("L10N");
	static const FString ExternalActorsFolderName = TEXT("__ExternalActors__");
	static const FString DeveloperPathWithoutSlash = FPackageName::FilenameToLongPackageName(FPaths::GameDevelopersDir()).LeftChop(1);
	static int32 DevelopersFolderDepth = ContentBrowserDataUtils::CalculateFolderDepthOfPath(DeveloperPathWithoutSlash);
	static int32 MaxFolderDepthToCheck = FMath::Max(DevelopersFolderDepth, 2);

	static auto GetRootFolderNameFromPath = [](const FStringView InFullPath)
	{
		FStringView Result(InFullPath);

		// Remove '/' from start
		if (Result.StartsWith(TEXT('/')))
		{
			Result.RightChopInline(1);
		}

		// Return up until just before next '/'
		int32 FoundIndex = INDEX_NONE;
		if (Result.FindChar(TEXT('/'), FoundIndex))
		{
			Result.LeftInline(FoundIndex);
		}

		return Result;
	};

	if (InAlreadyCheckedDepth < MaxFolderDepthToCheck)
	{
		if (InAlreadyCheckedDepth < 2)
		{
			FStringView RootName = GetRootFolderNameFromPath(InPath);
			if (RootName.Len() == 0)
			{
				return true;
			}

			// if not already checked root folder
			if (InAlreadyCheckedDepth < 1)
			{
				const bool bIncludeProject = EnumHasAnyFlags(InAttributeFilter, EContentBrowserItemAttributeFilter::IncludeProject);
				const bool bIncludeEngine = EnumHasAnyFlags(InAttributeFilter, EContentBrowserItemAttributeFilter::IncludeEngine);
				const bool bIncludePlugins = EnumHasAnyFlags(InAttributeFilter, EContentBrowserItemAttributeFilter::IncludePlugins);
				if (!bIncludePlugins || !bIncludeEngine || !bIncludeProject)
				{
					if (RootName.Equals(ProjectContentRootName))
					{
						if (!bIncludeProject)
						{
							return false;
						}
					}
					else if (RootName.Equals(EngineContentRootName))
					{
						if (!bIncludeEngine)
						{
							return false;
						}
					}
					else
					{
						if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(RootName))
						{
							if (Plugin->IsEnabled() && Plugin->CanContainContent())
							{
								if (!bIncludePlugins)
								{
									return false;
								}

								const EPluginLoadedFrom PluginSource = Plugin->GetLoadedFrom();
								if (PluginSource == EPluginLoadedFrom::Engine)
								{
									if (!bIncludeEngine)
									{
										return false;
									}
								}
								else if (PluginSource == EPluginLoadedFrom::Project)
								{
									if (!bIncludeProject)
									{
										return false;
									}
								}
							}
						}
					}
				}
			}
			
			const FStringView AfterFirstFolder = InPath.RightChop(RootName.Len() + 2);
			if (AfterFirstFolder.StartsWith(ExternalActorsFolderName) && (AfterFirstFolder.Len() == ExternalActorsFolderName.Len() || AfterFirstFolder[ExternalActorsFolderName.Len()] == TEXT('/')))
			{
				return false;
			}

			if (!EnumHasAnyFlags(InAttributeFilter, EContentBrowserItemAttributeFilter::IncludeLocalized))
			{
				if (AfterFirstFolder.StartsWith(LocalizationFolderName) && (AfterFirstFolder.Len() == LocalizationFolderName.Len() || AfterFirstFolder[LocalizationFolderName.Len()] == TEXT('/')))
				{
					return false;
				}
			}
		}

		if (InAlreadyCheckedDepth < DevelopersFolderDepth && !EnumHasAnyFlags(InAttributeFilter, EContentBrowserItemAttributeFilter::IncludeDeveloper))
		{
			if (InPath.StartsWith(DeveloperPathWithoutSlash) && (InPath.Len() == DeveloperPathWithoutSlash.Len() || InPath[DeveloperPathWithoutSlash.Len()] == TEXT('/')))
			{
				return false;
			}
		}
	}

	return true;
}
