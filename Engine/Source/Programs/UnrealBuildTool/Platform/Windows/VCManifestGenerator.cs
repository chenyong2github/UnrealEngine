// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.IO;
using System.Text.RegularExpressions;
using System.Xml;
using Tools.DotNETCommon;
using System.Xml.Linq;
using System.Text;
using System.Diagnostics;

namespace UnrealBuildTool
{
    abstract class VCManifestGenerator
    {
        protected virtual string Schema2010NS { get { return "http://schemas.microsoft.com/appx/2010/manifest"; } }
        protected virtual string Schema2013NS { get { return "http://schemas.microsoft.com/appx/2013/manifest"; } }

		protected virtual string IniSection_PlatformTargetSettings { get { return string.Format( "/Script/{0}PlatformEditor.{0}TargetSettings", Platform.ToString() ); } }
		protected virtual string IniSection_GeneralProjectSettings { get { return "/Script/EngineSettings.GeneralProjectSettings"; } }

		protected const string BuildResourceSubPath = "Resources";
		protected const string EngineResourceSubPath = "DefaultImages";

		protected virtual UnrealTargetPlatform ConfigPlatform { get { return Platform; } }

        // Manifest compliance values
        protected const int MaxResourceEntries = 200;

        // INI configuration cache
        protected ConfigHierarchy EngineIni;
        protected ConfigHierarchy GameIni;

        protected string DefaultCulture;
        protected List<string> CulturesToStage;

        protected UEResXWriter DefaultResourceWriter;
        protected Dictionary<string, UEResXWriter> PerCultureResourceWriters;
        protected UnrealTargetPlatform Platform;
		protected FileReference ProjectFile;
        protected string ProjectPath;
        protected string OutputPath;
        protected string IntermediatePath;

		protected List<string> UpdatedFilePaths;

		/// <summary>
		/// Create a manifest generator for the given platform variant.
		/// </summary>
		public VCManifestGenerator( UnrealTargetPlatform InPlatform )
		{
			this.Platform = InPlatform;
		}

        protected static bool SafeGetBool(IDictionary<string, string> InDictionary, string Key, bool DefaultValue = false)
		{
			if (InDictionary.ContainsKey(Key))
			{
				var Value = InDictionary[Key].Trim().ToLower();
				return Value == "true" || Value == "1" || Value == "yes";
			}

			return DefaultValue;
		}

        protected static bool CreateCheckDirectory(string TargetDirectory)
		{
			if (!Directory.Exists(TargetDirectory))
			{
				try
				{
					Directory.CreateDirectory(TargetDirectory);
				}
				catch (Exception)
				{
					Log.TraceError("Could not create directory {0}.", TargetDirectory);
					return false;
				}
				if (!Directory.Exists(TargetDirectory))
				{
					Log.TraceError("Path {0} does not exist or is not a directory.", TargetDirectory);
					return false;
				}
			}
			return true;
		}

        protected static void RecursivelyForceDeleteDirectory(string InDirectoryToDelete)
		{
			if (Directory.Exists(InDirectoryToDelete))
			{
				try
				{
					List<string> SubDirectories = new List<string>(Directory.GetDirectories(InDirectoryToDelete, "*.*", SearchOption.AllDirectories));
					foreach (string DirectoryToRemove in SubDirectories)
					{
						RecursivelyForceDeleteDirectory(DirectoryToRemove);
					}
					List<string> FilesInDirectory = new List<string>(Directory.GetFiles(InDirectoryToDelete));
					foreach (string FileToRemove in FilesInDirectory)
					{
						try
						{
							FileAttributes Attributes = File.GetAttributes(FileToRemove);
							if ((Attributes & FileAttributes.ReadOnly) == FileAttributes.ReadOnly)
							{
								Attributes &= ~FileAttributes.ReadOnly;
								File.SetAttributes(FileToRemove, Attributes);
							}
							File.Delete(FileToRemove);
						}
						catch (Exception)
						{
							Log.TraceWarning("Could not remove file {0} to remove directory {1}.", FileToRemove, InDirectoryToDelete);
						}
					}
					Directory.Delete(InDirectoryToDelete, true);
				}
				catch (Exception)
				{
					Log.TraceWarning("Could not remove directory {0}.", InDirectoryToDelete);
				}
			}
		}


        /// <summary>
        /// Runs external program. Blocking.
        /// </summary>
        /// <param name="Executable">  Executable</param>
        /// <param name="CommandLine">  Commandline</param>
        /// <returns>bool    Application ran successfully</returns>
        protected bool RunExternalProgram(string Executable, string CommandLine)
        {
			if (File.Exists(Executable) == false)
			{
				throw new BuildException("BUILD FAILED: Couldn't find the executable to Run: {0}", Executable);
			}

			int ExitCode;
			string StdOutString = Utils.RunLocalProcessAndReturnStdOut(Executable, CommandLine, out ExitCode, (Log.OutputLevel >= LogEventType.Verbose) );

			if (ExitCode == 0)
			{
				return true;
			}
			else
			{
				Log.TraceError(Path.GetFileName(Executable) + " returned an error.\nApplication output:\n" + StdOutString);
				return false;
			}
        }


