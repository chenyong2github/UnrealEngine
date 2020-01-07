// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using Tools.DotNETCommon;
using System.Linq;
using System.Xml;
using System.Xml.Serialization;
using System.Xml.Linq;

namespace UnrealBuildTool
{
	class UEDeployLumin : UEBuildDeploy, ILuminDeploy
	{
		private FileReference ProjectFile;

		public manifest PackageManifest = new manifest();

		protected UnrealPluginLanguage UPL;

		public void SetLuminPluginData(List<string> Architectures, List<string> inPluginExtraData)
		{
			UPL = new UnrealPluginLanguage(ProjectFile, inPluginExtraData, Architectures, "", "", UnrealTargetPlatform.Lumin);
			UPL.SetTrace();
		}

		public UEDeployLumin(FileReference InProjectFile)
		{
			ProjectFile = InProjectFile;
		}

		private ConfigHierarchy GetConfigCacheIni(ConfigHierarchyType Type)
		{
			return ConfigCache.ReadHierarchy(Type, DirectoryReference.FromFile(ProjectFile), UnrealTargetPlatform.Lumin);
		}

		private string GetRuntimeSetting(string Key)
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			string Value;
			Ini.GetString("/Script/LuminRuntimeSettings.LuminRuntimeSettings", Key, out Value);
			return Value;
		}

		public string GetPackageName(string ProjectName)
		{
			string PackageName = GetRuntimeSetting("PackageName");
			// replace some variables
			PackageName = PackageName.Replace("[PROJECT]", ProjectName);
			PackageName = PackageName.Replace("-", "_");
			// Package names are required to be all lower case.
			return PackageName.ToLower();
		}

		private manifestApplicationComponentType GetApplicationType()
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool Value = false;
			Ini.GetBool("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "bIsScreensApp", out Value);
			if (Value)
			{
				return manifestApplicationComponentType.ScreensImmersive;
			}
			return manifestApplicationComponentType.Fullscreen;
		}

		private string GetApplicationDisplayName(string ProjectName)
		{
			string ApplicationDisplayName = GetRuntimeSetting("ApplicationDisplayName");
			if (String.IsNullOrWhiteSpace(ApplicationDisplayName))
			{
				return ProjectName;
			}
			return ApplicationDisplayName;
		}

