// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using EpicGames.Core;
using UnrealBuildBase;
using Microsoft.Extensions.Logging;
using System.Text.RegularExpressions;

/*****

Here's how this works:

  * An XcodeProjectFile (subclass of generic ProjectFile class) is created, along with it - UnrealData and XcodeFileCollection objects are made
  * High level code calls AddModule() which this code will use to cache information about the Modules in the project (including build settings, etc)
    * These are used to determine what source files can be indexed together (we use native xcode code compilation for indexing, so we make compiling succesful for best index)
    * A few #defines are removed or modified (FOO_API, etc) which would otherwise make every module a separate target
  * High level code then calls WriteProjectFile() which is the meat of all this
  * This code then creates a hierarchy/reference-chain of xcode project nodes (an xcode project is basically a series of Guid/object pairs that reference each other)
  * Then each node writes itself into the project file that is saved to disk

.xcconfig files:
  * We also now use Xcconfig files for all of the build settings, instead of jamming them into the xcode project file itself
  * This makes it easier to see the various settings (you can also see them in the Xcode UI as before, but now with more read-only columns - they must be set via editing the file)
  * The files are in the Xcode file browsing pane, in the an Xcconfigs folder under each project
  * The files are:
    * _Project - settings apply to all targets in the projectapplies to all targets), one for each configuration (Debug, Development Editor, etc) which applies to all targets
    * _Debug/_DevelopmentEditor, etc - applies to all targets when building in that configuration
    * _Run - applies to the native run project, which does no compiling
    * _Index/SubIndex0, etc - applies when Indexing, which uses native Xcode compiling (these will include #defines from UBT, etc)
  * There is currently no _Build xcconfig file since it's a makefile project and has no build setting needs


Known issues:
  * No PBXFrameworksBuildPhase nodes are made
 
Future ideas:
  * I have started working on a Template project that we merge Build/Index targets into, which will allow a licensee to setup codesigning, add extensions, etc. 
  * Always make a final build xcodeproj from UBT for handling the codesigning/extensions/frameworks
  * Allow for non-conflicting #defines to share SubIndex targets, hopefully will greatly reduce the sub targets in UE5

**/

namespace UnrealBuildTool.XcodeProjectXcconfig
{
	class UnrealBuildConfig
	{
		public UnrealBuildConfig(string InDisplayName, string InBuildTarget, FileReference? InMacExecutablePath, FileReference? InIOSExecutablePath, FileReference? InTVOSExecutablePath,
			ProjectTarget? InProjectTarget, UnrealTargetConfiguration InBuildConfig)
		{
			DisplayName = InDisplayName;
			MacExecutablePath = InMacExecutablePath;
			IOSExecutablePath = InIOSExecutablePath;
			TVOSExecutablePath = InTVOSExecutablePath;
			BuildTarget = InBuildTarget;
			ProjectTarget = InProjectTarget;
			BuildConfig = InBuildConfig;

			if (BuildTarget != ProjectTarget?.Name)
			{
				throw new BuildException($"Name exepcted to match - {BuildTarget} != {ProjectTarget?.Name}");
			}
		}

		public string DisplayName;
		public FileReference? MacExecutablePath;
		public FileReference? IOSExecutablePath;
		public FileReference? TVOSExecutablePath;
		public string BuildTarget;
		public ProjectTarget? ProjectTarget;
		public UnrealTargetConfiguration BuildConfig;

		public bool bSupportsMac => Supports(UnrealTargetPlatform.Mac);
		public bool bSupportsIOS => Supports(UnrealTargetPlatform.IOS);
		public bool bSupportsTVOS => Supports(UnrealTargetPlatform.TVOS);
		public bool Supports(UnrealTargetPlatform? Platform)
		{
			return UnrealData.Supports(Platform) && (ProjectTarget == null || Platform == null || ProjectTarget.SupportedPlatforms.Contains((UnrealTargetPlatform)Platform));
		}
	};

	class UnrealBatchedFiles
	{
		// build settings that cause uniqueness
		public IEnumerable<String>? ForceIncludeFiles = null;
		// @todo can we actually use this effectively with indexing other than fotced include?
		public FileReference? PCHFile = null;
		public bool bEnableRTTI = false;

		// union of settings for all modules
		public HashSet<string> AllDefines = new() { "__INTELLISENSE__", "MONOLITHIC_BUILD=1" };
		public HashSet<DirectoryReference> SystemIncludePaths = new();
		public HashSet<DirectoryReference> UserIncludePaths = new();

		public List<XcodeSourceFile> Files = new();
		public UEBuildModuleCPP Module;

		public FileReference ResponseFile;

		public UnrealBatchedFiles(UnrealData UnrealData, int Index, UEBuildModuleCPP Module)
		{
			this.Module = Module;
			ResponseFile = FileReference.Combine(UnrealData.XcodeProjectFileLocation.ParentDirectory!, "ResponseFiles", $"{UnrealData.ProductName}{Index}.response");
		}

		public void GenerateResponseFile()
		{
			StringBuilder ResponseFileContents = new();
			ResponseFileContents.Append("-I");
			ResponseFileContents.AppendJoin(" -I", SystemIncludePaths.Select(x => x.FullName.Contains(' ') ? $"\"{x.FullName}\"" : x.FullName));
			ResponseFileContents.Append(" -I");
			ResponseFileContents.AppendJoin(" -I", UserIncludePaths.Select(x => x.FullName.Contains(' ') ? $"\"{x.FullName}\"" : x.FullName));
			if (ForceIncludeFiles != null)
			{
				ResponseFileContents.Append(" -include ");
				ResponseFileContents.AppendJoin(" -include ", ForceIncludeFiles.Select(x => x.Contains(' ') ? $"\"{x}\"" : x));
			}
			ResponseFileContents.Append(" -D");
			ResponseFileContents.AppendJoin(" -D", AllDefines);

			if (PCHFile != null)
			{
				ResponseFileContents.Append($" -include {PCHFile.FullName}");
			}

			ResponseFileContents.Append(bEnableRTTI ? " -fno-rtti" : " -frtti");

			DirectoryReference.CreateDirectory(ResponseFile.Directory);
			FileReference.WriteAllText(ResponseFile, ResponseFileContents.ToString());
		}
	}

	enum MetadataPlatform
	{
		MacEditor,
		Mac,
		IOS,
	}

	enum MetadataMode
	{
		UsePremade,
		UpdateTemplate,
	}

	class MetadataItem
	{
		public MetadataMode Mode;
		public FileReference? File = null;
		public string? XcodeProjectRelative = null;

		//public Metadata(MetadataMode Mode, FileReference File, DirectoryReference XcodeProjectFile)
		//{
		//	this.Mode = Mode;
		//	this.File = File;
		//	XcodeProjectRelative = File.MakeRelativeTo(XcodeProjectFile.ParentDirectory!);
		//}

		public MetadataItem(DirectoryReference ProductDirectory, DirectoryReference XcodeProject, ConfigHierarchy Ini, params string[] Locations)
		{
			foreach (string Loc in Locations)
			{
				string[] Tokens = Loc.Split(':');
				Mode = (Tokens[0] == "premade") ? MetadataMode.UsePremade : MetadataMode.UpdateTemplate;

				// no extension means it's a .ini entry
				if (Path.GetExtension(Tokens[1]) == "")
				{
					string? PremadeLocation;
					if (Ini.TryGetValue("XcodeConfiguration", Tokens[1], out PremadeLocation) && PremadeLocation != "")
					{
						FileReference TestFile;
						if (PremadeLocation.StartsWith("/Engine/"))
						{
							TestFile = FileReference.Combine(Unreal.EngineDirectory, PremadeLocation.Substring(8));
						}
						else if (PremadeLocation.StartsWith("/Game/"))
						{
							TestFile = FileReference.Combine(ProductDirectory, PremadeLocation.Substring(6));
						}
						else
						{
							TestFile = new FileReference(Tokens[1]);
						}

						// make sure the file (if it's a generated file, then it may not exist yet, so allow it through)
						if (TestFile.ContainsName("UBTGenerated", 0) || FileReference.Exists(TestFile))
						{
							File = TestFile;
							break;
						}

						throw new BuildException($"Metadata file {Tokens[1]} (resolved to {TestFile}) was specified in project settings, but does not exist. Ignoring...");
					}
				}
			}

			if (File == null)
			{
				// if null, we assume UpdateTemplate, since xcode can update "nothing" to something useful
				Mode = MetadataMode.UpdateTemplate;
			}
			else
			{
				// make it relative if it exists
				XcodeProjectRelative = File.MakeRelativeTo(XcodeProject.ParentDirectory!);
			}
			
			// Log.TraceInformation($"   Metadata {Locations[0]} found {Mode} File {File}");
		}
	}

	class Metadata
	{
		public Dictionary<MetadataPlatform, MetadataItem> PlistFiles = new();
		public Dictionary<MetadataPlatform, MetadataItem> EntitlementsFiles = new();


		public Metadata(DirectoryReference ProductDirectory, DirectoryReference XcodeProject, ConfigHierarchy Ini, bool bSupportsMac, bool bSupportsIOSOrTVOS, ILogger Logger)
		{
			Logger.LogInformation("making metadata for {ProductDirectory} / {XcodeProject}", ProductDirectory, XcodeProject);
			if (bSupportsMac)
			{
				PlistFiles[MetadataPlatform.MacEditor] = new MetadataItem(ProductDirectory, XcodeProject, Ini, "premade:PremadeMacEditorPlist", "template:TemplateMacEditorPlist");
				PlistFiles[MetadataPlatform.Mac] = new MetadataItem(ProductDirectory, XcodeProject, Ini, "premade:PremadeMacPlist", "template:TemplateMacPlist");
				EntitlementsFiles[MetadataPlatform.MacEditor] = new MetadataItem(ProductDirectory, XcodeProject, Ini, "premade:PremadeMacEditorEntitlements");
				EntitlementsFiles[MetadataPlatform.Mac] = new MetadataItem(ProductDirectory, XcodeProject, Ini, "premade:PremadeMacEntitlements");
			}
			if (bSupportsIOSOrTVOS)
			{
				PlistFiles[MetadataPlatform.IOS] = new MetadataItem(ProductDirectory, XcodeProject, Ini, "premade:PremadeIOSPlist", "template:TemplateIOSPlist");
				EntitlementsFiles[MetadataPlatform.IOS] = new MetadataItem(ProductDirectory, XcodeProject, Ini, "premade:PremadeIOSEntitlements");
			}
		}
	}


	class UnrealData
	{
		public bool bIsStubProject;
		public bool bIsForeignProject;
		public bool bMakeProjectPerTarget;

		public TargetRules TargetRules => _TargetRules!;
		public bool bIsAppBundle;

		public bool bUseAutomaticSigning = false;
		public bool bIsMergingProjects = false;
		public bool bWriteCodeSigningSettings = true;

		public Metadata? Metadata;

		public List<UnrealBuildConfig> AllConfigs = new();

		public List<UnrealBatchedFiles> BatchedFiles = new();

		public List<string> ExtraPreBuildScriptLines = new();

		public FileReference? UProjectFileLocation = null;
		public DirectoryReference XcodeProjectFileLocation;
		public DirectoryReference ProductDirectory;

		// settings read from project configs
		public IOSProjectSettings? IOSProjectSettings;
		public IOSProvisioningData? IOSProvisioningData;

		public TVOSProjectSettings? TVOSProjectSettings;
		public TVOSProvisioningData? TVOSProvisioningData;

		// Name of the product (usually the project name, but UE5.xcodeproj is actually UnrealGame product)
		public string ProductName;

		// Name of the xcode project
		public string XcodeProjectName;

		// Display name, can be overridden from commandline
		public string DisplayName;

		/// <summary>
		///  Used to mark the project for distribution (some platforms require this)
		/// </summary>
		public bool bForDistribution = false;

		/// <summary>
		/// Override for bundle identifier
		/// </summary>
		public string BundleIdentifier = "";

		/// <summary>
		/// Override AppName
		/// </summary>
		public string AppName = "";

		/// <summary>
		/// Architectures supported for iOS
		/// </summary>
		public string[] SupportedIOSArchitectures = { "arm64" };

		/// <summary>
		/// UBT logger object
		/// </summary>
		public ILogger? Logger;

		private XcodeProjectFile? ProjectFile;
		private TargetRules? _TargetRules;

		public static bool bSupportsMac => Supports(UnrealTargetPlatform.Mac);
		public static bool bSupportsIOS => Supports(UnrealTargetPlatform.IOS);
		public static bool bSupportsTVOS => Supports(UnrealTargetPlatform.TVOS);
		public static bool Supports(UnrealTargetPlatform? Platform)
		{
			return Platform == null || XcodeProjectFileGenerator.XcodePlatforms.Contains((UnrealTargetPlatform)Platform);
		}