        protected string ValidatePackageVersion(string InVersionNumber)
		{
			string WorkingVersionNumber = Regex.Replace(InVersionNumber, "[^.0-9]", "");
			string CompletedVersionString = "";
			if (WorkingVersionNumber != null)
			{
				string[] SplitVersionString = WorkingVersionNumber.Split(new char[] { '.' });
				int NumVersionElements = Math.Min(4, SplitVersionString.Length);
				for (int VersionElement = 0; VersionElement < NumVersionElements; VersionElement++)
				{
					string QuadElement = SplitVersionString[VersionElement];
					int QuadValue = 0;
					if (QuadElement.Length == 0 || !int.TryParse(QuadElement, out QuadValue))
					{
						CompletedVersionString += "0";
					}
					else
					{
						if (QuadValue < 0)
						{
							QuadValue = 0;
						}
						if (QuadValue > 65535)
						{
							QuadValue = 65535;
						}
						CompletedVersionString += QuadValue;
					}
					if (VersionElement < 3)
					{
						CompletedVersionString += ".";
					}
				}
				for (int VersionElement = NumVersionElements; VersionElement < 4; VersionElement++)
				{
					CompletedVersionString += "0";
					if (VersionElement < 3)
					{
						CompletedVersionString += ".";
					}
				}
			}
			if (CompletedVersionString == null || CompletedVersionString.Length <= 0)
			{
				Log.TraceError("Invalid package version {0}. Package versions must be in the format #.#.#.# where # is a number 0-65535.", InVersionNumber);
				Log.TraceError("Consider setting [{0}]:PackageVersion to provide a specific value.", IniSection_PlatformTargetSettings);
			}
			return CompletedVersionString;
		}

        protected string ValidateProjectBaseName(string InApplicationId)
		{
			string ReturnVal = Regex.Replace(InApplicationId, "[^A-Za-z0-9]", "");
			if (ReturnVal != null)
			{
				// Remove any leading numbers (must start with a letter)
				ReturnVal = Regex.Replace(ReturnVal, "^[0-9]*", "");
			}
			if (ReturnVal == null || ReturnVal.Length <= 0)
			{
				Log.TraceError("Invalid application ID {0}. Application IDs must only contain letters and numbers. And they must begin with a letter.", InApplicationId);
				Log.TraceError("Consider using the setting [{0}]:PackageName to provide a specific value.", IniSection_PlatformTargetSettings);
			}
			return ReturnVal;
		}

        protected string ReadIniString(string Key, string Section, string DefaultValue = null)
		{
			if (Key == null)
				return DefaultValue;

			string Value;
			if (GameIni.GetString(Section, Key, out Value) && !string.IsNullOrWhiteSpace(Value))
				return Value;

			if (EngineIni.GetString(Section, Key, out Value) && !string.IsNullOrWhiteSpace(Value))
				return Value;

			return DefaultValue;
		}

        protected string GetConfigString(string PlatformKey, string GenericKey, string DefaultValue = null)
		{
			var GenericValue = ReadIniString(GenericKey, IniSection_GeneralProjectSettings, DefaultValue);
			return ReadIniString(PlatformKey, IniSection_PlatformTargetSettings, GenericValue);
		}

        protected bool GetConfigBool(string PlatformKey, string GenericKey, bool DefaultValue = false)
		{
			var GenericValue = ReadIniString(GenericKey, IniSection_GeneralProjectSettings, null);
			var ResultStr = ReadIniString(PlatformKey, IniSection_PlatformTargetSettings, GenericValue);

			if (ResultStr == null)
				return DefaultValue;

			ResultStr = ResultStr.Trim().ToLower();

			return ResultStr == "true" || ResultStr == "1" || ResultStr == "yes";
		}

        protected string GetConfigColor(string PlatformConfigKey, string DefaultValue)
		{
			var ConfigValue = GetConfigString(PlatformConfigKey, null, null);
			if (ConfigValue == null)
				return DefaultValue;

			Dictionary<string, string> Pairs;
			int R, G, B;
			if (ConfigHierarchy.TryParse(ConfigValue, out Pairs) &&
				int.TryParse(Pairs["R"], out R) &&
				int.TryParse(Pairs["G"], out G) &&
				int.TryParse(Pairs["B"], out B))
			{
				return "#" + R.ToString("X2") + G.ToString("X2") + B.ToString("X2");
			}

			Log.TraceWarning("Failed to parse color config value. Using default.");
			return DefaultValue;
		}

