// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using Tools.DotNETCommon;
using System.Diagnostics;
using System.Linq;

namespace UnrealBuildTool
{
	class VSCodeProjectFolder : MasterProjectFolder
	{
		public VSCodeProjectFolder(ProjectFileGenerator InitOwnerProjectFileGenerator, string InitFolderName)
			: base(InitOwnerProjectFileGenerator, InitFolderName)
		{
		}
	}

	class VSCodeProject : ProjectFile
	{
		public VSCodeProject(FileReference InitFilePath)
			: base(InitFilePath)
		{
		}

		public override bool WriteProjectFile(List<UnrealTargetPlatform> InPlatforms, List<UnrealTargetConfiguration> InConfigurations, PlatformProjectGeneratorCollection PlatformProjectGenerators)
		{
			return true;
		}
	}

	class VSCodeProjectFileGenerator : ProjectFileGenerator
	{
		private DirectoryReference VSCodeDir;
		private UnrealTargetPlatform HostPlatform = BuildHostPlatform.Current.Platform;
		private bool bForeignProject;
		private DirectoryReference UE4ProjectRoot;
		private bool bBuildingForDotNetCore;
		private string FrameworkExecutableExtension;
		private string FrameworkLibraryExtension = ".dll";

		private readonly List<BuildTarget> BuildTargets = new List<BuildTarget>();

		/// <summary>
		/// Includes all files in the generated workspace.
		/// </summary>
		[XmlConfigFile(Name = "IncludeAllFiles")]
		private bool IncludeAllFiles = false;

		private enum EPathType
		{
			Absolute,
			Relative,
		}

		private enum EQuoteType
		{
			Single,	// can be ignored on platforms that don't need it (windows atm)
			Double,
		}

		private string CommonMakePathString(FileSystemReference InRef, EPathType InPathType, DirectoryReference InRelativeRoot)
		{
			if (InRelativeRoot == null)
			{
				InRelativeRoot = UE4ProjectRoot;
			}

			string Processed = InRef.ToString();
			
			switch (InPathType)
			{
				case EPathType.Relative:
				{
					if (InRef.IsUnderDirectory(InRelativeRoot))
					{
						Processed = InRef.MakeRelativeTo(InRelativeRoot).ToString();
					}

					break;
				}

				default:
				{
					break;
				}
			}

			if (HostPlatform == UnrealTargetPlatform.Win64)
			{
				Processed = Processed.Replace("\\", "\\\\");
				Processed = Processed.Replace("/", "\\\\");
			}
			else
			{
				Processed = Processed.Replace('\\', '/');
			}

			return Processed;
		}

		private string MakeQuotedPathString(FileSystemReference InRef, EPathType InPathType, DirectoryReference InRelativeRoot = null, EQuoteType InQuoteType = EQuoteType.Double)
		{
			string Processed = CommonMakePathString(InRef, InPathType, InRelativeRoot);

			if (Processed.Contains(" "))
			{
				if (HostPlatform == UnrealTargetPlatform.Win64 && InQuoteType == EQuoteType.Double)
				{
					Processed = "\\\"" + Processed + "\\\"";
				}
				else
				{
					Processed = "'" + Processed + "'";
				}
 			}

			return Processed;
		}

		private string MakeUnquotedPathString(FileSystemReference InRef, EPathType InPathType, DirectoryReference InRelativeRoot = null)
		{
			return CommonMakePathString(InRef, InPathType, InRelativeRoot);
		}

		private string MakePathString(FileSystemReference InRef, bool bInAbsolute = false, bool bForceSkipQuotes = false)
		{
			if (bForceSkipQuotes)
			{
				return MakeUnquotedPathString(InRef, bInAbsolute ? EPathType.Absolute : EPathType.Relative, UE4ProjectRoot);
			}
			else
			{
				return MakeQuotedPathString(InRef, bInAbsolute ? EPathType.Absolute : EPathType.Relative, UE4ProjectRoot);
			}
		}

		public VSCodeProjectFileGenerator(FileReference InOnlyGameProject)
			: base(InOnlyGameProject)
		{
			bBuildingForDotNetCore = Environment.CommandLine.Contains("-dotnetcore");
			FrameworkExecutableExtension = bBuildingForDotNetCore ? ".dll" : ".exe";
		}

		class JsonFile
		{
			public JsonFile()
			{
			}

			public void BeginRootObject()
			{
				BeginObject();
			}

			public void EndRootObject()
			{
				EndObject();
				if (TabString.Length > 0)
				{
					throw new Exception("Called EndRootObject before all objects and arrays have been closed");
				}
			}

			public void BeginObject(string Name = null)
			{
				string Prefix = Name == null ? "" : Quoted(Name) + ": ";
				Lines.Add(TabString + Prefix + "{");
				TabString += "\t";
			}

			public void EndObject()
			{
				Lines[Lines.Count - 1] = Lines[Lines.Count - 1].TrimEnd(',');
				TabString = TabString.Remove(TabString.Length - 1);
				Lines.Add(TabString + "},");
			}

			public void BeginArray(string Name = null)
			{
				string Prefix = Name == null ? "" : Quoted(Name) + ": ";
				Lines.Add(TabString + Prefix + "[");
				TabString += "\t";
			}

			public void EndArray()
			{
				Lines[Lines.Count - 1] = Lines[Lines.Count - 1].TrimEnd(',');
				TabString = TabString.Remove(TabString.Length - 1);
				Lines.Add(TabString + "],");
			}

			public void AddField(string Name, bool Value)
			{
				Lines.Add(TabString + Quoted(Name) + ": " + Value.ToString().ToLower() + ",");
			}

			public void AddField(string Name, string Value)
			{
				Lines.Add(TabString + Quoted(Name) + ": " + Quoted(Value) + ",");
			}

			public void AddUnnamedField(string Value)
			{
				Lines.Add(TabString + Quoted(Value) + ",");
			}

			public void Write(FileReference File)
			{
				Lines[Lines.Count - 1] = Lines[Lines.Count - 1].TrimEnd(',');
				FileReference.WriteAllLines(File, Lines.ToArray());
			}

			private string Quoted(string Value)
			{
				return "\"" + Value + "\"";
			}

			private List<string> Lines = new List<string>();
			private string TabString = "";
		}

		override public string ProjectFileExtension
		{
			get
			{
				return ".vscode";
			}
		}

		public override void CleanProjectFiles(DirectoryReference InMasterProjectDirectory, string InMasterProjectName, DirectoryReference InIntermediateProjectFilesPath)
		{
		}

		public override bool ShouldGenerateIntelliSenseData()
		{
			return true;
		}

		protected override ProjectFile AllocateProjectFile(FileReference InitFilePath)
		{
			return new VSCodeProject(InitFilePath);
		}

		public override MasterProjectFolder AllocateMasterProjectFolder(ProjectFileGenerator InitOwnerProjectFileGenerator, string InitFolderName)
		{
			return new VSCodeProjectFolder(InitOwnerProjectFileGenerator, InitFolderName);
		} 