		public UnrealData(FileReference XcodeProjectFileLocation, bool bIsForDistribution, string BundleID, string AppName, bool bMakeProjectPerTarget, bool bIsStubProject)
		{
			// the .xcodeproj is actually a directory
			this.XcodeProjectFileLocation = new DirectoryReference(XcodeProjectFileLocation.FullName);
			// default to engine director, will be fixed in Initialize if needed
			ProductDirectory = Unreal.EngineDirectory;
			XcodeProjectName = ProductName = XcodeProjectFileLocation.GetFileNameWithoutAnyExtensions();
			if (ProductName == "UE5")
			{
				ProductName = "UnrealGame";
			}

			this.bIsStubProject = bIsStubProject;
			this.bMakeProjectPerTarget = bMakeProjectPerTarget;
			this.bForDistribution = bIsForDistribution;
			this.BundleIdentifier = string.IsNullOrEmpty(BundleID) ? "$(UE_SIGNING_PREFIX).$(UE_PRODUCT_NAME_STRIPPED)" : BundleID;
			this.DisplayName = string.IsNullOrEmpty(AppName) ? "$(UE_PRODUCT_NAME)" : AppName;
		}

		public FileReference? FindUProjectFileLocation(XcodeProjectFile ProjectFile)
		{
			// find a uproject file (UE5 target won't have one)
			foreach (Project Target in ProjectFile.ProjectTargets)
			{
				if (Target.UnrealProjectFilePath != null)
				{
					UProjectFileLocation = Target.UnrealProjectFilePath;
					break;
				}
			}

			// now that we have a UProject file (or not), update the FileCollection RootDirectory to point to it
			ProjectFile.FileCollection.SetUProjectLocation(UProjectFileLocation);

			return UProjectFileLocation;
		}

		public bool Initialize(XcodeProjectFile ProjectFile, List<UnrealTargetConfiguration> Configurations, ILogger Logger)
		{
			this.ProjectFile = ProjectFile;
			this.bIsForeignProject = ProjectFile.IsForeignProject;
			this.Logger = Logger;

			FindUProjectFileLocation(ProjectFile);

			// make sure ProjectDir is something good
			if (UProjectFileLocation != null)
			{
				ProductDirectory = UProjectFileLocation.Directory;
			}
			else if (ProjectFile.ProjectTargets[0].TargetRules!.Type == TargetType.Program)
			{
				DirectoryReference? ProgramFinder = DirectoryReference.Combine(ProjectFile.BaseDir);
				while (ProgramFinder != null && string.Compare(ProgramFinder.GetDirectoryName(), "Source", true) != 0)
				{
					ProgramFinder = ProgramFinder.ParentDirectory;
				}
				// we are now at Source directory, go up one more, then into Programs, and finally the "project" directory
				if (ProgramFinder != null)
				{
					ProgramFinder = DirectoryReference.Combine(ProgramFinder, "../Programs", ProductName);
					// if it exists, we have a ProductDir we can use for plists, icons, etc
					if (DirectoryReference.Exists(ProgramFinder))
					{
						ProductDirectory = ProgramFinder;
					}
				}
			}

			InitializeMetadata(Logger);

			// Figure out all the desired configurations on the unreal side
			AllConfigs = GetSupportedBuildConfigs(XcodeProjectFileGenerator.XcodePlatforms, Configurations, Logger);
			// if we can't find any configs, we will fail to create a project
			if (AllConfigs.Count == 0)
			{
				return false;
			}

			// verify all configs share the same TargetRules
			if (!AllConfigs.All(x => x.ProjectTarget!.TargetRules == AllConfigs[0].ProjectTarget!.TargetRules))
			{
				throw new BuildException("All Configs must share a TargetRules. This indicates bMakeProjectPerTarget is returning false");
			}

			_TargetRules = AllConfigs[0].ProjectTarget!.TargetRules;
			
			// this project makes an app bundle (.app directory instead of a raw executable or dylib) if none of the fings make a non-appbundle
			bIsAppBundle = !AllConfigs.Any(x => x.ProjectTarget!.TargetRules!.bIsBuildingConsoleApplication || x.ProjectTarget.TargetRules.bShouldCompileAsDLL);

			// read config settings
			if (AllConfigs.Any(x => x.bSupportsIOS))
			{
				IOSPlatform IOSPlatform = ((IOSPlatform)UEBuildPlatform.GetBuildPlatform(UnrealTargetPlatform.IOS));
				IOSProjectSettings = IOSPlatform.ReadProjectSettings(UProjectFileLocation);
				if (!bUseAutomaticSigning)
				{
					IOSProvisioningData = IOSPlatform.ReadProvisioningData(IOSProjectSettings, bForDistribution);
				}
			}

			if (AllConfigs.Any(x => x.bSupportsTVOS))
			{
				TVOSPlatform TVOSPlatform = ((TVOSPlatform)UEBuildPlatform.GetBuildPlatform(UnrealTargetPlatform.TVOS));
				TVOSProjectSettings = TVOSPlatform.ReadProjectSettings(UProjectFileLocation);
				if (!bUseAutomaticSigning)
				{
					TVOSProvisioningData = TVOSPlatform.ReadProvisioningData(TVOSProjectSettings, bForDistribution);
				}
			}

			return true;
		}

		private void InitializeMetadata(ILogger Logger)
		{

			// read setings from the configs, now that we have a project
			ConfigHierarchy SharedPlatformIni = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, UProjectFileLocation?.Directory, UnrealTargetPlatform.Mac);
			SharedPlatformIni.TryGetValue("XcodeConfiguration", "bUseModernCodeSigning", out bUseAutomaticSigning);

			Metadata = new Metadata(ProductDirectory, XcodeProjectFileLocation, SharedPlatformIni, bSupportsMac, bSupportsIOS || bSupportsTVOS, Logger);
		}

		public string? FindFile(List<string> Paths, UnrealTargetPlatform Platform, bool bMakeRelative)
		{
			foreach (string Entry in Paths)
			{
				string FinalPath = Entry.Replace("$(Engine)", Unreal.EngineDirectory.FullName);
				FinalPath = FinalPath.Replace("$(Project)", ProductDirectory.FullName);
				FinalPath = FinalPath.Replace("$(Platform)", Platform.ToString());

//				Console.WriteLine($"Looking for {FinalPath}");
				if (File.Exists(FinalPath) || Directory.Exists(FinalPath))
				{
//					Console.WriteLine($"  Found it!");
					if (bMakeRelative)
					{
						FinalPath = new FileReference(FinalPath).MakeRelativeTo(XcodeProjectFileLocation.ParentDirectory!);
					}
					return FinalPath;
				}
			}
			return null;
		}

		public string ProjectOrEnginePath(string SubPath, bool bMakeRelative)
		{
			string? FinalPath = null;
			if (UProjectFileLocation != null)
			{
				string PathToCheck = Path.Combine(ProductDirectory.FullName, SubPath);
				if (File.Exists(PathToCheck) || Directory.Exists(PathToCheck))
				{
					FinalPath = PathToCheck;
				}
			}

			if (FinalPath == null)
			{
				FinalPath = Path.Combine(Unreal.EngineDirectory.FullName, SubPath);
			}
			if (bMakeRelative)
			{
				FinalPath = new FileReference(FinalPath).MakeRelativeTo(XcodeProjectFileLocation.ParentDirectory!);
			}

			return FinalPath;
		}

		public void AddModule(UEBuildModuleCPP Module, CppCompileEnvironment CompileEnvironment)
		{
			// one batched files per module
			UnrealBatchedFiles FileBatch = new UnrealBatchedFiles(this, BatchedFiles.Count + 1, Module);
			BatchedFiles.Add(FileBatch);

			if (CompileEnvironment.ForceIncludeFiles.Count == 0)
			{
				// if there are no ForceInclude files, then that means it's a module that forces the includes to come from a generated PCH file
				// and so we will use this for definitions and uniqueness
				if (CompileEnvironment.PrecompiledHeaderIncludeFilename != null)
				{
					FileBatch.PCHFile = FileReference.Combine(XcodeProjectFileLocation.ParentDirectory!, "PCHFiles", CompileEnvironment.PrecompiledHeaderIncludeFilename.GetFileName());
					DirectoryReference.CreateDirectory(FileBatch.PCHFile.Directory);
					FileReference.Copy(CompileEnvironment.PrecompiledHeaderIncludeFilename, FileBatch.PCHFile, true);
				}
			}
			else
			{
				FileBatch.ForceIncludeFiles = CompileEnvironment.ForceIncludeFiles.Select(x => x.FullName);
			}

			FileBatch.bEnableRTTI = CompileEnvironment.bUseRTTI;
			FileBatch.SystemIncludePaths.UnionWith(CompileEnvironment.SystemIncludePaths);
			FileBatch.UserIncludePaths.UnionWith(CompileEnvironment.UserIncludePaths);
		}