		protected bool FindResourceBinaryFile( out string SourcePath, string ResourceFileName, bool AllowEngineFallback = true)
		{
			// look in project normal Build location
			SourcePath = Path.Combine(ProjectPath, "Build", Platform.ToString(), BuildResourceSubPath);
			bool bFileExists = File.Exists(Path.Combine(SourcePath, ResourceFileName));

			// look in Platform Extensions next
			if (!bFileExists)
			{
				SourcePath = Path.Combine(ProjectPath, "Platforms", Platform.ToString(), "Build", BuildResourceSubPath);
				bFileExists = File.Exists(Path.Combine(SourcePath, ResourceFileName));
			}

			// look in Engine, if allowed
			if (!bFileExists && AllowEngineFallback)
			{
				SourcePath = Path.Combine(UnrealBuildTool.EngineDirectory.FullName, "Build", Platform.ToString(), EngineResourceSubPath);
				bFileExists = File.Exists(Path.Combine(SourcePath, ResourceFileName));

				// look in Platform extensions too
				if (!bFileExists)
				{
					SourcePath = Path.Combine(UnrealBuildTool.EngineDirectory.FullName, "Platforms", Platform.ToString(), "Build", EngineResourceSubPath);
					bFileExists = File.Exists(Path.Combine(SourcePath, ResourceFileName));
				}
			}

			return bFileExists;
		}

		protected bool DoesResourceBinaryFileExist(string ResourceFileName, bool AllowEngineFallback = true)
		{
			string SourcePath;
			return FindResourceBinaryFile( out SourcePath, ResourceFileName, AllowEngineFallback );
		}

        protected bool CopyAndReplaceBinaryIntermediate(string ResourceFileName, bool AllowEngineFallback = true)
		{
			string TargetPath = Path.Combine(IntermediatePath, BuildResourceSubPath);
			string SourcePath;
			bool bFileExists = FindResourceBinaryFile( out SourcePath, ResourceFileName, AllowEngineFallback );

			// At least the default culture entry for any resource binary must always exist
			if (!bFileExists)
			{
				return false;
			}

			// If the target resource folder doesn't exist yet, create it
			if (!CreateCheckDirectory(TargetPath))
			{
				return false;
			}

			// Find all copies of the resource file in the source directory (could be up to one for each culture and the default).
			IEnumerable<string> SourceResourceInstances = Directory.EnumerateFiles(SourcePath, ResourceFileName, SearchOption.AllDirectories);

			// Copy new resource files
			foreach (string SourceResourceFile in SourceResourceInstances)
			{
				//@todo only copy files for cultures we are staging
				string TargetResourcePath = Path.Combine(TargetPath, SourceResourceFile.Substring(SourcePath.Length + 1));
				if (!CreateCheckDirectory(Path.GetDirectoryName(TargetResourcePath)))
				{
					Log.TraceError("Unable to create intermediate directory {0}.", Path.GetDirectoryName(TargetResourcePath));
					continue;
				}
				if (!File.Exists(TargetResourcePath))
				{
					try
					{
						File.Copy(SourceResourceFile, TargetResourcePath);

						// File.Copy also copies the attributes, so make sure the new file isn't read only
						FileAttributes Attrs = File.GetAttributes(TargetResourcePath);
						if (Attrs.HasFlag(FileAttributes.ReadOnly))
						{
							File.SetAttributes(TargetResourcePath, Attrs & ~FileAttributes.ReadOnly);
						}
					}
					catch (Exception)
					{
						Log.TraceError("Unable to copy file {0} to {1}.", SourceResourceFile, TargetResourcePath);
						return false;
					}
				}
			}

			return true;
		}

        protected void CompareAndReplaceModifiedTarget(string IntermediatePath, string TargetPath)
		{
			if (!File.Exists(IntermediatePath))
			{
				Log.TraceError("Tried to copy non-existant intermediate file {0}.", IntermediatePath);
				return;
			}

			CreateCheckDirectory(Path.GetDirectoryName(TargetPath));

			// Check for differences in file contents
			if (File.Exists(TargetPath))
			{
				byte[] OriginalContents = File.ReadAllBytes(TargetPath);
				byte[] NewContents = File.ReadAllBytes(IntermediatePath);
				if (!OriginalContents.Equals(NewContents))
				{
					try
					{
						FileAttributes Attrs = File.GetAttributes(TargetPath);
						if ((Attrs & FileAttributes.ReadOnly) == FileAttributes.ReadOnly)
						{
							Attrs &= ~FileAttributes.ReadOnly;
							File.SetAttributes(TargetPath, Attrs);
						}
						File.Delete(TargetPath);
					}
					catch (Exception)
					{
						Log.TraceError("Could not replace file {0}.", TargetPath);
						return;
					}
				}
			}

			// If the file is present it is unmodified and should not be overwritten
			if (!File.Exists(TargetPath))
			{
				try
				{
					File.Copy(IntermediatePath, TargetPath);
				}
				catch (Exception)
				{
					Log.TraceError("Unable to copy file {0}.", TargetPath);
					return;
				}
				UpdatedFilePaths.Add(TargetPath);
			}
		}