		protected override bool WriteMasterProjectFile(ProjectFile UBTProject, PlatformProjectGeneratorCollection PlatformProjectGenerators)
		{
			VSCodeDir = DirectoryReference.Combine(MasterProjectPath, ".vscode");
			DirectoryReference.CreateDirectory(VSCodeDir);

			UE4ProjectRoot = UnrealBuildTool.RootDirectory;
			bForeignProject = !VSCodeDir.IsUnderDirectory(UE4ProjectRoot);

			List<ProjectFile> Projects;

			if (bForeignProject)
			{
				Projects = new List<ProjectFile>();
				foreach (var Project in AllProjectFiles)
				{
					if (GameProjectName == Project.ProjectFilePath.GetFileNameWithoutAnyExtensions())
					{
						Projects.Add(Project);
						break;
					}
				}
			}
			else
			{
				Projects = new List<ProjectFile>(AllProjectFiles);
			}
			Projects.Sort((A, B) => { return A.ProjectFilePath.GetFileName().CompareTo(B.ProjectFilePath.GetFileName()); });

			ProjectData ProjectData = GatherProjectData(Projects, PlatformProjectGenerators);

			WriteTasksFile(ProjectData);
			WriteLaunchFile(ProjectData);
			WriteWorkspaceIgnoreFile(Projects);
			WriteCppPropertiesFile(VSCodeDir, ProjectData);
			WriteWorkspaceFile();

			if (bForeignProject && bIncludeEngineSource)
			{
				// for installed builds we need to write the cpp properties file under the installed engine as well for intellisense to work
				DirectoryReference Ue4CodeDirectory = DirectoryReference.Combine(UnrealBuildTool.RootDirectory, ".vscode");
				WriteCppPropertiesFile(Ue4CodeDirectory, ProjectData);
			}

			return true;
		}
		private class BuildTarget
		{
			public readonly string Name;
			public readonly TargetType Type;
			public readonly UnrealTargetPlatform Platform;
			public readonly UnrealTargetConfiguration Configuration;
			public readonly FileReference CompilerPath;
			public readonly Dictionary<DirectoryReference, string> ModuleCommandLines;

			public BuildTarget(string InName, TargetType InType, UnrealTargetPlatform InPlatform, UnrealTargetConfiguration InConfiguration, FileReference InCompilerPath, Dictionary<DirectoryReference, string> InModulesCommandLines)
			{
				Name = InName;
				Type = InType;
				Platform = InPlatform;
				Configuration = InConfiguration;
				CompilerPath = InCompilerPath;
				ModuleCommandLines = InModulesCommandLines;
			}

			public override string ToString()
			{
				return Name.ToString() + " " + Type.ToString();
			}
		}

		protected override void AddTargetForIntellisense(UEBuildTarget Target)
		{
			base.AddTargetForIntellisense(Target);

			bool UsingClang = true;
			FileReference CompilerPath;
			if (HostPlatform == UnrealTargetPlatform.Win64)
			{
				VCEnvironment Environment = VCEnvironment.Create(WindowsPlatform.GetDefaultCompiler(null), Target.Platform, Target.Rules.WindowsPlatform.Architecture, null, Target.Rules.WindowsPlatform.WindowsSdkVersion, null);
				CompilerPath = FileReference.FromString(Environment.CompilerPath.FullName);
				UsingClang = false;
			}
			else if (HostPlatform == UnrealTargetPlatform.Linux)
			{
				CompilerPath = FileReference.FromString(LinuxCommon.WhichClang());
			}
			else if (HostPlatform == UnrealTargetPlatform.Mac)
			{
				MacToolChainSettings Settings = new MacToolChainSettings(false);
				CompilerPath = FileReference.FromString(Settings.ToolchainDir + "clang++");
			}
			else
			{
				throw new Exception("Unknown platform " + HostPlatform.ToString());
			}

			// we do not need to keep track of which binary the invocation belongs to, only which target, as such we join all binaries into a single set
			Dictionary<DirectoryReference, string> ModuleDirectoryToCompileCommand = new Dictionary<DirectoryReference, string>();

			// Generate a compile environment for each module in the binary
			CppCompileEnvironment GlobalCompileEnvironment = Target.CreateCompileEnvironmentForProjectFiles();
			foreach (UEBuildBinary Binary in Target.Binaries)
			{
				CppCompileEnvironment BinaryCompileEnvironment = Binary.CreateBinaryCompileEnvironment(GlobalCompileEnvironment);
				foreach (UEBuildModuleCPP Module in Binary.Modules.OfType<UEBuildModuleCPP>())
				{
					CppCompileEnvironment ModuleCompileEnvironment = Module.CreateCompileEnvironmentForIntellisense(Target.Rules, BinaryCompileEnvironment);

					List<FileReference> ForceIncludePaths = new List<FileReference>(ModuleCompileEnvironment.ForceIncludeFiles.Select(x => x.Location));
					if (ModuleCompileEnvironment.PrecompiledHeaderIncludeFilename != null)
					{
						ForceIncludePaths.Add(ModuleCompileEnvironment.PrecompiledHeaderIncludeFilename);
					}

					StringBuilder CommandBuilder = new StringBuilder();

					foreach (FileReference ForceIncludeFile in ForceIncludePaths)
					{
						CommandBuilder.AppendFormat("{0} \"{1}\" {2}", UsingClang ? "-include" : "/FI", ForceIncludeFile.FullName, Environment.NewLine);
					}
					foreach (string Definition in ModuleCompileEnvironment.Definitions)
					{
						CommandBuilder.AppendFormat("{0} \"{1}\" {2}", UsingClang ? "-D" : "/D", Definition, Environment.NewLine);
					}
					foreach (DirectoryReference IncludePath in ModuleCompileEnvironment.UserIncludePaths)
					{
						CommandBuilder.AppendFormat("{0} \"{1}\" {2}",  UsingClang ? "-I" : "/I", IncludePath, Environment.NewLine);
					}
					foreach (DirectoryReference IncludePath in ModuleCompileEnvironment.SystemIncludePaths)
					{
						CommandBuilder.AppendFormat("{0} \"{1}\" {2}", UsingClang ? "-I" : "/I", IncludePath, Environment.NewLine);
					}

					ModuleDirectoryToCompileCommand.Add(Module.ModuleDirectory, CommandBuilder.ToString());
				}
			}

			BuildTargets.Add(new BuildTarget(Target.TargetName, Target.TargetType, Target.Platform, Target.Configuration, CompilerPath, ModuleDirectoryToCompileCommand));
		}

		private class ProjectData
		{
			public enum EOutputType
			{
				Library,
				Exe,

				WinExe, // some projects have this so we need to read it, but it will be converted across to Exe so no code should handle it!
			}

			public class BuildProduct
			{
				public FileReference OutputFile { get; set; }
				public FileReference UProjectFile { get; set; }
				public UnrealTargetConfiguration Config { get; set; }
				public UnrealTargetPlatform Platform { get; set; }
				public EOutputType OutputType { get; set; }
		
				public CsProjectInfo CSharpInfo { get; set; }

				public override string ToString()
				{
					return Platform.ToString() + " " + Config.ToString(); 
				}

			}

			public class Target
			{
				public string Name;
				public TargetType Type;
				public List<BuildProduct> BuildProducts = new List<BuildProduct>();

				public Target(Project InParentProject, string InName, TargetType InType)
				{
					Name = InName;
					Type = InType;
					InParentProject.Targets.Add(this);
				}

				public override string ToString()
				{
					return Name.ToString() + " " + Type.ToString(); 
				}
			}

			public class Project
			{
				public string Name;
				public ProjectFile SourceProject;
				public List<Target> Targets = new List<Target>();

				public override string ToString()
				{
					return Name;
				}
			}

			public List<Project> NativeProjects = new List<Project>();
			public List<Project> CSharpProjects = new List<Project>();
			public List<Project> AllProjects = new List<Project>();
		}


