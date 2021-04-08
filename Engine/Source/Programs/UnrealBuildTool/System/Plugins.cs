// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Diagnostics;
using System.Linq;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Where a plugin was loaded from
	/// </summary>
	public enum PluginLoadedFrom
	{
		/// <summary>
		/// Plugin is built-in to the engine
		/// </summary>
		Engine,

		/// <summary>
		/// Project-specific plugin, stored within a game project directory
		/// </summary>
		Project
	}

	/// <summary>
	/// Where a plugin was loaded from. The order of this enum is important; in the case of name collisions, larger-valued types will take precedence. Plugins of the same type may not be duplicated.
	/// </summary>
	public enum PluginType
	{
		/// <summary>
		/// Plugin is built-in to the engine
		/// </summary>
		Engine,

		/// <summary>
		/// Enterprise plugin
		/// </summary>
		Enterprise,

		/// <summary>
		/// Project-specific plugin, stored within a game project directory
		/// </summary>
		Project,

		/// <summary>
		/// Plugin found in an external directory (found in an AdditionalPluginDirectory listed in the project file, or referenced on the command line)
		/// </summary>
		External,

		/// <summary>
		/// Project-specific mod plugin
		/// </summary>
		Mod,
	}

	/// <summary>
	/// Information about a single plugin
	/// </summary>
	[DebuggerDisplay("\\{{File}\\}")]
	public class PluginInfo
	{
		/// <summary>
		/// Plugin name
		/// </summary>
		public readonly string Name;

		/// <summary>
		/// Path to the plugin
		/// </summary>
		public readonly FileReference File;

		/// <summary>
		/// Path to the plugin's root directory
		/// </summary>
		public readonly DirectoryReference Directory;

		/// <summary>
		/// Children plugin files that can be added to this plugin (platform extensions)
		/// </summary>
		public List<FileReference> ChildFiles = new List<FileReference>();

		/// <summary>
		/// The plugin descriptor
		/// </summary>
		public PluginDescriptor Descriptor;

		/// <summary>
		/// The type of this plugin
		/// </summary>
		public PluginType Type;

		/// <summary>
		/// Constructs a PluginInfo object
		/// </summary>
		/// <param name="InFile">Path to the plugin descriptor</param>
		/// <param name="InType">The type of this plugin</param>
		public PluginInfo(FileReference InFile, PluginType InType)
		{
			Name = Path.GetFileNameWithoutExtension(InFile.FullName);
			File = InFile;
			Directory = File.Directory;
			Descriptor = PluginDescriptor.FromFile(File);
			Type = InType;
		}

		/// <summary>
		/// Determines whether the plugin should be enabled by default
		/// </summary>
		public bool IsEnabledByDefault(bool bAllowEnginePluginsEnabledByDefault)
		{
			if (Descriptor.bEnabledByDefault.HasValue)
			{
				if (Descriptor.bEnabledByDefault.Value)
				{
					return (LoadedFrom == PluginLoadedFrom.Project ? true : bAllowEnginePluginsEnabledByDefault);
				}
				else
				{
					return false;
				}
			}
			else
			{
				return (LoadedFrom == PluginLoadedFrom.Project);
			}
		}

		/// <summary>
		/// Determines where the plugin was loaded from
		/// </summary>
		public PluginLoadedFrom LoadedFrom
		{
			get
			{
				if(Type == PluginType.Engine || Type == PluginType.Enterprise)
				{
					return PluginLoadedFrom.Engine;
				}
				else
				{
					return PluginLoadedFrom.Project;
				}
			}
		}
	}

	/// <summary>
	/// Class for enumerating plugin metadata
	/// </summary>
	public static class Plugins
	{
		/// <summary>
		/// Cache of plugins under each directory
		/// </summary>
		static Dictionary<DirectoryReference, List<PluginInfo>> PluginInfoCache = new Dictionary<DirectoryReference, List<PluginInfo>>();

		/// <summary>
		/// Cache of plugin filenames under each directory
		/// </summary>
		static Dictionary<DirectoryReference, List<FileReference>> PluginFileCache = new Dictionary<DirectoryReference, List<FileReference>>();

		/// <summary>
		/// Invalidate cached plugin data so that we can pickup new things
		/// Warning: Will make subsequent plugin lookups and directory scans slow until the caches are repopulated
		/// </summary>
		public static void InvalidateCaches_SLOW()
		{
			PluginInfoCache = new Dictionary<DirectoryReference, List<PluginInfo>>();
			PluginFileCache = new Dictionary<DirectoryReference, List<FileReference>>();
			DirectoryItem.ResetAllCachedInfo_SLOW();
		}

		/// <summary>
		/// Returns a filtered list of plugins as a name:plugin dictionary to ensure that any game plugins override engine plugins with the same
		/// name, and otherwise that no two plugins with the same name exist. 
		/// </summary>
		/// <param name="Plugins">List of plugins to filter</param>
		/// <returns>Filtered Dictionary of plugins</returns>
		public static Dictionary<string, PluginInfo> ToFilteredDictionary(IEnumerable<PluginInfo> Plugins)
		{
			Dictionary<string, PluginInfo> NameToPluginInfo = new Dictionary<string, PluginInfo>(StringComparer.InvariantCultureIgnoreCase);
			foreach (PluginInfo Plugin in Plugins)
			{
				PluginInfo ExistingPluginInfo;
				if (!NameToPluginInfo.TryGetValue(Plugin.Name, out ExistingPluginInfo))
				{
					NameToPluginInfo.Add(Plugin.Name, Plugin);
				}
				else if (Plugin.Type > ExistingPluginInfo.Type)
				{
					NameToPluginInfo[Plugin.Name] = Plugin;
				}
				else if (Plugin.Type == ExistingPluginInfo.Type)
				{
					throw new BuildException(String.Format("Found '{0}' plugin in two locations ({1} and {2}). Plugin names must be unique.", Plugin.Name, ExistingPluginInfo.File, Plugin.File));
				}
			}
			return NameToPluginInfo;
		}

		/// <summary>
		/// Filters the list of plugins to ensure that any game plugins override engine plugins with the same name, and otherwise that no two
		/// plugins with the same name exist. 
		/// </summary>
		/// <param name="Plugins">List of plugins to filter</param>
		/// <returns>Filtered list of plugins in the original order</returns>
		public static IEnumerable<PluginInfo> FilterPlugins(IEnumerable<PluginInfo> Plugins)
		{
			Dictionary<string, PluginInfo> NameToPluginInfo = ToFilteredDictionary(Plugins);
			return Plugins.Where(x => NameToPluginInfo[x.Name] == x);
		}

		/// <summary>
		/// Read all the plugins available to a given project
		/// </summary>
		/// <param name="EngineDir">Path to the engine directory</param>
		/// <param name="ProjectDir">Path to the project directory (or null)</param>
		/// <param name="AdditionalDirectories">List of additional directories to scan for available plugins</param>
		/// <returns>Sequence of PluginInfo objects, one for each discovered plugin</returns>
		public static List<PluginInfo> ReadAvailablePlugins(DirectoryReference EngineDir, DirectoryReference ProjectDir, List<DirectoryReference> AdditionalDirectories)
		{
			List<PluginInfo> Plugins = new List<PluginInfo>();

			// Read all the engine plugins
			Plugins.AddRange(ReadEnginePlugins(EngineDir));

			// Read all the project plugins
			if (ProjectDir != null)
			{
				Plugins.AddRange(ReadProjectPlugins(ProjectDir));
			}

            // Scan for shared plugins in project specified additional directories
			if(AdditionalDirectories != null)
			{
				foreach (DirectoryReference AdditionalDirectory in AdditionalDirectories)
				{
					Plugins.AddRange(ReadPluginsFromDirectory(AdditionalDirectory, "", PluginType.External));
				}
			}

			return Plugins;
		}

		/// <summary>
		/// Enumerates all the plugin files available to the given project
		/// </summary>
		/// <param name="ProjectFile">Path to the project file</param>
		/// <returns>List of project files</returns>
		public static IEnumerable<FileReference> EnumeratePlugins(FileReference ProjectFile)
		{
			List<DirectoryReference> BaseDirs = new List<DirectoryReference>();
			BaseDirs.AddRange(UnrealBuildTool.GetExtensionDirs(UnrealBuildTool.EngineDirectory, "Plugins"));
			BaseDirs.AddRange(UnrealBuildTool.GetExtensionDirs(UnrealBuildTool.EnterpriseDirectory, "Plugins"));
			if(ProjectFile != null)
			{
				BaseDirs.AddRange(UnrealBuildTool.GetExtensionDirs(ProjectFile.Directory, "Plugins"));
				BaseDirs.AddRange(UnrealBuildTool.GetExtensionDirs(ProjectFile.Directory, "Mods"));
			}
			return BaseDirs.SelectMany(x => EnumeratePlugins(x)).ToList();
		}

		/// <summary>
		/// Read all the plugin descriptors under the given engine directory
		/// </summary>
		/// <param name="EngineDirectory">The parent directory to look in.</param>
		/// <returns>Sequence of the found PluginInfo object.</returns>
		public static IReadOnlyList<PluginInfo> ReadEnginePlugins(DirectoryReference EngineDirectory)
		{
			return ReadPluginsFromDirectory(EngineDirectory, "Plugins", PluginType.Engine);
		}

		/// <summary>
		/// Read all the plugin descriptors under the given enterprise directory
		/// </summary>
		/// <param name="EnterpriseDirectory">The parent directory to look in.</param>
		/// <returns>Sequence of the found PluginInfo object.</returns>
		public static IReadOnlyList<PluginInfo> ReadEnterprisePlugins(DirectoryReference EnterpriseDirectory)
		{
			return ReadPluginsFromDirectory(EnterpriseDirectory, "Plugins", PluginType.Enterprise);
		}

		/// <summary>
		/// Read all the plugin descriptors under the given project directory
		/// </summary>
		/// <param name="ProjectDirectory">The parent directory to look in.</param>
		/// <returns>Sequence of the found PluginInfo object.</returns>
		public static IReadOnlyList<PluginInfo> ReadProjectPlugins(DirectoryReference ProjectDirectory)
		{
			List<PluginInfo> Plugins = new List<PluginInfo>();
			Plugins.AddRange(ReadPluginsFromDirectory(ProjectDirectory, "Plugins", PluginType.Project));
			Plugins.AddRange(ReadPluginsFromDirectory(ProjectDirectory, "Mods", PluginType.Mod));
			return Plugins.AsReadOnly();
		}

		/// <summary>
		/// Read all of the plugins found in the project specified additional plugin directories
		/// </summary>
		/// <param name="AdditionalDirectory">The additional directory to scan</param>
		/// <returns>List of the found PluginInfo objects</returns>
		public static IReadOnlyList<PluginInfo> ReadAdditionalPlugins(DirectoryReference AdditionalDirectory)
		{
			DirectoryReference FullPath = DirectoryReference.Combine(AdditionalDirectory, "");
			if (!DirectoryReference.Exists(FullPath))
			{
				Log.TraceWarning("AdditionalPluginDirectory {0} not found. Path should be relative to the project", FullPath);
			}
			return ReadPluginsFromDirectory(AdditionalDirectory, "", PluginType.External);
		}

		/// <summary>
		/// Determines whether the given suffix is valid for a child plugin
		/// </summary>
		/// <param name="Suffix"></param>
		/// <returns>Whether the suffix is appopriate</returns>
		private static bool IsValidChildPluginSuffix(string Suffix)
		{
			foreach (UnrealPlatformGroup Group in UnrealPlatformGroup.GetValidGroups())
			{
				if (Group.ToString().Equals(Suffix, StringComparison.InvariantCultureIgnoreCase))
				{
					return true;
				}
			}

			foreach (UnrealTargetPlatform Platform in UnrealTargetPlatform.GetValidPlatforms())
			{
				if (Platform.ToString().Equals(Suffix, StringComparison.InvariantCultureIgnoreCase))
				{
					return true;
				}
			}

			return false;
		}

		/// <summary>
		///  Attempt to merge a child plugin up into a parent plugin (via file naming scheme). Very little merging happens
		///  but it does allow for platform extensions to extend a plugin with module files
		/// </summary>
		/// <param name="Child">Child plugin that needs to merge to a main, parent plugin</param>
		/// <param name="Filename">Child plugin's filename, used to determine the parent's name</param>
		private static void TryMergeWithParent(PluginInfo Child, FileReference Filename)
		{
			// find the parent
			PluginInfo Parent = null;

			string[] Tokens = Filename.GetFileNameWithoutAnyExtensions().Split("_".ToCharArray());
			if (Tokens.Length == 2)
			{
				string ParentPluginName = Tokens[0];
				foreach (KeyValuePair<DirectoryReference, List<PluginInfo>> Pair in PluginInfoCache)
				{
					Parent = Pair.Value.FirstOrDefault(x => x.Name.Equals(ParentPluginName, StringComparison.InvariantCultureIgnoreCase) && x.LoadedFrom == Child.LoadedFrom);
					if (Parent != null)
					{
						break;
					}
				}
			}
			else
			{
				throw new BuildException("Platform extension plugin {0} was named improperly. It must be in the form <ParentPlugin>_<Platform>.uplugin", Filename);
			}

			// did we find a parent plugin?
			if (Parent == null)
			{
				throw new BuildException("Unable to find parent plugin {0} for platform extension plugin {1}. Make sure {0}.uplugin exists.", Tokens[0], Filename);
			}

			// validate child plugin file name
			string PlatformName = Tokens[1];
			if (!IsValidChildPluginSuffix(PlatformName))
			{
				Log.TraceWarning("Ignoring child plugin: {0} - Unknown suffix \"{1}\". Expected valid platform or group", Child.File.GetFileName(), PlatformName);
				return;
			}

			// add our uplugin file to the existing plugin to be used to search for modules later
			Parent.ChildFiles.Add(Child.File);

			// this should cause an error if it's invalid platform name
			//UnrealTargetPlatform Platform = UnrealTargetPlatform.Parse(PlatformName);

			// merge the supported platforms
			if (Child.Descriptor.SupportedTargetPlatforms != null)
			{
				if (Parent.Descriptor.SupportedTargetPlatforms == null)
				{
					Parent.Descriptor.SupportedTargetPlatforms = Child.Descriptor.SupportedTargetPlatforms;
				}
				else
				{
					Parent.Descriptor.SupportedTargetPlatforms = Parent.Descriptor.SupportedTargetPlatforms.Union(Child.Descriptor.SupportedTargetPlatforms).ToList();
				}
			}

			// make sure we are whitelisted for any modules we list
			if (Child.Descriptor.Modules != null)
			{
				if (Parent.Descriptor.Modules == null)
				{
					Parent.Descriptor.Modules = Child.Descriptor.Modules;
				}
				else
				{
					foreach (ModuleDescriptor ChildModule in Child.Descriptor.Modules)
					{
						ModuleDescriptor ParentModule = Parent.Descriptor.Modules.FirstOrDefault(x => x.Name.Equals(ChildModule.Name) && x.Type == ChildModule.Type);
						if (ParentModule != null)
						{
							// merge white/blacklists (if the parent had a list, and child didn't specify a list, just add the child platform to the parent list - for white and black!)
							if (ChildModule.WhitelistPlatforms != null)
							{
								if (ParentModule.WhitelistPlatforms == null)
								{
									ParentModule.WhitelistPlatforms = ChildModule.WhitelistPlatforms;
								}
								else
								{
									ParentModule.WhitelistPlatforms = ParentModule.WhitelistPlatforms.Union(ChildModule.WhitelistPlatforms).ToList();
								}
							}
							if (ChildModule.BlacklistPlatforms != null)
							{
								if (ParentModule.BlacklistPlatforms == null)
								{
									ParentModule.BlacklistPlatforms = ChildModule.BlacklistPlatforms;
								}
								else
								{
									ParentModule.BlacklistPlatforms = ParentModule.BlacklistPlatforms.Union(ChildModule.BlacklistPlatforms).ToList();
								}
							}
						}
						else
						{
							Parent.Descriptor.Modules.Add(ChildModule);
						}
					}
				}
			}

			// make sure we are whitelisted for any plugins we list
			if (Child.Descriptor.Plugins != null)
			{
				if (Parent.Descriptor.Plugins == null)
				{
					Parent.Descriptor.Plugins = Child.Descriptor.Plugins;
				}
				else
				{ 
					foreach (PluginReferenceDescriptor ChildPluginReference in Child.Descriptor.Plugins)
					{
						PluginReferenceDescriptor ParentPluginReference = Parent.Descriptor.Plugins.FirstOrDefault(x => x.Name.Equals(ChildPluginReference.Name));
						if (ParentPluginReference != null)
						{
							// we only need to whitelist the platform if the parent had a whitelist (otherwise, we could mistakenly remove all other platforms)
							if (ParentPluginReference.WhitelistPlatforms != null)
							{
								if (ChildPluginReference.WhitelistPlatforms != null)
								{
									ParentPluginReference.WhitelistPlatforms = ParentPluginReference.WhitelistPlatforms.Union(ChildPluginReference.WhitelistPlatforms).ToList();
								}
							}

							// if we want to blacklist a platform, add it even if the parent didn't have a blacklist. this won't cause problems with other platforms
							if (ChildPluginReference.BlacklistPlatforms != null)
							{
								if (ParentPluginReference.BlacklistPlatforms == null)
								{
									ParentPluginReference.BlacklistPlatforms = ChildPluginReference.BlacklistPlatforms;
								}
								else
								{
									ParentPluginReference.BlacklistPlatforms = ParentPluginReference.BlacklistPlatforms.Union(ChildPluginReference.BlacklistPlatforms).ToList();
								}
							}
						}
						else
						{
							Parent.Descriptor.Plugins.Add(ChildPluginReference);
						}
					}
				}
			}
			// @todo platplug: what else do we want to support merging?!?
		}


		/// <summary>
		/// Read all the plugin descriptors under the given directory
		/// </summary>
		/// <param name="RootDirectory">The directory to look in.</param>
		/// <param name="Subdirectory">A subdirectory to look in in RootDirectory and any other Platform directories under Root</param>
		/// <param name="Type">The plugin type</param>
		/// <returns>Sequence of the found PluginInfo object.</returns>
		public static IReadOnlyList<PluginInfo> ReadPluginsFromDirectory(DirectoryReference RootDirectory, string Subdirectory, PluginType Type)
		{
			// look for directories in RootDirectory and and extension directories under RootDirectory
			List<DirectoryReference> RootDirectories = UnrealBuildTool.GetExtensionDirs(RootDirectory, Subdirectory);

			Dictionary<PluginInfo, FileReference> ChildPlugins = new Dictionary<PluginInfo, FileReference>();
			List<PluginInfo> AllParentPlugins = new List<PluginInfo>();

			foreach (DirectoryReference Dir in RootDirectories)
			{
				if (!DirectoryReference.Exists(Dir))
				{
					continue;
				}

				List<PluginInfo> Plugins;
				if (!PluginInfoCache.TryGetValue(Dir, out Plugins))
				{
					Plugins = new List<PluginInfo>();
					foreach (FileReference PluginFileName in EnumeratePlugins(Dir))
					{
						PluginInfo Plugin = new PluginInfo(PluginFileName, Type);

						// is there a parent to merge up into?
						if (Plugin.Descriptor.bIsPluginExtension)
						{
							ChildPlugins.Add(Plugin, PluginFileName);
						}
						else
						{
							Plugins.Add(Plugin);
						}
					}
					PluginInfoCache.Add(Dir, Plugins);
				}

				// gather all of the plugins into one list
				AllParentPlugins.AddRange(Plugins);
			}

			// now that all parent plugins are read in, we can let the children look up the parents
			foreach (KeyValuePair<PluginInfo, FileReference> Pair in ChildPlugins)
			{
				TryMergeWithParent(Pair.Key, Pair.Value);
			}

			return AllParentPlugins;
		}

		/// <summary>
		/// Find paths to all the plugins under a given parent directory (recursively)
		/// </summary>
		/// <param name="ParentDirectory">Parent directory to look in. Plugins will be found in any *subfolders* of this directory.</param>
		public static IEnumerable<FileReference> EnumeratePlugins(DirectoryReference ParentDirectory)
		{
			List<FileReference> FileNames;
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
		/// Determine if a plugin is enabled for a given project
		/// </summary>
		/// <param name="Project">The project to check. May be null.</param>
		/// <param name="Plugin">Information about the plugin</param>
		/// <param name="Platform">The target platform</param>
		/// <param name="Configuration">The target configuration</param>
		/// <param name="TargetType">The type of target being built</param>
		/// <returns>True if the plugin should be enabled for this project</returns>
		public static bool IsPluginEnabledForTarget(PluginInfo Plugin, ProjectDescriptor Project, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration, TargetType TargetType)
		{
			if (!Plugin.Descriptor.SupportsTargetPlatform(Platform))
			{
				return false;
			}

			bool bAllowEnginePluginsEnabledByDefault = (Project == null ? true : !Project.DisableEnginePluginsByDefault);
			bool bEnabled = Plugin.IsEnabledByDefault(bAllowEnginePluginsEnabledByDefault);
			if (Project != null && Project.Plugins != null)
			{
				foreach (PluginReferenceDescriptor PluginReference in Project.Plugins)
				{
					if (String.Compare(PluginReference.Name, Plugin.Name, true) == 0 && !PluginReference.bOptional)
					{
						bEnabled = PluginReference.IsEnabledForPlatform(Platform) && PluginReference.IsEnabledForTargetConfiguration(Configuration) && PluginReference.IsEnabledForTarget(TargetType);
					}
				}
			}
			return bEnabled;
		}

		/// <summary>
		/// Determine if a plugin is enabled for a given project
		/// </summary>
		/// <param name="Project">The project to check. May be null.</param>
		/// <param name="Plugin">Information about the plugin</param>
		/// <param name="Platform">The target platform</param>
		/// <param name="Configuration">The target configuration</param>
		/// <param name="TargetType">The type of target being built</param>
		/// <param name="bRequiresCookedData">Whether the target requires cooked data</param>
		/// <returns>True if the plugin should be enabled for this project</returns>
		public static bool IsPluginCompiledForTarget(PluginInfo Plugin, ProjectDescriptor Project, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration, TargetType TargetType, bool bRequiresCookedData)
		{
			bool bCompiledForTarget = false;
			if (IsPluginEnabledForTarget(Plugin, Project, Platform, Configuration, TargetType) && Plugin.Descriptor.Modules != null)
			{
				bool bBuildDeveloperTools = (TargetType == TargetType.Editor || TargetType == TargetType.Program || (Configuration != UnrealTargetConfiguration.Test && Configuration != UnrealTargetConfiguration.Shipping));
				foreach (ModuleDescriptor Module in Plugin.Descriptor.Modules)
				{
					if (Module.IsCompiledInConfiguration(Platform, Configuration, "", TargetType, bBuildDeveloperTools, bRequiresCookedData))
					{
						bCompiledForTarget = true;
						break;
					}
				}
			}
			return bCompiledForTarget;
		}
	}
}
