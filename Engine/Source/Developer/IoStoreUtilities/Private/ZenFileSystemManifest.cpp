// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZenFileSystemManifest.h"
#include "Interfaces/ITargetPlatform.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"

const FZenFileSystemManifest::FManifestEntry FZenFileSystemManifest::InvalidEntry;

FZenFileSystemManifest::FZenFileSystemManifest(const ITargetPlatform& InTargetPlatform, FString InCookDirectory)
	: TargetPlatform(InTargetPlatform)
	, CookDirectory(MoveTemp(InCookDirectory))
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	ServerRoot = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*FPaths::RootDir());
	FPaths::NormalizeDirectoryName(ServerRoot);
}

void FZenFileSystemManifest::Generate()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GenerateStorageServerFileSystemManifest);

	struct FFileFilter
	{
		bool IncludeDirectory(const TCHAR* Path) const
		{
			if (FCString::Stristr(Path, TEXT("/Intermediate")))
			{
				return false;
			}
			if (FCString::Stristr(Path, TEXT("/Binaries")))
			{
				return false;
			}
			if (FCString::Stristr(Path, TEXT("/Source")))
			{
				return false;
			}
			return true;
		}

		bool IncludeFile(const TCHAR* Path) const
		{
			if (FCString::Stristr(Path, TEXT("/Content/")))
			{
				const TCHAR* Extension = FCString::Strrchr(Path, '.');
				if (Extension)
				{
					if (!FCString::Stricmp(Extension, TEXT(".uasset")) ||
						!FCString::Stricmp(Extension, TEXT(".umap")) ||
						!FCString::Stricmp(Extension, TEXT(".uexp")) ||
						!FCString::Stricmp(Extension, TEXT(".ubulk")))
					{
						return false;
					}
				}
			}
			return true;
		}
	} Filter;
	
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	FString RootDir = FPaths::RootDir();
	FString EngineDir = FPaths::EngineDir();
	FPaths::NormalizeDirectoryName(EngineDir);
	FString ProjectDir = FPaths::ProjectDir();
	FPaths::NormalizeDirectoryName(ProjectDir);

	auto AddFilesFromDirectory =
		[this, &PlatformFile, &RootDir]
		(const FString& ClientDirectory, const FString& LocalDirectory, bool bIncludeSubdirs, FFileFilter* Filter = nullptr)
		{
			FString ServerRelativeDirectory = LocalDirectory;
			FPaths::MakePathRelativeTo(ServerRelativeDirectory, *RootDir);
			ServerRelativeDirectory = TEXT("/") + ServerRelativeDirectory;

			TArray<FString> DirectoriesToVisit;
			auto VisitorFunc =
				[this, &DirectoriesToVisit, &RootDir, &ClientDirectory, &LocalDirectory, &ServerRelativeDirectory, bIncludeSubdirs, Filter]
				(const TCHAR* InnerFileNameOrDirectory, bool bIsDirectory)
				{
					if (Filter)
					{
						if ((bIsDirectory && !Filter->IncludeDirectory(InnerFileNameOrDirectory)) ||
							(!bIsDirectory && !Filter->IncludeFile(InnerFileNameOrDirectory)))
						{
							return true;
						}
					}
					if (bIsDirectory)
					{
						if (bIncludeSubdirs)
						{
							DirectoriesToVisit.Add(InnerFileNameOrDirectory);
						}
					}
					else
					{
						FStringView RelativePath = InnerFileNameOrDirectory;
						RelativePath.RightChopInline(LocalDirectory.Len() + 1);

						AddManifestEntry(
							FPaths::Combine(ServerRelativeDirectory, RelativePath.GetData()),
							FPaths::Combine(ClientDirectory, RelativePath.GetData()));
					}

					return true;
				};

			DirectoriesToVisit.Push(LocalDirectory);
			while (!DirectoriesToVisit.IsEmpty())
			{
				PlatformFile.IterateDirectory(*DirectoriesToVisit.Pop(), VisitorFunc);
			}
		};

	AddFilesFromDirectory(TEXT("/{engine}"), FPaths::Combine(CookDirectory, TEXT("Engine")), true);
	AddFilesFromDirectory(TEXT("/{project}"), FPaths::Combine(CookDirectory, FApp::GetProjectName()), true);
	AddFilesFromDirectory(TEXT("/{project}"), ProjectDir, false);
	AddFilesFromDirectory(TEXT("/{engine}/Config"), FPaths::Combine(EngineDir, TEXT("Config")), true);
	AddFilesFromDirectory(TEXT("/{project}/Config"), FPaths::Combine(ProjectDir, TEXT("Config")), true);
	AddFilesFromDirectory(TEXT("/{engine}/Plugins"), FPaths::Combine(EngineDir, TEXT("Plugins")), true, &Filter);
	AddFilesFromDirectory(TEXT("/{project}/Plugins"), FPaths::Combine(ProjectDir, TEXT("Plugins")), true, &Filter);
	AddFilesFromDirectory(TEXT("/{engine}/Restricted"), FPaths::Combine(EngineDir, TEXT("Restricted")), true, &Filter);
	AddFilesFromDirectory(TEXT("/{project}/Restricted"), FPaths::Combine(ProjectDir, TEXT("Restricted")), true, &Filter);
	AddFilesFromDirectory(TEXT("/{engine}/Content/Internationalization"), FPaths::Combine(EngineDir, TEXT("Content"), TEXT("Internationalization")), true);
	AddFilesFromDirectory(TEXT("/{engine}/Content/Localization"), FPaths::Combine(EngineDir, TEXT("Content"), TEXT("Localization")), true);
	AddFilesFromDirectory(TEXT("/{project}/Content/Localization"), FPaths::Combine(ProjectDir, TEXT("Content"), TEXT("Localization")), true);
	AddFilesFromDirectory(TEXT("/{engine}/Content/Slate"), FPaths::Combine(EngineDir, TEXT("Content"), TEXT("Slate")), true);
	AddFilesFromDirectory(TEXT("/{project}/Content/Slate"), FPaths::Combine(ProjectDir, TEXT("Content"), TEXT("Slate")), true);
	AddFilesFromDirectory(TEXT("/{engine}/Content/Movies"), FPaths::Combine(EngineDir, TEXT("Content"), TEXT("Movies")), true);
	AddFilesFromDirectory(TEXT("/{project}/Content/Movies"), FPaths::Combine(ProjectDir, TEXT("Content"), TEXT("Movies")), true);

	const FDataDrivenPlatformInfo& Info = FDataDrivenPlatformInfoRegistry::GetPlatformInfo(TargetPlatform.IniPlatformName());

	TArray<FString> PlatformDirectories;
	PlatformDirectories.Reserve(Info.IniParentChain.Num() + 1);
	PlatformDirectories.Add(TargetPlatform.IniPlatformName());
	PlatformDirectories.Append(Info.IniParentChain);

	for (const FString& PlatformDirectory : PlatformDirectories)
	{
		AddFilesFromDirectory(FPaths::Combine(TEXT("/{engine}/Platforms"), PlatformDirectory), FPaths::Combine(EngineDir, TEXT("Platforms"), PlatformDirectory), true, &Filter);
		AddFilesFromDirectory(FPaths::Combine(TEXT("/{project}/Platforms"), PlatformDirectory), FPaths::Combine(ProjectDir, TEXT("Platforms"), PlatformDirectory), true, &Filter);
	}
}