		private ProjectData GatherProjectData(List<ProjectFile> InProjects, PlatformProjectGeneratorCollection PlatformProjectGenerators)
		{
			ProjectData ProjectData = new ProjectData();

			foreach (ProjectFile Project in InProjects)
			{
				// Create new project record
				ProjectData.Project NewProject = new ProjectData.Project();
				NewProject.Name = Project.ProjectFilePath.GetFileNameWithoutExtension();
				NewProject.SourceProject = Project;

				ProjectData.AllProjects.Add(NewProject);

				// Add into the correct easy-access list
				if (Project is VSCodeProject)
				{
					foreach (ProjectTarget Target in Project.ProjectTargets)
					{
						Array Configs = Enum.GetValues(typeof(UnrealTargetConfiguration));
						List<UnrealTargetPlatform> Platforms = new List<UnrealTargetPlatform>(Target.TargetRules.GetSupportedPlatforms());

						ProjectData.Target NewTarget = new ProjectData.Target(NewProject, Target.TargetRules.Name, Target.TargetRules.Type);

						if (HostPlatform != UnrealTargetPlatform.Win64)
						{
							Platforms.Remove(UnrealTargetPlatform.Win64);
							Platforms.Remove(UnrealTargetPlatform.Win32);
						}

						foreach (UnrealTargetPlatform Platform in Platforms)
						{
							var BuildPlatform = UEBuildPlatform.GetBuildPlatform(Platform, true);
							if (SupportedPlatforms.Contains(Platform) && (BuildPlatform != null) && (BuildPlatform.HasRequiredSDKsInstalled() == SDKStatus.Valid))
							{
								foreach (UnrealTargetConfiguration Config in Configs)
								{
									if (MSBuildProjectFile.IsValidProjectPlatformAndConfiguration(Target, Platform, Config, PlatformProjectGenerators))
									{
										NewTarget.BuildProducts.Add(new ProjectData.BuildProduct
										{
											Platform = Platform,
											Config = Config,
											UProjectFile = Target.UnrealProjectFilePath,
											OutputType = ProjectData.EOutputType.Exe,
											OutputFile = GetExecutableFilename(Project, Target, Platform, Config),
											CSharpInfo = null
										});
									}
								}
							}
						}
					}

					ProjectData.NativeProjects.Add(NewProject);
				}
				else
				{
					VCSharpProjectFile VCSharpProject = Project as VCSharpProjectFile;

					if (VCSharpProject.IsDotNETCoreProject() == bBuildingForDotNetCore)
					{
						string ProjectName = Project.ProjectFilePath.GetFileNameWithoutExtension();

						ProjectData.Target Target = new ProjectData.Target(NewProject, ProjectName, TargetType.Program);

						UnrealTargetConfiguration[] Configs = { UnrealTargetConfiguration.Debug, UnrealTargetConfiguration.Development };

						foreach (UnrealTargetConfiguration Config in Configs)
						{
							CsProjectInfo Info = VCSharpProject.GetProjectInfo(Config);

							if (!Info.IsDotNETCoreProject() && Info.Properties.ContainsKey("OutputPath"))
							{
								ProjectData.EOutputType OutputType;
								string OutputTypeName;
								if (Info.Properties.TryGetValue("OutputType", out OutputTypeName))
								{
									OutputType = (ProjectData.EOutputType)Enum.Parse(typeof(ProjectData.EOutputType), OutputTypeName);
								}
								else
								{
									OutputType = ProjectData.EOutputType.Library;
								}

								if (OutputType == ProjectData.EOutputType.WinExe)
								{
									OutputType = ProjectData.EOutputType.Exe;
								}

								FileReference OutputFile = null;
								HashSet<FileReference> ProjectBuildProducts = new HashSet<FileReference>();
								Info.FindCompiledBuildProducts(DirectoryReference.Combine(VCSharpProject.ProjectFilePath.Directory, Info.Properties["OutputPath"]), ProjectBuildProducts);
								foreach (FileReference ProjectBuildProduct in ProjectBuildProducts)
								{
									if ((OutputType == ProjectData.EOutputType.Exe && ProjectBuildProduct.GetExtension() == FrameworkExecutableExtension) ||
										(OutputType == ProjectData.EOutputType.Library && ProjectBuildProduct.GetExtension() == FrameworkLibraryExtension))
									{
										OutputFile = ProjectBuildProduct;
										break;
									}
								}

								if (OutputFile != null)
								{
									Target.BuildProducts.Add(new ProjectData.BuildProduct
									{
										Platform = HostPlatform,
										Config = Config,
										OutputFile = OutputFile,
										OutputType = OutputType,
										CSharpInfo = Info
									});
								}
							}
						}

						ProjectData.CSharpProjects.Add(NewProject);
					}
				}
			}

			return ProjectData;
		}

		private void WriteCppPropertiesFile(DirectoryReference OutputDirectory, ProjectData Projects)
		{
			DirectoryReference.CreateDirectory(OutputDirectory);

			JsonFile OutFile = new JsonFile();

			OutFile.BeginRootObject();
			{
				OutFile.BeginArray("configurations");
				{
					HashSet<FileReference> AllSourceFiles = new HashSet<FileReference>();
					Dictionary<DirectoryReference, string> AllModuleCommandLines = new Dictionary<DirectoryReference, string>();
					FileReference CompilerPath = null;
					
					foreach (ProjectData.Project Project in Projects.AllProjects)
					{
						foreach (ProjectData.Target ProjectTarget in Project.Targets)
						{
							BuildTarget BuildTarget = BuildTargets.FirstOrDefault(Target => Target.Name == ProjectTarget.Name);

							// we do not generate intellisense for every target, as that just causes a lot of redundancy, as such we will not find a mapping for a lot of the targets
							if (BuildTarget == null)
								continue;

							string Name = string.Format("{0} {1} {2} {3} ({4})", ProjectTarget.Name, ProjectTarget.Type, BuildTarget.Platform, BuildTarget.Configuration, Project.Name);
							WriteConfiguration(Name, Project.Name, Project.SourceProject.SourceFiles.Select(x => x.Reference), BuildTarget.CompilerPath, BuildTarget.ModuleCommandLines, OutFile, OutputDirectory);

							AllSourceFiles.UnionWith(Project.SourceProject.SourceFiles.Select(x => x.Reference));

							CompilerPath = BuildTarget.CompilerPath;
							foreach (KeyValuePair<DirectoryReference, string> Pair in BuildTarget.ModuleCommandLines)
							{
								if(!AllModuleCommandLines.ContainsKey(Pair.Key))
								{
									AllModuleCommandLines[Pair.Key] = Pair.Value;
								}
							}
						}
					}

					string DefaultConfigName;
					if (HostPlatform == UnrealTargetPlatform.Linux)
					{
						DefaultConfigName = "Linux";
					}
					else if (HostPlatform == UnrealTargetPlatform.Mac)
					{
						DefaultConfigName = "Mac";
					}
					else
					{
						DefaultConfigName = "Win32";
					}

					WriteConfiguration(DefaultConfigName, "Default", AllSourceFiles, CompilerPath, AllModuleCommandLines, OutFile, OutputDirectory);
				}
				OutFile.EndArray();
			}
			OutFile.EndRootObject();

			OutFile.Write(FileReference.Combine(OutputDirectory, "c_cpp_properties.json"));
		}

