// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using System.IO;
using AutomationTool;
using UnrealBuildTool;
using AutomationScripts;
using EpicGames.Core;
using UnrealBuildBase;


public class ModifyStageContext
{
	// any assets that end up in this list that are already in the DeploymentContext will be removed during Apply
	public List<FileReference> UFSFilesToStage = new List<FileReference>();
	// files in this list will remove the matching cooked package from the DeploymentContext and these uncooked assets will replace them
	public List<FileReference> FilesToUncook = new List<FileReference>();
	// these files will just be staged
	public List<FileReference> NonUFSFilesToStage = new List<FileReference>();

	[ConfigFile(ConfigHierarchyType.Game, "CookedEditorSettings")]
	public bool bStageShaderDirs = true;
	[ConfigFile(ConfigHierarchyType.Game, "CookedEditorSettings")]
	public bool bStageBuildDirs = true;
	[ConfigFile(ConfigHierarchyType.Game, "CookedEditorSettings")]
	public bool bStageExtrasDirs = false;
	[ConfigFile(ConfigHierarchyType.Game, "CookedEditorSettings")]
	public bool bStagePlatformDirs = true;
	[ConfigFile(ConfigHierarchyType.Game, "CookedEditorSettings")]
	public bool bStageRestrictedDirs = false;

	public DirectoryReference EngineDirectory;
	public DirectoryReference ProjectDirectory;
	public string ProjectName;
	public string IniPlatformName;

	// when creating a cooked editor against a premade client, this is the sub-directory in the Releases directory to compare against
	public DirectoryReference ReleaseMetadataLocation = null;

	// commandline etc helper
	private BuildCommand Command;

	public ModifyStageContext(DirectoryReference EngineDirectory, ProjectParams Params, BuildCommand Command)
	{
		this.EngineDirectory = EngineDirectory;
		this.Command = Command;

		// cache some useful properties
		ProjectDirectory = Params.RawProjectPath.Directory;
		ProjectName = Params.RawProjectPath.GetFileNameWithoutAnyExtensions();
		IniPlatformName = ConfigHierarchy.GetIniPlatformName(Params.ClientTargetPlatforms[0].Type);

		ConfigCache.ReadSettings(ProjectDirectory, BuildHostPlatform.Current.Platform, this);

		// cache info for DLC against a release
		if (Params.BasedOnReleaseVersionPathOverride != null)
		{
			ReleaseMetadataLocation = DirectoryReference.Combine(new DirectoryReference(Params.BasedOnReleaseVersionPathOverride), "Metadata");
		}
	}

	public void Apply(DeploymentContext SC)
	{
		if (ReleaseMetadataLocation != null)
		{
			// remove files that we are about to stage that were already in the shipped client
			RemoveReleasedFiles(SC);
		}

		// maps can't be cooked and loaded by the editor, so make sure no cooked ones exist
		UncookMaps(SC);

		Dictionary<StagedFileReference, FileReference> StagedUFSFiles = MimicStageFiles(SC, UFSFilesToStage);
		Dictionary<StagedFileReference, FileReference> StagedNonUFSFiles = MimicStageFiles(SC, NonUFSFilesToStage);
		Dictionary<StagedFileReference, FileReference> StagedUncookFiles = MimicStageFiles(SC, FilesToUncook);

		// filter out already-cooked assets
		foreach (var CookedFile in SC.FilesToStage.UFSFiles)
		{
			// remove any of the entries in the "staged" UFSFilesToStage that match already staged files
			// we don't check extension here because the UFSFilesToStage should only contain .uasset/.umap files, and not .uexp, etc, 
			// and .uasset/.umap files are going to be in SC.FilesToStage
			StagedUFSFiles.Remove(CookedFile.Key);
		}

		// remove already-cooked assets to be replaced with 
		string[] CookedExtensions = { ".uasset", ".umap", ".ubulk", ".uexp" };
		foreach (var UncookedFile in StagedUncookFiles)
		{
			string PathWithNoExtension = Path.ChangeExtension(UncookedFile.Key.Name, null);
			// we need to remove cooked files that match the files to Uncook, and there can be several extensions
			// for each source asset, so remove them all
			foreach (string CookedExtension in CookedExtensions)
			{
				StagedFileReference PathWithExtension = new StagedFileReference(PathWithNoExtension + CookedExtension);
				SC.FilesToStage.UFSFiles.Remove(PathWithExtension);
				StagedUFSFiles.Remove(PathWithExtension);
			}
		}

		// stage the filtered UFSFiles
		SC.StageFiles(StagedFileType.UFS, StagedUFSFiles.Values);
		
		// stage the Uncooked files now that any cooked ones are removed from SC
		SC.StageFiles(StagedFileType.UFS, StagedUncookFiles.Values);

		// stage the processed NonUFSFiles
		SC.StageFiles(StagedFileType.NonUFS, StagedNonUFSFiles.Values);

		// now remove or allow restricted files
		HandleRestrictedFiles(SC, ref SC.FilesToStage.UFSFiles);
		HandleRestrictedFiles(SC, ref SC.FilesToStage.NonUFSFiles);
	}