		private string GetMinimumAPILevelRequired()
		{
			const Int32 AbsoluteMinValue = 2;

			Int32 Value = AbsoluteMinValue;
			GetConfigCacheIni(ConfigHierarchyType.Engine).
				GetInt32("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "MinimumAPILevel", out Value);
			if (Value < AbsoluteMinValue)
			{
				Log.TraceInformation("Config-specified MinimumAPILevel {0} is lower than engine's minimum {1}", Value, AbsoluteMinValue);
				Value = AbsoluteMinValue;
			}
			return Value.ToString();
		}

		private string CleanFilePath(string FilePath)
		{
			// Removes the extra characters from a FFilePath parameter.
			// This functionality is required in the automation file to avoid having duplicate variables stored in the settings file.
			// Potentially this could be replaced with FParse::Value("IconForegroundModelPath="(FilePath="", Value).
			int startIndex = FilePath.IndexOf('"') + 1;
			int length = FilePath.LastIndexOf('"') - startIndex;
			return FilePath.Substring(startIndex, length);
		}

		public string GetIconModelStagingPath()
		{
			return "Icon/Model";
		}

		public string GetIconPortalStagingPath()
		{
			return "Icon/Portal";
		}

		public string GetProjectRelativeCertificatePath()
		{
			return CleanFilePath(GetRuntimeSetting("Certificate"));
		}

		public bool UseVulkan()
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool Value = false;
			Ini.GetBool("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "bUseVulkan", out Value);
			return Value;
		}

		public string GetVulkanValdationLayerLibsDir()
		{
			return CleanFilePath(GetRuntimeSetting("VulkanValidationLayerLibs"));
		}

		public string GetMLSDKVersion(ConfigHierarchy EngineIni)
		{
			string MLSDKPath;
			string Major = "0";
			string Minor = "0";
			MLSDKPath = Environment.GetEnvironmentVariable("MLSDK");
			if (!string.IsNullOrEmpty(MLSDKPath))
			{
				if (Directory.Exists(MLSDKPath))
				{
					String VersionFile = string.Format("{0}/include/ml_version.h", MLSDKPath).Replace('/', Path.DirectorySeparatorChar);
					if (File.Exists(VersionFile))
					{
						string FileText = File.ReadAllText(VersionFile);
						string Pattern = @"(MLSDK_VERSION_MAJOR) (?'MAJOR'\d+).*(MLSDK_VERSION_MINOR) (?'MINOR'\d+).*(MLSDK_VERSION_REVISION) (?'REV'\d+)";
						Regex VersionRegex = new Regex(Pattern, RegexOptions.Singleline);
						MatchCollection Matches = VersionRegex.Matches(FileText);
						if (Matches.Count > 0 &&
							!string.IsNullOrEmpty(Matches[0].Groups["MAJOR"].Value) &&
							!string.IsNullOrEmpty(Matches[0].Groups["MINOR"].Value))
						{
							Major = Matches[0].Groups["MAJOR"].Value;
							Minor = Matches[0].Groups["MINOR"].Value;
						}
					}
				}
			}
			return string.Format("{0}.{1}", Major, Minor);
		}

		private object GetComponentSubElement(string ElementType, string ElementValue)
		{
			switch (ElementType)
			{
				case "FileExtension":
					return new manifestApplicationComponentFileextension
					{
						name = ElementValue,
					};
				case "MimeType":
					return new manifestApplicationComponentMimetype
					{
						name = ElementValue,
					};
				case "MusicAttribute":
					return new manifestApplicationComponentMusicattribute
					{
						name = ElementValue,
					};
				case "Mode":
					return new manifestApplicationComponentMode
					{
						shareable = ElementValue,
					};
				case "Schema":
					return new manifestApplicationComponentSchema
					{
						name = ElementValue,
					};
				default:
					Log.TraceInformation("Tried to use an unsupported component sub-element type: {0}", ElementType);
					return null;
			}
		}

		private object GetComponentElement(Dictionary<string, string> ComponentElement)
		{
			manifestApplicationComponent OutComponent = new manifestApplicationComponent
			{
				name = ComponentElement["Name"],
				visible_name = ComponentElement["VisibleName"],
			};

			// App developer has the responsibility to package the executable in the bin folder,
			// perhaps by using UPL. We simply generate the manifest correctly.
			string BinaryName = ComponentElement["ExecutableName"];
			if (BinaryName.IndexOf("bin/") != 0)
			{
				// Prepend bin folder string to executable name if it not there already.
				BinaryName = string.Format("bin/{0}", BinaryName);
			}
			OutComponent.binary_name = BinaryName;

			switch (ComponentElement["ComponentType"])
			{
				case "Universe":
					OutComponent.type = manifestApplicationComponentType.Universe;
					break;
				case "Fullscreen":
					OutComponent.type = manifestApplicationComponentType.Fullscreen;
					break;
				case "SearchProvider":
					OutComponent.type = manifestApplicationComponentType.SearchProvider;
					break;
				case "MusicService":
					OutComponent.type = manifestApplicationComponentType.MusicService;
					break;
				case "Screens":
					OutComponent.type = manifestApplicationComponentType.Screens;
					break;
				case "ScreensImmersive":
					OutComponent.type = manifestApplicationComponentType.ScreensImmersive;
					break;
				case "Console":
				default:
					OutComponent.type = manifestApplicationComponentType.Console;
					break;
				case "SystemUI":
					OutComponent.type = manifestApplicationComponentType.SystemUI;
					break;
			}

			if (ComponentElement.ContainsKey("ExtraComponentSubElements"))
			{
				// Unfortunately there are no config object array parsing functions in UBT
				string SubElementsString = ComponentElement["ExtraComponentSubElements"];
				string ConfObjArrayPattern = "\\([a-zA-Z0-9]+=[a-zA-Z0-9]+,[a-zA-Z0-9]+=\"?[a-zA-Z0-9]+\"?\\)";
				Regex ConfigObjArrayRegex = new Regex(ConfObjArrayPattern);
				MatchCollection ConfigObjMatches = ConfigObjArrayRegex.Matches(SubElementsString);
				if (ConfigObjMatches.Count != 0)
				{
					OutComponent.Items = new object[ConfigObjMatches.Count];
					for (int Index = 0; Index < ConfigObjMatches.Count; ++Index)
					{
						Match Match = ConfigObjMatches[Index];
						Dictionary<string, string> SubElement;
						if (ConfigHierarchy.TryParse(Match.Value, out SubElement))
						{
							OutComponent.Items[Index] = GetComponentSubElement(SubElement["ElementType"], SubElement["Value"]);
						}
					}
				}
			}
			return OutComponent;
		}

		public string GenerateManifest(string ProjectName, bool bForDistribution, string Architecture)
		{
			ConfigHierarchy GameIni = GetConfigCacheIni(ConfigHierarchyType.Game);
			string ProjectVersion = string.Empty;
			GameIni.GetString("/Script/EngineSettings.GeneralProjectSettings", "ProjectVersion", out ProjectVersion);
			if (string.IsNullOrEmpty(ProjectVersion))
			{
				ProjectVersion = "1.0.0.0";
			}

			ConfigHierarchy EngineIni = GetConfigCacheIni(ConfigHierarchyType.Engine);
			Int32 VersionCode;
			EngineIni.GetInt32("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "VersionCode", out VersionCode);

			string SDKVersion = GetMLSDKVersion(EngineIni);
			string PackageName = GetPackageName(ProjectName);
			string ApplicationDisplayName = GetApplicationDisplayName(ProjectName);
			string MinimumAPILevel = GetMinimumAPILevelRequired();
			string TargetExecutableName = "bin/" + ProjectName;

			PackageManifest.version_name = ProjectVersion;
			PackageManifest.package = PackageName;
			PackageManifest.version_code = Convert.ToUInt64(VersionCode);

			PackageManifest.application = new manifestApplication
			{
				sdk_version = SDKVersion,
				min_api_level = MinimumAPILevel,
				visible_name = ApplicationDisplayName
			};

			List<string> AppPrivileges;
			EngineIni.GetArray("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "AppPrivileges", out AppPrivileges);

			List<string> ExtraComponentElements;
			EngineIni.GetArray("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "ExtraComponentElements", out ExtraComponentElements);

			// We always add an additional item as that will be our 'root' <component>
			int Size = (ExtraComponentElements == null ? AppPrivileges.Count() : AppPrivileges.Count() + ExtraComponentElements.Count()) + 1;
			// Index used for sibling elements (app privileges, root component and any extra components)
			int CurrentIndex = 0;
			PackageManifest.application.Items = new object[Size];
			// Remove all invalid strings from the list of strings
			AppPrivileges.RemoveAll(item => item == "Invalid");
			// Privileges get added first
			for (int Index = 0; Index < AppPrivileges.Count(); ++Index)
			{
				string TrimmedPrivilege = AppPrivileges[Index].Trim(' ');
				if (TrimmedPrivilege != "")
				{
					PackageManifest.application.Items[CurrentIndex] = new manifestApplicationUsesprivilege
					{
						name = TrimmedPrivilege,
					};
					CurrentIndex++;
				}
			}

			// Then our root component, this is important as `mldb launch` will use the first component in the manifest
			PackageManifest.application.Items[CurrentIndex] = new manifestApplicationComponent();
			manifestApplicationComponent RootComponent = (manifestApplicationComponent)PackageManifest.application.Items[CurrentIndex];
			RootComponent.name = ".fullscreen";
			RootComponent.visible_name = ApplicationDisplayName;
			RootComponent.binary_name = TargetExecutableName;
			RootComponent.type = GetApplicationType();

			// Sub-elements under root <component>
			List<string> ExtraComponentSubElements;
			EngineIni.GetArray("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "ExtraComponentSubElements", out ExtraComponentSubElements);
			RootComponent.Items = (ExtraComponentSubElements == null ? new object[1] : new object[ExtraComponentSubElements.Count() + 1]);

			// Root component icon
			RootComponent.Items[0] = new manifestApplicationComponentIcon();
			((manifestApplicationComponentIcon)RootComponent.Items[0]).model_folder = GetIconModelStagingPath();
			((manifestApplicationComponentIcon)RootComponent.Items[0]).portal_folder = GetIconPortalStagingPath();

			if (ExtraComponentSubElements != null)
			{
				for (int Index = 0; Index < ExtraComponentSubElements.Count(); ++Index)
				{
					Dictionary<string, string> NodeContent;
					if (ConfigHierarchy.TryParse(ExtraComponentSubElements[Index], out NodeContent))
					{
						RootComponent.Items[Index + 1] = GetComponentSubElement(NodeContent["ElementType"], NodeContent["Value"]);
					}
				}
			}

			// Finally, add additional components
			CurrentIndex++;
			if (ExtraComponentElements != null)
			{
				for (int Index = 0; Index < ExtraComponentElements.Count(); ++Index)
				{
					Dictionary<string, string> ComponentElement;
					if (ConfigHierarchy.TryParse(ExtraComponentElements[Index], out ComponentElement))
					{
						PackageManifest.application.Items[CurrentIndex] = GetComponentElement(ComponentElement);
						CurrentIndex++;
					}
				}
			}

			// Wrap up serialization
			XmlSerializer PackageManifestSerializer = new XmlSerializer(PackageManifest.GetType());
			XmlSerializerNamespaces MLNamespace = new XmlSerializerNamespaces();
			MLNamespace.Add("ml", "magicleap");
			StringWriter Writer = new StringWriter();

			PackageManifestSerializer.Serialize(Writer, PackageManifest, MLNamespace);

			// allow plugins to modify final manifest HERE
			XDocument XDoc;
			try
			{
				XDoc = XDocument.Parse(Writer.ToString());
			}
			catch (Exception e)
			{
				throw new BuildException("LuminManifest.xml is invalid {0}\n{1}", e, Writer.ToString());
			}

			UPL.ProcessPluginNode(Architecture, "luminManifestUpdates", "", ref XDoc);
			return XDoc.ToString();
		}


		private List<string> CollectPluginDataPaths(TargetReceipt Receipt)
		{
			List<string> PluginExtras = new List<string>();
			if (Receipt == null)
			{
				Log.TraceInformation("Receipt is NULL");
				return PluginExtras;
			}

			// collect plugin extra data paths from target receipt
			var Results = Receipt.AdditionalProperties.Where(x => x.Name == "LuminPlugin");
			foreach (var Property in Results)
			{
				// Keep only unique paths
				string PluginPath = Property.Value;
				if (PluginExtras.FirstOrDefault(x => x == PluginPath) == null)
				{
					PluginExtras.Add(PluginPath);
					Log.TraceInformation("LuminPlugin: {0}", PluginPath);
				}
			}
			return PluginExtras;
		}

		public void InitUPL(TargetReceipt Receipt)
		{
			DirectoryReference ProjectDirectory = Receipt.ProjectDir ?? UnrealBuildTool.EngineDirectory;

			string UE4BuildPath = Path.Combine(ProjectDirectory.FullName, "Intermediate/Lumin/Mabu");
			string RelativeEnginePath = UnrealBuildTool.EngineDirectory.MakeRelativeTo(DirectoryReference.GetCurrentDirectory());
			string RelativeProjectPath = ProjectDirectory.MakeRelativeTo(DirectoryReference.GetCurrentDirectory());//.MakeRelativeTo(ProjectDirectory);

			string ConfigurationString = Receipt.Configuration.ToString();

			string Architecture = "arm64-v8a";
			List<string> MLSDKArches = new List<string>();
			MLSDKArches.Add(Architecture);

			SetLuminPluginData(MLSDKArches, CollectPluginDataPaths(Receipt));

			bool bIsEmbedded = Receipt.HasValueForAdditionalProperty("CompileAsDll", "true");

			//gather all of the xml
			UPL.Init(MLSDKArches, true, RelativeEnginePath, UE4BuildPath, RelativeProjectPath, ConfigurationString, bIsEmbedded);
		}

		public string StageFiles()
		{
			string Architecture = "arm64-v8a";

			//hard code for right now until we have multiple architectures
			return UPL.ProcessPluginNode(Architecture, "stageFiles", "");
		}

		private bool GetRemoveDebugInfo()
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool Value = false;
			Ini.GetBool("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "bRemoveDebugInfo", out Value);
			return Value;
		}

		private void MakeMabuPackage(string ProjectName, DirectoryReference ProjectDirectory, string ExePath, bool bForDistribution, string EngineDir, string MpkName)
		{
			string UE4BuildPath = Path.Combine(ProjectDirectory.FullName, "Intermediate/Lumin/Mabu");
			string MabuOutputPath = Path.Combine(UE4BuildPath, "Packaged");
			// note this must match LuminPlatform.Automation:Package
			string MabuFile = Path.Combine(UE4BuildPath, GetPackageName(ProjectName) + ".package");
			string ManifestFile = Path.Combine(UE4BuildPath, "manifest.xml");


			LuminToolChain ToolChain = new LuminToolChain(ProjectFile);
			string ExecSrcFile = Path.Combine(UE4BuildPath, "Binaries", Path.GetFileName(ExePath));

			if (bForDistribution || GetRemoveDebugInfo())
			{
				// If asked for, and if we are doing a distribution package, we strip debug symbols.
				Directory.CreateDirectory(Path.GetDirectoryName(ExecSrcFile));
				ToolChain.StripSymbols(new Tools.DotNETCommon.FileReference(ExePath), new Tools.DotNETCommon.FileReference(ExecSrcFile));
			}
			else
			{
				// The generated mabu needs the src exe file. So we copy the original as-is so mabu can find it.
				Directory.CreateDirectory(Path.GetDirectoryName(ExecSrcFile));
				File.Copy(ExePath, ExecSrcFile, true);
			}

			// We also create a SYM file to support debugging
			string SymFile = Path.ChangeExtension(ExecSrcFile, "sym");
			ToolChain.ExtractSymbols(new FileReference(ExePath), new FileReference(SymFile));
			ToolChain.LinkSymbols(new FileReference(SymFile), new FileReference(ExecSrcFile));
			
			// Generate manifest (after UPL is setup
			const string Architecture = "arm64-v8a";
			var Manifest = GenerateManifest(ProjectName, bForDistribution, Architecture);
			File.WriteAllText(ManifestFile, Manifest);

			string MabuPackagingMessage = "Building mabu package....";
			string Certificate = GetRuntimeSetting("Certificate");
			Certificate = CleanFilePath(Certificate);
			if (!string.IsNullOrEmpty(Certificate))
			{
				// For legacy sakes. We used to print this message when signing via the mbu commnd line so should continue to do so.
				// However, now this would only indicate if we are signing via the .package file and does not take into consideration
				// the MLCERT env var.
				MabuPackagingMessage = "Building signed mabu package....";
			}
			else if (bForDistribution)
			{
				// The user could be signing via the MLCERT env var so instead of throwing an exception, we simply log a warning.
				Log.TraceWarning("Packaging for distribution, however no certificate file has been chosen. Are you using the MLCERT environment variable instead?");
			}

			ToolChain.RunMabuWithException(Path.GetDirectoryName(MabuFile), String.Format("-t device --allow-unsigned -o \"{0}\" \"{1}\"", MabuOutputPath, Path.GetFileName(MabuFile)), MabuPackagingMessage);

			// copy the .mpk into binaries
			// @todo Lumin: Move this logic into a function in this class, and have AndroidAutomation call into it in GetFinalMpkName
			// @todo Lumin: Handle the whole Prebuilt thing, it may need to go somwehere else, or maybe this isn't even called?
			// @todo Lumin: This is losing the -Debug-Lumin stuff :|
			string SourceMpkPath = Path.Combine(MabuOutputPath, GetPackageName(ProjectName) + ".mpk");

			if (!Directory.Exists(Path.GetDirectoryName(MpkName)))
			{
				Directory.CreateDirectory(Path.GetDirectoryName(MpkName));
			}
			if (File.Exists(MpkName))
			{
				File.Delete(MpkName);
			}
			File.Copy(SourceMpkPath, MpkName);
		}

		public bool PrepForUATPackageOrDeploy(FileReference ProjectFile, string InProjectName, DirectoryReference InProjectDirectory, string InExecutablePath, string InEngineDir, bool bForDistribution, string CookFlavor, bool bIsDataDeploy, string MpkName)
		{
			if (!bIsDataDeploy)
			{
				MakeMabuPackage(InProjectName, InProjectDirectory, InExecutablePath, bForDistribution, InEngineDir, MpkName);
			}
			return true;
		}

		public override bool PrepTargetForDeployment(TargetReceipt Receipt)
		{
			// @todo Lumin: Need to create a MabuFile with no data files - including the executable!!
			return true;
		}


	}
}