		private void WriteConfiguration(string Name, string ProjectName, IEnumerable<FileReference> SourceFiles, FileReference CompilerPath, Dictionary<DirectoryReference, string> ModuleCommandLines, JsonFile OutFile, DirectoryReference OutputDirectory)
		{
			OutFile.BeginObject();

			OutFile.AddField("name", Name);

			if (HostPlatform == UnrealTargetPlatform.Win64)
			{
				OutFile.AddField("intelliSenseMode", "msvc-x64");
			}
			else
			{
				OutFile.AddField("intelliSenseMode", "clang-x64");
			}

			if (HostPlatform == UnrealTargetPlatform.Mac)
			{
				OutFile.BeginArray("macFrameworkPath");
				{
					OutFile.AddUnnamedField("/System/Library/Frameworks");
					OutFile.AddUnnamedField("/Library/Frameworks");
				}
				OutFile.EndArray();
			}

			FileReference CompileCommands = FileReference.Combine(OutputDirectory, string.Format("compileCommands_{0}.json", ProjectName));
			WriteCompileCommands(CompileCommands, SourceFiles, CompilerPath, ModuleCommandLines);
			OutFile.AddField("compileCommands", MakePathString(CompileCommands, bInAbsolute: true, bForceSkipQuotes: true));

			OutFile.EndObject();
		}