	#region Private implementation

	private StagedFileReference MakeRelativeStagedReference(DeploymentContext SC, FileSystemReference Ref)
	{
		return MakeRelativeStagedReference(SC, Ref, out _);
	}

	private StagedFileReference MakeRelativeStagedReference(DeploymentContext SC, FileSystemReference Ref, out DirectoryReference RootDir)
	{
		if (Ref.IsUnderDirectory(ProjectDirectory))
		{
			RootDir = ProjectDirectory;
			return Project.ApplyDirectoryRemap(SC, new StagedFileReference(ProjectName + "/" + Ref.MakeRelativeTo(ProjectDirectory).Replace('\\', '/')));
		}
		else if (Ref.IsUnderDirectory(EngineDirectory))
		{
			RootDir = EngineDirectory;
			return Project.ApplyDirectoryRemap(SC, new StagedFileReference( "Engine/" + Ref.MakeRelativeTo(EngineDirectory).Replace('\\', '/')));
		}
		throw new Exception();
	}

	private FileReference UnmakeRelativeStagedReference(DeploymentContext SC, StagedFileReference Ref)
	{
		// paths will be in the form "Engine/Foo" or "{ProjectName}/Foo" (or something that we don't handle, so assert)
		// So, replace the Engine/ with {EngineDir} and {ProjectName}/ with {ProjectDir}, and then append Foo
		if (Ref.Name.StartsWith("Engine/"))
		{
			// skip over "Engine/" which is 7 chars long
			return FileReference.Combine(EngineDirectory, Ref.Name.Substring(7));
		}
		else if (Ref.Name.StartsWith(ProjectName + "/"))
		{
			return FileReference.Combine(ProjectDirectory, Ref.Name.Substring(ProjectName.Length + 1));
		}
		throw new Exception();
	}

	private void RemoveReleasedFiles(DeploymentContext SC)
	{
		HashSet<StagedFileReference> ShippedFiles = new HashSet<StagedFileReference>();
		Action<string, string> FindShippedFiles = (string ParamName, string FileNamePortion) =>
		{
			FileReference UFSManifestFile = Command.ParseOptionalFileReferenceParam(ParamName);
			if (UFSManifestFile == null)
			{
				UFSManifestFile = FileReference.Combine(ReleaseMetadataLocation, $"Manifest_{FileNamePortion}_{SC.StageTargetPlatform.PlatformType}.txt");
			}
			if (FileReference.Exists(UFSManifestFile))
			{
				foreach (string Line in File.ReadAllLines(UFSManifestFile.FullName))
				{
					string[] Tokens = Line.Split("\t".ToCharArray());
					if (Tokens?.Length > 1)
					{
						ShippedFiles.Add(new StagedFileReference(Tokens[0]));
					}
				}
			}
		};

		FindShippedFiles("ClientUFSManifest", "UFSFiles");
		FindShippedFiles("ClientNonUFSManifest", "NonUFSFiles");
		FindShippedFiles("ClientDebugManifest", "DebugFiles");

		ShippedFiles.RemoveWhere(x => x.HasExtension(".ttf") && !x.Name.Contains("LastResort"));

		var RemappedNonUFS = NonUFSFilesToStage.Select(x => MakeRelativeStagedReference(SC, x));

		UFSFilesToStage.RemoveAll(x => ShippedFiles.Contains(MakeRelativeStagedReference(SC, x)));
		NonUFSFilesToStage.RemoveAll(x => ShippedFiles.Contains(MakeRelativeStagedReference(SC, x)));
	}
	private Dictionary<StagedFileReference, FileReference> MimicStageFiles(DeploymentContext SC, List<FileReference> SourceFiles)
	{
		Dictionary<StagedFileReference, FileReference> Mapping = new Dictionary<StagedFileReference, FileReference>();

		foreach (FileReference FileRef in new HashSet<FileReference>(SourceFiles))
		{
			DirectoryReference RootDir;
			StagedFileReference StagedFile = MakeRelativeStagedReference(SC, FileRef, out RootDir);

			// check if the remapped file is restricted
			FileReference StagedFileRef = FileReference.Combine(RootDir, StagedFile.Name);
			if (StagedFileRef.ContainsAnyNames(SC.RestrictedFolderNames, RootDir))
			{
//				Console.WriteLine("{0} is restricted", FileRef.FullName);
				if (bStageRestrictedDirs)
				{
					// if we want to stage restricted files, then we need to add the folder to the allow list
					if (!SC.DirectoriesAllowList.Contains(StagedFile.Directory))
					{
						Console.WriteLine("Allowing dir {0}", StagedFile.Directory.Name);
						SC.DirectoriesAllowList.Add(StagedFile.Directory);
					}
				}
				else
				{
//					Console.WriteLine(" .. skipping");
					// otherwise, don't return this file in the output
					continue;
				}
			}

			// add the mapping
			Mapping.Add(StagedFile, FileRef);
		}

		return Mapping;
	}