		private List<UnrealBuildConfig> GetSupportedBuildConfigs(List<UnrealTargetPlatform> Platforms, List<UnrealTargetConfiguration> Configurations, ILogger Logger)
		{
			List<UnrealBuildConfig> BuildConfigs = new List<UnrealBuildConfig>();

			//string ProjectName = ProjectFilePath.GetFileNameWithoutExtension();

			foreach (UnrealTargetConfiguration Configuration in Configurations)
			{
				if (InstalledPlatformInfo.IsValidConfiguration(Configuration, EProjectType.Code))
				{
					foreach (UnrealTargetPlatform Platform in Platforms)
					{
						if (InstalledPlatformInfo.IsValidPlatform(Platform, EProjectType.Code) && (Platform == UnrealTargetPlatform.Mac || Platform == UnrealTargetPlatform.IOS || Platform == UnrealTargetPlatform.TVOS)) // @todo support other platforms
						{
							UEBuildPlatform? BuildPlatform;
							if (UEBuildPlatform.TryGetBuildPlatform(Platform, out BuildPlatform) && (BuildPlatform.HasRequiredSDKsInstalled() == SDKStatus.Valid))
							{
								// Check we have targets (Expected to be no Engine targets when generating for a single .uproject)
								if (ProjectFile!.ProjectTargets.Count == 0 && ProjectFile!.BaseDir != Unreal.EngineDirectory)
								{
									throw new BuildException($"Expecting at least one ProjectTarget to be associated with project '{XcodeProjectFileLocation}' in the TargetProjects list ");
								}

								// Now go through all of the target types for this project
								foreach (ProjectTarget ProjectTarget in ProjectFile.ProjectTargets.OfType<ProjectTarget>())
								{
									if (MSBuildProjectFile.IsValidProjectPlatformAndConfiguration(ProjectTarget, Platform, Configuration, Logger))
									{
										// Figure out if this is a monolithic build
										bool bShouldCompileMonolithic = BuildPlatform.ShouldCompileMonolithicBinary(Platform);
										bShouldCompileMonolithic |= (ProjectTarget.CreateRulesDelegate(Platform, Configuration).LinkType == TargetLinkType.Monolithic);

										string ConfigName = Configuration.ToString();
										if (!bMakeProjectPerTarget)
										{
											if (ProjectTarget.TargetRules!.Type != TargetType.Game && ProjectTarget.TargetRules.Type != TargetType.Program)
											{
												ConfigName += " " + ProjectTarget.TargetRules.Type.ToString();
											}
										}

										if (BuildConfigs.Where(Config => Config.DisplayName == ConfigName).ToList().Count == 0)
										{
											string TargetName = ProjectTarget.TargetFilePath.GetFileNameWithoutAnyExtensions();
											// Get the .uproject directory
											DirectoryReference? UProjectDirectory = DirectoryReference.FromFile(ProjectTarget.UnrealProjectFilePath);

											// Get the output directory
											DirectoryReference RootDirectory;
											if (UProjectDirectory != null && 
												(bShouldCompileMonolithic || ProjectTarget.TargetRules!.BuildEnvironment == TargetBuildEnvironment.Unique) && 
												ProjectTarget.TargetRules!.File!.IsUnderDirectory(UProjectDirectory))
											{
												RootDirectory = UEBuildTarget.GetOutputDirectoryForExecutable(UProjectDirectory, ProjectTarget.TargetRules.File!);
											}
											else
											{
												RootDirectory = UEBuildTarget.GetOutputDirectoryForExecutable(Unreal.EngineDirectory, ProjectTarget.TargetRules!.File!);
											}

											string ExeName = TargetName;
											if (!bShouldCompileMonolithic && ProjectTarget.TargetRules.Type != TargetType.Program)
											{
												// Figure out what the compiled binary will be called so that we can point the IDE to the correct file
												if (ProjectTarget.TargetRules.Type != TargetType.Game)
												{
													// Only if shared - unique retains the Target Name
													if (ProjectTarget.TargetRules.BuildEnvironment == TargetBuildEnvironment.Shared)
													{
														ExeName = "Unreal" + ProjectTarget.TargetRules.Type.ToString();
													}
												}
											}

											// Get the output directory
											DirectoryReference OutputDirectory = DirectoryReference.Combine(RootDirectory, "Binaries");
											DirectoryReference MacBinaryDir = DirectoryReference.Combine(OutputDirectory, "Mac");
											DirectoryReference IOSBinaryDir = DirectoryReference.Combine(OutputDirectory, "IOS");
											DirectoryReference TVOSBinaryDir = DirectoryReference.Combine(OutputDirectory, "TVOS");
											if (!string.IsNullOrEmpty(ProjectTarget.TargetRules.ExeBinariesSubFolder))
											{
												MacBinaryDir = DirectoryReference.Combine(MacBinaryDir, ProjectTarget.TargetRules.ExeBinariesSubFolder);
												IOSBinaryDir = DirectoryReference.Combine(IOSBinaryDir, ProjectTarget.TargetRules.ExeBinariesSubFolder);
												TVOSBinaryDir = DirectoryReference.Combine(TVOSBinaryDir, ProjectTarget.TargetRules.ExeBinariesSubFolder);
											}

											if (BuildPlatform.Platform == UnrealTargetPlatform.Mac)
											{
												string MacExecutableName = XcodeUtils.MakeExecutableFileName(ExeName, UnrealTargetPlatform.Mac, Configuration, ProjectTarget.TargetRules.Architectures, ProjectTarget.TargetRules.UndecoratedConfiguration);
												string IOSExecutableName = MacExecutableName.Replace("-Mac-", "-IOS-");
												string TVOSExecutableName = MacExecutableName.Replace("-Mac-", "-TVOS-");
												BuildConfigs.Add(new UnrealBuildConfig(ConfigName, TargetName, FileReference.Combine(MacBinaryDir, MacExecutableName), FileReference.Combine(IOSBinaryDir, IOSExecutableName), FileReference.Combine(TVOSBinaryDir, TVOSExecutableName), ProjectTarget, Configuration));
											}
											else if (BuildPlatform.Platform == UnrealTargetPlatform.IOS || BuildPlatform.Platform == UnrealTargetPlatform.TVOS)
											{
												string IOSExecutableName = XcodeUtils.MakeExecutableFileName(ExeName, UnrealTargetPlatform.IOS, Configuration, ProjectTarget.TargetRules.Architectures, ProjectTarget.TargetRules.UndecoratedConfiguration);
												string TVOSExecutableName = IOSExecutableName.Replace("-IOS-", "-TVOS-");
												//string MacExecutableName = IOSExecutableName.Replace("-IOS-", "-Mac-");
												BuildConfigs.Add(new UnrealBuildConfig(ConfigName, TargetName, FileReference.Combine(MacBinaryDir, IOSExecutableName), FileReference.Combine(IOSBinaryDir, IOSExecutableName), FileReference.Combine(TVOSBinaryDir, TVOSExecutableName), ProjectTarget, Configuration));
											}
										}
									}
								}
							}
						}
					}
				}
			}

			return BuildConfigs;
		}
	}

	abstract class XcodeProjectNode
	{
		// keeps a list of other node this node references, which is used when writing out the whole xcode project file
		public List<XcodeProjectNode> References = new();

		// optional Xcconfig file 
		public XcconfigFile? Xcconfig = null;

		/// <summary>
		/// Abstract function the individual node classes must override to write out the node to the project file
		/// </summary>
		/// <param name="Content"></param>
		public abstract void Write(StringBuilder Content);


		/// <summary>
		/// Walks the references of the given node to find all nodes of the given type. 
		/// </summary>
		/// <typeparam name="T">Parent class of the nodes to return</typeparam>
		/// <param name="Node">Root node to start with</param>
		/// <returns>Set of matching nodes</returns>
		public static IEnumerable<T> GetNodesOfType<T>(XcodeProjectNode Node) where T : XcodeProjectNode
		{
			// gather the nodes without recursion
			LinkedList<XcodeProjectNode> Nodes = new();
			Nodes.AddLast(Node);

			// pull off the front of the "deque" amd add its references to the back, gather
			List<XcodeProjectNode> Return = new();
			while (Nodes.Count() > 0)
			{
				XcodeProjectNode Head = Nodes.First();
				Nodes.RemoveFirst();
				Head.References.ForEach(x => Nodes.AddLast(x));

				// remember them all 
				Return.AddRange(Head.References);
			}

			// filter down
			return Return.OfType<T>();
		}


		public void CreateXcconfigFile(XcodeProject Project, UnrealTargetPlatform? Platform, string Name)
		{
			DirectoryReference XcodeProjectDirectory = Project.UnrealData.XcodeProjectFileLocation.ParentDirectory!;
			Xcconfig = new XcconfigFile(XcodeProjectDirectory, Platform, Name);
			Project.FileCollection.AddFileReference(Xcconfig.Guid, Xcconfig.FileRef.MakeRelativeTo(XcodeProjectDirectory), "explicitFileType", "test.xcconfig", "\"<group>\"", "Xcconfigs");
		}

		public virtual void WriteXcconfigFile()
		{
			
		}


		/// <summary>
		/// THhis will walk the node reference tree and call WRite on each node to add all needed nodes to the xcode poject file
		/// </summary>
		/// <param name="Content"></param>
		/// <param name="Node"></param>
		/// <param name="WrittenNodes"></param>
		public static void WriteNodeAndReferences(StringBuilder Content, XcodeProjectNode Node, HashSet<XcodeProjectNode>? WrittenNodes = null)
		{
			if (WrittenNodes == null)
			{
				WrittenNodes = new();
			}

			// write the node into the xcode project file
			Node.Write(Content);
			Node.WriteXcconfigFile();

			foreach (XcodeProjectNode Reference in Node.References)
			{
				if (!WrittenNodes.Contains(Reference))
				{
					WrittenNodes.Add(Reference);
					WriteNodeAndReferences(Content, Reference, WrittenNodes);
				}
			}	
		}
	}


	class XcodeDependency : XcodeProjectNode
	{
		public XcodeTarget Target;
		public string Guid = XcodeProjectFileGenerator.MakeXcodeGuid();
		public string ProxyGuid = XcodeProjectFileGenerator.MakeXcodeGuid();
		public string ProjectGuid;


		public XcodeDependency(XcodeTarget Target, string ProjectGuid)
		{
			this.Target = Target;
			this.ProjectGuid = ProjectGuid;

			References.Add(Target);
		}

		public override void Write(StringBuilder Content)
		{
			Content.WriteLine("/* Begin PBXContainerItemProxy section */");
			Content.WriteLine($"\t\t{ProxyGuid} /* PBXContainerItemProxy */ = {{");
			Content.WriteLine("\t\t\tisa = PBXContainerItemProxy;");
			Content.WriteLine($"\t\t\tcontainerPortal = {ProjectGuid} /* Project object */;");
			Content.WriteLine("\t\t\tproxyType = 1;");
			Content.WriteLine($"\t\t\tremoteGlobalIDString = {Target.Guid};");
			Content.WriteLine($"\t\t\tremoteInfo = \"{Target.Name}\";");
			Content.WriteLine("\t\t};");
			Content.WriteLine("/* End PBXContainerItemProxy section */");
			Content.WriteLine("");

			Content.WriteLine("/* Begin PBXTargetDependency section */");
			Content.WriteLine($"\t\t{Guid} /* PBXTargetDependency */ = {{");
			Content.WriteLine("\t\t\tisa = PBXTargetDependency;");
			Content.WriteLine($"\t\t\ttarget = {Target.Guid} /* {Target.Name} */;");
			Content.WriteLine($"\t\t\ttargetProxy = {ProxyGuid} /* PBXContainerItemProxy */;");
			Content.WriteLine("\t\t};");
			Content.WriteLine("/* End PBXTargetDependency section */");
		}
	}

	abstract class XcodeBuildPhase : XcodeProjectNode
	{
		public string Name;
		public string IsAType;
		public string Guid = XcodeProjectFileGenerator.MakeXcodeGuid();
		protected List<XcodeSourceFile> FileItems = new();
		protected List<string> MiscItems = new();

		public XcodeBuildPhase(string Name, string IsAType)
		{
			this.Name = Name;
			this.IsAType = IsAType;
		}

		public override void Write(StringBuilder Content)
		{
			Content.WriteLine($"/* Begin {IsAType} section */");
			Content.WriteLine(2, $"{Guid} = {{");
			Content.WriteLine(3,	$"isa = {IsAType};");
			Content.WriteLine(3,	"buildActionMask = 2147483647;");
			Content.WriteLine(3,	"files = (");
			foreach (XcodeSourceFile File in FileItems)
			{
				Content.WriteLine(4,		$"{File.FileGuid} /* {File.Reference.GetFileName()} in {Name} */,");
			}
			Content.WriteLine(3, ");");

			foreach (string Line in MiscItems)
			{
				Content.WriteLine(3, Line);
			}

			Content.WriteLine(2, "};");
			Content.WriteLine($"/* End {IsAType} section */");
		}
	}

	class XcodeSourcesBuildPhase : XcodeBuildPhase
	{
		public XcodeSourcesBuildPhase()
			: base("Sources", "PBXSourcesBuildPhase")
		{
		}

		public void AddFile(XcodeSourceFile File)
		{
			FileItems.Add(File);
		}
	}

	class XcodeResourcesBuildPhase : XcodeBuildPhase
	{
		private XcodeFileCollection FileCollection;

		public XcodeResourcesBuildPhase(XcodeFileCollection FileCollection)
			: base("Resources", "PBXResourcesBuildPhase")
		{
			this.FileCollection = FileCollection;
		}

		public void AddResource(FileReference Resource)
		{
			XcodeSourceFile ResourceSource = new XcodeSourceFile(Resource, null);
			FileCollection.ProcessFile(ResourceSource, true, false, "Resources");

			FileItems.Add(ResourceSource);
		}

		public void AddFolderResource(DirectoryReference Resource, string GroupName)
		{
			XcodeSourceFile ResourceSource = new XcodeSourceFile(new FileReference(Resource.FullName), null);
			FileCollection.ProcessFile(ResourceSource, true, false, GroupName);

			//Project.FileCollection.AddFolderReference(CookedData.MakeRelativeTo(UnrealData.XcodeProjectFileLocation.ParentDirectory!), "CookedData_Game");
			FileItems.Add(ResourceSource);
		}
	}

	class XcodeFrameworkBuildPhase : XcodeBuildPhase
	{
		private XcodeFileCollection FileCollection;

		public XcodeFrameworkBuildPhase(XcodeFileCollection FileCollection)
			: base("Frameworks", "PBXFrameworksBuildPhase")
		{
			this.FileCollection = FileCollection;
		}

		public void AddFramework(DirectoryReference Framework, string FileRefGuid)
		{
			XcodeSourceFile FrameworkSource = new XcodeSourceFile(new FileReference(Framework.FullName), null, FileRefGuid);
			FileCollection.ProcessFile(FrameworkSource, true, false, "Frameworks", ""); ;
			FileItems.Add(FrameworkSource);

		}
	}

	class XcodeCopyFilesBuildPhase : XcodeBuildPhase
	{
		private XcodeFileCollection FileCollection;

		public XcodeCopyFilesBuildPhase(XcodeFileCollection FileCollection)
			: base("Embed Frameworks", "PBXCopyFilesBuildPhase")
		{
			this.FileCollection = FileCollection;

			MiscItems.Add($"dstPath = \"\";");
			MiscItems.Add($"dstSubfolderSpec = 10;");
			MiscItems.Add($"name = \"{Name}\";");
		}

		public void AddFramework(DirectoryReference Framework, string FileRefGuid)
		{
			XcodeSourceFile FrameworkSource = new XcodeSourceFile(new FileReference(Framework.FullName), null, FileRefGuid);
			FileCollection.ProcessFile(FrameworkSource, true, false, "", "settings = {ATTRIBUTES = (CodeSignOnCopy, RemoveHeadersOnCopy, ); };");
			FileItems.Add(FrameworkSource);
		}
	}

	class XcodeShellScriptBuildPhase : XcodeBuildPhase
	{
		public XcodeShellScriptBuildPhase(string Name, IEnumerable<string> ScriptLines, IEnumerable<string> Inputs, IEnumerable<string> Outputs, bool bInstallOnly=false)
			: base(Name, "PBXShellScriptBuildPhase")
		{
			MiscItems.Add($"name = \"{Name}\";");

			MiscItems.Add($"inputPaths = (");
			foreach (string Input in Inputs)
			{
				MiscItems.Add($"\t\"{Input}\"");
			}
			MiscItems.Add($");");

			MiscItems.Add($"outputPaths = (");
			foreach (string Output in Outputs)
			{
				MiscItems.Add($"\t\"{Output}\"");
			}
			MiscItems.Add($");");

			//			string Script = string.Join("&#10", ScriptLines);
			string Script = string.Join("\\n", ScriptLines);
			MiscItems.Add($"shellPath = /bin/sh;");
			MiscItems.Add($"shellScript = \"{Script}\";");
			if (bInstallOnly)
			{
				MiscItems.Add("runOnlyForDeploymentPostprocessing = 1;");
			}
		}
	}



	class XcodeBuildConfig : XcodeProjectNode
	{
		public string Guid = XcodeProjectFileGenerator.MakeXcodeGuid();
		public UnrealBuildConfig Info;
		// Because we don't make project-wide .xcconfig files, we need to specify at the projet level that all of the platforms are supported,
		// so that the _Build targets will have any platform as a supported platform, otherwise it can only compile for Mac (default Xcode platform)
		private bool bIncludeAllPlatforms;

		public XcodeBuildConfig(UnrealBuildConfig Info, bool bIncludeAllPlatforms)
		{
			this.Info = Info;
			this.bIncludeAllPlatforms = bIncludeAllPlatforms;
		}

		public override void Write(StringBuilder Content)
		{
			Content.WriteLine(2, $"{Guid} /* {Info.DisplayName} */ = {{");
			Content.WriteLine(3,	"isa = XCBuildConfiguration;");
			if (Xcconfig != null)
			{
				Content.WriteLine(3,	$"baseConfigurationReference = {Xcconfig.Guid} /* {Xcconfig.Name}.xcconfig */;");
			}
			Content.WriteLine(3,	"buildSettings = {");
			if (bIncludeAllPlatforms)
			{
				Content.WriteLine(4,		$"SUPPORTED_PLATFORMS = \"macosx iphonesimulator iphoneos appletvsimulator appletvos\";");
				Content.WriteLine(4,		$"ONLY_ACTIVE_ARCH = YES;");
			}
			Content.WriteLine(3,	"};");
			Content.WriteLine(3,	$"name = \"{Info.DisplayName}\";");
			Content.WriteLine(2, "};");
		}
	}
	
	class XcodeBuildConfigList : XcodeProjectNode
	{ 
		public string Guid = XcodeProjectFileGenerator.MakeXcodeGuid();
		public string TargetName;
		private UnrealData UnrealData;

		public List<XcodeBuildConfig> BuildConfigs = new();

		public bool bSupportsMac => Supports(UnrealTargetPlatform.Mac);
		public bool bSupportsIOS => Supports(UnrealTargetPlatform.IOS);
		public bool bSupportsTVOS => Supports(UnrealTargetPlatform.TVOS);
		public bool Supports(UnrealTargetPlatform? Platform)
		{
			return this.Platform == Platform || (this.Platform == null && BuildConfigs.Any(x => x.Info.Supports(Platform)));
		}

		public UnrealTargetPlatform? Platform;
		public XcodeBuildConfigList(UnrealTargetPlatform? Platform, string TargetName, UnrealData UnrealData, bool bIncludeAllPlatformsInConfig)
		{
			this.Platform = Platform;
			this.UnrealData = UnrealData;

			if (UnrealData.AllConfigs.Count == 0)
			{
				throw new BuildException("Created a XcodeBuildConfigList with no BuildConfigs. This likely means a target was created too early");
			}

			this.TargetName = TargetName;

			// create build config objects for each info passed in, and them as references
			IEnumerable<XcodeBuildConfig> Configs = UnrealData.AllConfigs.Select(x => new XcodeBuildConfig(x, bIncludeAllPlatformsInConfig));
			// filter out configs that dont match a platform if we are single-platform mode
			Configs = Configs.Where(x => Platform == null || x.Info.Supports((UnrealTargetPlatform)Platform));
			BuildConfigs = Configs.ToList();
			References.AddRange(BuildConfigs);
		}

		public override void Write(StringBuilder Content)
		{
			// figure out the default configuration to use
			string Default = "Development";
			if (!UnrealData.bMakeProjectPerTarget && BuildConfigs.Any(x => x.Info.DisplayName.Contains(" Editor")))
			{
				Default = "Development Editor";
			}

			Content.WriteLine(2, $"{Guid} /* Build configuration list for target {TargetName} */ = {{");
			Content.WriteLine(3,	"isa = XCConfigurationList;");
			Content.WriteLine(3,	"buildConfigurations = (");
			foreach (XcodeBuildConfig Config in BuildConfigs)
			{
				Content.WriteLine(4,		$"{Config.Guid} /* {Config.Info.DisplayName} */,");
			}
			Content.WriteLine(3,	");");
			Content.WriteLine(3,	"defaultConfigurationIsVisible = 0;");
			Content.WriteLine(3,	$"defaultConfigurationName = \"{Default}\";");
			Content.WriteLine(2, "};");
		}
	}

	class XcodeTarget : XcodeProjectNode
	{
		public enum Type
		{
			Run_App,
			Run_Tool,
			Build,
			Index,
		}

		// Guid for this target
		public string Guid = XcodeProjectFileGenerator.MakeXcodeGuid();
//		string TargetAppGuid = XcodeProjectFileGenerator.MakeXcodeGuid();

		// com.apple.product-type.application, etc
		string ProductType;

		// xcode target type name
		string TargetTypeName;
		Type TargetType;

		// UnrealEngine_Build, etc
		public string Name;

		// QAGame, QAGameEditor, etc
		private string UnrealTargetName;

		// list of build configs this target supports (for instance, the Index target only indexes a Development config) 
		public XcodeBuildConfigList? BuildConfigList;

		// dependencies for this target
		public List<XcodeDependency> Dependencies = new List<XcodeDependency>();

		// build phases for this target (source, resource copying, etc)
		public List<XcodeBuildPhase> BuildPhases = new List<XcodeBuildPhase>();

		private FileReference? GameProject;

		public XcodeTarget(Type Type, UnrealData UnrealData, string? OverrideName=null)
		{
			GameProject = UnrealData.UProjectFileLocation;

			string ConfigName;
			TargetType = Type;
			switch (Type)
			{
				case Type.Run_App:
					ProductType = "com.apple.product-type.application";
					TargetTypeName = "PBXNativeTarget";
					ConfigName = "_Run";
					break;
				case Type.Run_Tool:
					ProductType = "com.apple.product-type.tool";
					TargetTypeName = "PBXNativeTarget";
					ConfigName = "_Run";
					break;
				case Type.Build:
					ProductType = "com.apple.product-type.library.static";
					TargetTypeName = "PBXLegacyTarget";
					ConfigName = "_Build";
					break;
				case Type.Index:
					ProductType = "com.apple.product-type.library.static";
					TargetTypeName = "PBXNativeTarget";
					ConfigName = "_Index";
					break;
				default:
					throw new BuildException($"Unhandled target type {Type}");
			}

			// set up names
			UnrealTargetName = UnrealData.XcodeProjectName;
			Name = OverrideName ?? (UnrealData.XcodeProjectName + ConfigName);
		}

		public void AddDependency(XcodeTarget Target, XcodeProject Project)
		{
			XcodeDependency Dependency = new XcodeDependency(Target, Project.Guid);
			Dependencies.Add(Dependency);
			References.Add(Dependency);
		}

		public override void Write(StringBuilder Content)
		{
			Content.WriteLine($"/* Begin {TargetType} section */");

			Content.WriteLine(2, $"{Guid} /* {Name} */ = {{");
			Content.WriteLine(3,	$"isa = {TargetTypeName};");
				
			Content.WriteLine(3,	$"buildConfigurationList = {BuildConfigList!.Guid} /* Build configuration list for {TargetTypeName} \"{Name}\" */;");

			if (TargetType == Type.Build)
			{
				// get paths to Unreal bits to be able ro tun UBT
				string UProjectParam = GameProject == null ? "" : $"{GameProject.FullName.Replace(" ", "\\ ")}";
				string UEDir = XcodeFileCollection.ConvertPath(Path.GetFullPath(Directory.GetCurrentDirectory() + "../../.."));
				string BuildToolPath = UEDir + "/Engine/Build/BatchFiles/Mac/XcodeBuild.sh";

				// insert elements to call UBT when building
				Content.WriteLine(3,	$"buildArgumentsString = \"$(ACTION) {UnrealTargetName} $(PLATFORM_NAME) $(CONFIGURATION) {UProjectParam}\";");
				Content.WriteLine(3,	$"buildToolPath = \"{BuildToolPath}\";");
				Content.WriteLine(3,	$"buildWorkingDirectory = \"{UEDir}\";");
			}
			Content.WriteLine(3,	"buildPhases = (");
			foreach (XcodeBuildPhase BuildPhase in BuildPhases)
			{
				Content.WriteLine(4,		$"{BuildPhase.Guid} /* {BuildPhase.Name} */,");
			}
			Content.WriteLine(3,	");");
			Content.WriteLine(3,	"dependencies = (");
			foreach (XcodeDependency Dependency in Dependencies)
			{
				Content.WriteLine(4,		$"{Dependency.Guid} /* {Dependency.Target.Name} */,");
			}
			Content.WriteLine(3,	");");
			Content.WriteLine(3,	$"name = \"{Name}\";");
			Content.WriteLine(3,	"passBuildSettingsInEnvironment = 1;");
			Content.WriteLine(3,	$"productType = \"{ProductType}\";");
			WriteExtraTargetProperties(Content);
			Content.WriteLine(2, "};");

			Content.WriteLine($"/* End {TargetType} section */");
		}

		/// <summary>
		/// Let subclasses add extra properties into this target section
		/// </summary>
		protected virtual void WriteExtraTargetProperties(StringBuilder Content)
		{
			// nothing by default
		}
	}

	class XcodeRunTarget : XcodeTarget
	{
		private string ProductGuid = XcodeProjectFileGenerator.MakeXcodeGuid();
		private UnrealData UnrealData;
		public UnrealTargetPlatform Platform;
		public TargetType TargetType;

		public XcodeRunTarget(XcodeProject Project, string TargetName, TargetType TargetType, UnrealTargetPlatform Platform, XcodeBuildTarget? BuildTarget, XcodeProjectFile ProjectFile, ILogger Logger)
			: base(Project.UnrealData.bIsAppBundle ? XcodeTarget.Type.Run_App : XcodeTarget.Type.Run_Tool, Project.UnrealData,
				  TargetName + (XcodeProjectFileGenerator.PerPlatformMode == XcodePerPlatformMode.OneWorkspacePerPlatform ? "" : $"_{Platform}"))
		{
			this.TargetType = TargetType;
			this.Platform = Platform;
			this.UnrealData = Project.UnrealData;

			BuildConfigList = new XcodeBuildConfigList(Platform, Name, Project.UnrealData, bIncludeAllPlatformsInConfig:false);
			References.Add(BuildConfigList);

			// add the Product item to the project to be visible in left pane
			Project.FileCollection.AddFileReference(ProductGuid, UnrealData.ProductName, "explicitFileType", Project.UnrealData.bIsAppBundle ? "wrapper.application" : "\"compiled.mach-o.executable\"", "BUILT_PRODUCTS_DIR", "Products");

			if (Project.UnrealData.bIsAppBundle)
			{
				XcodeResourcesBuildPhase ResourcesBuildPhase = new XcodeResourcesBuildPhase(Project.FileCollection);
				BuildPhases.Add(ResourcesBuildPhase);
				References.Add(ResourcesBuildPhase);

				ProcessFrameworks(ResourcesBuildPhase, Project, ProjectFile, Logger);
				ProcessScripts(ResourcesBuildPhase, Project);
				ProcessAssets(ResourcesBuildPhase);
			}

			if (BuildTarget != null)
			{
				AddDependency(BuildTarget, Project);
			}

			CreateXcconfigFile(Project, Platform, $"{Name}");
			// create per-config Xcconfig files
			foreach (XcodeBuildConfig Config in BuildConfigList.BuildConfigs)
			{
				Config.CreateXcconfigFile(Project, Platform, $"{Name}_{Config.Info.DisplayName.Replace(" ", "")}");
			}
		}


		/// <summary>
		/// Write some scripts to do some fixup with how UBT links files. There is currently a difference between Mac and IOS/TVOS:
		/// Mac:
		///   - UBT will link directly to Foo.app/Contents/MacOS/Foo, for .app bundled apps
		///   - During normal processing, that's all that is needed, so we don't need to copy anything
		///   - However, during Archiving, the .app is created in a intermediate location, so then here we copy from Binaries/Mac to the intermediate location
		///     - Trying to have UBT link directly to the intermeidate location causes various issues, so we copy it like IOS does
		/// IOS/TVOS:
		///   - IOS will link to Binaries/IOS/Foo
		///   - During normal operation, here we copy from Binaries/IOS/Foo to Binaries/IOS/Foo.app/Foo
		///     - Note that IOS and Mac have different internal directory structures (which EXECUTABLE_PATH expresses)
		///   - When Archiving, we copy from Binaries/IOS/Foo to the intermediate location's .app
		/// All:
		///   - At this point, the executable is in the correct spot, and so CONFIGURATION_BUILD_DIR/EXECUTABLE_PATH points to it
		///   - So here we gneerate a dSYM from the executable, copying it to where Xcode wants it (DWARF_DSYM_FOLDER_PATH/DWARF_DSYM_FILE_NAME), and
		///       then we strip the executable in place
		/// </summary>
		/// <param name="ResourcesBuildPhase"></param>
		/// <param name="Project"></param>
		protected void ProcessScripts(XcodeResourcesBuildPhase ResourcesBuildPhase, XcodeProject Project)
		{
			// @todo remove this if UBT links directly into the .app
			if (Project.Platform == UnrealTargetPlatform.IOS || Project.Platform == UnrealTargetPlatform.TVOS)
			{
				// UBT no longer copies the executable into the .app directory in PostBuild, so we do it here
				// EXECUTABLE_NAME is Foo, EXECUTABLE_PATH is Foo.app/Foo
				// NOTE: We read from hardcoded location where UBT writes to, but we write to CONFIGURATION_BUILD_DIR because
				// when Archiving, the .app is somewhere else
				string[] Script = {
						$"cp \\\"${{UE_PROJECT_DIR}}/Binaries/{Platform}/${{EXECUTABLE_NAME}}\\\" \\\"${{CONFIGURATION_BUILD_DIR}}/${{EXECUTABLE_PATH}}\\\"",
					};
				string ScriptInput = $"$(UE_PROJECT_DIR)/Binaries/{Platform}/$(EXECUTABLE_NAME)";
				string ScriptOutput = $"$(CONFIGURATION_BUILD_DIR)/$(EXECUTABLE_PATH)";
				XcodeShellScriptBuildPhase ScriptPhase = new("Copy UE Executable into .app", Script, new string[] { ScriptInput }, new string[] { ScriptOutput });
				BuildPhases.Add(ScriptPhase);
				References.Add(ScriptPhase);
			}

			// always generate a dsym file when we archive, and by having Xcode do it, it will be put into the archive properly
			// (note bInstallOnly which will make this onle run when archiving)
			List<string> DsymScript = new();
			if (Platform == UnrealTargetPlatform.Mac)
			{
				DsymScript.AddRange(new string[]
				{
					"# Copy the Mac executable from where it write to into the archiving working directory",
					$"cp \\\"${{UE_PROJECT_DIR}}/Binaries/{Platform}/${{EXECUTABLE_PATH}}\\\" \\\"${{CONFIGURATION_BUILD_DIR}}/${{EXECUTABLE_PATH}}\\\"",
					"",
				});
			}

			DsymScript.AddRange(new string[]
			{
				"# Generate the dsym when making an archive, then stripping the executable",
				"rm -rf \\\"${DWARF_DSYM_FOLDER_PATH}/${DWARF_DSYM_FILE_NAME}\\\"",
				"dsymutil \\\"${CONFIGURATION_BUILD_DIR}/${EXECUTABLE_PATH}\\\" -o \\\"${DWARF_DSYM_FOLDER_PATH}/${DWARF_DSYM_FILE_NAME}\\\"",
				"strip -D \\\"${CONFIGURATION_BUILD_DIR}/${EXECUTABLE_PATH}\\\"",
			});
			string DsymScriptInput = $"\\\"$(CONFIGURATION_BUILD_DIR)/$(EXECUTABLE_PATH)\\\"";
			string DsymScriptOutput = $"\\\"$(DWARF_DSYM_FOLDER_PATH)/$(DWARF_DSYM_FILE_NAME)\\\"";
			XcodeShellScriptBuildPhase DsymScriptPhase = new("Generate dsym for archive, and strip", DsymScript, new string[] { DsymScriptInput }, new string[] { DsymScriptOutput }, bInstallOnly:true);
			BuildPhases.Add(DsymScriptPhase);
			References.Add(DsymScriptPhase);
		}
		private void ProcessAssets(XcodeResourcesBuildPhase ResourcesBuildPhase)
		{
			if (Platform == UnrealTargetPlatform.IOS || Platform == UnrealTargetPlatform.TVOS)
			{
				// cookeddata needs to be brought in from the Staged directory, (IOS for Game, IOSClient for Client, etc)
				string TargetPlatformName = Platform.ToString()!;
				if (TargetType != TargetType.Game)
				{
					TargetPlatformName += TargetType.ToString();
				}

				// @todo do this for Mac?
				DirectoryReference CookedData = DirectoryReference.Combine(UnrealData.ProductDirectory, "Saved", "StagedBuilds", TargetPlatformName, "cookeddata");
				ResourcesBuildPhase.AddFolderResource(CookedData, "Resources");
			}

			List<string> StoryboardPaths = new List<string>()
				{
					"$(Project)/Build/$(Platform)/Resources/Interface/LaunchScreen.storyboardc",
					"$(Project)/Build/$(Platform)/Resources/Interface/LaunchScreen.storyboard",
					"$(Project)/Build/Apple/Resources/Interface/LaunchScreen.storyboardc",
					"$(Project)/Build/Apple/Resources/Interface/LaunchScreen.storyboard",
					"$(Engine)/Build/$(Platform)/Resources/Interface/LaunchScreen.storyboardc",
					"$(Engine)/Build/$(Platform)/Resources/Interface/LaunchScreen.storyboard",
					"$(Engine)/Build/Apple/Resources/Interface/LaunchScreen.storyboardc",
					"$(Engine)/Build/Apple/Resources/Interface/LaunchScreen.storyboard",
				};

			// look for Assets
			string? StoryboardPath;
			if (Platform == UnrealTargetPlatform.Mac)
			{
				string AssetsPath = UnrealData.ProjectOrEnginePath("Build/Mac/Resources/Assets.xcassets", false);
				ResourcesBuildPhase.AddResource(new FileReference(AssetsPath));
				StoryboardPath = UnrealData.FindFile(StoryboardPaths, UnrealTargetPlatform.Mac, false);
			}
			else
			{
				string AssetsPath = UnrealData.ProjectOrEnginePath("Build/IOS/Resources/Assets.xcassets", false);
				ResourcesBuildPhase.AddResource(new FileReference(AssetsPath));
				StoryboardPath = UnrealData.FindFile(StoryboardPaths, UnrealTargetPlatform.IOS, false);
			}

			if (StoryboardPath != null)
			{
				ResourcesBuildPhase.AddResource(new FileReference(StoryboardPath));
			}

		}
		protected void ProcessFrameworks(XcodeResourcesBuildPhase ResourcesBuildPhase, XcodeProject Project, XcodeProjectFile ProjectFile, ILogger Logger)
		{
			// look up to see if we had cached any Frameworks
			Tuple<ProjectFile, UnrealTargetPlatform> FrameworkKey = Tuple.Create((ProjectFile)ProjectFile, Platform);
			List<UEBuildFramework>? Frameworks;
			if (XcodeProjectFileGenerator.TargetFrameworks.TryGetValue(FrameworkKey, out Frameworks))
			{
				XcodeCopyFilesBuildPhase EmbedFrameworks = new XcodeCopyFilesBuildPhase(Project.FileCollection);
				XcodeFrameworkBuildPhase FrameworkPhase = new XcodeFrameworkBuildPhase(Project.FileCollection);

				// filter frameworks that need to installed into the .app (either the framework or a bundle inside a .zip)
				IEnumerable<UEBuildFramework> InstalledFrameworks = Frameworks.Where(x => x.bCopyFramework || !string.IsNullOrEmpty(x.CopyBundledAssets));
				// filter frameworks that need to be unzipped before we compile
				IEnumerable<UEBuildFramework> ZippedFrameworks = Frameworks.Where(x => x.ZipFile != null);

				bool bHasEmbeddedFrameworks = false;
				// only look at frameworks that need anything copied into 
				foreach (UEBuildFramework Framework in InstalledFrameworks)
				{
					DirectoryReference? FinalFrameworkDir = Framework.GetFrameworkDirectory(null, null, Logger);
					if (FinalFrameworkDir == null)
					{
						continue;
					}
					// the framework may come with FrameworkDir being parent of a .framework with name of Framework.Name
					if (!FinalFrameworkDir.HasExtension(".framework") && !FinalFrameworkDir.HasExtension(".xcframework"))
					{
						FinalFrameworkDir = DirectoryReference.Combine(FinalFrameworkDir, Framework.Name + ".framework");
					}

					DirectoryReference BundleRootDir = Framework.GetFrameworkDirectory(null, null, Logger)!;

					if (Framework.ZipFile != null)
					{
						if (Framework.ZipFile.FullName.EndsWith(".embeddedframework.zip"))
						{
							// foo.embeddedframework.zip would have foo.framework inside it, which is what we want to install
							FinalFrameworkDir = DirectoryReference.Combine(Framework.ZipOutputDirectory!, Framework.ZipFile.GetFileNameWithoutAnyExtensions() + ".framework");
							BundleRootDir = Framework.ZipOutputDirectory!;
						}
						else
						{
							FinalFrameworkDir = Framework.ZipOutputDirectory!;
						}
					}

					// set up the CopyBundle to be copied
					if (!string.IsNullOrEmpty(Framework.CopyBundledAssets))
					{
						ResourcesBuildPhase.AddFolderResource(DirectoryReference.Combine(BundleRootDir, Framework.CopyBundledAssets), "Resources");
					}

					if (Framework.bCopyFramework)
					{
						string FileRefGuid = XcodeProjectFileGenerator.MakeXcodeGuid();
						EmbedFrameworks.AddFramework(FinalFrameworkDir, FileRefGuid);
						FrameworkPhase.AddFramework(FinalFrameworkDir, FileRefGuid);

						bHasEmbeddedFrameworks = true;
					}
				}

				if (bHasEmbeddedFrameworks)
				{
					BuildPhases.Add(EmbedFrameworks);
					References.Add(EmbedFrameworks);
					BuildPhases.Add(FrameworkPhase);
					References.Add(FrameworkPhase);
				}

				// each zipped framework needs to be unzipped in case C++ code needs to compile/link against it - this will add unzip commands
				// to the PreBuild script that is run before anything else happens - note that the ZipDependToken is shared with UBT, so that
				// if a new framework is unzipped, it will dirty any source files in modules that use this framework
				foreach (UEBuildFramework Framework in ZippedFrameworks)
				{
					string ZipIn = Utils.MakePathSafeToUseWithCommandLine(Framework.ZipFile!.FullName);
					string ZipOut = Utils.MakePathSafeToUseWithCommandLine(Framework.ZipOutputDirectory!.FullName);
					// Zip contains folder with the same name, hence ParentDirectory
					string ZipOutParent = Utils.MakePathSafeToUseWithCommandLine(Framework.ZipOutputDirectory.ParentDirectory!.FullName);
					string ZipDependToken = Utils.MakePathSafeToUseWithCommandLine(Framework.ExtractedTokenFile!.FullName);

					UnrealData.ExtraPreBuildScriptLines.AddRange(new[]
					{
						// delete any output and make sure parent dir exists
						$"if [ {ZipIn} -nt {ZipDependToken} ] ",
						$"then",
						$"  [ -d {ZipOut} ] &amp;&amp; rm -rf {ZipOut}",
						$"  mkdir -p {ZipOutParent}",
						// unzip the framework and maybe extra data
						$"  unzip -q -o {ZipIn} -d {ZipOutParent}",
						$"  touch {ZipDependToken}",
						$"fi",
						$"",
					});
				}
			}
		}

		protected override void WriteExtraTargetProperties(StringBuilder Content)
		{
			Content.WriteLine($"\t\t\tproductReference = {ProductGuid};");
			Content.WriteLine($"\t\t\tproductName = \"{UnrealData.ProductName}\";");
		}

		public override void WriteXcconfigFile()
		{
			// gather general, all-platform, data we are doing to put into the configs
			UnrealBuildConfig BuildConfig = BuildConfigList!.BuildConfigs[0].Info;
			TargetRules TargetRules = BuildConfig.ProjectTarget!.TargetRules!;

			MetadataPlatform MetadataPlatform;
			string TargetPlatformName = Platform.ToString();
			if (TargetRules.Type != TargetType.Game)
			{
				TargetPlatformName += TargetRules.Type.ToString();
			}

			// get ini file for the platform
			DirectoryReference ProjectOrEngineDir = UnrealData.UProjectFileLocation?.Directory ?? Unreal.EngineDirectory;
			ConfigHierarchy PlatformIni = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, UnrealData.UProjectFileLocation?.Directory, Platform);

			// settings for all platforms
			bool bAutomaticSigning = true;
			string? SigningTeam;
			string? SigningPrefix;
			string SupportedPlatforms;
			string SDKRoot;
			DirectoryReference ConfigBuildDir;
			string DeploymentTarget;
			string DeploymentTargetKey;
			string? SigningCert = null;
			string? ProvisioningProfile = null;
			string? SupportedDevices = null;
			string? MarketingVersion = null;
			string BundleIdentifier;
			List<string> ExtraConfigLines = new();

			// get codesigning info (all platforms)
			PlatformIni.TryGetValue("XcodeConfiguration", "bUseModernCodeSigning", out bAutomaticSigning);
			PlatformIni.TryGetValue("XcodeConfiguration", "ModernSigningTeam", out SigningTeam);
			PlatformIni.TryGetValue("XcodeConfiguration", "ModernSigningPrefix", out SigningPrefix);


			if (Platform == UnrealTargetPlatform.Mac)
			{
				// editor vs game metadata
				bool bIsEditor = UnrealData.TargetRules.Type == TargetType.Editor;
				MetadataPlatform = bIsEditor ? MetadataPlatform.MacEditor : MetadataPlatform.Mac;

				SDKRoot = "macosx";
				SupportedPlatforms = "macosx";
				DeploymentTargetKey = "MACOSX_DEPLOYMENT_TARGET";
				DeploymentTarget = MacToolChain.Settings.MacOSVersion;
				ConfigBuildDir = BuildConfig.MacExecutablePath!.Directory;
				BundleIdentifier = bIsEditor ? "com.epicgames.UnrealEditor" : UnrealData.BundleIdentifier;

				// @todo: get a version for  games, like IOS has
				MarketingVersion = MacToolChain.LoadEngineDisplayVersion();

				string SupportedMacArchitectures = string.Join(" ", XcodeUtils.GetSupportedMacArchitectures(BuildConfig.BuildTarget, UnrealData.UProjectFileLocation).Architectures.Select(x => x.AppleName));
				ExtraConfigLines.Add($"VALID_ARCHS = {SupportedMacArchitectures}");
			}
			else
			{
				MetadataPlatform = MetadataPlatform.IOS;

				// get IOS (same as TVOS) BundleID, and if there's a specified plist with a bundleID, use it, as Xcode would warn if they don't match
				BundleIdentifier = UnrealData.BundleIdentifier;

				// short version string
				PlatformIni.GetString($"/Script/IOSRuntimeSettings.IOSRuntimeSettings", "VersionInfo", out MarketingVersion);

				if (Platform == UnrealTargetPlatform.IOS)
				{
					SDKRoot = "iphoneos";
					SupportedPlatforms = "iphoneos"; // iphonesimulator
					DeploymentTargetKey = "IPHONEOS_DEPLOYMENT_TARGET";
					ConfigBuildDir = BuildConfig.IOSExecutablePath!.Directory;
					SupportedDevices = UnrealData.IOSProjectSettings!.RuntimeDevices;
					DeploymentTarget = UnrealData.IOSProjectSettings.RuntimeVersion;

					if (UnrealData.IOSProvisioningData != null && UnrealData.bWriteCodeSigningSettings)
					{
						ProvisioningProfile = UnrealData.IOSProvisioningData.MobileProvisionUUID;
						SigningCert = UnrealData.IOSProvisioningData.SigningCertificate ?? (UnrealData.bForDistribution ? "Apple Distribution" : "Apple Developer");
					}

					// only iphone deals with orientation
					List<string> SupportedOrientations = XcodeUtils.GetSupportedOrientations(PlatformIni);
					ExtraConfigLines.Add($"INFOPLIST_KEY_UISupportedInterfaceOrientations = \"{string.Join(" ", SupportedOrientations)}\"");
				}
				else // tvos
				{
					SDKRoot = "appletvos";
					SupportedPlatforms = "appletvos"; // appletvsimulator
					DeploymentTargetKey = "TVOS_DEPLOYMENT_TARGET";
					ConfigBuildDir = BuildConfig.TVOSExecutablePath!.Directory;
					SupportedDevices = UnrealData.TVOSProjectSettings!.RuntimeDevices;
					DeploymentTarget = UnrealData.TVOSProjectSettings.RuntimeVersion;

					if (UnrealData.TVOSProvisioningData != null && UnrealData.bWriteCodeSigningSettings)
					{
						ProvisioningProfile = UnrealData.TVOSProvisioningData.MobileProvisionUUID;
						SigningCert = UnrealData.TVOSProvisioningData.SigningCertificate ?? (UnrealData.bForDistribution ? "Apple Distribution" : "Apple Developer");
					}
				}
			}


			// get metadata for the platform set above
			MetadataItem PlistMetadata = UnrealData.Metadata!.PlistFiles[MetadataPlatform];
			MetadataItem EntitlementsMetadata = UnrealData.Metadata!.PlistFiles[MetadataPlatform];
			// now pull the bundle id's out, as xcode will warn if they don't match (this has to happen after each platform set bundle id above)
			XcodeUtils.FindPlistId(PlistMetadata, "CFBundleIdentifier", ref BundleIdentifier);


			// include another xcconfig for versions that UBT writes out
			Xcconfig!.AppendLine($"#include? \"{ProjectOrEngineDir}/Intermediate/Build/Versions.xcconfig\"");

			// write out some UE variables that can be used in premade .plist files, etc
			Xcconfig.AppendLine("");
			Xcconfig.AppendLine("// Unreal project-wide variables");
			Xcconfig.AppendLine($"UE_PRODUCT_NAME = {UnrealData.ProductName}");
			Xcconfig.AppendLine($"UE_PRODUCT_NAME_STRIPPED = {UnrealData.ProductName.Replace("_", "").Replace(" ", "")}");
			Xcconfig.AppendLine($"UE_DISPLAY_NAME = {UnrealData.DisplayName}");
			Xcconfig.AppendLine($"UE_SIGNING_PREFIX = {SigningPrefix}");
			Xcconfig.AppendLine($"UE_ENGINE_DIR = {Unreal.EngineDirectory}");
			Xcconfig.AppendLine($"UE_PROJECT_DIR = {ProjectOrEngineDir}");
			Xcconfig.AppendLine($"UE_PLATFORM_NAME = {Platform}");
			Xcconfig.AppendLine($"UE_TARGET_NAME = {UnrealData.TargetRules.Name}");
			Xcconfig.AppendLine($"UE_TARGET_PLATFORM_NAME = {TargetPlatformName}");

			Xcconfig.AppendLine("");
			Xcconfig.AppendLine("// Constant settings (same for all platforms and targets)");
			// #jira UE-143619: Pre Monterey macOS requires this option for a packaged app to run on iOS15 due to new code signature format. Could be removed once Monterey is miniuS.
			Xcconfig.AppendLine("OTHER_CODE_SIGN_FLAGS = --generate-entitlement-der");
			Xcconfig.AppendLine("INFOPLIST_OUTPUT_FORMAT = xml");
			Xcconfig.AppendLine("COMBINE_HIDPI_IMAGES = YES");
			Xcconfig.AppendLine("USE_HEADERMAP = NO");
			Xcconfig.AppendLine("ONLY_ACTIVE_ARCH = YES");

			Xcconfig.AppendLine("");
			Xcconfig.AppendLine("// Platform settings");
			Xcconfig.AppendLine($"SUPPORTED_PLATFORMS = {SupportedPlatforms}");
			Xcconfig.AppendLine($"SDKROOT = {SDKRoot}");

			// Xcode creates the Build Dir (where the .app is) by combining {SYMROOT}/{CONFIGURATION}{EFFECTIVE_PLATFORM_NAME}
			Xcconfig.AppendLine("");
			Xcconfig.AppendLine($"// These settings combined will tell Xcode to write to Binaries/{Platform} (instead of something like Binaries/Development-iphoneos)");
			Xcconfig.AppendLine($"SYMROOT = {ConfigBuildDir.ParentDirectory}");
			Xcconfig.AppendLine($"CONFIGURATION = ");
			Xcconfig.AppendLine($"EFFECTIVE_PLATFORM_NAME = {Platform}");

			if (ExtraConfigLines.Count > 0)
			{
				Xcconfig.AppendLine("");
				Xcconfig.AppendLine("// Misc settings");
				foreach (string Line in ExtraConfigLines)
				{
					Xcconfig.AppendLine(Line);
				}
			}

			Xcconfig.AppendLine("");
			Xcconfig.AppendLine("// Project settings");
			Xcconfig.AppendLine($"TARGETED_DEVICE_FAMILY = {SupportedDevices}");
			Xcconfig.AppendLine($"PRODUCT_BUNDLE_IDENTIFIER = {BundleIdentifier}");
			Xcconfig.AppendLine($"{DeploymentTargetKey} = {DeploymentTarget}");


			Xcconfig.AppendLine("");
			Xcconfig.AppendLine("// Plist settings");
			Xcconfig.AppendLine($"INFOPLIST_FILE = {PlistMetadata.XcodeProjectRelative}");
			if (PlistMetadata.Mode == MetadataMode.UpdateTemplate)
			{
				// allow Xcode to generate the final plist file from our input, some INFOPLIST settings and other settings 
				Xcconfig.AppendLine("GENERATE_INFOPLIST_FILE = YES");
				Xcconfig.AppendLine($"ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon");
				Xcconfig.AppendLine($"CURRENT_PROJECT_VERSION = $(UE_{Platform.ToString().ToUpper()}_BUILD_VERSION)");
				Xcconfig.AppendLine($"MARKETING_VERSION = {MarketingVersion}");
			}

			if (UnrealData.bWriteCodeSigningSettings)
			{
				Xcconfig.AppendLine("");
				Xcconfig.AppendLine("// Code-signing settings");
				Xcconfig.AppendLine("CODE_SIGN_STYLE = " + (bAutomaticSigning ? "Automatic" : "Manual"));
				if (EntitlementsMetadata.Mode == MetadataMode.UsePremade)
				{
					Xcconfig.AppendLine($"CODE_SIGN_ENTITLEMENTS = {EntitlementsMetadata.XcodeProjectRelative}");
				}
				if (!string.IsNullOrEmpty(SigningTeam))
				{
					Xcconfig.AppendLine($"DEVELOPMENT_TEAM = {SigningTeam}");
				}
				if (!string.IsNullOrEmpty(ProvisioningProfile))
				{
					Xcconfig.AppendLine($"PROVISIONING_PROFILE_SPECIFIER = {ProvisioningProfile}");
				}
				if (SigningCert != null)
				{
					Xcconfig.AppendLine($"CODE_SIGN_IDENTITY = {SigningCert}");
				}
			}

			Xcconfig.Write();



			// Now for each config write out the specific settings
			DirectoryReference? GameDir = UnrealData.UProjectFileLocation?.Directory;
			string? GamePath = GameDir != null ? XcodeFileCollection.ConvertPath(GameDir.FullName) : null;
			foreach (UnrealBuildConfig Config in UnrealData.AllConfigs)
			{
				XcodeBuildConfig? MatchedConfig = BuildConfigList!.BuildConfigs.FirstOrDefault(x => x.Info == Config);
				if (MatchedConfig == null || !Config.Supports(Platform))
				{
					continue;
				}

				string ProductName;
				string ExecutableName;
				string ExecutableKey = $"UE_{Platform.ToString().ToUpper()}_EXECUTABLE_NAME";
				if (Platform == UnrealTargetPlatform.Mac)
				{
					ExecutableName = Config.MacExecutablePath!.GetFileName();
					ProductName = ExecutableName;
				}
				else // IOS,TVOS
				{
					ExecutableName = ((Platform == UnrealTargetPlatform.IOS) ? Config.IOSExecutablePath : Config.TVOSExecutablePath)!.GetFileName();
					ProductName = ExecutableName;
				}


				// hook up the Buildconfig that matches this info to this xcconfig file
				XcconfigFile ConfigXcconfig = MatchedConfig.Xcconfig!;

				ConfigXcconfig.AppendLine("// pull in the shared settings for all configs for this target");
				ConfigXcconfig.AppendLine($"#include \"{Xcconfig.Name}.xcconfig\"");
				ConfigXcconfig.AppendLine("");
				ConfigXcconfig.AppendLine("// Unreal per-config variables");
				ConfigXcconfig.AppendLine($"UE_TARGET_CONFIG = {Config.BuildConfig}");
				ConfigXcconfig.AppendLine($"PRODUCT_NAME = {ProductName}");
				ConfigXcconfig.AppendLine($"{ExecutableKey} = {ExecutableName}");

				// debug settings
				if (Config.BuildConfig == UnrealTargetConfiguration.Debug)
				{
					ConfigXcconfig.AppendLine("ENABLE_TESTABILITY = YES");
				}

				ConfigXcconfig.Write();
			}
		}
	}

	class XcodeBuildTarget : XcodeTarget
	{
		public XcodeBuildTarget(XcodeProject Project)
			: base(XcodeTarget.Type.Build, Project.UnrealData)
		{
			BuildConfigList = new XcodeBuildConfigList(Project.Platform, Name, Project.UnrealData, bIncludeAllPlatformsInConfig: false);
			References.Add(BuildConfigList);
		}
	}

	class XcodeIndexTarget : XcodeTarget
	{
		private UnrealData UnrealData;

		// just take the Project since it has everything we need, and is needed when adding target dependencies
		public XcodeIndexTarget(XcodeProject Project)
			: base(XcodeTarget.Type.Index, Project.UnrealData)
		{
			UnrealData = Project.UnrealData;

			BuildConfigList = new XcodeBuildConfigList(Project.Platform, Name, UnrealData, bIncludeAllPlatformsInConfig: false);
			References.Add(BuildConfigList);

			CreateXcconfigFile(Project, Project.Platform, Name);
			// hook up each buildconfig to this Xcconfig
			BuildConfigList.BuildConfigs.ForEach(x => x.Xcconfig = Xcconfig);


			// add all of the files to be natively compiled by this target
			XcodeSourcesBuildPhase SourcesBuildPhase = new XcodeSourcesBuildPhase();
			BuildPhases.Add(SourcesBuildPhase);
			References.Add(SourcesBuildPhase);

			foreach (KeyValuePair<XcodeSourceFile, FileReference?> Pair in Project.FileCollection.BuildableFilesToResponseFile)
			{
				// only add files that found a moduleto be part of (since we can't build without the build settings that come from a module)
				if (Pair.Value != null)
				{
					SourcesBuildPhase.AddFile(Pair.Key);
				}
			}
		}

		public override void WriteXcconfigFile()
		{
			// write out settings that for compiling natively
			Xcconfig!.AppendLine("CLANG_CXX_LANGUAGE_STANDARD = c++17");
			Xcconfig.AppendLine("GCC_WARN_CHECK_SWITCH_STATEMENTS = NO");
			Xcconfig.AppendLine("GCC_PRECOMPILE_PREFIX_HEADER = YES");
			Xcconfig.AppendLine("GCC_OPTIMIZATION_LEVEL = 0");
			Xcconfig.AppendLine($"PRODUCT_NAME = {Name}");
			Xcconfig.AppendLine("SYMROOT = build");
			Xcconfig.Write();
		}
	}

	class XcodeProject : XcodeProjectNode
	{
		// a null platform here means all platforms like the old way
		public UnrealTargetPlatform? Platform;

		// the blob of data coming from unreal that we can pass around
		public UnrealData UnrealData;

		// container for all files and groups
		public XcodeFileCollection FileCollection;

		// Guid for the project node
		public string Guid = XcodeProjectFileGenerator.MakeXcodeGuid();
		private string ProvisioningStyle;

		public List<XcodeRunTarget> RunTargets = new();

		public XcodeBuildConfigList ProjectBuildConfigs;

		public XcodeProject(UnrealTargetPlatform? Platform, UnrealData UnrealData, XcodeFileCollection FileCollection, XcodeProjectFile ProjectFile, ILogger Logger)
		{
			this.Platform = Platform;
			this.UnrealData = UnrealData;
			this.FileCollection = FileCollection;

			ProvisioningStyle = UnrealData.bUseAutomaticSigning ? "Automatic" : "Manual";

			// if we are run-only, then we don't need a build target (this is shared between platforms if we are doing multi-target)
			XcodeBuildTarget? BuildTarget = null;
			if (!XcodeProjectFileGenerator.bGenerateRunOnlyProject && !UnrealData.bIsStubProject)
			{
				BuildTarget = new XcodeBuildTarget(this);
			}

			// create one run target for each platform if our platform is null (ie XcodeProjectGenerator.PerPlatformMode is RunTargetPerPlatform)
			List<UnrealTargetPlatform> TargetPlatforms = Platform == null ? XcodeProjectFileGenerator.XcodePlatforms : new() { Platform.Value };
			foreach (UnrealTargetPlatform TargetPlatform in TargetPlatforms)
			{
				XcodeRunTarget RunTarget = new XcodeRunTarget(this, UnrealData.ProductName, UnrealData.AllConfigs[0].ProjectTarget!.TargetRules!.Type, TargetPlatform, BuildTarget, ProjectFile, Logger);
				RunTargets.Add(RunTarget);
				References.Add(RunTarget);
			}

			ProjectBuildConfigs = new XcodeBuildConfigList(Platform, UnrealData.ProductName, UnrealData, bIncludeAllPlatformsInConfig: true);
			References.Add(ProjectBuildConfigs);
			
			// make an indexing target if we aren't just a run-only project, and it has buildable source files
			if (!XcodeProjectFileGenerator.bGenerateRunOnlyProject && UnrealData.BatchedFiles.Count != 0)
			{				
				// index isn't a dependency of run, it's simply a target that xcode will find to index from
				XcodeIndexTarget IndexTarget = new XcodeIndexTarget(this);
				References.Add(IndexTarget);
			}
		}

		public override void Write(StringBuilder Content)
		{
			Content.WriteLine("/* Begin PBXProject section */");

			Content.WriteLine(2, $"{Guid} /* Project object */ = {{");
			Content.WriteLine(3,	"isa = PBXProject;");
			Content.WriteLine(3,	"attributes = {");
			Content.WriteLine(4,		"LastUpgradeCheck = 2000;");
			Content.WriteLine(4,		"ORGANIZATIONNAME = \"Epic Games, Inc.\";");
			Content.WriteLine(4,		"TargetAttributes = {");
			foreach (XcodeRunTarget RunTarget in RunTargets)
			{ 
				Content.WriteLine(5,			$"{RunTarget.Guid} = {{");
				Content.WriteLine(6,				$"ProvisioningStyle = {ProvisioningStyle};");
				Content.WriteLine(5,			"};");
			}
			Content.WriteLine(4,		"};");
			Content.WriteLine(3,	"};");
			Content.WriteLine(3,	$"buildConfigurationList = {ProjectBuildConfigs.Guid} /* Build configuration list for PBXProject \"{ProjectBuildConfigs.TargetName}\" */;");
			Content.WriteLine(3,	"compatibilityVersion = \"Xcode 8.0\";");
			Content.WriteLine(3,	"developmentRegion = English;");
			Content.WriteLine(3,	"hasScannedForEncodings = 0;");
			Content.WriteLine(3,	"knownRegions = (");
			Content.WriteLine(4,		"en");
			Content.WriteLine(3,	");");
			Content.WriteLine(3,	$"mainGroup = {FileCollection.MainGroupGuid};");
			Content.WriteLine(3,	$"productRefGroup = {FileCollection.GetProductGroupGuid()};");
			Content.WriteLine(3,	"projectDirPath = \"\";");
			Content.WriteLine(3,	"projectRoot = \"\";");
			Content.WriteLine(3,	"targets = (");
			foreach (XcodeTarget Target in XcodeProjectNode.GetNodesOfType<XcodeTarget>(this))
			{
				Content.WriteLine(4, $"{Target.Guid} /* {Target.Name} */,");
			}
			Content.WriteLine(3, ");");
			Content.WriteLine(2, "};");

			Content.WriteLine("/* End PBXProject section */");
		}
	}

	class XcodeProjectFile : ProjectFile
	{

		/// <summary>
		/// Constructs a new project file object
		/// </summary>
		/// <param name="InitFilePath">The path to the project file on disk</param>
		/// <param name="BaseDir">The base directory for files within this project</param>
		/// <param name="bIsForDistribution">True for distribution builds</param>
		/// <param name="BundleID">Override option for bundle identifier</param>
		/// <param name="AppName"></param>
		/// <param name="bMakeProjectPerTarget"></param>
		public XcodeProjectFile(FileReference InitFilePath, DirectoryReference BaseDir, bool bIsForDistribution, string BundleID, string AppName, bool bMakeProjectPerTarget)
			: base(InitFilePath, BaseDir)
		{
			UnrealData = new UnrealData(InitFilePath, bIsForDistribution, BundleID, AppName, bMakeProjectPerTarget, IsStubProject);

			// create the container for all the files that will 
			SharedFileCollection = new XcodeFileCollection(this);
			FileCollection = SharedFileCollection;
		}

		public UnrealData UnrealData;

		/// <summary>
		///  The PBXPRoject node, root of everything
		/// </summary>
		public Dictionary<XcodeProject, UnrealTargetPlatform?> RootProjects = new();


		private XcodeProjectLegacy.XcodeProjectFile? LegacyProjectFile = null;
		private bool bHasCheckedForLegacy = false;
		public bool bHasLegacyProject => LegacyProjectFile != null;

		/// <summary>
		/// Gathers the files and generates project sections
		/// </summary>
		private XcodeFileCollection SharedFileCollection;
		public XcodeFileCollection FileCollection;


		/// <summary>
		/// Allocates a generator-specific source file object
		/// </summary>
		/// <param name="InitFilePath">Path to the source file on disk</param>
		/// <param name="InitProjectSubFolder">Optional sub-folder to put the file in.  If empty, this will be determined automatically from the file's path relative to the project file</param>
		/// <returns>The newly allocated source file object</returns>
		public override SourceFile? AllocSourceFile(FileReference InitFilePath, DirectoryReference? InitProjectSubFolder)
		{
			if (InitFilePath.GetFileName().StartsWith("."))
			{
				return null;
			}
			return new XcodeSourceFile(InitFilePath, InitProjectSubFolder);
		}

		public void ConditionalCreateLegacyProject()
		{
			if (!bHasCheckedForLegacy)
			{
				bHasCheckedForLegacy = true;
				if (ProjectTargets.Count == 0)
				{
					throw new BuildException("Expected to have a target before AddModule is called");
				}
				FileReference? UProjectFileLocation = UnrealData.FindUProjectFileLocation(this);
				ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, UProjectFileLocation?.Directory, UnrealTargetPlatform.Mac);
				bool bUseModernXcode;
				if (!(Ini.TryGetValue("XcodeConfiguration", "bUseModernXcode", out bUseModernXcode) && bUseModernXcode))
				{
					LegacyProjectFile = new XcodeProjectLegacy.XcodeProjectFile(ProjectFilePath, BaseDir, UnrealData.bForDistribution, UnrealData.BundleIdentifier, UnrealData.AppName, UnrealData.bMakeProjectPerTarget);
					LegacyProjectFile.ProjectTargets.AddRange(ProjectTargets);
					LegacyProjectFile.SourceFiles.AddRange(SourceFiles);
					LegacyProjectFile.IsGeneratedProject = IsGeneratedProject;
					LegacyProjectFile.IsStubProject = IsStubProject;
					LegacyProjectFile.IsForeignProject = IsForeignProject;
				}
			}
		}

		public override void AddModule(UEBuildModuleCPP Module, CppCompileEnvironment CompileEnvironment)
		{
			ConditionalCreateLegacyProject();

			if (LegacyProjectFile != null)
			{
				LegacyProjectFile.AddModule(Module, CompileEnvironment);
				return;
			}

			UnrealData.AddModule(Module, CompileEnvironment);
		}

		/// <summary>
		/// Generates bodies of all sections that contain a list of source files plus a dictionary of project navigator groups.
		/// </summary>
		private void ProcessSourceFiles()
		{
			// process the files that came from UE/cross-platform land
			SourceFiles.SortBy(x => x.Reference.FullName);

			foreach (XcodeSourceFile SourceFile in SourceFiles.OfType<XcodeSourceFile>())
			{
				SharedFileCollection.ProcessFile(SourceFile, bIsForBuild:IsGeneratedProject, false);
			}

			// cache the main group
			SharedFileCollection.MainGroupGuid = XcodeFileCollection.GetRootGroupGuid(SharedFileCollection.Groups);

			// filter each file into the appropriate batch
			foreach (XcodeSourceFile File in SharedFileCollection.BuildableFilesToResponseFile.Keys)
			{
				AddFileToBatch(File, SharedFileCollection);
			}

			// write out the response files for each batch now that everything is done
			foreach (UnrealBatchedFiles Batch in UnrealData.BatchedFiles)
			{
				Batch.GenerateResponseFile();
			}
		}

		private void AddFileToBatch(XcodeSourceFile File, XcodeFileCollection FileCollection)
		{
			foreach (UnrealBatchedFiles Batch in UnrealData.BatchedFiles)
			{
				if (Batch.Module.ContainsFile(File.Reference))
				{
					Batch.Files.Add(File);
					FileCollection.BuildableFilesToResponseFile[File] = Batch.ResponseFile;
					return;
				}
			}
		}


		public FileReference ProjectFilePathForPlatform(UnrealTargetPlatform? Platform)
		{
			return new FileReference(XcodeUtils.ProjectDirPathForPlatform(UnrealData.XcodeProjectFileLocation, Platform).FullName);
		}

		public FileReference PBXFilePathForPlatform(UnrealTargetPlatform? Platform)
		{
			return FileReference.Combine(XcodeUtils.ProjectDirPathForPlatform(UnrealData.XcodeProjectFileLocation, Platform), "project.pbxproj");
		}

		/// Implements Project interface
		public override bool WriteProjectFile(List<UnrealTargetPlatform> InPlatforms, List<UnrealTargetConfiguration> InConfigurations, PlatformProjectGeneratorCollection PlatformProjectGenerators, ILogger Logger)
		{
			ConditionalCreateLegacyProject();

			if (LegacyProjectFile != null)
			{
				return LegacyProjectFile.WriteProjectFile(InPlatforms, InConfigurations, PlatformProjectGenerators, Logger);
			}

			if (UnrealData.Initialize(this, InConfigurations, Logger) == false)
			{
				// if we failed to initialize, we silently return to move on (it's not an error, it's a project with nothing to do)
				return true;
			}

			// look for an existing project to use as a template (if none found, create one from scratch)
			DirectoryReference BuildDirLocation = UnrealData.UProjectFileLocation == null ? Unreal.EngineDirectory : UnrealData.UProjectFileLocation.Directory;
			string ExistingProjectName = UnrealData.ProductName;
			FileReference TemplateProject = FileReference.Combine(BuildDirLocation, $"Build/IOS/{UnrealData.ProductName}.xcodeproj/project.pbxproj");

			// @todo this for per-platform!
			UnrealData.bIsMergingProjects = FileReference.Exists(TemplateProject);
			UnrealData.bWriteCodeSigningSettings = !UnrealData.bIsMergingProjects;


			// turn all UE files into internal representation
			ProcessSourceFiles();

			bool bSuccess = true;
			foreach (UnrealTargetPlatform? Platform in XcodeProjectFileGenerator.WorkspacePlatforms)
			{
				// skip the platform if the project has no configurations for it
				if (!UnrealData.AllConfigs.Any(x => x.Supports(Platform)))
				{
					continue;
				}
				FileReference PBXFilePath = PBXFilePathForPlatform(Platform);

				// now create the xcodeproject elements (project -> target -> buildconfigs, etc)
				FileCollection = new XcodeFileCollection(SharedFileCollection);
				XcodeProject RootProject = new XcodeProject(Platform, UnrealData, FileCollection, this, Logger);
				RootProjects[RootProject] = Platform;

				if (FileReference.Exists(TemplateProject))
				{
					// @todo hahahaah
					continue;
					//bSuccess = MergeIntoTemplateProject(PBXFilePath, RootProject, TemplateProject);
				}
				else
				{
					// write metadata now so we can add them to the FileCollection
					ConditionalWriteMetadataFiles(UnrealTargetPlatform.IOS);


					StringBuilder Content = new StringBuilder();

					Content.WriteLine(0, "// !$*UTF8*$!");
					Content.WriteLine(0, "{");
					Content.WriteLine(1, "archiveVersion = 1;");
					Content.WriteLine(1, "classes = {");
					Content.WriteLine(1, "};");
					Content.WriteLine(1, "objectVersion = 46;");
					Content.WriteLine(1, "objects = {");

					// write out the list of files and groups
					FileCollection.Write(Content);

					// now write out the project node and its recursive dependent nodes
					XcodeProjectNode.WriteNodeAndReferences(Content, RootProject);

					Content.WriteLine(1, "};");
					Content.WriteLine(1, $"rootObject = {RootProject.Guid} /* Project object */;");
					Content.WriteLine(0, "}");

					// finally write out the pbxproj file!
					bSuccess = ProjectFileGenerator.WriteFileIfChanged(PBXFilePath.FullName, Content.ToString(), Logger, new UTF8Encoding()) && bSuccess;
				}

				bool bNeedScheme = XcodeUtils.ShouldIncludeProjectInWorkspace(this, Logger);
				if (bNeedScheme)
				{
					if (bSuccess)
					{
						string ProjectName = ProjectFilePathForPlatform(Platform).GetFileNameWithoutAnyExtensions();
						string? BuildTargetGuid = XcodeProjectNode.GetNodesOfType<XcodeBuildTarget>(RootProject).FirstOrDefault()?.Guid;
						string? IndexTargetGuid = XcodeProjectNode.GetNodesOfType<XcodeIndexTarget>(RootProject).FirstOrDefault()?.Guid;
						XcodeSchemeFile.WriteSchemeFile(UnrealData, Platform, ProjectName, RootProject.RunTargets, BuildTargetGuid, IndexTargetGuid);
					}
				}
				else
				{
					XcodeSchemeFile.CleanSchemeFile(UnrealData, Platform);
				}
			}
			return bSuccess;
		}

		private UnrealTargetPlatform CurrentPlistPlatform;
		private void ConditionalWriteMetadataFiles(UnrealTargetPlatform Platform)
		{
			CurrentPlistPlatform = Platform;

			// we now use templates or premade, no writing out here
			foreach (MetadataItem Data in UnrealData.Metadata!.PlistFiles.Values)
			{
				if (Data.XcodeProjectRelative != null)
				{
					FileCollection.AddFileReference(XcodeProjectFileGenerator.MakeXcodeGuid(), Data.XcodeProjectRelative, "explicitFileType", "text.plist", "\"<group>\"", "Metadata");
				}
			}
			foreach (MetadataItem Data in UnrealData.Metadata.EntitlementsFiles.Values)
			{
				if (Data.XcodeProjectRelative != null && Data.Mode == MetadataMode.UsePremade)
				{
					FileCollection.AddFileReference(XcodeProjectFileGenerator.MakeXcodeGuid(), Data.XcodeProjectRelative, "explicitFileType", "text.plist", "\"<group>\"", "Metadata");
				}
			}
		}

		private string Plist(string Command)
		{
			return XcodeUtils.Plist(Command);
		}

		bool MergeIntoTemplateProject(FileReference PBXProjFilePath, XcodeProject RootProject, FileReference TemplateProject)
		{
			// activate a file for plist reading/writing here
			XcodeUtils.SetActivePlistFile(PBXFilePathForPlatform(CurrentPlistPlatform).FullName);

			// copy existing template project to final location
			if (FileReference.Exists(PBXProjFilePath))
			{
				FileReference.Delete(PBXProjFilePath);
			}
			DirectoryReference.CreateDirectory(PBXProjFilePath.Directory);
			FileReference.Copy(TemplateProject, PBXProjFilePath);

			// write the nodes we need to add (Build/Index targets)
			XcodeRunTarget RunTarget = XcodeProjectNode.GetNodesOfType<XcodeRunTarget>(RootProject).First();
			XcodeBuildTarget BuildTarget = XcodeProjectNode.GetNodesOfType<XcodeBuildTarget>(RunTarget).First();
			XcodeIndexTarget IndexTarget = XcodeProjectNode.GetNodesOfType<XcodeIndexTarget>(RootProject).First();
			XcodeDependency BuildDependency = XcodeProjectNode.GetNodesOfType<XcodeDependency>(RunTarget).First();

			// the runtarget and project need to write out so all of their xcconfigs get written as well,
			// so write everything to a temp string that is tossed, but all xcconfigs will be done at least
			StringBuilder Temp = new StringBuilder();
			XcodeProjectNode.WriteNodeAndReferences(Temp, RootProject!);

			StringBuilder Content = new StringBuilder();
			Content.WriteLine(0, "{");
			FileCollection.Write(Content);
			XcodeProjectNode.WriteNodeAndReferences(Content, BuildTarget);
			XcodeProjectNode.WriteNodeAndReferences(Content, IndexTarget);
			XcodeProjectNode.WriteNodeAndReferences(Content, BuildDependency);
			Content.WriteLine(0, "}");

			// write to disk
			FileReference ImportFile = FileReference.Combine(PBXProjFilePath.Directory, "import.plist");
			File.WriteAllText(ImportFile.FullName, Content.ToString());


			// cache some standard guids from the template project
			string ProjectGuid = Plist($"Print :rootObject");
			string TemplateMainGroupGuid = Plist($"Print :objects:{ProjectGuid}:mainGroup");

			// fixup paths that were relative to original project to be relative to merged project
//			List<string> ObjectGuids = PlistObjects();
			IEnumerable<string> MainGroupChildrenGuids = XcodeUtils.PlistArray($":objects:{TemplateMainGroupGuid}:children");

			string RelativeFromMergedToTemplate = TemplateProject.Directory.ParentDirectory!.MakeRelativeTo(PBXProjFilePath.Directory.ParentDirectory!);

			// look for groups with a 'path' element that is in the main group, so that it and everything will get redirected to new location
			string? FixedPath;
			foreach (string ChildGuid in MainGroupChildrenGuids)
			{
				string IsA = Plist($"Print :objects:{ChildGuid}:isa");
				// if a Group has a path
				if (IsA == "PBXGroup")
				{
					if ((FixedPath = XcodeUtils.PlistFixPath($":objects:{ChildGuid}:path", RelativeFromMergedToTemplate)) != null)
					{
						// if there wasn't a name before, it will now have a nasty path as the name, so add it now
						XcodeUtils.PlistSetAdd($":objects:{ChildGuid}:name", Path.GetFileName(FixedPath));
					}
				}
			}

			// and import it into the template
			Plist($"Merge \"{ImportFile.FullName}\" :objects");


			// get all the targets in the template that are application types
			IEnumerable<string> AppTargetGuids = XcodeUtils.PlistArray($":objects:{ProjectGuid}:targets")
				.Where(TargetGuid => (Plist($"Print :objects:{TargetGuid}:productType") == "com.apple.product-type.application"));

			// add a dependency on the build target from the app target(s)
			foreach (string AppTargetGuid in AppTargetGuids)
			{
				Plist($"Add :objects:{AppTargetGuid}:dependencies:0 string {BuildDependency.Guid}");
			}

			// the BuildDependency object was in the "container" of the generated project, not the merged one, so fix it up now
			Plist($"Set :objects:{BuildDependency.ProxyGuid}:containerPortal {ProjectGuid}");

			// now add all the non-run targets from the generated
			foreach (XcodeTarget Target in XcodeProjectNode.GetNodesOfType<XcodeTarget>(RootProject!).Where(x => x.GetType() != typeof(XcodeRunTarget)))
			{
				Plist($"Add :objects:{ProjectGuid}:targets:0 string {Target.Guid}");
			}


			// hook up Xcconfig files to the project and the project configs
			// @todo how to manage with conflicts already present...
			//PlistSetAdd($":objects:{ProjectGuid}:baseConfigurationReference", RootProject.Xcconfig!.Guid, "string");

			// re-get the list of targets now that we merged in the other file
			IEnumerable<string> AllTargetGuids = XcodeUtils.PlistArray($":objects:{ProjectGuid}:targets");

			List<string> NodesToFix = new() { ProjectGuid };
			NodesToFix.AddRange(AllTargetGuids);

			bool bIsProject = true;
			foreach (string NodeGuid in NodesToFix)
			{
				bool bIsAppTarget = AppTargetGuids.Contains(NodeGuid);

				// get the config list, and from there we can get the configs
				string ProjectBuildConfigListGuid = Plist($"Print :objects:{NodeGuid}:buildConfigurationList");

				IEnumerable<string> ConfigGuids = XcodeUtils.PlistArray($":objects:{ProjectBuildConfigListGuid}:buildConfigurations");
				foreach (string ConfigGuid in ConfigGuids)
				{
					// find the matching unreal generated project build config to hook up to
					// for now we assume Release is Development [Editor], but we should make sure the template project has good configs
					// we have to rename the template config from Release because it won't find the matching config in the build target
					string ConfigName = Plist($"Print :objects:{ConfigGuid}:name");
					if (ConfigName == "Release")
					{
						ConfigName = "Development";
						if (UnrealData.bMakeProjectPerTarget && UnrealData.TargetRules.Type == TargetType.Editor)
						{
							ConfigName = "Development Editor";
						}
						Plist($"Set :objects:{ConfigGuid}:name \"{ConfigName}\"");
					}

					// if there's a plist path, then it will need to be fixed up
					XcodeUtils.PlistFixPath($":objects:{ConfigGuid}:buildSettings:INFOPLIST_FILE", RelativeFromMergedToTemplate);

					if (bIsProject)
					{
						//Console.WriteLine("Looking for " + ConfigName);
						XcodeBuildConfig Config = RootProject!.ProjectBuildConfigs.BuildConfigs.First(x => x.Info.DisplayName == ConfigName);
						XcodeUtils.PlistSetAdd($":objects:{ConfigGuid}:baseConfigurationReference", Config.Xcconfig!.Guid, "string");
					}

					// the Build target used some ini settings to compile, and Run target must match, so we override a few settings, at
					// whatever level they were already specified at (Projet and/or Target)
					XcodeUtils.PlistSetUpdate($":objects:{ConfigGuid}:buildSettings:MACOSX_DEPLOYMENT_TARGET", MacToolChain.Settings.MacOSVersion);
					if (UnrealData.IOSProjectSettings != null)
					{
						XcodeUtils.PlistSetUpdate($":objects:{ConfigGuid}:buildSettings:IPHONEOS_DEPLOYMENT_TARGET", UnrealData.IOSProjectSettings.RuntimeVersion);
					}
					if (UnrealData.TVOSProjectSettings != null)
					{
						XcodeUtils.PlistSetUpdate($":objects:{ConfigGuid}:buildSettings:TVOS_DEPLOYMENT_TARGET", UnrealData.TVOSProjectSettings.RuntimeVersion);
					}
				}

				bIsProject = false;
			}

			// now we need to merge the main groups together
			string GeneratedMainGroupGuid = FileCollection.MainGroupGuid;
			int Index = 0;
			while (true)
			{
				// we copy to a high index to put the copied entries at the end in the same order
				string Output = Plist($"Copy :objects:{GeneratedMainGroupGuid}:children:{Index} :objects:{TemplateMainGroupGuid}:children:100000000");

				// loop until error
				if (Output != "")
				{
					break;
				}
				Index++;
			}
			// and remove the one we copied from
			Plist($"Delete :objects:{GeneratedMainGroupGuid}");

			return true;
		}
	}
}