		private void WriteNativeTaskDeployAndroid(ProjectData.Project InProject, JsonFile OutFile, ProjectData.Target Target, ProjectData.BuildProduct BuildProduct)
		{
			if (BuildProduct.UProjectFile == null)
			{
				return;
			}

			string[] ConfigTypes = new string[] { "Cook+Deploy", "Cook", "Deploy" };

			foreach (string ConfigType in ConfigTypes)
			{
				OutFile.BeginObject();
				{
					string TaskName = String.Format("{0} {1} {2} {3}", Target.Name, BuildProduct.Platform.ToString(), BuildProduct.Config, ConfigType);
					OutFile.AddField("label", TaskName);
					OutFile.AddField("group", "build");

					if (HostPlatform == UnrealTargetPlatform.Win64)
					{
						OutFile.AddField("command", MakePathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Build", "BatchFiles", "RunUAT.bat")));
					}
					else
					{
						OutFile.AddField("command", MakePathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Build", "BatchFiles", "RunUAT.sh")));
					}

					OutFile.BeginArray("args");
					{
						OutFile.AddUnnamedField("BuildCookRun");
						OutFile.AddUnnamedField("-ScriptsForProject=" + BuildProduct.UProjectFile.ToNormalizedPath());
						OutFile.AddUnnamedField("-Project=" + BuildProduct.UProjectFile.ToNormalizedPath());
						OutFile.AddUnnamedField("-noP4");
						OutFile.AddUnnamedField(String.Format("-ClientConfig={0}", BuildProduct.Config.ToString()));
						OutFile.AddUnnamedField(String.Format("-ServerConfig={0}", BuildProduct.Config.ToString()));
						OutFile.AddUnnamedField("-NoCompileEditor");
						OutFile.AddUnnamedField("-utf8output");
						OutFile.AddUnnamedField(String.Format("-Platform={0}", BuildProduct.Platform.ToString()));
						OutFile.AddUnnamedField(String.Format("-TargetPlatform={0}", BuildProduct.Platform.ToString()));
						OutFile.AddUnnamedField("-ini:Game:[/Script/UnrealEd.ProjectPackagingSettings]:BlueprintNativizationMethod=Disabled");
						OutFile.AddUnnamedField("-Compressed");
						OutFile.AddUnnamedField("-IterativeCooking");
						OutFile.AddUnnamedField("-IterativeDeploy");
						switch (ConfigType)
						{
							case "Cook+Deploy":
								{
									OutFile.AddUnnamedField("-Cook");
									OutFile.AddUnnamedField("-Stage");
									OutFile.AddUnnamedField("-Deploy");
									break;
								}
							case "Cook":
								{
									OutFile.AddUnnamedField("-Cook");
									break;
								}
							case "Deploy":
								{
									OutFile.AddUnnamedField("-DeploySoToDevice");
									OutFile.AddUnnamedField("-SkipCook");
									OutFile.AddUnnamedField("-Stage");
									OutFile.AddUnnamedField("-Deploy");
									break;
								}
						}
					}
					OutFile.EndArray();
					OutFile.BeginArray("dependsOn");
					{
						switch (ConfigType)
						{
							case "Cook+Deploy":
							case "Cook":
								{
									OutFile.AddUnnamedField(String.Format("{0}Editor {1} Development Build", Target.Name, HostPlatform.ToString()));
									OutFile.AddUnnamedField(String.Format("{0} {1} {2} Build", Target.Name, BuildProduct.Platform.ToString(), BuildProduct.Config));
									break;
								}
							default:
								{
									OutFile.AddUnnamedField(String.Format("{0} {1} {2} Build", Target.Name, BuildProduct.Platform.ToString(), BuildProduct.Config));
									break;
								}
						}
					}
					OutFile.EndArray();

					OutFile.AddField("type", "shell");

					OutFile.BeginObject("options");
					{
						OutFile.AddField("cwd", MakeUnquotedPathString(UE4ProjectRoot, EPathType.Absolute));
					}
					OutFile.EndObject();
				}
				OutFile.EndObject();
			}

		}

		private void WriteCompileCommands(FileReference CompileCommandsFile, IEnumerable<FileReference> SourceFiles, 
			FileReference CompilerPath, Dictionary<DirectoryReference, string> ModuleCommandLines)
		{
			// this creates a compileCommands.json
			// see VsCode Docs - https://code.visualstudio.com/docs/cpp/c-cpp-properties-schema-reference (compileCommands attribute)
			// and the clang format description https://clang.llvm.org/docs/JSONCompilationDatabase.html

			using (JsonWriter Writer = new JsonWriter(CompileCommandsFile))
			{
				Writer.WriteArrayStart();

				DirectoryReference ResponseFileDir = DirectoryReference.Combine(CompileCommandsFile.Directory, CompileCommandsFile.GetFileNameWithoutExtension());
				DirectoryReference.CreateDirectory(ResponseFileDir);

				Dictionary<DirectoryReference, FileReference> DirectoryToResponseFile = new Dictionary<DirectoryReference, FileReference>();
				foreach(KeyValuePair<DirectoryReference, string> Pair in ModuleCommandLines)
				{
					FileReference ResponseFile = FileReference.Combine(ResponseFileDir, String.Format("{0}.{1}.rsp", Pair.Key.GetDirectoryName(), DirectoryToResponseFile.Count));
					FileReference.WriteAllText(ResponseFile, Pair.Value);
					DirectoryToResponseFile.Add(Pair.Key, ResponseFile);
				}

				foreach (FileReference File in SourceFiles.OrderBy(x => x.FullName))
				{
					DirectoryReference Directory = File.Directory;

					FileReference ResponseFile = null;
					if (!DirectoryToResponseFile.TryGetValue(Directory, out ResponseFile))
					{
						for (DirectoryReference ParentDir = Directory; ParentDir != null && ParentDir != UnrealBuildTool.RootDirectory; ParentDir = ParentDir.ParentDirectory)
						{
							if (DirectoryToResponseFile.TryGetValue(ParentDir, out ResponseFile))
							{
								break;
							}
						}
						DirectoryToResponseFile[Directory] = ResponseFile;
					}

					if (ResponseFile == null)
					{
						// no compiler command associated with the file, will happen for any file that is not a C++ file and is not an error
						continue;
					}

					Writer.WriteObjectStart();
					Writer.WriteValue("file", MakePathString(File, bInAbsolute: true, bForceSkipQuotes: true));
					Writer.WriteValue("command", String.Format("{0} @\"{1}\"", CompilerPath, ResponseFile.FullName));
					Writer.WriteValue("directory", UnrealBuildTool.EngineSourceDirectory.ToString());
					Writer.WriteObjectEnd();
				}
				Writer.WriteArrayEnd();
			}
		}

		private void WriteNativeTask(ProjectData.Project InProject, JsonFile OutFile)
		{
			string[] Commands = { "Build", "Rebuild", "Clean" };

			foreach (ProjectData.Target Target in InProject.Targets)
			{
				foreach (ProjectData.BuildProduct BuildProduct in Target.BuildProducts)
				{
					foreach (string BaseCommand in Commands)
					{
						string Command = BaseCommand == "Rebuild" ? "Build" : BaseCommand;
						string TaskName = String.Format("{0} {1} {2} {3}", Target.Name, BuildProduct.Platform.ToString(), BuildProduct.Config, BaseCommand);
						string CleanTaskName = String.Format("{0} {1} {2} {3}", Target.Name, BuildProduct.Platform.ToString(), BuildProduct.Config, "Clean");

						OutFile.BeginObject();
						{
							OutFile.AddField("label", TaskName);
							OutFile.AddField("group", "build");

							string CleanParam = Command == "Clean" ? "-clean" : null;

							if (bBuildingForDotNetCore)
							{
								OutFile.AddField("command", "dotnet");
							}
							else
							{
								if (HostPlatform == UnrealTargetPlatform.Win64)
								{
									OutFile.AddField("command", MakePathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Build", "BatchFiles", Command + ".bat")));
									CleanParam = null;
								}
								else
								{
									OutFile.AddField("command", MakePathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Build", "BatchFiles", HostPlatform.ToString(), "Build.sh")));

									if (Command == "Clean")
									{
										CleanParam = "-clean";
									}
								}
							}

							OutFile.BeginArray("args");
							{
								if (bBuildingForDotNetCore)
								{
									OutFile.AddUnnamedField(MakeUnquotedPathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Binaries", "DotNET", "UnrealBuildTool_NETCore.dll"), EPathType.Relative));
								}

								OutFile.AddUnnamedField(Target.Name);
								OutFile.AddUnnamedField(BuildProduct.Platform.ToString());
								OutFile.AddUnnamedField(BuildProduct.Config.ToString());
								if (bForeignProject)
								{
									OutFile.AddUnnamedField(MakeUnquotedPathString(BuildProduct.UProjectFile, EPathType.Relative, null));
								}
								OutFile.AddUnnamedField("-waitmutex");

								if (!string.IsNullOrEmpty(CleanParam))
								{
									OutFile.AddUnnamedField(CleanParam);
								}
							}
							OutFile.EndArray();
							OutFile.AddField("problemMatcher", "$msCompile");
							if (!bForeignProject || BaseCommand == "Rebuild")
							{
								OutFile.BeginArray("dependsOn");
								{
									if (!bForeignProject)
									{
										if (Command == "Build" && Target.Type == TargetType.Editor)
										{
											OutFile.AddUnnamedField("ShaderCompileWorker " + HostPlatform.ToString() + " Development Build");
										}
										else
										{
											OutFile.AddUnnamedField("UnrealBuildTool " + HostPlatform.ToString() + " Development Build");
										}
									}

									if (BaseCommand == "Rebuild")
									{
										OutFile.AddUnnamedField(CleanTaskName);
									}
								}
								OutFile.EndArray();
							}

							OutFile.AddField("type", "shell");

							OutFile.BeginObject("options");
							{
								OutFile.AddField("cwd", MakeUnquotedPathString(UE4ProjectRoot, EPathType.Absolute));
							}
							OutFile.EndObject();
						}
						OutFile.EndObject();

						if (BuildProduct.Platform == UnrealTargetPlatform.Android && BaseCommand.Equals("Build"))
						{
							WriteNativeTaskDeployAndroid(InProject, OutFile, Target, BuildProduct);
						}
					}
				}
			}
		}

		private void WriteCSharpTask(ProjectData.Project InProject, JsonFile OutFile)
		{
			VCSharpProjectFile ProjectFile = InProject.SourceProject as VCSharpProjectFile;
			bool bIsDotNetCore = ProjectFile.IsDotNETCoreProject();
			string[] Commands = { "Build", "Clean" };

			foreach (ProjectData.Target Target in InProject.Targets)
			{
				foreach (ProjectData.BuildProduct BuildProduct in Target.BuildProducts)
				{
					foreach (string Command in Commands)
					{
						string TaskName = String.Format("{0} {1} {2} {3}", Target.Name, BuildProduct.Platform, BuildProduct.Config, Command);

						OutFile.BeginObject();
						{
							OutFile.AddField("label", TaskName);
							OutFile.AddField("group", "build");
							if (bIsDotNetCore)
							{
								OutFile.AddField("command", "dotnet");
							}
							else
							{
								if (Utils.IsRunningOnMono)
								{
									OutFile.AddField("command", MakePathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Build", "BatchFiles", HostPlatform.ToString(), "RunXBuild.sh")));
								}
								else
								{
									OutFile.AddField("command", MakePathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Build", "BatchFiles", "MSBuild.bat")));
								}
							}
							OutFile.BeginArray("args");
							{
								if (bIsDotNetCore)
								{
									OutFile.AddUnnamedField(Command.ToLower());
								}
								else
								{
									OutFile.AddUnnamedField("/t:" + Command.ToLower());
								}
								
								DirectoryReference BuildRoot = HostPlatform == UnrealTargetPlatform.Win64 ? UE4ProjectRoot : DirectoryReference.Combine(UE4ProjectRoot, "Engine");
								OutFile.AddUnnamedField(MakeUnquotedPathString(InProject.SourceProject.ProjectFilePath, EPathType.Relative, BuildRoot));

								OutFile.AddUnnamedField("/p:GenerateFullPaths=true");
								if (HostPlatform == UnrealTargetPlatform.Win64)
								{
									OutFile.AddUnnamedField("/p:DebugType=portable");
								}
								OutFile.AddUnnamedField("/verbosity:minimal");

								if (bIsDotNetCore)
								{
									OutFile.AddUnnamedField("--configuration");
									OutFile.AddUnnamedField(BuildProduct.Config.ToString());
									OutFile.AddUnnamedField("--output");
									OutFile.AddUnnamedField(MakePathString(BuildProduct.OutputFile.Directory));
								}
								else
								{
									OutFile.AddUnnamedField("/p:Configuration=" + BuildProduct.Config.ToString());
								}
							}
							OutFile.EndArray();
						}
						OutFile.AddField("problemMatcher", "$msCompile");

						if (!bBuildingForDotNetCore)
						{
							OutFile.AddField("type", "shell");
						}

						OutFile.BeginObject("options");
						{
							OutFile.AddField("cwd", MakeUnquotedPathString(UE4ProjectRoot, EPathType.Absolute));
						}

						OutFile.EndObject();
						OutFile.EndObject();
					}
				}
			}
		}

		private void WriteTasksFile(ProjectData ProjectData)
		{
			JsonFile OutFile = new JsonFile();

			OutFile.BeginRootObject();
			{
				OutFile.AddField("version", "2.0.0");

				OutFile.BeginArray("tasks");
				{
					foreach (ProjectData.Project NativeProject in ProjectData.NativeProjects)
					{
						WriteNativeTask(NativeProject, OutFile);
					}

					foreach (ProjectData.Project CSharpProject in ProjectData.CSharpProjects)
					{
						WriteCSharpTask(CSharpProject, OutFile);
					}

					OutFile.EndArray();
				}
			}
			OutFile.EndRootObject();

			OutFile.Write(FileReference.Combine(VSCodeDir, "tasks.json"));
		}
		
		private FileReference GetExecutableFilename(ProjectFile Project, ProjectTarget Target, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration)
		{
			TargetRules TargetRulesObject = Target.TargetRules;
			FileReference TargetFilePath = Target.TargetFilePath;
			string TargetName = TargetFilePath == null ? Project.ProjectFilePath.GetFileNameWithoutExtension() : TargetFilePath.GetFileNameWithoutAnyExtensions();
			string UBTPlatformName = Platform.ToString();

			// Setup output path
			UEBuildPlatform BuildPlatform = UEBuildPlatform.GetBuildPlatform(Platform);

			// Figure out if this is a monolithic build
			bool bShouldCompileMonolithic = BuildPlatform.ShouldCompileMonolithicBinary(Platform);

			if (TargetRulesObject != null)
			{
				bShouldCompileMonolithic |= (Target.CreateRulesDelegate(Platform, Configuration).LinkType == TargetLinkType.Monolithic);
			}

			TargetType TargetRulesType = Target.TargetRules == null ? TargetType.Program : Target.TargetRules.Type;

			// Get the output directory
			DirectoryReference RootDirectory = UnrealBuildTool.EngineDirectory;
			if (TargetRulesType != TargetType.Program && (bShouldCompileMonolithic || TargetRulesObject.BuildEnvironment == TargetBuildEnvironment.Unique))
			{
				if(Target.UnrealProjectFilePath != null)
				{
					RootDirectory = Target.UnrealProjectFilePath.Directory;
				}
			}

			if (TargetRulesType == TargetType.Program)
			{
				if(Target.UnrealProjectFilePath != null)
				{
					RootDirectory = Target.UnrealProjectFilePath.Directory;
				}
			}

			// Get the output directory
			DirectoryReference OutputDirectory = DirectoryReference.Combine(RootDirectory, "Binaries", UBTPlatformName);

			// Get the executable name (minus any platform or config suffixes)
			string BinaryName;
			if(Target.TargetRules.BuildEnvironment == TargetBuildEnvironment.Shared && TargetRulesType != TargetType.Program)
			{
				BinaryName = UEBuildTarget.GetAppNameForTargetType(TargetRulesType);
			}
			else
			{
				BinaryName = TargetName;
			}

			// Make the output file path
			string BinaryFileName = UEBuildTarget.MakeBinaryFileName(BinaryName, Platform, Configuration, TargetRulesObject.Architecture, TargetRulesObject.UndecoratedConfiguration, UEBuildBinaryType.Executable);
			string ExecutableFilename = FileReference.Combine(OutputDirectory, BinaryFileName).FullName;

			// Include the path to the actual executable for a Mac app bundle
			if (Platform == UnrealTargetPlatform.Mac && !Target.TargetRules.bIsBuildingConsoleApplication)
			{
				ExecutableFilename += ".app/Contents/MacOS/" + Path.GetFileName(ExecutableFilename);
			}

			return new FileReference(ExecutableFilename);
		}

		private void WriteNativeLaunchConfigAndroidOculus(ProjectData.Project InProject, JsonFile OutFile, ProjectData.Target Target, ProjectData.BuildProduct BuildProduct)
		{
			ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, DirectoryReference.FromFile(BuildProduct.UProjectFile), BuildProduct.Platform);

			List<string> OculusMobileDevices;
			bool result = Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "PackageForOculusMobile", out OculusMobileDevices);
			// Check if packaging for oculus
			if (!result || OculusMobileDevices == null || OculusMobileDevices.Count == 0)
			{
				return;
			}

			// Get package name
			string PackageName;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "PackageName", out PackageName);
			if (PackageName.Contains("[PROJECT]"))
			{
				// project name must start with a letter
				if (!char.IsLetter(Target.Name[0]))
				{
					Trace.TraceWarning("Package name segments must all start with a letter. Please replace [PROJECT] with a valid name");
				}

				string ProjectName = Target.Name;
				// hyphens not allowed so change them to underscores in project name
				if (ProjectName.Contains("-"))
				{
					Trace.TraceWarning("Project name contained hyphens, converted to underscore");
					ProjectName = ProjectName.Replace("-", "_");
				}

				// check for special characters
				for (int Index = 0; Index < ProjectName.Length; Index++)
				{
					char c = ProjectName[Index];
					if (c != '.' && c != '_' && !char.IsLetterOrDigit(c))
					{
						Trace.TraceWarning("Project name contains illegal characters (only letters, numbers, and underscore allowed); please replace [PROJECT] with a valid name");
						ProjectName.Replace(c, '_');
					}
				}

				PackageName = PackageName.Replace("[PROJECT]", ProjectName);
			}

			// Get store version
			int StoreVersion = 1;
			int StoreVersionArm64 = 1;
			int StoreVersionArmV7 = 1;
			int StoreVersionOffsetArm64 = 0;
			int StoreVersionOffsetArmV7 = 0;
			Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "StoreVersion", out StoreVersion);
			Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "StoreVersionOffsetArm64", out StoreVersionOffsetArm64);
			Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "StoreVersionOffsetArmV7", out StoreVersionOffsetArmV7);
			StoreVersionArm64 = StoreVersion + StoreVersionOffsetArm64;
			StoreVersionArmV7 = StoreVersion + StoreVersionOffsetArmV7;

			DirectoryReference SymbolPathArm64 = DirectoryReference.Combine(
				BuildProduct.OutputFile.Directory,
				Target.Name + "_Symbols_v" + StoreVersionArm64.ToString(),
				Target.Name + "-arm64");

			DirectoryReference SymbolPathArmV7 = DirectoryReference.Combine(
				BuildProduct.OutputFile.Directory,
				Target.Name + "_Symbols_v" + StoreVersionArmV7.ToString(),
				Target.Name + "-armv7");


			string LaunchTaskName = String.Format("{0} {1} {2} Deploy", Target.Name, BuildProduct.Platform, BuildProduct.Config);

			List<string> ConfigTypes = new List<string>();
			ConfigTypes.Add("Launch");
			if (BuildProduct.Config == UnrealTargetConfiguration.Development)
			{
				ConfigTypes.Add("Attach");
			}

			foreach (string ConfigType in ConfigTypes)
			{
				OutFile.BeginObject();
				{
					OutFile.AddField("name", Target.Name + " Oculus (" + BuildProduct.Config.ToString() + ") " + ConfigType);
					OutFile.AddField("request", ConfigType.ToLowerInvariant());
					if (ConfigType == "Launch")
					{
						OutFile.AddField("preLaunchTask", LaunchTaskName);
					}
					OutFile.AddField("type", "fb-lldb");

					OutFile.BeginObject("android");
					{
						OutFile.BeginObject("application");
						{
							OutFile.AddField("package", PackageName);
							OutFile.AddField("activity", "com.epicgames.ue4.GameActivity");
						}
						OutFile.EndObject();

						OutFile.BeginObject("lldbConfig");
						{
							OutFile.BeginArray("librarySearchPaths");
							OutFile.AddUnnamedField("\\\"" + SymbolPathArm64.ToNormalizedPath() + "\\\"");
							OutFile.AddUnnamedField("\\\"" + SymbolPathArmV7.ToNormalizedPath() + "\\\"");
							OutFile.EndArray();

							OutFile.BeginArray("lldbPreTargetCreateCommands");
							FileReference UE4DataFormatters = FileReference.Combine(UE4ProjectRoot, "Engine", "Extras", "LLDBDataFormatters", "UE4DataFormatters_2ByteChars.py");
							OutFile.AddUnnamedField("command script import \\\"" + UE4DataFormatters.FullName.Replace("\\", "/") + "\\\"");
							OutFile.EndArray();

							OutFile.BeginArray("lldbPostTargetCreateCommands");
							//on Oculus devices, we use SIGILL for input redirection, so the debugger shouldn't catch it.
							OutFile.AddUnnamedField("process handle --pass true --stop false --notify true SIGILL");
							OutFile.EndArray();
						}
						OutFile.EndObject();
					}
					OutFile.EndObject();
				}
				OutFile.EndObject();
			}
		}

		private void WriteNativeLaunchConfig(ProjectData.Project InProject, JsonFile OutFile)
		{
			foreach (ProjectData.Target Target in InProject.Targets)
			{
				foreach (ProjectData.BuildProduct BuildProduct in Target.BuildProducts)
				{
					if (BuildProduct.Platform == HostPlatform)
					{
						string LaunchTaskName = String.Format("{0} {1} {2} Build", Target.Name, BuildProduct.Platform, BuildProduct.Config);

						OutFile.BeginObject();
						{
							OutFile.AddField("name", Target.Name + " (" + BuildProduct.Config.ToString() + ")");
							OutFile.AddField("request", "launch");
							OutFile.AddField("preLaunchTask", LaunchTaskName);
							OutFile.AddField("program", MakeUnquotedPathString(BuildProduct.OutputFile, EPathType.Absolute));								
							
							OutFile.BeginArray("args");
							{
								if (Target.Type == TargetRules.TargetType.Editor)
								{
									if (InProject.Name != "UE4")
									{
										if (bForeignProject)
										{
											OutFile.AddUnnamedField(MakePathString(BuildProduct.UProjectFile, false, true));
										}
										else
										{
											OutFile.AddUnnamedField(InProject.Name);
										}
									}
								}

							}
							OutFile.EndArray();

/*
							DirectoryReference CWD = BuildProduct.OutputFile.Directory;
							while (HostPlatform == UnrealTargetPlatform.Mac && CWD != null && CWD.ToString().Contains(".app"))
							{
								CWD = CWD.ParentDirectory;
							}
							if (CWD != null)
							{
								OutFile.AddField("cwd", MakePathString(CWD, true, true));
							}
 */
							OutFile.AddField("cwd", MakeUnquotedPathString(UE4ProjectRoot, EPathType.Absolute));

							if (HostPlatform == UnrealTargetPlatform.Win64)
							{
								OutFile.AddField("stopAtEntry", false);
								OutFile.AddField("externalConsole", true);

								OutFile.AddField("type", "cppvsdbg");
								OutFile.AddField("visualizerFile", MakeUnquotedPathString(FileReference.Combine(UE4ProjectRoot, "Engine", "Extras", "VisualStudioDebugging", "UE4.natvis"), EPathType.Absolute));
							}
							else
							{
								OutFile.AddField("type", "lldb");
							}
						}
						OutFile.EndObject();
					}
					else if (BuildProduct.Platform == UnrealTargetPlatform.Android)
					{
						WriteNativeLaunchConfigAndroidOculus(InProject, OutFile, Target, BuildProduct);
					}
				}
			}
		}

		private void WriteSingleCSharpLaunchConfig(JsonFile OutFile, string InTaskName, string InBuildTaskName, FileReference InExecutable, string[] InArgs, bool bIsDotNetCore)
		{
			OutFile.BeginObject();
			{
				OutFile.AddField("name", InTaskName);

				if (bIsDotNetCore)
				{
					OutFile.AddField("type", "coreclr");
				}
				else
				{
					if (HostPlatform == UnrealTargetPlatform.Win64)
					{
						OutFile.AddField("type", "clr");
					}
					else
					{
						OutFile.AddField("type", "mono");
					}
				}

				OutFile.AddField("request", "launch");

				if (!string.IsNullOrEmpty(InBuildTaskName))
				{
					OutFile.AddField("preLaunchTask", InBuildTaskName);
				}
				
				DirectoryReference CWD = UE4ProjectRoot;

				if (bIsDotNetCore)
				{
					OutFile.AddField("program", "dotnet");
					OutFile.BeginArray("args");
					{
						OutFile.AddUnnamedField(MakePathString(InExecutable));

						if (InArgs != null)
						{
							foreach (string Arg in InArgs)
							{
								OutFile.AddUnnamedField(Arg);
							}
						}
					}
					OutFile.EndArray();
					OutFile.AddField("externalConsole", true);
					OutFile.AddField("stopAtEntry", false);
				}
				else
				{
					OutFile.AddField("program", MakeUnquotedPathString(InExecutable, EPathType.Absolute));

					if (HostPlatform == UnrealTargetPlatform.Win64)
					{
						OutFile.AddField("console", "externalTerminal");
					}
					else
					{
						OutFile.AddField("console", "internalConsole");
					}

					OutFile.BeginArray("args");
					if (InArgs != null)
					{
						foreach (string Arg in InArgs)
						{
							OutFile.AddUnnamedField(Arg);
						}
					}
					OutFile.EndArray();

					OutFile.AddField("internalConsoleOptions", "openOnSessionStart");
				}

				OutFile.AddField("cwd", MakeUnquotedPathString(CWD, EPathType.Absolute));
			}
			OutFile.EndObject();
		}

		private void WriteCSharpLaunchConfig(ProjectData.Project InProject, JsonFile OutFile)
		{
			VCSharpProjectFile CSharpProject = InProject.SourceProject as VCSharpProjectFile;
			bool bIsDotNetCore = CSharpProject.IsDotNETCoreProject();

			foreach (ProjectData.Target Target in InProject.Targets)
			{
				foreach (ProjectData.BuildProduct BuildProduct in Target.BuildProducts)
				{
					if (BuildProduct.OutputType == ProjectData.EOutputType.Exe)
					{
						string TaskName = String.Format("{0} ({1})", Target.Name, BuildProduct.Config);
						string BuildTaskName = String.Format("{0} {1} {2} Build", Target.Name, HostPlatform, BuildProduct.Config);

						WriteSingleCSharpLaunchConfig(OutFile, TaskName, BuildTaskName, BuildProduct.OutputFile, null, bIsDotNetCore);
					}
				}
			}
		}

		private void WriteLaunchFile(ProjectData ProjectData)
		{
			JsonFile OutFile = new JsonFile();

			OutFile.BeginRootObject();
			{
				OutFile.AddField("version", "0.2.0");
				OutFile.BeginArray("configurations");
				{
					foreach (ProjectData.Project Project in ProjectData.NativeProjects)
					{
						WriteNativeLaunchConfig(Project, OutFile);
					}

					foreach (ProjectData.Project Project in ProjectData.CSharpProjects)
					{
						WriteCSharpLaunchConfig(Project, OutFile);
					}
				}

				// Add in a special task for regenerating project files
				string PreLaunchTask = "";
				List<string> Args = new List<string>();
				Args.Add("-projectfiles");
				Args.Add("-vscode");

				if (bForeignProject)
				{
					Args.Add("-project=" + MakeUnquotedPathString(OnlyGameProject, EPathType.Absolute));
					Args.Add("-game");
					Args.Add("-engine");
				}
				else
				{
					PreLaunchTask = "UnrealBuildTool " + HostPlatform.ToString() + " Development Build";
				}

				WriteSingleCSharpLaunchConfig(
					OutFile,
					"Generate Project Files",
					PreLaunchTask,
					FileReference.Combine(UE4ProjectRoot, "Engine", "Binaries", "DotNET", "UnrealBuildTool.exe"),
					Args.ToArray(),
					bBuildingForDotNetCore
				);

				OutFile.EndArray();
			}
			OutFile.EndRootObject();

			OutFile.Write(FileReference.Combine(VSCodeDir, "launch.json"));
		}

		private void WriteWorkspaceIgnoreFile(List<ProjectFile> Projects)
		{
			List<string> PathsToExclude = new List<string>();

			foreach (ProjectFile Project in Projects)
			{
				bool bFoundTarget = false;
				foreach (ProjectTarget Target in Project.ProjectTargets)
				{
					if (Target.TargetFilePath != null)
					{
						DirectoryReference ProjDir = Target.TargetFilePath.Directory.GetDirectoryName() == "Source" ? Target.TargetFilePath.Directory.ParentDirectory : Target.TargetFilePath.Directory;
						GetExcludePathsCPP(ProjDir, PathsToExclude);
						
						DirectoryReference PluginRootDir = DirectoryReference.Combine(ProjDir, "Plugins");
						WriteWorkspaceIgnoreFileForPlugins(PluginRootDir, PathsToExclude);

						bFoundTarget = true;
					}
				}

				if (!bFoundTarget)
				{
					GetExcludePathsCSharp(Project.ProjectFilePath.Directory.ToString(), PathsToExclude);
				}
			}

			StringBuilder OutFile = new StringBuilder();
			if (!IncludeAllFiles)
			{
				// TODO: Adding ignore patterns to .ignore hides files from Open File Dialog but it does not hide them in the File Explorer
				// but using files.exclude with our full set of excludes breaks vscode for larger code bases so a verbose file explorer
				// seems like less of an issue and thus we are not adding these to files.exclude.
				// see https://github.com/microsoft/vscode/issues/109380 for discussions with vscode team
				DirectoryReference WorkspaceRoot = bForeignProject ? Projects[0].BaseDir : UnrealBuildTool.RootDirectory;
				string WorkspaceRootPath = WorkspaceRoot.ToString().Replace('\\', '/') + "/";

				if (!bForeignProject)
				{
					OutFile.AppendLine(".vscode");
				}

				foreach (string PathToExclude in PathsToExclude)
				{
					OutFile.AppendLine(PathToExclude.Replace('\\', '/').Replace(WorkspaceRootPath, ""));
				}
			}
			FileReference.WriteAllText(FileReference.Combine(MasterProjectPath, ".ignore"), OutFile.ToString());
		}

		private void WriteWorkspaceIgnoreFileForPlugins(DirectoryReference PluginBaseDir, List<string> PathsToExclude)
		{
			if (DirectoryReference.Exists(PluginBaseDir))
			{
				foreach (DirectoryReference SubDir in DirectoryReference.EnumerateDirectories(PluginBaseDir, "*", SearchOption.TopDirectoryOnly))
				{
					string[] UPluginFiles = Directory.GetFiles(SubDir.ToString(), "*.uplugin");
					if (UPluginFiles.Length == 1)
					{
						DirectoryReference PluginDir = SubDir;
						GetExcludePathsCPP(PluginDir, PathsToExclude);
					}
					else
					{
						WriteWorkspaceIgnoreFileForPlugins(SubDir, PathsToExclude);
					}
				}
			}
		}
		
		private void WriteWorkspaceFile()
		{
			JsonFile WorkspaceFile = new JsonFile();

			WorkspaceFile.BeginRootObject();
			{
				WorkspaceFile.BeginArray("folders");
				{
					// Add the directory in which which the code-workspace file exists.
					// This is also known as ${workspaceRoot}
					WorkspaceFile.BeginObject();
					{
						string ProjectName = bForeignProject ? GameProjectName : "UE4";
						WorkspaceFile.AddField("name", ProjectName);
						WorkspaceFile.AddField("path", ".");
					}
					WorkspaceFile.EndObject();

					// If this project is outside the engine folder, add the root engine directory
					if (bIncludeEngineSource && bForeignProject)
					{
						WorkspaceFile.BeginObject();
						{
							WorkspaceFile.AddField("name", "UE4");
							WorkspaceFile.AddField("path", MakeUnquotedPathString(UnrealBuildTool.RootDirectory, EPathType.Absolute));
						}
						WorkspaceFile.EndObject();
					}
				}
				WorkspaceFile.EndArray();
			}

			WorkspaceFile.BeginObject("settings");
			{
				// disable autodetect for typescript files to workaround slowdown in vscode as a result of parsing all files
				WorkspaceFile.AddField("typescript.tsc.autoDetect", "off");
			}
			WorkspaceFile.EndObject();
			
			WorkspaceFile.BeginObject("extensions");
			{
				// extensions is a set of recommended extensions that a user should install.
				// Adding this section aids discovery of extensions which are helpful to have installed for Unreal development.
				WorkspaceFile.BeginArray("recommendations");
				{
					WorkspaceFile.AddUnnamedField("ms-vscode.cpptools");
					WorkspaceFile.AddUnnamedField("ms-dotnettools.csharp");

					// If the platform we run the generator on uses mono, there are additional debugging extensions to add.
					if (Utils.IsRunningOnMono)
					{
						WorkspaceFile.AddUnnamedField("vadimcn.vscode-lldb");
						WorkspaceFile.AddUnnamedField("ms-vscode.mono-debug");
					}
				}
				WorkspaceFile.EndArray();
			}
			WorkspaceFile.EndObject();

			WorkspaceFile.EndRootObject();

			string WorkspaceName = bForeignProject ? GameProjectName : "UE4";
			WorkspaceFile.Write(FileReference.Combine(MasterProjectPath, WorkspaceName + ".code-workspace"));
		}

		private void GetExcludePathsCPP(DirectoryReference BaseDir, List<string> PathsToExclude)
		{
			string[] DirWhiteList = { "Binaries", "Build", "Config", "Plugins", "Source", "Private", "Public", "Classes", "Resources" };
			foreach (DirectoryReference SubDir in DirectoryReference.EnumerateDirectories(BaseDir, "*", SearchOption.TopDirectoryOnly))
			{
				if (Array.Find(DirWhiteList, Dir => Dir == SubDir.GetDirectoryName()) == null)
				{
					string NewSubDir = SubDir.ToString();
					if (!PathsToExclude.Contains(NewSubDir))
					{
						PathsToExclude.Add(NewSubDir);
					}
				}
			}
		}

		private void GetExcludePathsCSharp(string BaseDir, List<string> PathsToExclude)
		{
			string[] BlackList =
			{
				"obj",
				"bin"
			};

			foreach (string BlackListDir in BlackList)
			{
				string ExcludePath = Path.Combine(BaseDir, BlackListDir);
				if (!PathsToExclude.Contains(ExcludePath))
				{
					PathsToExclude.Add(ExcludePath);
				}
			}
		}
	}
}