	private void HandleRestrictedFiles(DeploymentContext SC, ref Dictionary<StagedFileReference, FileReference> Files)
	{
		if (bStageRestrictedDirs)
		{
			foreach (var Pair in Files)
			{
				if (SC.RestrictedFolderNames.Any(x => Pair.Key.ContainsName(x)))
				{
					Console.WriteLine("Allowing dir {0}", Pair.Value.Directory.FullName);
					SC.DirectoriesAllowList.Add(Pair.Key.Directory);
				}
			}
		}
		else
		{
			// remove entries where any restricted folder names are in the name
			Files = Files.Where(x => !SC.RestrictedFolderNames.Any(y => x.Key.ContainsName(y))).ToDictionary(x => x.Key, x => x.Value);
		}
		//foreach (var Pair in Files)
		//{
		//	if (SC.RestrictedFolderNames.Any(x => Pair.Key.ContainsName(x))
		//	{
		//		//				Console.WriteLine("{0} is restricted", FileRef.FullName);
		//		if (bStageRestrictedDirs)
		//		{
		//			// if we want to stage restricted files, then we need to explicitly allow the folder
		//			if (!SC.DirectoriesAllowList.Contains(StagedFile.Directory))
		//			{
		//				Console.WriteLine("Allowing dir {0}", StagedFile.Directory.Name);
		//				SC.DirectoriesAllowList.Add(StagedFile.Directory);
		//			}
		//		}
		//		else
		//		{
		//			//					Console.WriteLine(" .. skipping");
		//			// otherwise, don't return this file in the output
		//			continue;
		//		}
		//	}
		//}
	}

	private void UncookMaps(DeploymentContext SC)
	{
		// remove maps from SC and Context (SC has path to the cooked map, so we have to come back from Staged refernece that doesn't have the Cooked dir in it)
		FilesToUncook.AddRange(SC.FilesToStage.UFSFiles.Keys.Where(x => x.HasExtension("umap")).Select(y => UnmakeRelativeStagedReference(SC, y)));
		FilesToUncook.AddRange(UFSFilesToStage.Where(x => x.GetExtension() == ".umap"));
	}

	#endregion
}


public class MakeCookedEditor : BuildCommand
{
	public override void ExecuteBuild()
	{
		LogInformation("************************* MakeCookedEditor");

		ProjectParams BuildParams = GetParams();

		LogInformation("Build? {0}", BuildParams.Build);

		Project.Build(this, BuildParams);
		Project.Cook(BuildParams);
		Project.CopyBuildToStagingDirectory(BuildParams);

		//this will do packaging if requested, and also symbol upload if requested.
		Project.Package(BuildParams);

		Project.Archive(BuildParams);
		PrintRunTime();
		Project.Deploy(BuildParams);


	}