        protected void CopyResourcesToTargetDir()
		{
			string TargetPath = Path.Combine(OutputPath, BuildResourceSubPath);
			string SourcePath = Path.Combine(IntermediatePath, BuildResourceSubPath);

			// If the target resource folder doesn't exist yet, create it
			if (!CreateCheckDirectory(TargetPath))
			{
				return;
			}

			// Find all copies of the resource file in both target and source directories (could be up to one for each culture and the default, but must have at least the default).
			var TargetResourceInstances = Directory.EnumerateFiles(TargetPath, "*.*", SearchOption.AllDirectories);
			var SourceResourceInstances = Directory.EnumerateFiles(SourcePath, "*.*", SearchOption.AllDirectories);

			// Remove any target files that aren't part of the source file list
			foreach (string TargetResourceFile in TargetResourceInstances)
			{
				// Ignore string tables (the only non-binary resources that will be present)
				if (!TargetResourceFile.Contains(".resw"))
				{
					//@todo always delete for cultures we aren't staging
					bool bRelativeSourceFileFound = false;
					foreach (string SourceResourceFile in SourceResourceInstances)
					{
						string SourceRelativeFile = SourceResourceFile.Substring(SourcePath.Length + 1);
						string TargetRelativeFile = TargetResourceFile.Substring(TargetPath.Length + 1);
						if (SourceRelativeFile.Equals(TargetRelativeFile))
						{
							bRelativeSourceFileFound = true;
							break;
						}
					}
					if (!bRelativeSourceFileFound)
					{
						try
						{
							File.Delete(TargetResourceFile);
						}
						catch (Exception E)
						{
							Log.TraceError("Could not remove stale resource file {0} - {1}.", TargetResourceFile, E.Message);
						}
					}
				}
			}

			// Copy new resource files only if they differ from the destination
			foreach (string SourceResourceFile in SourceResourceInstances)
			{
				//@todo only copy files for cultures we are staging
				string TargetResourcePath = Path.Combine(TargetPath, SourceResourceFile.Substring(SourcePath.Length + 1));
				CompareAndReplaceModifiedTarget(SourceResourceFile, TargetResourcePath);
			}
		}

        protected string AddResourceEntry(string ResourceEntryName, string ConfigKey, string GenericINISection, string GenericINIKey, string DefaultValue, string ValueSuffix = "")
		{
			string ConfigScratchValue = null;

			// Get the default culture value
			string DefaultCultureScratchValue;
			if (EngineIni.GetString(IniSection_PlatformTargetSettings, "CultureStringResources", out DefaultCultureScratchValue))
			{
				Dictionary<string, string> Values;
				if (!ConfigHierarchy.TryParse(DefaultCultureScratchValue, out Values))
				{
					Log.TraceError("Invalid default culture string resources: \"{0}\". Unable to add resource entry.", DefaultCultureScratchValue);
					return "";
				}

				ConfigScratchValue = Values[ConfigKey];
			}

			if (string.IsNullOrEmpty(ConfigScratchValue))
			{
				// No platform specific value is provided. Use the generic config or default value
				ConfigScratchValue = ReadIniString(GenericINIKey, GenericINISection, DefaultValue);
			}

			DefaultResourceWriter.AddResource(ResourceEntryName, ConfigScratchValue + ValueSuffix);

			// Find the default value
			List<string> PerCultureValues;
			if (EngineIni.GetArray(IniSection_PlatformTargetSettings, "PerCultureResources", out PerCultureValues))
			{
				foreach (string CultureCombinedValues in PerCultureValues)
				{
					Dictionary<string, string> SeparatedCultureValues;
					if (!ConfigHierarchy.TryParse(CultureCombinedValues, out SeparatedCultureValues)
						|| !SeparatedCultureValues.ContainsKey("CultureStringResources")
						|| !SeparatedCultureValues.ContainsKey("CultureId"))
					{
						Log.TraceError("Invalid per-culture resource: \"{0}\". Unable to add resource entry.", CultureCombinedValues);
						continue;
					}

					var CultureId = SeparatedCultureValues["CultureId"];
					if (CulturesToStage.Contains(CultureId))
					{
						Dictionary<string, string> CultureStringResources;
						if (!ConfigHierarchy.TryParse(SeparatedCultureValues["CultureStringResources"], out CultureStringResources))
						{
							Log.TraceError("Invalid culture string resources: \"{0}\". Unable to add resource entry.", CultureCombinedValues);
							continue;
						}

						var Value = CultureStringResources[ConfigKey];

						if (CulturesToStage.Contains(CultureId) && !string.IsNullOrEmpty(Value))
						{
							var Writer = PerCultureResourceWriters[CultureId];
							Writer.AddResource(ResourceEntryName, Value + ValueSuffix);
						}
					}
				}
			}

			return "ms-resource:" + ResourceEntryName;
		}