const FZenFileSystemManifest::FManifestEntry& FZenFileSystemManifest::AddFile(const FString& Filename)
{
	FString CookedEngineDirectory = FPaths::Combine(CookDirectory, TEXT("Engine"));

	auto AddEntry = [this, &Filename](const FString& ClientDirectory, const FString& LocalDirectory) -> const FManifestEntry&
	{
		FStringView RelativePath = Filename;
		RelativePath.RightChopInline(LocalDirectory.Len() + 1);

		FString ServerRelativeDirectory = LocalDirectory;
		FPaths::MakePathRelativeTo(ServerRelativeDirectory, *FPaths::RootDir());
		ServerRelativeDirectory = TEXT("/") + ServerRelativeDirectory;

		return AddManifestEntry(
			FPaths::Combine(ServerRelativeDirectory, RelativePath.GetData()),
			FPaths::Combine(ClientDirectory, RelativePath.GetData()));
	};

	if (Filename.StartsWith(CookedEngineDirectory))
	{
		return AddEntry(TEXT("/{engine}"), CookedEngineDirectory);
	}

	FString CookedProjectDirectory = FPaths::Combine(CookDirectory, FApp::GetProjectName());
	if (Filename.StartsWith(CookedProjectDirectory))
	{
		return AddEntry(TEXT("/{project}"), CookedProjectDirectory);
	}

	return InvalidEntry;
}

const FZenFileSystemManifest::FManifestEntry& FZenFileSystemManifest::AddManifestEntry(FString ServerPath, FString ClientPath)
{
	ServerPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	ClientPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	int32& EntryIndex = ServerPathToEntry.FindOrAdd(ServerPath, INDEX_NONE);

	if (EntryIndex != INDEX_NONE)
	{
		return Entries[EntryIndex];
	}

	EntryIndex = Entries.Num();

	FManifestEntry Entry;
	Entry.ServerPath = MoveTemp(ServerPath);
	Entry.ClientPath = MoveTemp(ClientPath);
	Entry.FileId = static_cast<uint32>(EntryIndex + 1);

	Entries.Add(MoveTemp(Entry));

	return Entries[EntryIndex];
}

void FZenFileSystemManifest::Save(const TCHAR* Filename)
{
	check(Filename);

	TArray<FString> CsvLines;
	CsvLines.Add(FString::Printf(TEXT(";ServerRoot=%s, Platform=%s, CookDirectory=%s"), *ServerRoot, *TargetPlatform.PlatformName(), *CookDirectory));
	CsvLines.Add(TEXT("FileId, ServerPath, ClientPath"));

	for (const FManifestEntry& Entry : Entries)
	{
		CsvLines.Add(FString::Printf(TEXT("%d, %s, %s"), Entry.FileId, *Entry.ServerPath, *Entry.ClientPath));
	}

	FFileHelper::SaveStringArrayToFile(CsvLines, Filename);
}