	protected virtual void StageEngineEditorFiles(ProjectParams Params, DeploymentContext SC, ModifyStageContext Context)
	{
		StagePlatformExtensionFiles(Params, SC, Context, Unreal.EngineDirectory);
		StagePluginFiles(Params, SC, Context, true);

		// engine shaders
		if (Context.bStageShaderDirs)
		{
			Context.NonUFSFilesToStage.AddRange(DirectoryReference.EnumerateFiles(DirectoryReference.Combine(Unreal.EngineDirectory, "Shaders"), "*", SearchOption.AllDirectories));
			GatherTargetDependencies(Params, SC, Context, "ShaderCompileWorker");
		}

		StageIniPathArray(Params, SC, "EngineExtraStageFiles", Unreal.EngineDirectory, Context);

		Context.FilesToUncook.Add(FileReference.Combine(Context.EngineDirectory, "Content", "EngineMaterials", "DefaultMaterial.uasset"));
	}

	protected virtual void StageProjectEditorFiles(ProjectParams Params, DeploymentContext SC, ModifyStageContext Context)
	{
		// always stage the main exe, in case DLC mode is on, then it won't by default
		GatherTargetDependencies(Params, SC, Context, SC.StageExecutables[0]);

		StagePlatformExtensionFiles(Params, SC, Context, Context.ProjectDirectory);
		StagePluginFiles(Params, SC, Context, false);

		// add stripped out editor .ini files back in
		Context.UFSFilesToStage.AddRange(DirectoryReference.EnumerateFiles(DirectoryReference.Combine(Context.ProjectDirectory, "Config"), "*Editor*", SearchOption.AllDirectories));

		StageIniPathArray(Params, SC, "ProjectExtraStageFiles", Context.ProjectDirectory, Context);

		if (Context.ReleaseMetadataLocation != null)
		{
			// we need to remap this file, so stage it directly
			SC.StageFile(StagedFileType.UFS, FileReference.Combine(Context.ReleaseMetadataLocation, "DevelopmentAssetRegistry.bin"), new StagedFileReference($"{Context.ProjectName}/EditorClientAssetRegistry.bin"));
		}
	}

	protected virtual void StagePluginDirectory(DirectoryReference PluginDir, ModifyStageContext Context, bool bStageUncookedContent)
	{
		foreach (DirectoryReference Subdir in DirectoryReference.EnumerateDirectories(PluginDir))
		{
			StagePluginSubdirectory(Subdir, Context, bStageUncookedContent);
		}
	}

	protected virtual void StagePluginSubdirectory(DirectoryReference PluginSubdir, ModifyStageContext Context, bool bStageUncookedContent)
	{
		string DirNameLower = PluginSubdir.GetDirectoryName().ToLower();

		if (DirNameLower == "content")
		{
			if (bStageUncookedContent)
			{
				Context.FilesToUncook.AddRange(DirectoryReference.EnumerateFiles(PluginSubdir, "*", SearchOption.AllDirectories));
			}
			else
			{
				Context.UFSFilesToStage.AddRange(DirectoryReference.EnumerateFiles(PluginSubdir, "*", SearchOption.AllDirectories));
			}
		}
		else if (DirNameLower == "resources" || DirNameLower == "config" || DirNameLower == "scripttemplates")
		{
			Context.UFSFilesToStage.AddRange(DirectoryReference.EnumerateFiles(PluginSubdir, "*", SearchOption.AllDirectories));
		}
		else if (DirNameLower == "shaders" && Context.bStageShaderDirs)
		{
			Context.NonUFSFilesToStage.AddRange(DirectoryReference.EnumerateFiles(PluginSubdir, "*", SearchOption.AllDirectories));
		}
	}

	protected virtual ModifyStageContext CreateContext(ProjectParams Params)
	{
		return new ModifyStageContext(Unreal.EngineDirectory, Params, this);
	}

	protected virtual void ModifyParams(ProjectParams BuildParams)
	{
	}

	protected virtual void PreModifyDeploymentContext(ProjectParams Params, DeploymentContext SC)
	{
		ModifyStageContext Context = CreateContext(Params);

		DefaultPreModifyDeploymentContext(Params, SC, Context);

		Context.Apply(SC);
	}

	protected virtual void ModifyDeploymentContext(ProjectParams Params, DeploymentContext SC)
	{
		ModifyStageContext Context = CreateContext(Params);

		DefaultModifyDeploymentContext(Params, SC, Context);

		Context.Apply(SC);
	}