		protected string AddExternalResourceEntry(string ResourceEntryName, string DefaultValue, Dictionary<string,string> CultureIdToCultureValues)
		{
			DefaultResourceWriter.AddResource(ResourceEntryName, DefaultValue);

			foreach( KeyValuePair<string,string> CultureIdToCultureValue in CultureIdToCultureValues)
			{
				var Writer = PerCultureResourceWriters[CultureIdToCultureValue.Key];
				Writer.AddResource(ResourceEntryName, CultureIdToCultureValue.Value);
			}

			return "ms-resource:" + ResourceEntryName;
		}

		protected string AddDebugResourceString(string ResourceEntryName, string Value)
		{
			DefaultResourceWriter.AddResource(ResourceEntryName, Value);

			foreach (var CultureId in CulturesToStage)
			{
				var Writer = PerCultureResourceWriters[CultureId];
				Writer.AddResource(ResourceEntryName, Value);
			}

			return "ms-resource:" + ResourceEntryName;
		}

		protected virtual XName GetName( string BaseName, string SchemaName )
		{
			return XName.Get(BaseName);
		}

        protected XElement GetResources()
		{
			var ResourceCulturesList = CulturesToStage.ToList();
			// Move the default culture to the front of the list
			ResourceCulturesList.Remove(DefaultCulture);
			ResourceCulturesList.Insert(0, DefaultCulture);

			// Check that we have a valid number of cultures
			if (CulturesToStage.Count < 1 || CulturesToStage.Count >= MaxResourceEntries)
			{
				Log.TraceWarning("Incorrect number of cultures to stage. There must be between 1 and {0} cultures selected.", MaxResourceEntries);
			}

			// Create the culture list. This list is unordered except that the default language must be first which we already took care of above.
			var CultureElements = ResourceCulturesList.Select(c =>
				new XElement(GetName("Resource", Schema2010NS), new XAttribute("Language", c)));

			return new XElement(GetName("Resources", Schema2010NS), CultureElements);
		}

		protected string GetIdentityPackageName()
		{
            // Read the PackageName from config
            var PackageName = Regex.Replace(GetConfigString("PackageName", "ProjectName", "DefaultUE4Project"), "[^-.A-Za-z0-9]", "");
            if (string.IsNullOrWhiteSpace(PackageName))
            {
                Log.TraceError("Invalid package name {0}. Package names must only contain letters, numbers, dash, and period and must be at least one character long.", PackageName);
                Log.TraceError("Consider using the setting [{0}]:PackageName to provide a specific value.", IniSection_PlatformTargetSettings);
            }

			return PackageName;
		}

		protected string GetIdentityPublisherName()
		{
            var PublisherName = GetConfigString("PublisherName", "CompanyDistinguishedName", "CN=NoPublisher");
			return PublisherName;
		}

		protected string GetIdentityVersionNumber()
		{
            var VersionNumber = GetConfigString("PackageVersion", "ProjectVersion", "1.0.0.0");
            VersionNumber = ValidatePackageVersion(VersionNumber);

            // If specified in the project settings attempt to retrieve the current build number and increment the version number by that amount, accounting for overflows
            bool bIncludeEngineVersionInPackageVersion;
            if (EngineIni.GetBool(IniSection_PlatformTargetSettings, "bIncludeEngineVersionInPackageVersion", out bIncludeEngineVersionInPackageVersion) && bIncludeEngineVersionInPackageVersion)
            {
				VersionNumber = IncludeBuildVersionInPackageVersion(VersionNumber);
            }

			return VersionNumber;
		}

        protected XElement GetIdentity(out string IdentityName)
        {
            var PackageName = GetIdentityPackageName();
            var PublisherName = GetIdentityPublisherName();
            var VersionNumber = GetIdentityVersionNumber();

            IdentityName = PackageName;

            return new XElement(GetName("Identity", Schema2010NS),
                new XAttribute("Name", PackageName),
                new XAttribute("Publisher", PublisherName),
                new XAttribute("Version", VersionNumber));
        }

