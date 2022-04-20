// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace UnrealBuildBase
{
	public class PluginsBase
	{
		/// <summary>
		/// Cache of plugin filenames under each directory
		/// </summary>
		static Dictionary<DirectoryReference, List<FileReference>> PluginFileCache = new Dictionary<DirectoryReference, List<FileReference>>();

		/// <summary>
		/// Invalidate cached plugin data so that we can pickup new things
		/// Warning: Will make subsequent plugin lookups and directory scans slow until the caches are repopulated
		/// </summary>
		public static void InvalidateCache_SLOW()
		{
			PluginFileCache = new Dictionary<DirectoryReference, List<FileReference>>();
			DirectoryItem.ResetAllCachedInfo_SLOW();
		}

		/// <summary>
		/// Enumerates all the plugin files available to the given project
		/// </summary>
		/// <param name="ProjectFile">Path to the project file</param>
		/// <returns>List of project files</returns>
		public static IEnumerable<FileReference> EnumeratePlugins(FileReference? ProjectFile)
		{
			List<DirectoryReference> BaseDirs = new List<DirectoryReference>();
			BaseDirs.AddRange(Unreal.GetExtensionDirs(Unreal.EngineDirectory, "Plugins"));
			if(ProjectFile != null)
			{
				BaseDirs.AddRange(Unreal.GetExtensionDirs(ProjectFile.Directory, "Plugins"));
				BaseDirs.AddRange(Unreal.GetExtensionDirs(ProjectFile.Directory, "Mods"));
			}
			return BaseDirs.SelectMany(x => EnumeratePlugins(x)).ToList();
		}

		/// <summary>
		/// Find paths to all the plugins under a given parent directory (recursively)
		/// </summary>
		/// <param name="ParentDirectory">Parent directory to look in. Plugins will be found in any *subfolders* of this directory.</param>
		public static IEnumerable<FileReference> EnumeratePlugins(DirectoryReference ParentDirectory)
		{
			List<FileReference>? FileNames;
			if (!PluginFileCache.TryGetValue(ParentDirectory, out FileNames))
			{
				FileNames = new List<FileReference>();

				DirectoryItem ParentDirectoryItem = DirectoryItem.GetItemByDirectoryReference(ParentDirectory);
				if (ParentDirectoryItem.Exists)
				{
					using(ThreadPoolWorkQueue Queue = new ThreadPoolWorkQueue())
					{
						EnumeratePluginsInternal(ParentDirectoryItem, FileNames, Queue);
					}
				}

				// Sort the filenames to ensure that the plugin order is deterministic; otherwise response files will change with each build.
				FileNames = FileNames.OrderBy(x => x.FullName, StringComparer.OrdinalIgnoreCase).ToList();

				PluginFileCache.Add(ParentDirectory, FileNames);
			}
			return FileNames;
		}

		/// <summary>
		/// Find paths to all the plugins under a given parent directory (recursively)
		/// </summary>
		/// <param name="ParentDirectory">Parent directory to look in. Plugins will be found in any *subfolders* of this directory.</param>
		/// <param name="FileNames">List of filenames. Will have all the discovered .uplugin files appended to it.</param>
		/// <param name="Queue">Queue for tasks to be executed</param>
		static void EnumeratePluginsInternal(DirectoryItem ParentDirectory, List<FileReference> FileNames, ThreadPoolWorkQueue Queue)
		{
			foreach (DirectoryItem ChildDirectory in ParentDirectory.EnumerateDirectories())
			{
				bool bSearchSubDirectories = true;
				foreach (FileItem PluginFile in ChildDirectory.EnumerateFiles())
				{
					if(PluginFile.HasExtension(".uplugin"))
					{
						lock(FileNames)
						{
							FileNames.Add(PluginFile.Location);
						}
						bSearchSubDirectories = false;
					}
				}

				if (bSearchSubDirectories)
				{
					Queue.Enqueue(() => EnumeratePluginsInternal(ChildDirectory, FileNames, Queue));
				}
			}
		}

		/// <summary>
		/// Enumerates all the UBT plugin files available to the given project
		/// </summary>
		/// <param name="ProjectFile">Path to the project file</param>
		/// <param name="Plugins">Collection of found plugins</param>
		/// <returns>True if the plugins compiled</returns>
		public static bool EnumerateUbtPlugins(FileReference? ProjectFile, out (FileReference ProjectFile, FileReference TargetAssembly)[]? Plugins)
		{
			bool bBuildSuccess = true;
			Plugins = null;

			List<DirectoryReference> PluginDirectories = new List<DirectoryReference>();
			PluginDirectories.AddRange(Unreal.GetExtensionDirs(Unreal.EngineDirectory, "Source"));
			PluginDirectories.AddRange(Unreal.GetExtensionDirs(Unreal.EngineDirectory, "Plugins"));
			if (ProjectFile != null)
			{
				PluginDirectories.AddRange(Unreal.GetExtensionDirs(ProjectFile.Directory, "Source"));
				PluginDirectories.AddRange(Unreal.GetExtensionDirs(ProjectFile.Directory, "Plugins"));
				PluginDirectories.AddRange(Unreal.GetExtensionDirs(ProjectFile.Directory, "Mods"));
			}

			// Scan for UBT plugin projects
			HashSet<FileReference> ScannedPlugins = new HashSet<FileReference>(UnrealBuildBase.Rules.FindAllRulesFiles(
				PluginDirectories, UnrealBuildBase.Rules.RulesFileType.UbtPlugin));

			// Build the located plugins
			Dictionary<FileReference, (CsProjBuildRecord BuildRecord, FileReference BuildRecordFile)>? BuiltPlugins = null;
			if (ScannedPlugins.Count > 0)
			{
				List<DirectoryReference> BaseDirectories = new List<DirectoryReference>(2);
				BaseDirectories.Add(Unreal.EngineDirectory);
				if (ProjectFile != null)
				{
					BaseDirectories.Add(ProjectFile.Directory);
				}

				BuiltPlugins = CompileScriptModule.Build(UnrealBuildBase.Rules.RulesFileType.UbtPlugin, ScannedPlugins, BaseDirectories, false, false, true, out bBuildSuccess,
					Count =>
					{
						if (Log.OutputFile != null)
						{
							Log.TraceInformation($"Building {Count} plugins (see Log '{Log.OutputFile}' for more details)");
						}
						else
						{
							Log.TraceInformation($"Building {Count} plugins");
						}
					}
				);

				Plugins = BuiltPlugins.Select(P => (P.Key, CompileScriptModule.GetTargetPath(P))).OrderBy(X => X.Key.FullName).ToArray();
			}
			return bBuildSuccess;
		}
	}
}