	protected virtual void SetupDLCMode(FileReference ProjectFile, out string DLCName, out string ReleaseVersion, out TargetType Type)
	{
		bool bBuildAgainstRelease;
		ConfigHierarchy GameConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, ProjectFile.Directory, BuildHostPlatform.Current.Platform);
		if (GameConfig.GetBool("CookedEditorSettings", "bBuildAgainstRelease", out bBuildAgainstRelease) && bBuildAgainstRelease)
		{
			GameConfig.GetString("CookedEditorSettings", "DLCPluginName", out DLCName);
			GameConfig.GetString("CookedEditorSettings", "ReleaseName", out ReleaseVersion);

			// if not set, default to gamename
			if (string.IsNullOrEmpty(ReleaseVersion))
			{
				ReleaseVersion = ProjectFile.GetFileNameWithoutAnyExtensions();
			}

			string TargetTypeString;
			GameConfig.GetString("CookedEditorSettings", "ReleaseTargetType", out TargetTypeString);
			Type = (TargetType)Enum.Parse(typeof(TargetType), TargetTypeString);
		}
		else
		{
			DLCName = null;
			ReleaseVersion = null;
			Type = TargetType.Game;
		}
	}







	protected void StagePlatformExtensionFiles(ProjectParams Params, DeploymentContext SC, ModifyStageContext Context, DirectoryReference RootDir)
	{
		if (!Context.bStagePlatformDirs)
		{
			return;
		}

		DirectoryReference[] RootPlatformsFolders = 
		{
			DirectoryReference.Combine(RootDir, "Platforms"),
			DirectoryReference.Combine(RootDir, "Restricted", "NotForLicensees", "Platforms"),
		};

		List<string> RootFoldersToStrip = new List<string> { "Source", "Binaries" };
		List<string> SubFoldersToStrip = new List<string> { "Source", "Intermediate", "Tests", "Binaries" + Path.DirectorySeparatorChar + HostPlatform.Current.HostEditorPlatform.ToString() };
		if (!Context.bStageShaderDirs)
		{
			RootFoldersToStrip.Add("Shaders");
		}
		if (!Context.bStageBuildDirs)
		{
			RootFoldersToStrip.Add("Build");
		}
		if (!Context.bStageExtrasDirs)
		{
			RootFoldersToStrip.Add("Extras");
		}

		foreach (DirectoryReference PlatformsDir in RootPlatformsFolders)
		{
			if (!DirectoryReference.Exists(PlatformsDir))
			{
				continue;
			}
			foreach (DirectoryReference PlatformDir in DirectoryReference.EnumerateDirectories(PlatformsDir, "*", SearchOption.TopDirectoryOnly))
			{
				foreach (DirectoryReference Subdir in DirectoryReference.EnumerateDirectories(PlatformDir, "*", SearchOption.TopDirectoryOnly))
				{
					// Remvoe some unnecessary folders that can be large
					List<FileReference> ContextFileList = Context.UFSFilesToStage;

					if (Subdir.GetDirectoryName() == "Shaders")
					{
						ContextFileList = Context.NonUFSFilesToStage;
					}

					List<FileReference> FilesToStage = new List<FileReference>();
					// if we aren't in a bad subdir, add files
					if (!RootFoldersToStrip.Contains(Subdir.GetDirectoryName(), StringComparer.InvariantCultureIgnoreCase))
					{
						FilesToStage.AddRange(DirectoryReference.EnumerateFiles(Subdir, "*", SearchOption.AllDirectories));

						// now remove files in subdirs we want to skip
						FilesToStage.RemoveAll(x => x.ContainsAnyNames(SubFoldersToStrip, Subdir));
						ContextFileList.AddRange(FilesToStage);
					}
				}
			}
		}
	}

	protected void StagePluginFiles(ProjectParams Params, DeploymentContext SC, ModifyStageContext Context, bool bEnginePlugins)
	{
		List<FileReference> ActivePlugins = new List<FileReference>();
		foreach (StageTarget Target in SC.StageTargets)
		{
			if (Target.Receipt.TargetType == TargetType.Editor)
			{
				IEnumerable<RuntimeDependency> TargetPlugins = Target.Receipt.RuntimeDependencies.Where(x => x.Path.GetExtension().ToLower() == ".uplugin");
				// grab just engine plugins, or non-engine plugins depending
				TargetPlugins = TargetPlugins.Where(x => (bEnginePlugins ? x.Path.IsUnderDirectory(Unreal.EngineDirectory) : !x.Path.IsUnderDirectory(Unreal.EngineDirectory)));

				// convert to paths
				ActivePlugins.AddRange(TargetPlugins.Select(x => x.Path));
			}
		}

		foreach (FileReference ActivePlugin in ActivePlugins)
		{
			PluginInfo Plugin = new PluginInfo(ActivePlugin, bEnginePlugins ? PluginType.Engine : PluginType.Project);
			// we don't cook for unsupported target platforms, but the plugin may still need to be used in the editor, so
			// stage uncooked assets for these plugins
			bool bStageUncookedContent = (!Plugin.Descriptor.SupportsTargetPlatform(SC.StageTargetPlatform.PlatformType));

			StagePluginDirectory(ActivePlugin.Directory, Context, bStageUncookedContent);
		}

	}

	protected void StageIniPathArray(ProjectParams Params, DeploymentContext SC, string IniKey, DirectoryReference BaseDirectory, ModifyStageContext Context)
	{
		List<string> Entries;
		ConfigHierarchy GameConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, Context.ProjectDirectory, BuildHostPlatform.Current.Platform);

		if (GameConfig.GetArray("CookedEditorSettings", IniKey, out Entries))
		{
			foreach (string Entry in Entries)
			{
				Dictionary<string, string> Props = ParseStructProperties(Entry);

				string SubPath = Props["Path"];
				string FileWildcard = "*";
				List<FileReference> FileList = Context.UFSFilesToStage;
				SearchOption SearchMode = SearchOption.AllDirectories;
				if (Props.ContainsKey("Files"))
				{
					FileWildcard = Props["Files"];
				}
				if (Props.ContainsKey("NonUFS") && bool.Parse(Props["NonUFS"]) == true)
				{
					FileList = Context.NonUFSFilesToStage;
				}
				if (Props.ContainsKey("Recursive") && bool.Parse(Props["Recursive"]) == false)
				{
					SearchMode = SearchOption.TopDirectoryOnly;
				}

				// now enumerate files based on the settings
				DirectoryReference Dir = DirectoryReference.Combine(BaseDirectory, SubPath);
				if (DirectoryReference.Exists(Dir))
				{
					FileList.AddRange(DirectoryReference.EnumerateFiles(Dir, FileWildcard, SearchMode));
				}
			}
		}
	}


	protected void DefaultPreModifyDeploymentContext(ProjectParams Params, DeploymentContext SC, ModifyStageContext Context)
	{

	}
	protected void DefaultModifyDeploymentContext(ProjectParams Params, DeploymentContext SC, ModifyStageContext Context)
	{
		StageEngineEditorFiles(Params, SC, Context);
		StageProjectEditorFiles(Params, SC, Context);


		// final filtering

		// we already cooked assets, so remove assets we may have found, except for the Uncook ones
		Context.UFSFilesToStage.RemoveAll(x => x.GetExtension() == ".uasset");

		// don't need the .target files
		Context.NonUFSFilesToStage.RemoveAll(x => x.GetExtension() == ".target");

		if (!Context.bStageShaderDirs)
		{
			// don't need standalone shaders
			Context.UFSFilesToStage.RemoveAll(x => x.GetExtension() == ".glsl");
			Context.UFSFilesToStage.RemoveAll(x => x.GetExtension() == ".hlsl");
		}

		// move some files from UFS to NonUFS if they ended up there
		List<string> UFSIncompatibleExtensions = new List<string> { ".py", ".pyc" };
		Context.NonUFSFilesToStage.AddRange(Context.UFSFilesToStage.Where(x => UFSIncompatibleExtensions.Contains(x.GetExtension())));
		Context.UFSFilesToStage.RemoveAll(x => UFSIncompatibleExtensions.Contains(x.GetExtension()));
	}

	private ProjectParams GetParams()
	{
		FileReference ProjectPath = ParseProjectParam();

		// setup DLC defaults, then ask project if it should 
		string DLCName;
		string BasedOnReleaseVersion;
		TargetType ReleaseType;
		SetupDLCMode(ProjectPath, out DLCName, out BasedOnReleaseVersion, out ReleaseType);

		var Params = new ProjectParams
		(
			Command: this,
			RawProjectPath: ProjectPath

			// standard cookededitor settings
			//			, Client:false
			//			, EditorTargets: new ParamList<string>()
			// , SkipBuildClient: true
			, NoBootstrapExe: true
			// , Client: true
			, DLCName: DLCName
			, BasedOnReleaseVersion: BasedOnReleaseVersion
		);

		string TargetPlatformType = "CookedEditor";
		string TargetName;

		// look to see if ini overrides tgarget name
		ConfigHierarchy GameConfig = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, ProjectPath.Directory, BuildHostPlatform.Current.Platform);
		if (!GameConfig.GetString("CookedEditorSettings", "CookedEditorTargetName", out TargetName))
		{
			// if not, then use ProjectCookedEditor
			TargetName = ProjectPath.GetFileNameWithoutAnyExtensions() + TargetPlatformType;
		}

		// cook the cooked editor targetplatorm as the "client"
		Params.ClientCookedTargets.Clear();
		Params.ClientCookedTargets.Add(TargetName);
		//Params.ClientCookedTargets.Add("CrashReportClientEditor");
		Params.ClientTargetPlatforms = new List<TargetPlatformDescriptor>() { new TargetPlatformDescriptor(Params.ClientTargetPlatforms[0].Type, TargetPlatformType) };

		Params.ServerCookedTargets.Clear();

		// when making cooked editors, we some special commandline options to override some assumptions about editor data
		Params.AdditionalCookerOptions += " -ini:Engine:[Core.System]:CanStripEditorOnlyExportsAndImports=False";
		// We tend to "over-cook" packages to get everything we might need, so some non-editor BPs that are referencing editor BPs may
		// get cooked. This is okay, because the editor stuff should exist. We may want to revist this, and not cook anything that would
		// cause the issues
		Params.AdditionalCookerOptions += " -AllowUnsafeBlueprintCalls";
		Params.AdditionalCookerOptions += " -dpcvars=cook.displaymode=2,r.ForceDebugViewModes=1";

		// set up cooking against a client, as DLC
		if (BasedOnReleaseVersion != null)
		{
			// make the platform name, like "WindowsClient", or "LinuxGame", of the premade build we are cooking/staging against
			string IniPlatformName = ConfigHierarchy.GetIniPlatformName(Params.ClientTargetPlatforms[0].Type);
			string ReleaseTargetName = IniPlatformName + (ReleaseType == TargetType.Game ? "NoEditor" : ReleaseType.ToString());

			Params.AdditionalCookerOptions += " -CookAgainstFixedBase";
			Params.AdditionalCookerOptions += $" -DevelopmentAssetRegistryPlatformOverride={ReleaseTargetName}";
			Params.AdditionalIoStoreOptions += $" -DevelopmentAssetRegistryPlatformOverride={ReleaseTargetName}";

			// point to where the premade asset registry can be found
			Params.BasedOnReleaseVersionPathOverride = CommandUtils.CombinePaths(ProjectPath.Directory.FullName, "Releases", BasedOnReleaseVersion, ReleaseTargetName);

			Params.DLCOverrideStagedSubDir = "";
			Params.DLCIncludeEngineContent = true;

		}



		// set up override functions
		Params.PreModifyDeploymentContextCallback = new Action<ProjectParams, DeploymentContext>((ProjectParams P, DeploymentContext SC) => { PreModifyDeploymentContext(P, SC); });
		Params.ModifyDeploymentContextCallback = new Action<ProjectParams, DeploymentContext>((ProjectParams P, DeploymentContext SC) => { ModifyDeploymentContext(P, SC); });

		ModifyParams(Params);

		return Params;
	}



	protected static void GatherTargetDependencies(ProjectParams Params, DeploymentContext SC, ModifyStageContext Context, string ReceiptName)
	{
		GatherTargetDependencies(Params, SC, Context, ReceiptName, UnrealTargetConfiguration.Development);
	}

	protected static void GatherTargetDependencies(ProjectParams Params, DeploymentContext SC, ModifyStageContext Context, string ReceiptName, UnrealTargetConfiguration Configuration)
	{
		string Architecture = Params.SpecifiedArchitecture;
		if (string.IsNullOrEmpty(Architecture))
		{
			Architecture = "";
			if (PlatformExports.IsPlatformAvailable(SC.StageTargetPlatform.IniPlatformType))
			{
				Architecture = PlatformExports.GetDefaultArchitecture(SC.StageTargetPlatform.IniPlatformType, Params.RawProjectPath);
			}
		}

		FileReference ReceiptFilename = TargetReceipt.GetDefaultPath(Params.RawProjectPath.Directory, ReceiptName, SC.StageTargetPlatform.IniPlatformType, Configuration, Architecture);
		if (!FileReference.Exists(ReceiptFilename))
		{
			ReceiptFilename = TargetReceipt.GetDefaultPath(Unreal.EngineDirectory, ReceiptName, SC.StageTargetPlatform.IniPlatformType, Configuration, Architecture);
		}

		TargetReceipt Receipt;
		if (!TargetReceipt.TryRead(ReceiptFilename, out Receipt))
		{
			throw new AutomationException("Missing or invalid target receipt ({0})", ReceiptFilename);
		}

		foreach (BuildProduct BuildProduct in Receipt.BuildProducts)
		{
			Context.NonUFSFilesToStage.Add(BuildProduct.Path);
		}

		foreach (RuntimeDependency RuntimeDependency in Receipt.RuntimeDependencies)
		{
			if (RuntimeDependency.Type == StagedFileType.UFS)
			{
				Context.UFSFilesToStage.Add(RuntimeDependency.Path);
			}
			else// if (RuntimeDependency.Type == StagedFileType.NonUFS)
			{
				Context.NonUFSFilesToStage.Add(RuntimeDependency.Path);
			}
			//else
			//{
			//	// otherwise, just stage it directly
			//	// @todo: add a FilesToStage type to context like SC has?
			//	SC.StageFile(RuntimeDependency.Type, RuntimeDependency.Path);
			//}
		}

		Context.NonUFSFilesToStage.Add(ReceiptFilename);
	}




	// @todo: Move this into UBT or something
	private static Dictionary<string, string> ParseStructProperties(string PropsString)
	{
		// we expect parens around a properly encoded struct
		if (!PropsString.StartsWith("(") || !PropsString.EndsWith(")"))
		{
			return null;
		}
		// strip ()
		PropsString = PropsString.Substring(1, PropsString.Length - 2);

		List<string> Props = new List<string>();

		int TokenStart = 0;
		int StrLen = PropsString.Length;
		while (TokenStart < StrLen)
		{
			// get the next location of each special character
			int NextComma = PropsString.IndexOf(',', TokenStart);
			int NextQuote = PropsString.IndexOf('\"', TokenStart);
			// comma first? easy
			if (NextComma != -1 && NextComma < NextQuote)
			{
				Props.Add(PropsString.Substring(TokenStart, NextComma - TokenStart));
				TokenStart = NextComma + 1;
			}
			// comma but no quotes
			else if (NextComma != -1 && NextQuote == -1)
			{
				Props.Add(PropsString.Substring(TokenStart, NextComma - TokenStart));
				TokenStart = NextComma + 1;
			}
			// neither found, use the rest
			else if (NextComma == -1 && NextQuote == -1)
			{
				Props.Add(PropsString.Substring(TokenStart));
				break;
			}
			// quote first? look for quote after
			else
			{
				NextQuote = PropsString.IndexOf('\"', NextQuote + 1);
				// are we at the end?
				if (NextQuote + 1 == StrLen)
				{
					// use the rest of the string
					Props.Add(PropsString.Substring(TokenStart));
					break;
				}
				// it's expected that the following character is a comma, if not, give up
				if (PropsString[NextQuote + 1] != ',')
				{
					break;
				}
				// if next is comma, we are done this token
				Props.Add(PropsString.Substring(TokenStart, (NextQuote - TokenStart) + 1));
				// skip over the quote and following commma
				TokenStart = NextQuote + 2;
			}
		}

		// now make a dictionary from the properties
		Dictionary<string, string> KeyValues = new Dictionary<string, string>();
		foreach (string AProp in Props)
		{
			string Prop = AProp.Trim(" \t".ToCharArray());
			// find the first = (UE4 properties can't have an equal sign, so it's valid to do)
			int Equals = Prop.IndexOf('=');
			// we must have one
			if (Equals == -1)
			{
				continue;
			}

			string Key = Prop.Substring(0, Equals);
			string Value = Prop.Substring(Equals + 1);
			// trim off any quotes around the entire value
			Value = Value.Trim(" \"".ToCharArray());
			Key = Key.Trim(" ".ToCharArray());
			KeyValues.Add(Key, Value);
		}

		// convert to array type
		return KeyValues;
	}
}