		protected virtual string IncludeBuildVersionInPackageVersion(string VersionNumber)
		{
			BuildVersion BuildVersionForPackage;
			if (BuildVersion.TryRead(BuildVersion.GetDefaultFileName(), out BuildVersionForPackage) && BuildVersionForPackage.Changelist != 0)
			{
				// Break apart the version number into individual elements
				string[] SplitVersionString = VersionNumber.Split('.');
				VersionNumber = string.Format("{0}.{1}.{2}.{3}",
					SplitVersionString[0],
					SplitVersionString[1],
					BuildVersionForPackage.Changelist / 10000,
					BuildVersionForPackage.Changelist % 10000);
			}

			return VersionNumber;
		}

		protected abstract string GetSDKDirectory();

		protected abstract string GetMakePriBinaryPath();

		protected abstract XElement GetManifest(List<UnrealTargetConfiguration> TargetConfigs, List<string> Executables, out string IdentityName);

		protected virtual void ProcessManifest(List<UnrealTargetConfiguration> TargetConfigs, List<string> Executables, string ManifestName, string ManifestTargetPath, string ManifestIntermediatePath)
        {
		}

        public List<string> CreateManifest(string InManifestName, string InOutputPath, string InIntermediatePath, FileReference InProjectFile, string InProjectDirectory, List<UnrealTargetConfiguration> InTargetConfigs, List<string> InExecutables)
		{
			// Verify we can find the SDK.
			string SDKDirectory = GetSDKDirectory();
			if (string.IsNullOrEmpty(SDKDirectory))
			{
				return null;
			}

			// Check parameter values are valid.
			if (InTargetConfigs.Count != InExecutables.Count)
			{
				Log.TraceError("The number of target configurations ({0}) and executables ({1}) passed to manifest generation do not match.", InTargetConfigs.Count, InExecutables.Count);
				return null;
			}
			if (InTargetConfigs.Count < 1)
			{
				Log.TraceError("The number of target configurations is zero, so we cannot generate a manifest.");
				return null;
			}

			if (!CreateCheckDirectory(InOutputPath))
			{
				Log.TraceError("Failed to create output directory \"{0}\".", InOutputPath);
				return null;
			}
			if (!CreateCheckDirectory(InIntermediatePath))
			{
				Log.TraceError("Failed to create intermediate directory \"{0}\".", InIntermediatePath);
				return null;
			}

			OutputPath = InOutputPath;
			IntermediatePath = InIntermediatePath;
			ProjectFile = InProjectFile;
			ProjectPath = InProjectDirectory;
			UpdatedFilePaths = new List<string>();

			// Load up INI settings. We'll use engine settings to retrieve the manifest configuration, but these may reference
			// values in either game or engine settings, so we'll keep both.
			GameIni = ConfigCache.ReadHierarchy(ConfigHierarchyType.Game, DirectoryReference.FromFile(InProjectFile), ConfigPlatform);
			EngineIni = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, DirectoryReference.FromFile(InProjectFile), ConfigPlatform);

			// Load and verify/clean culture list
			{
				List<string> CulturesToStageWithDuplicates;
				GameIni.GetArray("/Script/UnrealEd.ProjectPackagingSettings", "CulturesToStage", out CulturesToStageWithDuplicates);
				GameIni.GetString("/Script/UnrealEd.ProjectPackagingSettings", "DefaultCulture", out DefaultCulture);
				if (CulturesToStageWithDuplicates == null || CulturesToStageWithDuplicates.Count < 1)
				{
					Log.TraceError("At least one culture must be selected to stage.");
					return null;
				}

				CulturesToStage = CulturesToStageWithDuplicates.Distinct().ToList();
			}
			if (DefaultCulture == null || DefaultCulture.Length < 1)
			{
				DefaultCulture = CulturesToStage[0];
				Log.TraceWarning("A default culture must be selected to stage. Using {0}.", DefaultCulture);
			}
			if (!CulturesToStage.Contains(DefaultCulture))
			{
				DefaultCulture = CulturesToStage[0];
				Log.TraceWarning("The default culture must be one of the staged cultures. Using {0}.", DefaultCulture);
			}

			List<string> PerCultureValues;
			if (EngineIni.GetArray(IniSection_PlatformTargetSettings, "PerCultureResources", out PerCultureValues))
			{
				foreach (string CultureCombinedValues in PerCultureValues)
				{
					Dictionary<string, string> SeparatedCultureValues;
					if (!ConfigHierarchy.TryParse(CultureCombinedValues, out SeparatedCultureValues))
					{
						Log.TraceWarning("Invalid per-culture resource value: {0}", CultureCombinedValues);
						continue;
					}

					string StageId = SeparatedCultureValues["StageId"];
					int CultureIndex = CulturesToStage.FindIndex(x => x == StageId);
					if (CultureIndex >= 0)
					{
						CulturesToStage[CultureIndex] = SeparatedCultureValues["CultureId"];
						if (DefaultCulture == StageId)
						{
							DefaultCulture = SeparatedCultureValues["CultureId"];
						}
					}
				}
			}
			// Only warn if shipping, we can run without translated cultures they're just needed for cert
			else if (InTargetConfigs.Contains(UnrealTargetConfiguration.Shipping))
			{
				Log.TraceInformation("Staged culture mappings not setup in the editor. See Per Culture Resources in the {0} Target Settings.", Platform.ToString() );
			}

