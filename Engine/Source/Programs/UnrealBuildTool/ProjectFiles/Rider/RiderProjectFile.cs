// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Linq;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	internal class RiderProjectFile : ProjectFile
	{
		public DirectoryReference RootPath;
		public HashSet<TargetType> TargetTypes;
		public CommandLineArguments Arguments;

		public RiderProjectFile(FileReference InProjectFilePath) : base(InProjectFilePath)
		{
		}

		/// <summary>
		/// Write project file info in JSON file.
		/// For every combination of <c>UnrealTargetPlatform</c>, <c>UnrealTargetConfiguration</c> and <c>TargetType</c>
		/// will be generated separate JSON file.
		/// Project file will be stored:
		/// For UE4:  {UE4Root}/Engine/Intermediate/ProjectFiles/.Rider/{Platform}/{Configuration}/{TargetType}/{ProjectName}.json
		/// For game: {GameRoot}/Intermediate/ProjectFiles/.Rider/{Platform}/{Configuration}/{TargetType}/{ProjectName}.json
		/// </summary>
		/// <remarks>
		/// * <c>UnrealTargetPlatform.Win32</c> will be always ignored.
		/// * <c>TargetType.Editor</c> will be generated for current platform only and will ignore <c>UnrealTargetConfiguration.Test</c> and <c>UnrealTargetConfiguration.Shipping</c> configurations
		/// * <c>TargetType.Program</c>  will be generated for current platform only and <c>UnrealTargetConfiguration.Development</c> configuration only 
		/// </remarks>
		/// <param name="InPlatforms"></param>
		/// <param name="InConfigurations"></param>
		/// <param name="PlatformProjectGenerators"></param>
		/// <returns></returns>
		public override bool WriteProjectFile(List<UnrealTargetPlatform> InPlatforms,
			List<UnrealTargetConfiguration> InConfigurations,
			PlatformProjectGeneratorCollection PlatformProjectGenerators)
		{
			string ProjectName = ProjectFilePath.GetFileNameWithoutAnyExtensions();
			DirectoryReference projectRootFolder = DirectoryReference.Combine(RootPath, ".Rider");
			foreach (UnrealTargetPlatform Platform in InPlatforms.Where(it => it != UnrealTargetPlatform.Win32))
			{
				DirectoryReference PlatformFolder = DirectoryReference.Combine(projectRootFolder, Platform.ToString());
				foreach (UnrealTargetConfiguration Configuration in InConfigurations)
				{
					DirectoryReference configurationFolder = DirectoryReference.Combine(PlatformFolder, Configuration.ToString());
					foreach (ProjectTarget ProjectTarget in ProjectTargets)
					{
						if (TargetTypes.Any() && !TargetTypes.Contains(ProjectTarget.TargetRules.Type)) continue;

						// Skip Programs for all configs except for current platform + Development configuration
						if (ProjectTarget.TargetRules.Type == TargetType.Program && (BuildHostPlatform.Current.Platform != Platform || Configuration != UnrealTargetConfiguration.Development))
						{
							continue;
						}

						// Skip Editor for all platforms except for current platform
						if (ProjectTarget.TargetRules.Type == TargetType.Editor && (BuildHostPlatform.Current.Platform != Platform || (Configuration == UnrealTargetConfiguration.Test || Configuration == UnrealTargetConfiguration.Shipping)))
						{
							continue;
						}

						DirectoryReference TargetFolder =
							DirectoryReference.Combine(configurationFolder, ProjectTarget.TargetRules.Type.ToString());

						string DefaultArchitecture = UEBuildPlatform
							.GetBuildPlatform(BuildHostPlatform.Current.Platform)
							.GetDefaultArchitecture(ProjectTarget.UnrealProjectFilePath);
						TargetDescriptor TargetDesc = new TargetDescriptor(ProjectTarget.UnrealProjectFilePath, ProjectTarget.Name,
							BuildHostPlatform.Current.Platform, UnrealTargetConfiguration.Development,
							DefaultArchitecture, Arguments);
						UEBuildTarget BuildTarget = UEBuildTarget.Create(TargetDesc, false, false);
						FileReference OutputFile = FileReference.Combine(TargetFolder, $"{ProjectName}.json");
						DirectoryReference.CreateDirectory(OutputFile.Directory);
						using (JsonWriter Writer = new JsonWriter(OutputFile))
						{
							ExportTarget(BuildTarget, Writer);	
						}
					}
				}
			}

			return true;
		}

		/// <summary>
		/// Write a Target to a JSON writer. Is array is empty, don't write anything
		/// </summary>
		/// <param name="Target"></param>
		/// <param name="Writer">Writer for the array data</param>
		private static void ExportTarget(UEBuildTarget Target, JsonWriter Writer)
		{
			Writer.WriteObjectStart();

			Writer.WriteValue("Name", Target.TargetName);
			Writer.WriteValue("Configuration", Target.Configuration.ToString());
			Writer.WriteValue("Platform", Target.Platform.ToString());
			Writer.WriteValue("TargetFile", Target.TargetRulesFile.FullName);
			if (Target.ProjectFile != null)
			{
				Writer.WriteValue("ProjectFile", Target.ProjectFile.FullName);
			}
			
			ExportEnvironmentToJson(Target, Writer);
			
			if(Target.Binaries.Any())
			{
				Writer.WriteArrayStart("Binaries");
				foreach (UEBuildBinary Binary in Target.Binaries)
				{
					Writer.WriteObjectStart();
					ExportBinary(Binary, Writer);
					Writer.WriteObjectEnd();
				}
				Writer.WriteArrayEnd();
			}

			List<UEBuildModule> TargetModules = Target.Binaries.SelectMany(x => x.Modules).ToList();
			if(TargetModules.Any())
			{
				Writer.WriteObjectStart("Modules");
				foreach (UEBuildModule Module in TargetModules)
				{
					Writer.WriteObjectStart(Module.Name);
					ExportModule(Module, Target.GetExecutableDir(), Writer);
					Writer.WriteObjectEnd();
				}
				Writer.WriteObjectEnd();
			}
			
			ExportPluginsFromTarget(Target, Writer);
			
			Writer.WriteObjectEnd();
		}

		/// <summary>
		/// Write a Module to a JSON writer. If array is empty, don't write anything
		/// </summary>
		/// <param name="TargetOutputDir"></param>
		/// <param name="Writer">Writer for the array data</param>
		/// <param name="Module"></param>
		private static void ExportModule(UEBuildModule Module, DirectoryReference TargetOutputDir, JsonWriter Writer)
		{
			DirectoryReference BinaryOutputDir = Module.Binary?.OutputDir;
			Writer.WriteValue("Name", Module.Name);
			Writer.WriteValue("Directory", Module.ModuleDirectory.FullName);
			Writer.WriteValue("Rules", Module.RulesFile.FullName);
			Writer.WriteValue("PCHUsage", Module.Rules.PCHUsage.ToString());

			UEBuildModuleCPP ModuleCPP = Module as UEBuildModuleCPP;
			if (ModuleCPP != null)
			{
				Writer.WriteValue("GeneratedCodeDirectory", ModuleCPP.GeneratedCodeDirectory != null ? ModuleCPP.GeneratedCodeDirectory.FullName : string.Empty);
			}

			if (Module.Rules.PrivatePCHHeaderFile != null)
			{
				Writer.WriteValue("PrivatePCH", FileReference.Combine(Module.ModuleDirectory, Module.Rules.PrivatePCHHeaderFile).FullName);
			}

			if (Module.Rules.SharedPCHHeaderFile != null)
			{
				Writer.WriteValue("SharedPCH", FileReference.Combine(Module.ModuleDirectory, Module.Rules.SharedPCHHeaderFile).FullName);
			}

			ExportJsonModuleArray(Writer, "PublicDependencyModules", Module.PublicDependencyModules);
			ExportJsonModuleArray(Writer, "PublicIncludePathModules", Module.PublicIncludePathModules);
			ExportJsonModuleArray(Writer, "PrivateDependencyModules", Module.PrivateDependencyModules);
			ExportJsonModuleArray(Writer, "PrivateIncludePathModules", Module.PrivateIncludePathModules);
			ExportJsonModuleArray(Writer, "DynamicallyLoadedModules", Module.DynamicallyLoadedModules);

			ExportJsonStringArray(Writer, "PublicSystemIncludePaths", Module.PublicSystemIncludePaths.Select(x => x.FullName));
			ExportJsonStringArray(Writer, "PublicIncludePaths", Module.PublicIncludePaths.Select(x => x.FullName));
			
			ExportJsonStringArray(Writer, "LegacyPublicIncludePaths", Module.LegacyPublicIncludePaths.Select(x => x.FullName));
			
			ExportJsonStringArray(Writer, "PrivateIncludePaths", Module.PrivateIncludePaths.Select(x => x.FullName));
			ExportJsonStringArray(Writer, "PublicLibraryPaths", Module.PublicSystemLibraryPaths.Select(x => x.FullName));
			ExportJsonStringArray(Writer, "PublicAdditionalLibraries", Module.PublicAdditionalLibraries);
			ExportJsonStringArray(Writer, "PublicFrameworks", Module.PublicFrameworks);
			ExportJsonStringArray(Writer, "PublicWeakFrameworks", Module.PublicWeakFrameworks);
			ExportJsonStringArray(Writer, "PublicDelayLoadDLLs", Module.PublicDelayLoadDLLs);
			ExportJsonStringArray(Writer, "PublicDefinitions", Module.PublicDefinitions);
			
			ExportJsonStringArray(Writer, "PrivateDefinitions", Module.Rules.PrivateDefinitions);
			ExportJsonStringArray(Writer, "ProjectDefinitions", /* TODO: Add method ShouldAddProjectDefinitions */ !Module.Rules.bTreatAsEngineModule ? Module.Rules.Target.ProjectDefinitions : new string[0]);
			ExportJsonStringArray(Writer, "ApiDefinitions", Module.GetEmptyApiMacros());
			Writer.WriteValue("ShouldAddLegacyPublicIncludePaths", Module.Rules.bLegacyPublicIncludePaths);

			if(Module.Rules.CircularlyReferencedDependentModules.Any())
			{
				Writer.WriteArrayStart("CircularlyReferencedModules");
				foreach (string ModuleName in Module.Rules.CircularlyReferencedDependentModules)
				{
					Writer.WriteValue(ModuleName);
				}
				Writer.WriteArrayEnd();
			}
			
			if(Module.Rules.RuntimeDependencies.Inner.Any())
			{
				Writer.WriteArrayStart("RuntimeDependencies");
				foreach (ModuleRules.RuntimeDependency RuntimeDependency in Module.Rules.RuntimeDependencies.Inner)
				{
					Writer.WriteObjectStart();
					
					Writer.WriteValue("Path",
						Module.ExpandPathVariables(RuntimeDependency.Path, BinaryOutputDir, TargetOutputDir));
					if (RuntimeDependency.SourcePath != null)
					{
						Writer.WriteValue("SourcePath",
							Module.ExpandPathVariables(RuntimeDependency.SourcePath, BinaryOutputDir, TargetOutputDir));
					}

					Writer.WriteValue("Type", RuntimeDependency.Type.ToString());
					
					Writer.WriteObjectEnd();
				}
				Writer.WriteArrayEnd();
			}
		}
		
		/// <summary>
		/// Write an array of Modules to a JSON writer. If array is empty, don't write anything
		/// </summary>
		/// <param name="Writer">Writer for the array data</param>
		/// <param name="ArrayName">Name of the array property</param>
		/// <param name="Modules">Sequence of Modules to write. May be null.</param>
		private static void ExportJsonModuleArray(JsonWriter Writer, string ArrayName, IEnumerable<UEBuildModule> Modules)
		{
			if (Modules == null || !Modules.Any()) return;
			
			Writer.WriteArrayStart(ArrayName);
			foreach (UEBuildModule Module in Modules)
			{
				Writer.WriteValue(Module.Name);
			}
			Writer.WriteArrayEnd();
		}
		
		/// <summary>
		/// Write an array of strings to a JSON writer. Ifl array is empty, don't write anything
		/// </summary>
		/// <param name="Writer">Writer for the array data</param>
		/// <param name="ArrayName">Name of the array property</param>
		/// <param name="Strings">Sequence of strings to write. May be null.</param>
		static void ExportJsonStringArray(JsonWriter Writer, string ArrayName, IEnumerable<string> Strings)
		{
			if (Strings == null || !Strings.Any()) return;
			
			Writer.WriteArrayStart(ArrayName);
			foreach (string String in Strings)
			{
				Writer.WriteValue(String);
			}
			Writer.WriteArrayEnd();
		}
		
		/// <summary>
		/// Write uplugin content to a JSON writer
		/// </summary>
		/// <param name="Plugin">Uplugin description</param>
		/// <param name="Writer">JSON writer</param>
		private static void ExportPlugin(UEBuildPlugin Plugin, JsonWriter Writer)
		{
			Writer.WriteObjectStart(Plugin.Name);
			
			Writer.WriteValue("File", Plugin.File.FullName);
			Writer.WriteValue("Type", Plugin.Type.ToString());
			if(Plugin.Dependencies.Any())
			{
				Writer.WriteStringArrayField("Dependencies", Plugin.Dependencies.Select(it => it.Name));
			}
			if(Plugin.Modules.Any())
			{
				Writer.WriteStringArrayField("Modules", Plugin.Modules.Select(it => it.Name));
			}
			
			Writer.WriteObjectEnd();
		}
		
		/// <summary>
		/// Setup plugins for Target and write plugins to JSON writer. Don't write anything if there are no plugins 
		/// </summary>
		/// <param name="Target"></param>
		/// <param name="Writer"></param>
		private static void ExportPluginsFromTarget(UEBuildTarget Target, JsonWriter Writer)
		{
			Target.SetupPlugins();
			if (!Target.BuildPlugins.Any()) return;
			
			Writer.WriteObjectStart("Plugins");
			foreach (UEBuildPlugin plugin in Target.BuildPlugins)
			{
				ExportPlugin(plugin, Writer);
			}
			Writer.WriteObjectEnd();
		}

		/// <summary>
		/// Write information about this binary to a JSON file
		/// </summary>
		/// <param name="Binary"></param>
		/// <param name="Writer">Writer for this binary's data</param>
		private static void ExportBinary(UEBuildBinary Binary, JsonWriter Writer)
		{
			Writer.WriteValue("File", Binary.OutputFilePath.FullName);
			Writer.WriteValue("Type", Binary.Type.ToString());

			Writer.WriteArrayStart("Modules");
			foreach(UEBuildModule Module in Binary.Modules)
			{
				Writer.WriteValue(Module.Name);
			}
			Writer.WriteArrayEnd();
		}
		
		/// <summary>
		/// Write C++ toolchain information to JSON writer
		/// </summary>
		/// <param name="Target"></param>
		/// <param name="Writer"></param>
		private static void ExportEnvironmentToJson(UEBuildTarget Target, JsonWriter Writer)
		{
			CppCompileEnvironment GlobalCompileEnvironment = Target.CreateCompileEnvironmentForProjectFiles();
			
			Writer.WriteArrayStart("EnvironmentIncludePaths");
			foreach (DirectoryReference Path in GlobalCompileEnvironment.UserIncludePaths)
			{
				Writer.WriteValue(Path.FullName);
			}
			foreach (DirectoryReference Path in GlobalCompileEnvironment.SystemIncludePaths)
			{
				Writer.WriteValue(Path.FullName);
			}
			
			// TODO: get corresponding includes for specific platforms
			if (UEBuildPlatform.IsPlatformInGroup(Target.Platform, UnrealPlatformGroup.Windows))
			{
				foreach (DirectoryReference Path in Target.Rules.WindowsPlatform.Environment.IncludePaths)
				{
					Writer.WriteValue(Path.FullName);
				}
			}
			Writer.WriteArrayEnd();
	
			Writer.WriteArrayStart("EnvironmentDefinitions");
			foreach (string Definition in GlobalCompileEnvironment.Definitions)
			{
				Writer.WriteValue(Definition);
			}
			Writer.WriteArrayEnd();
		}
	}
}