			// Clean out the resources intermediate path so that we know there are no stale binary files.
			string IntermediateResourceDirectory = Path.Combine(IntermediatePath, BuildResourceSubPath);
			RecursivelyForceDeleteDirectory(IntermediateResourceDirectory);
			if (!CreateCheckDirectory(IntermediateResourceDirectory))
			{
				Log.TraceError("Could not create directory {0}.", IntermediateResourceDirectory);
				return null;
			}

			// Construct a single resource writer for the default (no-culture) values
			string DefaultResourceIntermediatePath = Path.Combine(IntermediateResourceDirectory, "resources.resw");
			DefaultResourceWriter = new UEResXWriter(DefaultResourceIntermediatePath);

			// Construct the ResXWriters for each culture
			PerCultureResourceWriters = new Dictionary<string, UEResXWriter>();
			foreach (string Culture in CulturesToStage)
			{
				string IntermediateStringResourcePath = Path.Combine(IntermediateResourceDirectory, Culture);
				string IntermediateStringResourceFile = Path.Combine(IntermediateStringResourcePath, "resources.resw");
				if (!CreateCheckDirectory(IntermediateStringResourcePath))
				{
					Log.TraceWarning("Culture {0} resources not staged.", Culture);
					CulturesToStage.Remove(Culture);
					if (Culture == DefaultCulture)
					{
						DefaultCulture = CulturesToStage[0];
						Log.TraceWarning("Default culture skipped. Using {0} as default culture.", DefaultCulture);
					}
					continue;
				}
				PerCultureResourceWriters.Add(Culture, new UEResXWriter(IntermediateStringResourceFile));
			}



			// Create the manifest document
			string IdentityName = null;
			var ManifestXmlDocument = new XDocument(GetManifest(InTargetConfigs, InExecutables, out IdentityName));

			// Export manifest to the intermediate directory then compare the contents to any existing target manifest
			// and replace if there are differences.
			string ManifestIntermediatePath = Path.Combine(IntermediatePath, InManifestName);
			string ManifestTargetPath = Path.Combine(OutputPath, InManifestName);
			ManifestXmlDocument.Save(ManifestIntermediatePath);
			CompareAndReplaceModifiedTarget(ManifestIntermediatePath, ManifestTargetPath);
			ProcessManifest(InTargetConfigs, InExecutables, InManifestName, ManifestTargetPath, ManifestIntermediatePath);

			// Clean out any resource directories that we aren't staging
			string TargetResourcePath = Path.Combine(OutputPath, BuildResourceSubPath);
			if (Directory.Exists(TargetResourcePath))
			{
				List<string> TargetResourceDirectories = new List<string>(Directory.GetDirectories(TargetResourcePath, "*.*", SearchOption.AllDirectories));
				foreach (string ResourceDirectory in TargetResourceDirectories)
				{
					if (!CulturesToStage.Contains(Path.GetFileName(ResourceDirectory)))
					{
						RecursivelyForceDeleteDirectory(ResourceDirectory);
					}
				}
			}

			// Export the resource tables starting with the default culture
			string DefaultResourceTargetPath = Path.Combine(OutputPath, BuildResourceSubPath, "resources.resw");
			DefaultResourceWriter.Close();
			CompareAndReplaceModifiedTarget(DefaultResourceIntermediatePath, DefaultResourceTargetPath);

			foreach (var Writer in PerCultureResourceWriters)
			{
				Writer.Value.Close();

				string IntermediateStringResourceFile = Path.Combine(IntermediateResourceDirectory, Writer.Key, "resources.resw");
				string TargetStringResourceFile = Path.Combine(OutputPath, BuildResourceSubPath, Writer.Key, "resources.resw");

				CompareAndReplaceModifiedTarget(IntermediateStringResourceFile, TargetStringResourceFile);
			}

			// Copy all the binary resources into the target directory.
			CopyResourcesToTargetDir();

			// The resource database is dependent on everything else calculated here (manifest, resource string tables, binary resources).
			// So if any file has been updated we'll need to run the config.
			if (UpdatedFilePaths.Count > 0)
			{
				// Create resource index configuration
				string PriExecutable = GetMakePriBinaryPath();
				string ResourceConfigFile = Path.Combine(IntermediatePath, "priconfig.xml");
				bool bEnableAutoResourcePacks = false;
				EngineIni.GetBool(IniSection_PlatformTargetSettings, "bEnableAutoResourcePacks", out bEnableAutoResourcePacks);

				// If the game is not going to support language resource packs then merge the culture qualifiers.
				if (bEnableAutoResourcePacks || CulturesToStage.Count <= 1)
				{
					RunExternalProgram(PriExecutable, "createconfig /cf \"" + ResourceConfigFile + "\" /dq " + DefaultCulture + " /o");
				}
				else
				{
					RunExternalProgram(PriExecutable, "createconfig /cf \"" + ResourceConfigFile + "\" /dq " + String.Join("_", CulturesToStage) + " /o");
				}

				// Modify configuration to restrict indexing to the Resources directory (saves time and space)
				XmlDocument PriConfig = new XmlDocument();
				PriConfig.Load(ResourceConfigFile);

				// If the game is not going to support resource packs then remove the autoResourcePackages.
				if (!bEnableAutoResourcePacks)
				{
					XmlNode PackagingNode = PriConfig.SelectSingleNode("/resources/packaging");
					PackagingNode.ParentNode.RemoveChild(PackagingNode);
				}

				// The previous implementation using startIndexAt="Resources" did not produce the expected ResourceMapSubtree hierarchy, so this manually specifies all resources in a .resfiles instead.
				string ResourcesResFile = Path.Combine(IntermediatePath, "resources.resfiles");

				XmlNode PriIndexNode = PriConfig.SelectSingleNode("/resources/index");
				XmlAttribute PriStartIndex = PriIndexNode.Attributes["startIndexAt"];
				PriStartIndex.Value = ResourcesResFile;

				// Swap the default folder indexer-config to a RESFILES indexer-config.
				XmlElement FolderIndexerConfigNode = PriConfig.SelectSingleNode("/resources/index/indexer-config[@type='folder']") as XmlElement;
				FolderIndexerConfigNode.SetAttribute("type", "RESFILES");
				FolderIndexerConfigNode.RemoveAttribute("foldernameAsQualifier");
				FolderIndexerConfigNode.RemoveAttribute("filenameAsQualifier");

				PriConfig.Save(ResourceConfigFile);

				IEnumerable<string> Resources = Directory.EnumerateFiles(Path.Combine(OutputPath, BuildResourceSubPath), "*.*", SearchOption.AllDirectories);
				System.Text.StringBuilder ResourcesList = new System.Text.StringBuilder();
				foreach (string Resource in Resources)
				{
					ResourcesList.AppendLine(Resource.Replace(OutputPath, "").TrimStart('\\'));
				}
				File.WriteAllText(ResourcesResFile, ResourcesList.ToString());

				// Remove previous pri files so we can enumerate which ones are new since the resource generator could produce a file for each staged language.
				IEnumerable<string> OldPriFiles = Directory.EnumerateFiles(IntermediatePath, "*.pri");
				foreach (string OldPri in OldPriFiles)
				{
					try
					{
						File.Delete(OldPri);
					}
					catch (Exception)
					{
						Log.TraceError("Could not delete file {0}.", OldPri);
					}
				}

				// Generate the resource index
				string ResourceLogFile = Path.Combine(IntermediatePath, "ResIndexLog.xml");
				string ResourceIndexFile = Path.Combine(IntermediatePath, "resources.pri");

				string MakePriCommandLine = "new /pr \"" + OutputPath + "\" /cf \"" + ResourceConfigFile + "\" /mn \"" + ManifestTargetPath + "\" /il \"" + ResourceLogFile + "\" /of \"" + ResourceIndexFile + "\" /o";

				if (IdentityName != null)
				{
					MakePriCommandLine += " /indexName \"" + IdentityName + "\"";
				}
				RunExternalProgram(PriExecutable, MakePriCommandLine);

				// Remove any existing pri target files that were not generated by this latest update
				IEnumerable<string> NewPriFiles = Directory.EnumerateFiles(IntermediatePath, "*.pri");
				IEnumerable<string> TargetPriFiles = Directory.EnumerateFiles(OutputPath, "*.pri");
				foreach (string TargetPri in TargetPriFiles)
				{
					if (!NewPriFiles.Contains(TargetPri))
					{
						try
						{
							File.Delete(TargetPri);
						}
						catch (Exception)
						{
							Log.TraceError("Could not remove stale file {0}.", TargetPri);
						}
					}
				}

				// Stage all the modified pri files to the output directory
				foreach (string NewPri in NewPriFiles)
				{
					string NewResourceIndexFile = Path.Combine(IntermediatePath, Path.GetFileName(NewPri));
					string FinalResourceIndexFile = Path.Combine(OutputPath, Path.GetFileName(NewPri));
					CompareAndReplaceModifiedTarget(NewResourceIndexFile, FinalResourceIndexFile);
				}
			}

			return UpdatedFilePaths;
		}
	}
}
