// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Xml;
using System.Diagnostics;
using System.IO;
using Microsoft.Win32;
using System.Xml.Linq;
using Tools.DotNETCommon;
using System.Security.Cryptography;

namespace UnrealBuildTool
{
	class UEDeployAndroid : UEBuildDeploy, IAndroidDeploy
	{
		private const string XML_HEADER = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";

		// filename of current BundleTool
		private const string BUNDLETOOL_JAR = "bundletool-all-0.13.0.jar";

		// classpath of default android build tools gradle plugin
		private const string ANDROID_TOOLS_BUILD_GRADLE_VERSION = "com.android.tools.build:gradle:4.0.0";

		// name of the only vulkan validation layer we're interested in 
		private const string ANDROID_VULKAN_VALIDATION_LAYER = "libVkLayer_khronos_validation.so";

		// Minimum Android SDK that must be used for Java compiling
		readonly int MinimumSDKLevel = 28;

		// Minimum SDK version needed for App Bundles
		readonly int MinimumSDKLevelForBundle = 21;

		// Minimum SDK version needed for Gradle based on active plugins
		private int MinimumSDKLevelForGradle = 19;

		// Reserved Java keywords not allowed in package names without modification
		static private string[] JavaReservedKeywords = new string[] {
			"abstract", "assert", "boolean", "break", "byte", "case", "catch", "char", "class", "const", "continue", "default", "do",
			"double", "else", "enum", "extends", "final", "finally", "float", "for", "goto", "if", "implements", "import", "instanceof",
			"int", "interface", "long", "native", "new", "package", "private", "protected", "public", "return", "short", "static",
			"strictfp", "super", "switch", "sychronized", "this", "throw", "throws", "transient", "try", "void", "volatile", "while",
			"false", "null", "true"
		};

		/// <summary>
		/// Internal usage for GetApiLevel
		/// </summary>
		private List<string> PossibleApiLevels = null;

		protected FileReference ProjectFile;

		/// <summary>
		/// Determines whether we package data inside the APK. Based on and  OR of "-ForcePackageData" being
		/// false and bPackageDataInsideApk in /Script/AndroidRuntimeSettings.AndroidRuntimeSettings being true
		/// </summary>
		protected bool bPackageDataInsideApk = false;

		/// <summary>
		/// Ignore AppBundle (AAB) generation setting if "-ForceAPKGeneration" specified
		/// </summary>
		[CommandLine("-ForceAPKGeneration", Value = "true")]
		public bool ForceAPKGeneration = false;

		public UEDeployAndroid(FileReference InProjectFile, bool InForcePackageData)
		{
			ProjectFile = InProjectFile;

			// read the ini value and OR with the command line value
			bool IniValue = ReadPackageDataInsideApkFromIni(null);
			bPackageDataInsideApk = InForcePackageData || IniValue == true;

			CommandLine.ParseArguments(Environment.GetCommandLineArgs(), this);
		}

		private UnrealPluginLanguage UPL = null;
		private string ActiveUPLFiles = "";
		private string UPLHashCode = null;
		private bool ARCorePluginEnabled = false;
		private bool FacebookPluginEnabled = false;
		private bool OculusMobilePluginEnabled = false;
		private bool GoogleVRPluginEnabled = false;
		private bool EOSSDKPluginEnabled = false;

		public void SetAndroidPluginData(List<string> Architectures, List<string> inPluginExtraData)
		{
			List<string> NDKArches = new List<string>();
			foreach (string NDKArch in Architectures)
			{
				if (!NDKArches.Contains(NDKArch))
				{
					NDKArches.Add(GetNDKArch(NDKArch));
				}
			}

			// check if certain plugins are enabled
			ARCorePluginEnabled = false;
			FacebookPluginEnabled = false;
			OculusMobilePluginEnabled = false;
			GoogleVRPluginEnabled = false;
			EOSSDKPluginEnabled = false;
			ActiveUPLFiles = "";
			foreach (string Plugin in inPluginExtraData)
			{
				ActiveUPLFiles += Plugin + "\n";

				// check if the Facebook plugin was enabled
				if (Plugin.Contains("OnlineSubsystemFacebook_UPL"))
				{
					FacebookPluginEnabled = true;
					continue;
				}

				// check if the ARCore plugin was enabled
				if (Plugin.Contains("GoogleARCoreBase_APL"))
				{
					ARCorePluginEnabled = true;
					continue;
				}

				// check if the Oculus Mobile plugin was enabled
				if (Plugin.Contains("OculusMobile_APL"))
				{
					OculusMobilePluginEnabled = true;
					continue;
				}

				// check if the GoogleVR plugin was enabled
				if (Plugin.Contains("GoogleVRHMD"))
				{
					GoogleVRPluginEnabled = true;
					continue;
				}

				// check if the EOSShared plugin was enabled
				if (Plugin.Contains("EOSSDK"))
				{
					EOSSDKPluginEnabled = true;
					continue;
				}
			}

			UPL = new UnrealPluginLanguage(ProjectFile, inPluginExtraData, NDKArches, "http://schemas.android.com/apk/res/android", "xmlns:android=\"http://schemas.android.com/apk/res/android\"", UnrealTargetPlatform.Android);
			UPLHashCode = UPL.GetUPLHash();
//			APL.SetTrace();
		}

		private void SetMinimumSDKLevelForGradle()
		{
			if (FacebookPluginEnabled)
			{
				MinimumSDKLevelForGradle = Math.Max(MinimumSDKLevelForGradle, 15);
			}
			if (ARCorePluginEnabled)
			{
				MinimumSDKLevelForGradle = Math.Max(MinimumSDKLevelForGradle, 19);
			}
			if(EOSSDKPluginEnabled)
			{
				MinimumSDKLevelForGradle = Math.Max(MinimumSDKLevelForGradle, 23);
			}
		}

		/// <summary>
		/// Simple function to pipe output asynchronously
		/// </summary>
		private void ParseApiLevel(object Sender, DataReceivedEventArgs Event)
		{
			// DataReceivedEventHandler is fired with a null string when the output stream is closed.  We don't want to
			// print anything for that event.
			if (!String.IsNullOrEmpty(Event.Data))
			{
				string Line = Event.Data;
				if (Line.StartsWith("id:"))
				{
					// the line should look like: id: 1 or "android-19"
					string[] Tokens = Line.Split("\"".ToCharArray());
					if (Tokens.Length >= 2)
					{
						PossibleApiLevels.Add(Tokens[1]);
					}
				}
			}
		}

		private ConfigHierarchy GetConfigCacheIni(ConfigHierarchyType Type)
		{
			return ConfigCache.ReadHierarchy(Type, DirectoryReference.FromFile(ProjectFile), UnrealTargetPlatform.Android);
		}

		private string GetLatestSDKApiLevel(AndroidToolChain ToolChain, string PlatformsDir)
		{
			// get a list of SDK platforms
			if (!Directory.Exists(PlatformsDir))
			{
				throw new BuildException("No platforms found in {0}", PlatformsDir);
			}

			// return the latest of them
			string[] PlatformDirectories = Directory.GetDirectories(PlatformsDir);
			if (PlatformDirectories != null && PlatformDirectories.Length > 0)
			{
				return ToolChain.GetLargestApiLevel(PlatformDirectories);
			}

			throw new BuildException("Can't make an APK without an API installed ({0} does not contain any SDKs)", PlatformsDir);
		}

		private bool ValidateSDK(string PlatformsDir, string ApiString)
		{
			if (!Directory.Exists(PlatformsDir))
			{
				return false;
			}

			string SDKPlatformDir = Path.Combine(PlatformsDir, ApiString);
			return Directory.Exists(SDKPlatformDir);
		}

		private int GetApiLevelInt(string ApiString)
		{
			int VersionInt = 0;
			if (ApiString.Contains("-"))
			{
				int Version;
				if (int.TryParse(ApiString.Substring(ApiString.LastIndexOf('-') + 1), out Version))
				{
					VersionInt = Version;
				}
			}
			return VersionInt;
		}

		private string CachedSDKLevel = null;
		private string GetSdkApiLevel(AndroidToolChain ToolChain)
		{
			if (CachedSDKLevel == null)
			{
				// ask the .ini system for what version to use
				ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
				string SDKLevel;
				Ini.GetString("/Script/AndroidPlatformEditor.AndroidSDKSettings", "SDKAPILevel", out SDKLevel);

				// check for project override of SDK API level
				string ProjectSDKLevel;
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "SDKAPILevelOverride", out ProjectSDKLevel);
				ProjectSDKLevel = ProjectSDKLevel.Trim();
				if (ProjectSDKLevel != "")
				{
					SDKLevel = ProjectSDKLevel;
				}

				// if we want to use whatever version the ndk uses, then use that
				if (SDKLevel == "matchndk")
				{
					SDKLevel = ToolChain.GetNdkApiLevel();
				}

				string PlatformsDir = Environment.ExpandEnvironmentVariables("%ANDROID_HOME%/platforms");

				// run a command and capture output
				if (SDKLevel == "latest")
				{
					SDKLevel = GetLatestSDKApiLevel(ToolChain, PlatformsDir);
				}

				// make sure it is at least android-23
				int SDKLevelInt = GetApiLevelInt(SDKLevel);
				if (SDKLevelInt < MinimumSDKLevel)
				{
					Log.TraceInformation("Requires at least SDK API level {0}, currently set to '{1}'", MinimumSDKLevel, SDKLevel);
					SDKLevel = GetLatestSDKApiLevel(ToolChain, PlatformsDir);

					SDKLevelInt = GetApiLevelInt(SDKLevel);
					if (SDKLevelInt < MinimumSDKLevel)
					{
						SDKLevelInt = MinimumSDKLevel;
						SDKLevel = "android-" + MinimumSDKLevel.ToString();
						Log.TraceInformation("Gradle will attempt to download SDK API level {0}", SDKLevelInt);
					}
				}

				// validate the platform SDK is installed
				if (!ValidateSDK(PlatformsDir, SDKLevel))
				{
					Log.TraceWarning("The SDK API requested '{0}' not installed in {1}; Gradle will attempt to download it.", SDKLevel, PlatformsDir);
				}

				Log.TraceInformation("Building Java with SDK API level '{0}'", SDKLevel);
				CachedSDKLevel = SDKLevel;
			}

			return CachedSDKLevel;
		}

		private string CachedBuildToolsVersion = null;
		private string LastAndroidHomePath = null;

		private uint GetRevisionValue(string VersionString)
		{
			// read up to 4 sections (ie. 20.0.3.5), first section most significant
			// each section assumed to be 0 to 255 range
			uint Value = 0;
			try
			{
				string[] Sections = VersionString.Split(".".ToCharArray());
				Value |= (Sections.Length > 0) ? (uint.Parse(Sections[0]) << 24) : 0;
				Value |= (Sections.Length > 1) ? (uint.Parse(Sections[1]) << 16) : 0;
				Value |= (Sections.Length > 2) ? (uint.Parse(Sections[2]) << 8) : 0;
				Value |= (Sections.Length > 3) ? uint.Parse(Sections[3]) : 0;
			}
			catch (Exception)
			{
				// ignore poorly formed version
			}
			return Value;
		}

		private string GetBuildToolsVersion()
		{
			// return cached path if ANDROID_HOME has not changed
			string HomePath = Environment.ExpandEnvironmentVariables("%ANDROID_HOME%");
			if (CachedBuildToolsVersion != null && LastAndroidHomePath == HomePath)
			{
				return CachedBuildToolsVersion;
			}

			// get a list of the directories in build-tools.. may be more than one set installed (or none which is bad)
			string[] Subdirs = Directory.GetDirectories(Path.Combine(HomePath, "build-tools"));
			if (Subdirs.Length == 0)
			{
				throw new BuildException("Failed to find %ANDROID_HOME%/build-tools subdirectory. Run SDK manager and install build-tools.");
			}

			// valid directories will have a source.properties with the Pkg.Revision (there is no guarantee we can use the directory name as revision)
			string BestVersionString = null;
			uint BestVersion = 0;
			foreach (string CandidateDir in Subdirs)
			{
				string AaptFilename = Path.Combine(CandidateDir, Utils.IsRunningOnMono ? "aapt" : "aapt.exe");
				string RevisionString = "";
				uint RevisionValue = 0;

				if (File.Exists(AaptFilename))
				{
					string SourcePropFilename = Path.Combine(CandidateDir, "source.properties");
					if (File.Exists(SourcePropFilename))
					{
						string[] PropertyContents = File.ReadAllLines(SourcePropFilename);
						foreach (string PropertyLine in PropertyContents)
						{
							if (PropertyLine.StartsWith("Pkg.Revision="))
							{
								RevisionString = PropertyLine.Substring(13);
								RevisionValue = GetRevisionValue(RevisionString);
								break;
							}
						}
					}
				}

				// remember it if newer version or haven't found one yet
				if (RevisionValue > BestVersion || BestVersionString == null)
				{
					BestVersion = RevisionValue;
					BestVersionString = RevisionString;
				}
			}

			if (BestVersionString == null)
			{
				throw new BuildException("Failed to find %ANDROID_HOME%/build-tools subdirectory with aapt. Run SDK manager and install build-tools.");
			}

			// with Gradle enabled use at least 24.0.2 (will be installed by Gradle if missing)
			if (BestVersion < ((24 << 24) | (0 << 16) | (2 << 8)))
			{
				BestVersionString = "24.0.2";
			}

			CachedBuildToolsVersion = BestVersionString;
			LastAndroidHomePath = HomePath;

			Log.TraceInformation("Building with Build Tools version '{0}'", CachedBuildToolsVersion);

			return CachedBuildToolsVersion;
		}

		public static string GetOBBVersionNumber(int PackageVersion)
		{
			string VersionString = PackageVersion.ToString("0");
			return VersionString;
		}

		public bool GetPackageDataInsideApk()
		{
			return bPackageDataInsideApk;
		}

		/// <summary>
		/// Reads the bPackageDataInsideApk from AndroidRuntimeSettings
		/// </summary>
		/// <param name="Ini"></param>
		protected bool ReadPackageDataInsideApkFromIni(ConfigHierarchy Ini)
		{		
			// make a new one if one wasn't passed in
			if (Ini == null)
			{
				Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			}

			// we check this a lot, so make it easy 
			bool bIniPackageDataInsideApk;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bPackageDataInsideApk", out bIniPackageDataInsideApk);

			return bIniPackageDataInsideApk;
		}

		public bool UseExternalFilesDir(bool bDisallowExternalFilesDir, ConfigHierarchy Ini = null)
		{
			if (bDisallowExternalFilesDir)
			{
				return false;
			}

			// make a new one if one wasn't passed in
			if (Ini == null)
			{
				Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			}

			// we check this a lot, so make it easy 
			bool bUseExternalFilesDir;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bUseExternalFilesDir", out bUseExternalFilesDir);

			return bUseExternalFilesDir;
		}

		public bool IsPackagingForDaydream(ConfigHierarchy Ini = null)
		{
			// always false if the GoogleVR plugin wasn't enabled
			if (!GoogleVRPluginEnabled)
			{
				return false;
			}

			// make a new one if one wasn't passed in
			if (Ini == null)
			{
				Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			}

			List<string> GoogleVRCaps = new List<string>();
			if(Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "GoogleVRCaps", out GoogleVRCaps))
			{
				return GoogleVRCaps.Contains("Daydream33") || GoogleVRCaps.Contains("Daydream63") || GoogleVRCaps.Contains("Daydream66");
			}
			else
			{
				// the default values for the VRCaps are Cardboard and Daydream33, so unless the
				// developer changes the mode, there will be no setting string to look up here
				return true;
			}
		}

		public List<string> GetTargetOculusMobileDevices(ConfigHierarchy Ini = null)
		{
			// always false if the Oculus Mobile plugin wasn't enabled
			if (!OculusMobilePluginEnabled)
			{
				return new List<string>();
			}

			// make a new one if one wasn't passed in
			if (Ini == null)
			{
				Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			}

			List<string> OculusMobileDevices;
			bool result = Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "PackageForOculusMobile", out OculusMobileDevices);
			if (!result || OculusMobileDevices == null)
			{
				OculusMobileDevices = new List<string>();
			}

			return OculusMobileDevices;
		}

		public bool IsPackagingForOculusMobile(ConfigHierarchy Ini = null)
		{
			List<string> TargetOculusDevices = GetTargetOculusMobileDevices(Ini);
			bool bTargetOculusDevices = (TargetOculusDevices != null && TargetOculusDevices.Count() > 0);

			return bTargetOculusDevices;
		}

		public bool DisableVerifyOBBOnStartUp(ConfigHierarchy Ini = null)
		{
			// make a new one if one wasn't passed in
			if (Ini == null)
			{
				Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			}

			// we check this a lot, so make it easy 
			bool bDisableVerifyOBBOnStartUp;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bDisableVerifyOBBOnStartUp", out bDisableVerifyOBBOnStartUp);

			return bDisableVerifyOBBOnStartUp;
		}

		private static bool SafeDeleteFile(string Filename, bool bCheckExists = true)
		{
			if (!bCheckExists || File.Exists(Filename))
			{
				try
				{
					File.SetAttributes(Filename, FileAttributes.Normal);
					File.Delete(Filename);
					return true;
				}
				catch (System.UnauthorizedAccessException)
				{
					throw new BuildException("File '{0}' is in use; unable to modify it.", Filename);
				}
				catch (System.Exception)
				{
					return false;
				}
			}
			return true;
		}

		private static void CopyFileDirectory(string SourceDir, string DestDir, Dictionary<string, string> Replacements = null, string[] Excludes = null)
		{
			if (!Directory.Exists(SourceDir))
			{
				return;
			}

			string[] Files = Directory.GetFiles(SourceDir, "*.*", SearchOption.AllDirectories);
			foreach (string Filename in Files)
			{
				if (Excludes != null)
				{
					// skip files in excluded directories
					string DirectoryName = Path.GetFileName(Path.GetDirectoryName(Filename));
					bool bExclude = false;
					foreach (string Exclude in Excludes)
					{
						if (DirectoryName == Exclude)
						{
							bExclude = true;
							break;
						}
					}
					if (bExclude)
					{
						continue;
					}
				}

				// skip template files
				if (Path.GetExtension(Filename) == ".template")
				{
					continue;
				}

				// make the dst filename with the same structure as it was in SourceDir
				string DestFilename = Path.Combine(DestDir, Utils.MakePathRelativeTo(Filename, SourceDir)).Replace('\\', Path.DirectorySeparatorChar).Replace('/', Path.DirectorySeparatorChar); 

				// make the subdirectory if needed
				string DestSubdir = Path.GetDirectoryName(DestFilename);
				if (!Directory.Exists(DestSubdir))
				{
					Directory.CreateDirectory(DestSubdir);
				}

				// some files are handled specially
				string Ext = Path.GetExtension(Filename);
				if (Ext == ".xml" && Replacements != null)
				{
					string Contents = File.ReadAllText(Filename);

					// replace some variables
					foreach (KeyValuePair<string, string> Pair in Replacements)
					{
						Contents = Contents.Replace(Pair.Key, Pair.Value);
					}

					bool bWriteFile = true;
					if (File.Exists(DestFilename))
					{
						string OriginalContents = File.ReadAllText(DestFilename);
						if (Contents == OriginalContents)
						{
							bWriteFile = false;
						}
					}

					// write out file if different
					if (bWriteFile)
					{
						SafeDeleteFile(DestFilename);
						File.WriteAllText(DestFilename, Contents);
					}
				}
				else
				{
					SafeDeleteFile(DestFilename);
					File.Copy(Filename, DestFilename);

					// preserve timestamp and clear read-only flags
					FileInfo DestFileInfo = new FileInfo(DestFilename);
					DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
					File.SetLastWriteTimeUtc(DestFilename, File.GetLastWriteTimeUtc(Filename));
				}
			}
		}

		private static void DeleteDirectory(string InPath, string SubDirectoryToKeep = "")
		{
			// skip the dir we want to
			if (String.Compare(Path.GetFileName(InPath), SubDirectoryToKeep, true) == 0)
			{
				return;
			}

			// delete all files in here
			string[] Files;
			try
			{
				Files = Directory.GetFiles(InPath);
			}
			catch (Exception)
			{
				// directory doesn't exist so all is good
				return;
			}
			foreach (string Filename in Files)
			{
				try
				{
					// remove any read only flags
					FileInfo FileInfo = new FileInfo(Filename);
					FileInfo.Attributes = FileInfo.Attributes & ~FileAttributes.ReadOnly;
					FileInfo.Delete();
				}
				catch (Exception)
				{
					Log.TraceInformation("Failed to delete all files in directory {0}. Continuing on...", InPath);
				}
			}

			string[] Dirs = Directory.GetDirectories(InPath, "*.*", SearchOption.TopDirectoryOnly);
			foreach (string Dir in Dirs)
			{
				DeleteDirectory(Dir, SubDirectoryToKeep);
				// try to delete the directory, but allow it to fail (due to SubDirectoryToKeep still existing)
				try
				{
					Directory.Delete(Dir);
				}
				catch (Exception)
				{
					// do nothing
				}
			}
		}

		private bool BinaryFileEquals(string SourceFilename, string DestFilename)
		{
			if (!File.Exists(SourceFilename))
			{
				return false;
			}
			if (!File.Exists(DestFilename))
			{
				return false;
			}

			FileInfo SourceInfo = new FileInfo(SourceFilename);
			FileInfo DestInfo = new FileInfo(DestFilename);
			if (SourceInfo.Length != DestInfo.Length)
			{
				return false;
			}

			using (FileStream SourceStream = new FileStream(SourceFilename, FileMode.Open, FileAccess.Read, FileShare.Read))
			using (BinaryReader SourceReader = new BinaryReader(SourceStream))
			using (FileStream DestStream = new FileStream(DestFilename, FileMode.Open, FileAccess.Read, FileShare.Read))
			using (BinaryReader DestReader = new BinaryReader(DestStream))
			{
				while (true)
				{
					byte[] SourceData = SourceReader.ReadBytes(4096);
					byte[] DestData = DestReader.ReadBytes(4096);
					if (SourceData.Length != DestData.Length)
					{
						return false;
					}
					if (SourceData.Length == 0)
					{
						return true;
					}
					if (!SourceData.SequenceEqual(DestData))
					{
						return false;
					}
				}
			}
		}

		private bool CopyIfDifferent(string SourceFilename, string DestFilename, bool bLog, bool bContentCompare)
		{
			if (!File.Exists(SourceFilename))
			{
				return false;
			}

			bool bDestFileAlreadyExists = File.Exists(DestFilename);
			bool bNeedCopy = !bDestFileAlreadyExists;

			if (!bNeedCopy)
			{
				if (bContentCompare)
				{
					bNeedCopy = !BinaryFileEquals(SourceFilename, DestFilename);
				}
				else
				{
					FileInfo SourceInfo = new FileInfo(SourceFilename);
					FileInfo DestInfo = new FileInfo(DestFilename);

					if (SourceInfo.Length != DestInfo.Length)
					{
						bNeedCopy = true;
					}
					else if (File.GetLastWriteTimeUtc(DestFilename) < File.GetLastWriteTimeUtc(SourceFilename))
					{
						// destination file older than source
						bNeedCopy = true;
					}
				}
			}

			if (bNeedCopy)
			{
				if (bLog)
				{
					Log.TraceInformation("Copying {0} to {1}", SourceFilename, DestFilename);
				}

				if (bDestFileAlreadyExists)
				{
					SafeDeleteFile(DestFilename, false);
				}
				File.Copy(SourceFilename, DestFilename);
				File.SetLastWriteTimeUtc(DestFilename, File.GetLastWriteTimeUtc(SourceFilename));

				// did copy
				return true;
			}

			// did not copy
			return false;
		}

		private void CleanCopyDirectory(string SourceDir, string DestDir, string[] Excludes = null)
		{
			if (!Directory.Exists(SourceDir))
			{
				return;
			}
			if (!Directory.Exists(DestDir))
			{
				CopyFileDirectory(SourceDir, DestDir, null, Excludes);
				return;
			}

			// copy files that are different and make a list of ones to keep
			string[] StartingSourceFiles = Directory.GetFiles(SourceDir, "*.*", SearchOption.AllDirectories);
			List<string> FilesToKeep = new List<string>();
			foreach (string Filename in StartingSourceFiles)
			{
				if (Excludes != null)
				{
					// skip files in excluded directories
					string DirectoryName = Path.GetFileName(Path.GetDirectoryName(Filename));
					bool bExclude = false;
					foreach (string Exclude in Excludes)
					{
						if (DirectoryName == Exclude)
						{
							bExclude = true;
							break;
						}
					}
					if (bExclude)
					{
						continue;
					}
				}

				// make the dest filename with the same structure as it was in SourceDir
				string DestFilename = Path.Combine(DestDir, Utils.MakePathRelativeTo(Filename, SourceDir));

				// remember this file to keep
				FilesToKeep.Add(DestFilename);

				// only copy files that are new or different
				if (FilesAreDifferent(Filename, DestFilename))
				{
					if (File.Exists(DestFilename))
					{
						// xml files may have been rewritten but contents still the same so check contents also
						string Ext = Path.GetExtension(Filename);
						if (Ext == ".xml")
						{
							if (File.ReadAllText(Filename) == File.ReadAllText(DestFilename))
							{
								continue;
							}
						}

						// delete it so can copy over it
						SafeDeleteFile(DestFilename);
					}

					// make the subdirectory if needed
					string DestSubdir = Path.GetDirectoryName(DestFilename);
					if (!Directory.Exists(DestSubdir))
					{
						Directory.CreateDirectory(DestSubdir);
					}

					// copy it
					File.Copy(Filename, DestFilename);

					// preserve timestamp and clear read-only flags
					FileInfo DestFileInfo = new FileInfo(DestFilename);
					DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
					File.SetLastWriteTimeUtc(DestFilename, File.GetLastWriteTimeUtc(Filename));

					Log.TraceInformation("Copied file {0}.", DestFilename);
				}
			}

			// delete any files not in the keep list
			string[] StartingDestFiles = Directory.GetFiles(DestDir, "*.*", SearchOption.AllDirectories);
			foreach (string Filename in StartingDestFiles)
			{
				if (!FilesToKeep.Contains(Filename))
				{
					Log.TraceInformation("Deleting unneeded file {0}.", Filename);
					SafeDeleteFile(Filename);
				}
			}

			// delete any empty directories
			try
			{
				IEnumerable<string> BaseDirectories = Directory.EnumerateDirectories(DestDir, "*", SearchOption.AllDirectories).OrderByDescending(x => x);
				foreach (string directory in BaseDirectories)
				{
					if (Directory.Exists(directory) && Directory.GetFiles(directory, "*.*", SearchOption.AllDirectories).Count() == 0)
					{
						Log.TraceInformation("Cleaning Directory {0} as empty.", directory);
						Directory.Delete(directory, true);
					}
				}
			}
			catch (Exception)
			{
				// likely System.IO.DirectoryNotFoundException, ignore it
			}
		}

		public string GetUE4BuildFilePath(String EngineDirectory)
		{
			return Path.GetFullPath(Path.Combine(EngineDirectory, "Build/Android/Java"));
		}

		public string GetUE4JavaSrcPath()
		{
			return Path.Combine("src", "com", "epicgames", "ue4");
		}

		public string GetUE4JavaFilePath(String EngineDirectory)
		{
			return Path.GetFullPath(Path.Combine(GetUE4BuildFilePath(EngineDirectory), GetUE4JavaSrcPath()));
		}

		public string GetUE4JavaBuildSettingsFileName(String EngineDirectory)
		{
			return Path.Combine(GetUE4JavaFilePath(EngineDirectory), "JavaBuildSettings.java");
		}

		public string GetUE4JavaDownloadShimFileName(string Directory)
		{
			return Path.Combine(Directory, "DownloadShim.java");
		}

		public string GetUE4TemplateJavaSourceDir(string Directory)
		{
			return Path.Combine(GetUE4BuildFilePath(Directory), "JavaTemplates");
		}

		public string GetUE4TemplateJavaDestination(string Directory, string FileName)
		{
			return Path.Combine(Directory, FileName);
		}

		public string GetUE4JavaOBBDataFileName(string Directory)
		{
			return Path.Combine(Directory, "OBBData.java");
		}

		public class TemplateFile
		{
			public string SourceFile;
			public string DestinationFile;
		}

		private void MakeDirectoryIfRequired(string DestFilename)
		{
			string DestSubdir = Path.GetDirectoryName(DestFilename);
			if (!Directory.Exists(DestSubdir))
			{
				Directory.CreateDirectory(DestSubdir);
			}
		}

		private int CachedStoreVersion = -1;
		private int CachedStoreVersionOffsetArmV7 = 0;
		private int CachedStoreVersionOffsetArm64 = 0;
		private int CachedStoreVersionOffsetX8664= 0;

		public int GetStoreVersion(string UE4Arch)
		{
			if (CachedStoreVersion < 1)
			{
				ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
				int StoreVersion = 1;
				Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "StoreVersion", out StoreVersion);

				bool bUseChangeListAsStoreVersion = false;
				Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bUseChangeListAsStoreVersion", out bUseChangeListAsStoreVersion);

				bool IsBuildMachine = Environment.GetEnvironmentVariable("IsBuildMachine") == "1";
				// override store version with changelist if enabled and is build machine
				if (bUseChangeListAsStoreVersion && IsBuildMachine)
				{
					// make sure changelist is cached (clear unused warning)
					string EngineVersion = ReadEngineVersion();
					if (EngineVersion == null)
					{
						throw new BuildException("No engine version!");
					}

					int Changelist = 0;
					if (int.TryParse(EngineChangelist, out Changelist))
					{
						if (Changelist != 0)
						{
							StoreVersion = Changelist;
						}
					}
				}

				Log.TraceInformation("GotStoreVersion found v{0}. (bUseChangeListAsStoreVersion={1} IsBuildMachine={2} EngineChangeList={3})", StoreVersion, bUseChangeListAsStoreVersion, IsBuildMachine, EngineChangelist);

				CachedStoreVersion = StoreVersion;

				Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "StoreVersionOffsetArmV7", out CachedStoreVersionOffsetArmV7);
				Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "StoreVersionOffsetArm64", out CachedStoreVersionOffsetArm64);
				Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "StoreVersionOffsetX8664", out CachedStoreVersionOffsetX8664);
			}

			switch (UE4Arch)
			{
				case "-armv7": return CachedStoreVersion + CachedStoreVersionOffsetArmV7;
				case "-arm64": return CachedStoreVersion + CachedStoreVersionOffsetArm64;
				case "-x64": return CachedStoreVersion + CachedStoreVersionOffsetX8664;
			}

			return CachedStoreVersion;
		}

		private string CachedVersionDisplayName;

		public string GetVersionDisplayName(bool bIsEmbedded)
		{
			if (string.IsNullOrEmpty(CachedVersionDisplayName))
			{
				ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
				string VersionDisplayName = "";
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "VersionDisplayName", out VersionDisplayName);

				if (Environment.GetEnvironmentVariable("IsBuildMachine") == "1")
				{
					bool bAppendChangeListToVersionDisplayName = false;
					Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bAppendChangeListToVersionDisplayName", out bAppendChangeListToVersionDisplayName);
					if (bAppendChangeListToVersionDisplayName)
					{
						VersionDisplayName = string.Format("{0}-{1}", VersionDisplayName, EngineChangelist);
					}

					bool bAppendPlatformToVersionDisplayName = false;
					Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bAppendPlatformToVersionDisplayName", out bAppendPlatformToVersionDisplayName);
					if (bAppendPlatformToVersionDisplayName)
					{
						VersionDisplayName = string.Format("{0}-Android", VersionDisplayName);
					}

					// append optional text to version name if embedded build
					if (bIsEmbedded)
					{
						string EmbeddedAppendDisplayName = "";
						if (Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "EmbeddedAppendDisplayName", out EmbeddedAppendDisplayName))
						{
							VersionDisplayName = VersionDisplayName + EmbeddedAppendDisplayName;
						}
					}
				}

				CachedVersionDisplayName = VersionDisplayName;
			}

			return CachedVersionDisplayName;
		}

		public void WriteJavaOBBDataFile(string FileName, string PackageName, List<string> ObbSources, string CookFlavor, bool bPackageDataInsideApk, string UE4Arch)
		{
			Log.TraceInformation("\n==== Writing to OBB data file {0} ====", FileName);

			// always must write if file does not exist
			bool bFileExists = File.Exists(FileName);
			bool bMustWriteFile = !bFileExists;

			string AppType = "";
			if (CookFlavor.EndsWith("Client"))
			{
//				AppType = ".Client";		// should always be empty now; fix up the name in batch file instead
			}

			int StoreVersion = GetStoreVersion(UE4Arch);

			StringBuilder obbData = new StringBuilder("package " + PackageName + ";\n\n");
			obbData.Append("public class OBBData\n{\n");
			obbData.Append("public static final String AppType = \"" + AppType + "\";\n\n");
			obbData.Append("public static class XAPKFile {\npublic final boolean mIsMain;\npublic final String mFileVersion;\n");
			obbData.Append("public final long mFileSize;\nXAPKFile(boolean isMain, String fileVersion, long fileSize) {\nmIsMain = isMain;\nmFileVersion = fileVersion;\nmFileSize = fileSize;\n");
			obbData.Append("}\n}\n\n");

			// write the data here
			obbData.Append("public static final XAPKFile[] xAPKS = {\n");
			// For each obb file... but we only have one... for now anyway.
			bool first = ObbSources.Count > 1;
			bool AnyOBBExists = false;
			foreach (string ObbSource in ObbSources)
			{
				bool bOBBExists = File.Exists(ObbSource);
				AnyOBBExists |= bOBBExists;

				obbData.Append("new XAPKFile(\n" + (ObbSource.Contains(".patch.") ? "false, // false signifies a patch file\n" : "true, // true signifies a main file\n"));
				obbData.AppendFormat("\"{0}\", // the version of the APK that the file was uploaded against\n", GetOBBVersionNumber(StoreVersion));
				obbData.AppendFormat("{0}L // the length of the file in bytes\n", bOBBExists ? new FileInfo(ObbSource).Length : 0);
				obbData.AppendFormat("){0}\n", first ? "," : "");
				first = false;
			}
			obbData.Append("};\n"); // close off data
			obbData.Append("};\n"); // close class definition off

			// see if we need to replace the file if it exists
			if (!bMustWriteFile && bFileExists)
			{
				string[] obbDataFile = File.ReadAllLines(FileName);

				// Must always write if AppType not defined
				bool bHasAppType = false;
				foreach (string FileLine in obbDataFile)
				{
					if (FileLine.Contains("AppType ="))
					{
						bHasAppType = true;
						break;
					}
				}
				if (!bHasAppType)
				{
					bMustWriteFile = true;
				}

				// OBB must exist, contents must be different, and not packaging in APK to require replacing
				if (!bMustWriteFile && AnyOBBExists && !bPackageDataInsideApk && !obbDataFile.SequenceEqual((obbData.ToString()).Split('\n')))
				{
					bMustWriteFile = true;
				}
			}

			if (bMustWriteFile)
			{
				MakeDirectoryIfRequired(FileName);
				using (StreamWriter outputFile = new StreamWriter(FileName, false))
				{
					string[] obbSrc = obbData.ToString().Split('\n');
					foreach (string line in obbSrc)
					{
						outputFile.WriteLine(line);
					}
				}
			}
			else
			{
				Log.TraceInformation("\n==== OBB data file up to date so not writing. ====");
			}
		}

		public void WriteJavaDownloadSupportFiles(string ShimFileName, IEnumerable<TemplateFile> TemplateFiles, Dictionary<string, string> replacements)
		{
			// Deal with the Shim first as that is a known target and is easy to deal with
			// If it exists then read it
			string[] DestFileContent = File.Exists(ShimFileName) ? File.ReadAllLines(ShimFileName) : null;

			StringBuilder ShimFileContent = new StringBuilder("package com.epicgames.ue4;\n\n");

			ShimFileContent.AppendFormat("import {0}.OBBDownloaderService;\n", replacements["$$PackageName$$"]);
			ShimFileContent.AppendFormat("import {0}.DownloaderActivity;\n", replacements["$$PackageName$$"]);

			// Do OBB file checking without using DownloadActivity to avoid transit to another activity
				ShimFileContent.Append("import android.app.Activity;\n");
				ShimFileContent.Append("import com.google.android.vending.expansion.downloader.Helpers;\n");
				ShimFileContent.AppendFormat("import {0}.OBBData;\n", replacements["$$PackageName$$"]);

			ShimFileContent.Append("\n\npublic class DownloadShim\n{\n");
			ShimFileContent.Append("\tpublic static OBBDownloaderService DownloaderService;\n");
			ShimFileContent.Append("\tpublic static DownloaderActivity DownloadActivity;\n");
			ShimFileContent.Append("\tpublic static Class<DownloaderActivity> GetDownloaderType() { return DownloaderActivity.class; }\n");

			// Do OBB file checking without using DownloadActivity to avoid transit to another activity
			ShimFileContent.Append("\tpublic static boolean expansionFilesDelivered(Activity activity, int version) {\n");
			ShimFileContent.Append("\t\tfor (OBBData.XAPKFile xf : OBBData.xAPKS) {\n");
			ShimFileContent.Append("\t\t\tString fileName = Helpers.getExpansionAPKFileName(activity, xf.mIsMain, Integer.toString(version), OBBData.AppType);\n");
			ShimFileContent.Append("\t\t\tGameActivity.Log.debug(\"Checking for file : \" + fileName);\n");
			ShimFileContent.Append("\t\t\tString fileForNewFile = Helpers.generateSaveFileName(activity, fileName);\n");
			ShimFileContent.Append("\t\t\tString fileForDevFile = Helpers.generateSaveFileNameDevelopment(activity, fileName);\n");
			ShimFileContent.Append("\t\t\tGameActivity.Log.debug(\"which is really being resolved to : \" + fileForNewFile + \"\\n Or : \" + fileForDevFile);\n");
			ShimFileContent.Append("\t\t\tif (Helpers.doesFileExist(activity, fileName, xf.mFileSize, false)) {\n");
			ShimFileContent.Append("\t\t\t\tGameActivity.Log.debug(\"Found OBB here: \" + fileForNewFile);\n");
			ShimFileContent.Append("\t\t\t}\n");
			ShimFileContent.Append("\t\t\telse if (Helpers.doesFileExistDev(activity, fileName, xf.mFileSize, false)) {\n");
			ShimFileContent.Append("\t\t\t\tGameActivity.Log.debug(\"Found OBB here: \" + fileForDevFile);\n");
				ShimFileContent.Append("\t\t\t}\n");
			ShimFileContent.Append("\t\t\telse return false;\n");
			ShimFileContent.Append("\t\t}\n");
				ShimFileContent.Append("\t\treturn true;\n");
				ShimFileContent.Append("\t}\n");

			ShimFileContent.Append("}\n");
			Log.TraceInformation("\n==== Writing to shim file {0} ====", ShimFileName);

			// If they aren't the same then dump out the settings
			if (DestFileContent == null || !DestFileContent.SequenceEqual((ShimFileContent.ToString()).Split('\n')))
			{
				MakeDirectoryIfRequired(ShimFileName);
				using (StreamWriter outputFile = new StreamWriter(ShimFileName, false))
				{
					string[] shimSrc = ShimFileContent.ToString().Split('\n');
					foreach (string line in shimSrc)
					{
						outputFile.WriteLine(line);
					}
				}
			}
			else
			{
				Log.TraceInformation("\n==== Shim data file up to date so not writing. ====");
			}

			// Now we move on to the template files
			foreach (TemplateFile template in TemplateFiles)
			{
				string[] templateSrc = File.ReadAllLines(template.SourceFile);
				string[] templateDest = File.Exists(template.DestinationFile) ? File.ReadAllLines(template.DestinationFile) : null;

				for (int i = 0; i < templateSrc.Length; ++i)
				{
					string srcLine = templateSrc[i];
					bool changed = false;
					foreach (KeyValuePair<string, string> kvp in replacements)
					{
						if (srcLine.Contains(kvp.Key))
						{
							srcLine = srcLine.Replace(kvp.Key, kvp.Value);
							changed = true;
						}
					}
					if (changed)
					{
						templateSrc[i] = srcLine;
					}
				}

				Log.TraceInformation("\n==== Writing to template target file {0} ====", template.DestinationFile);

				if (templateDest == null || templateSrc.Length != templateDest.Length || !templateSrc.SequenceEqual(templateDest))
				{
					MakeDirectoryIfRequired(template.DestinationFile);
					using (StreamWriter outputFile = new StreamWriter(template.DestinationFile, false))
					{
						foreach (string line in templateSrc)
						{
							outputFile.WriteLine(line);
						}
					}
				}
				else
				{
					Log.TraceInformation("\n==== Template target file up to date so not writing. ====");
				}
			}
		}

		public void WriteCrashlyticsResources(string UEBuildPath, string PackageName, string ApplicationDisplayName, bool bIsEmbedded, string UE4Arch)
		{
			System.DateTime CurrentDateTime = System.DateTime.Now;
			string BuildID = Guid.NewGuid().ToString();

			string VersionDisplayName = GetVersionDisplayName(bIsEmbedded);

			StringBuilder CrashPropertiesContent = new StringBuilder("");
			CrashPropertiesContent.Append("# This file is automatically generated by Crashlytics to uniquely\n");
			CrashPropertiesContent.Append("# identify individual builds of your Android application.\n");
			CrashPropertiesContent.Append("#\n");
			CrashPropertiesContent.Append("# Do NOT modify, delete, or commit to source control!\n");
			CrashPropertiesContent.Append("#\n");
			CrashPropertiesContent.Append("# " + CurrentDateTime.ToString("D") + "\n");
			CrashPropertiesContent.Append("version_name=" + VersionDisplayName + "\n");
			CrashPropertiesContent.Append("package_name=" + PackageName + "\n");
			CrashPropertiesContent.Append("build_id=" + BuildID + "\n");
			CrashPropertiesContent.Append("version_code=" + GetStoreVersion(UE4Arch).ToString() + "\n");

			string CrashPropertiesFileName = Path.Combine(UEBuildPath, "assets", "crashlytics-build.properties");
			MakeDirectoryIfRequired(CrashPropertiesFileName);
			File.WriteAllText(CrashPropertiesFileName, CrashPropertiesContent.ToString());
			Log.TraceInformation("==== Write {0}  ====", CrashPropertiesFileName);

			StringBuilder BuildIDContent = new StringBuilder("");
			BuildIDContent.Append("<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"no\"?>\n");
			BuildIDContent.Append("<resources xmlns:tools=\"http://schemas.android.com/tools\">\n");
			BuildIDContent.Append("<!--\n");
			BuildIDContent.Append("  This file is automatically generated by Crashlytics to uniquely\n");
			BuildIDContent.Append("  identify individual builds of your Android application.\n");
			BuildIDContent.Append("\n");
			BuildIDContent.Append("  Do NOT modify, delete, or commit to source control!\n");
			BuildIDContent.Append("-->\n");
			BuildIDContent.Append("<string tools:ignore=\"UnusedResources, TypographyDashes\" name=\"com.crashlytics.android.build_id\" translatable=\"false\">" + BuildID + "</string>\n");
			BuildIDContent.Append("</resources>\n");

			string BuildIDFileName = Path.Combine(UEBuildPath, "res", "values", "com_crashlytics_build_id.xml");
			MakeDirectoryIfRequired(BuildIDFileName);
			File.WriteAllText(BuildIDFileName, BuildIDContent.ToString());
			Log.TraceInformation("==== Write {0}  ====", BuildIDFileName);
		}

		private static string GetNDKArch(string UE4Arch)
		{
			switch (UE4Arch)
			{
				case "-armv7":	return "armeabi-v7a";
				case "-arm64":  return "arm64-v8a";
				case "-x64":	return "x86_64";
				case "-x86":	return "x86";

				default: throw new BuildException("Unknown UE4 architecture {0}", UE4Arch);
			}
		}

		public static string GetUE4Arch(string NDKArch)
		{
			switch (NDKArch)
			{
				case "armeabi-v7a": return "-armv7";
				case "arm64-v8a":   return "-arm64";
				case "x86":         return "-x86";
				case "arm64":       return "-arm64";
				case "x86_64":
				case "x64":			return "-x64";
					
	//				default: throw new BuildException("Unknown NDK architecture '{0}'", NDKArch);
				// future-proof by returning armv7 for unknown
				default:            return "-armv7";
			}
		}

		private static void StripDebugSymbols(string SourceFileName, string TargetFileName, string UE4Arch, bool bStripAll = false)
		{
			// Copy the file and remove read-only if necessary
			File.Copy(SourceFileName, TargetFileName, true);
			FileAttributes Attribs = File.GetAttributes(TargetFileName);
			if (Attribs.HasFlag(FileAttributes.ReadOnly))
			{
				File.SetAttributes(TargetFileName, Attribs & ~FileAttributes.ReadOnly);
			}

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.FileName = AndroidToolChain.GetStripExecutablePath(UE4Arch).Trim('"');
			if (bStripAll)
			{
				StartInfo.Arguments = "--strip-unneeded \"" + TargetFileName + "\"";
			}
			else
			{
				StartInfo.Arguments = "--strip-debug \"" + TargetFileName + "\"";
			}
			StartInfo.UseShellExecute = false;
			StartInfo.CreateNoWindow = true;
			Utils.RunLocalProcessAndLogOutput(StartInfo);
		}

		private static void CopySTL(AndroidToolChain ToolChain, string UE4BuildPath, string UE4Arch, string NDKArch, bool bForDistribution)
		{
			// copy it in!
			string SourceSTLSOName = Environment.ExpandEnvironmentVariables("%NDKROOT%/sources/cxx-stl/llvm-libc++/libs/") +  NDKArch + "/libc++_shared.so";
			string FinalSTLSOName = UE4BuildPath + "/jni/" + NDKArch + "/libc++_shared.so";

			// check to see if libc++_shared.so is newer than last time we copied
			bool bFileExists = File.Exists(FinalSTLSOName);
			TimeSpan Diff = File.GetLastWriteTimeUtc(FinalSTLSOName) - File.GetLastWriteTimeUtc(SourceSTLSOName);
			if (!bFileExists || Diff.TotalSeconds < -1 || Diff.TotalSeconds > 1)
			{
				SafeDeleteFile(FinalSTLSOName);
				Directory.CreateDirectory(Path.GetDirectoryName(FinalSTLSOName));
				File.Copy(SourceSTLSOName, FinalSTLSOName, true);

				// make sure it's writable if the source was readonly (e.g. autosdks)
				new FileInfo(FinalSTLSOName).IsReadOnly = false;
				File.SetLastWriteTimeUtc(FinalSTLSOName, File.GetLastWriteTimeUtc(SourceSTLSOName));
			}
		}

		private void CopyGfxDebugger(string UE4BuildPath, string UE4Arch, string NDKArch)
		{
			string AndroidGraphicsDebugger;
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "AndroidGraphicsDebugger", out AndroidGraphicsDebugger);

			switch (AndroidGraphicsDebugger.ToLower())
			{
				case "mali":
					{
						string MaliGraphicsDebuggerPath;
						AndroidPlatformSDK.GetPath(Ini, "/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "MaliGraphicsDebuggerPath", out MaliGraphicsDebuggerPath);
						if (Directory.Exists(MaliGraphicsDebuggerPath))
						{
							Directory.CreateDirectory(Path.Combine(UE4BuildPath, "libs", NDKArch));
							string MaliLibSrcPath = Path.Combine(MaliGraphicsDebuggerPath, "target", "android-non-root", "arm", NDKArch, "libMGD.so");
							if (!File.Exists(MaliLibSrcPath))
							{
								// in v4.3.0 library location was changed
								MaliLibSrcPath = Path.Combine(MaliGraphicsDebuggerPath, "target", "android", "arm", "unrooted", NDKArch, "libMGD.so");
							}
							string MaliLibDstPath = Path.Combine(UE4BuildPath, "libs", NDKArch, "libMGD.so");

							Log.TraceInformation("Copying {0} to {1}", MaliLibSrcPath, MaliLibDstPath);
							File.Copy(MaliLibSrcPath, MaliLibDstPath, true);
							File.SetLastWriteTimeUtc(MaliLibDstPath, File.GetLastWriteTimeUtc(MaliLibSrcPath));

							string MaliVkLayerLibSrcPath = Path.Combine(MaliGraphicsDebuggerPath, "target", "android", "arm", "rooted", NDKArch, "libGLES_aga.so");
							if (File.Exists(MaliVkLayerLibSrcPath))
							{
								string MaliVkLayerLibDstPath = Path.Combine(UE4BuildPath, "libs", NDKArch, "libVkLayerAGA.so");
								Log.TraceInformation("Copying {0} to {1}", MaliVkLayerLibSrcPath, MaliVkLayerLibDstPath);
								File.Copy(MaliVkLayerLibSrcPath, MaliVkLayerLibDstPath, true);
								File.SetLastWriteTimeUtc(MaliVkLayerLibDstPath, File.GetLastWriteTimeUtc(MaliVkLayerLibSrcPath));
							}
						}
					}
					break;

				// @TODO: Add NVIDIA Gfx Debugger
				/*
				case "nvidia":
					{
						Directory.CreateDirectory(UE4BuildPath + "/libs/" + NDKArch);
						File.Copy("F:/NVPACK/android-kk-egl-t124-a32/Stripped_libNvPmApi.Core.so", UE4BuildPath + "/libs/" + NDKArch + "/libNvPmApi.Core.so", true);
						File.Copy("F:/NVPACK/android-kk-egl-t124-a32/Stripped_libNvidia_gfx_debugger.so", UE4BuildPath + "/libs/" + NDKArch + "/libNvidia_gfx_debugger.so", true);
					}
					break;
				*/
				default:
					break;
			}
		}

		void LogBuildSetup()
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool bBuildForES31 = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bBuildForES31", out bBuildForES31);
			bool bSupportsVulkan = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSupportsVulkan", out bSupportsVulkan);

			Log.TraceInformation("bBuildForES31: {0}", (bBuildForES31 ? "true" : "false"));
			Log.TraceInformation("bSupportsVulkan: {0}", (bSupportsVulkan ? "true" : "false"));
		}
		
		void CopyVulkanValidationLayers(string UE4BuildPath, string UE4Arch, string NDKArch, string Configuration)
		{
			bool bSupportsVulkan = false;
			bool bSupportsVulkanSM5 = false;
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSupportsVulkan", out bSupportsVulkan);
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSupportsVulkanSM5", out bSupportsVulkanSM5);

			bool bCopyVulkanLayers = (bSupportsVulkan || bSupportsVulkanSM5) && (Configuration == "Debug" || Configuration == "Development");
			if (bCopyVulkanLayers)
			{
				string VulkanLayersDir = Environment.ExpandEnvironmentVariables("%NDKROOT%/sources/third_party/vulkan/src/build-android/jniLibs/") + NDKArch;
				if (Directory.Exists(VulkanLayersDir))
				{
					Log.TraceInformation("Copying {0} vulkan layer from {1}", ANDROID_VULKAN_VALIDATION_LAYER, VulkanLayersDir);
					string DestDir = Path.Combine(UE4BuildPath, "libs", NDKArch);
					Directory.CreateDirectory(DestDir);
					string SourceFilename = Path.Combine(VulkanLayersDir, ANDROID_VULKAN_VALIDATION_LAYER);
					string DestFilename = Path.Combine(DestDir, ANDROID_VULKAN_VALIDATION_LAYER);
					SafeDeleteFile(DestFilename);
					File.Copy(SourceFilename, DestFilename);
					FileInfo DestFileInfo = new FileInfo(DestFilename);
					DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
					File.SetLastWriteTimeUtc(DestFilename, File.GetLastWriteTimeUtc(SourceFilename));
				}
			}
		}

		void CopyClangSanitizerLib(string UE4BuildPath, string UE4Arch, string NDKArch, AndroidToolChain.ClangSanitizer Sanitizer)
		{
			string Architecture = "-aarch64";
			switch (NDKArch)
			{
				case "armeabi-v7a":
					Architecture = "-arm";
					break;
				case "x86_64":
					Architecture = "-x86_64";
					break;
				case "x86":
					Architecture = "-i686";
					break;
			}

			string LibName = "asan";
			switch (Sanitizer)
			{
				case AndroidToolChain.ClangSanitizer.HwAddress:
					LibName = "hwasan";
					break;
				case AndroidToolChain.ClangSanitizer.UndefinedBehavior:
					LibName = "ubsan_standalone";
					break;
				case AndroidToolChain.ClangSanitizer.UndefinedBehaviorMinimal:
					LibName = "ubsan_minimal";
					break;
			}
			
			string SanitizerFullLibName = "libclang_rt." + LibName + Architecture + "-android.so";

			string WrapSh = Path.Combine(UnrealBuildTool.EngineDirectory.ToString(), "Build", "Android", "ClangSanitizers", "wrap.sh");
			string SanitizerLib = Path.Combine(Environment.ExpandEnvironmentVariables("%NDKROOT%"), "toolchains", "llvm", "prebuilt", "windows-x86_64", "lib64", "clang", "9.0.8", "lib", "linux", SanitizerFullLibName);
			if (File.Exists(SanitizerLib) && File.Exists(WrapSh))
			{
				string LibDestDir = Path.Combine(UE4BuildPath, "libs", NDKArch);
				Directory.CreateDirectory(LibDestDir);
				Log.TraceInformation("Copying asan lib from {0} to {1}", SanitizerLib, LibDestDir);
				File.Copy(SanitizerLib, Path.Combine(LibDestDir, SanitizerFullLibName), true);
				string WrapDestDir = Path.Combine(UE4BuildPath, "resources", "lib", NDKArch);
				Directory.CreateDirectory(WrapDestDir);
				Log.TraceInformation("Copying wrap.sh from {0} to {1}", WrapSh, WrapDestDir);
				File.Copy(WrapSh, Path.Combine(WrapDestDir, "wrap.sh"), true);
			}
			else
			{
				throw new BuildException("No asan lib found in {0} or wrap.sh in {1}", SanitizerLib, WrapSh);
			}
		}

		private static int RunCommandLineProgramAndReturnResult(string WorkingDirectory, string Command, string Params, string OverrideDesc = null, bool bUseShellExecute = false)
		{
			if (OverrideDesc == null)
			{
				Log.TraceInformation("\nRunning: " + Command + " " + Params);
			}
			else if (OverrideDesc != "")
			{
				Log.TraceInformation(OverrideDesc);
				Log.TraceVerbose("\nRunning: " + Command + " " + Params);
			}

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.WorkingDirectory = WorkingDirectory;
			StartInfo.FileName = Command;
			StartInfo.Arguments = Params;
			StartInfo.UseShellExecute = bUseShellExecute;
			StartInfo.WindowStyle = ProcessWindowStyle.Minimized;

			Process Proc = new Process();
			Proc.StartInfo = StartInfo;
			Proc.Start();
			Proc.WaitForExit();

			return Proc.ExitCode;
		}

		private static void RunCommandLineProgramWithException(string WorkingDirectory, string Command, string Params, string OverrideDesc = null, bool bUseShellExecute = false)
		{
			if (OverrideDesc == null)
			{
				Log.TraceInformation("\nRunning: " + Command + " " + Params);
			}
			else if (OverrideDesc != "")
			{
				Log.TraceInformation(OverrideDesc);
				Log.TraceVerbose("\nRunning: " + Command + " " + Params);
			}

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.WorkingDirectory = WorkingDirectory;
			StartInfo.FileName = Command;
			StartInfo.Arguments = Params;
			StartInfo.UseShellExecute = bUseShellExecute;
			StartInfo.WindowStyle = ProcessWindowStyle.Minimized;

			Process Proc = new Process();
			Proc.StartInfo = StartInfo;
			Proc.Start();
			Proc.WaitForExit();

			// android bat failure
			if (Proc.ExitCode != 0)
			{
				throw new BuildException("{0} failed with args {1}", Command, Params);
			}
		}

		private enum FilterAction
		{
			Skip,
			Replace,
			Error
		}

		private class FilterOperation
		{
			public FilterAction Action;
			public string Condition;
			public string Match;
			public string ReplaceWith;

			public FilterOperation(FilterAction InAction, string InCondition, string InMatch, string InReplaceWith)
			{
				Action = InAction;
				Condition = InCondition;
				Match = InMatch;
				ReplaceWith = InReplaceWith;
			}
		}

		static private List<FilterOperation> ActiveStdOutFilter = null;

		static List<string> ParseCSVString(string Input)
		{
			List<string> Results = new List<string>();
			StringBuilder WorkString = new StringBuilder();

			int FinalIndex = Input.Length;
			int CurrentIndex = 0;
			bool InQuote = false;

			while (CurrentIndex < FinalIndex)
			{
				char CurChar = Input[CurrentIndex++];

				if (InQuote)
				{
					if (CurChar == '\\')
					{
						if (CurrentIndex < FinalIndex)
						{
							CurChar = Input[CurrentIndex++];
							WorkString.Append(CurChar);
						}
					}
					else if (CurChar == '"')
					{
						InQuote = false;
					}
					else
					{
						WorkString.Append(CurChar);
					}
				}
				else
				{
					if (CurChar == '"')
					{
						InQuote = true;
					}
					else if (CurChar == ',')
					{
						Results.Add(WorkString.ToString());
						WorkString.Clear();
					}
					else if (!char.IsWhiteSpace(CurChar))
					{
						WorkString.Append(CurChar);
					}
				}
			}
			if (CurrentIndex > 0)
			{
				Results.Add(WorkString.ToString());
			}

			return Results;
		}

		static void ParseFilterFile(string Filename)
		{

			if (File.Exists(Filename))
			{
				ActiveStdOutFilter = new List<FilterOperation>();

				string[] FilterContents = File.ReadAllLines(Filename);
				foreach (string FileLine in FilterContents)
				{
					List<string> Parts = ParseCSVString(FileLine);

					if (Parts.Count > 1)
					{
						if (Parts[0].Equals("S"))
						{
							ActiveStdOutFilter.Add(new FilterOperation(FilterAction.Skip, Parts[1], "", ""));
						}
						else if (Parts[0].Equals("R"))
						{
							if (Parts.Count == 4)
							{
								ActiveStdOutFilter.Add(new FilterOperation(FilterAction.Replace, Parts[1], Parts[2], Parts[3]));
							}
						}
						else if (Parts[0].Equals("E"))
						{
							if (Parts.Count == 4)
							{
								ActiveStdOutFilter.Add(new FilterOperation(FilterAction.Error, Parts[1], Parts[2], Parts[3]));
							}
						}
					}
				}

				if (ActiveStdOutFilter.Count == 0)
				{
					ActiveStdOutFilter = null;
				}
			}
		}

		static void FilterStdOutErr(object sender, DataReceivedEventArgs e)
		{
			if (e.Data != null)
			{
				if (ActiveStdOutFilter != null)
				{
					foreach (FilterOperation FilterOp in ActiveStdOutFilter)
					{
						if (e.Data.Contains(FilterOp.Condition))
						{
							switch (FilterOp.Action)
							{
								case FilterAction.Skip:
									break;

								case FilterAction.Replace:
									Log.TraceInformation("{0}", e.Data.Replace(FilterOp.Match, FilterOp.ReplaceWith));
									break;

								case FilterAction.Error:
									Log.TraceError("{0}", e.Data.Replace(FilterOp.Match, FilterOp.ReplaceWith));
									break;

								default:
									break;
							}
							return;
						}
					}
				}
				Log.TraceInformation("{0}", e.Data);
			}
		}

		private static void RunCommandLineProgramWithExceptionAndFiltering(string WorkingDirectory, string Command, string Params, string OverrideDesc = null, bool bUseShellExecute = false)
		{
			if (OverrideDesc == null)
			{
				Log.TraceInformation("\nRunning: " + Command + " " + Params);
			}
			else if (OverrideDesc != "")
			{
				Log.TraceInformation(OverrideDesc);
				Log.TraceVerbose("\nRunning: " + Command + " " + Params);
			}

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.WorkingDirectory = WorkingDirectory;
			StartInfo.FileName = Command;
			StartInfo.Arguments = Params;
			StartInfo.UseShellExecute = bUseShellExecute;
			StartInfo.WindowStyle = ProcessWindowStyle.Minimized;
			StartInfo.RedirectStandardOutput = true;
			StartInfo.RedirectStandardError = true;

			Process Proc = new Process();
			Proc.StartInfo = StartInfo;
			Proc.OutputDataReceived += FilterStdOutErr;
			Proc.ErrorDataReceived += FilterStdOutErr;
			Proc.Start();
			Proc.BeginOutputReadLine();
			Proc.BeginErrorReadLine();
			Proc.WaitForExit();

			// android bat failure
			if (Proc.ExitCode != 0)
			{
				throw new BuildException("{0} failed with args {1}", Command, Params);
			}
		}

		private bool CheckApplicationName(string UE4BuildPath, string ProjectName, out string ApplicationDisplayName)
		{
			string StringsXMLPath = Path.Combine(UE4BuildPath, "res/values/strings.xml");

			ApplicationDisplayName = null;
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ApplicationDisplayName", out ApplicationDisplayName);

			// use project name if display name is left blank
			if (String.IsNullOrWhiteSpace(ApplicationDisplayName))
			{
				ApplicationDisplayName = ProjectName;
			}

			// replace escaped characters (note: changes &# pattern before &, then patches back to allow escaped character codes in the string)
			ApplicationDisplayName = ApplicationDisplayName.Replace("&#", "$@#$").Replace("&", "&amp;").Replace("'", "\\'").Replace("\"", "\\\"").Replace("<", "&lt;").Replace(">", "&gt;").Replace("$@#$", "&#");

			// if it doesn't exist, need to repackage
			if (!File.Exists(StringsXMLPath))
			{
				return true;
			}

			// read it and see if needs to be updated
			string Contents = File.ReadAllText(StringsXMLPath);

			// find the key
			string AppNameTag = "<string name=\"app_name\">";
			int KeyIndex = Contents.IndexOf(AppNameTag);

			// if doesn't exist, need to repackage
			if (KeyIndex < 0)
			{
				return true;
			}

			// get the current value
			KeyIndex += AppNameTag.Length;
			int TagEnd = Contents.IndexOf("</string>", KeyIndex);
			if (TagEnd < 0)
			{
				return true;
			}
			string CurrentApplicationName = Contents.Substring(KeyIndex, TagEnd - KeyIndex);

			// no need to do anything if matches
			if (CurrentApplicationName == ApplicationDisplayName)
			{
				// name matches, no need to force a repackage
				return false;
			}

			// need to repackage
			return true;
		}

		private string GetAllBuildSettings(AndroidToolChain ToolChain, bool bForDistribution, bool bMakeSeparateApks, bool bPackageDataInsideApk, bool bDisableVerifyOBBOnStartUp, bool bUseExternalFilesDir, string TemplatesHashCode)
		{
			// make the settings string - this will be char by char compared against last time
			StringBuilder CurrentSettings = new StringBuilder();
			CurrentSettings.AppendLine(string.Format("NDKROOT={0}", Environment.GetEnvironmentVariable("NDKROOT")));
			CurrentSettings.AppendLine(string.Format("ANDROID_HOME={0}", Environment.GetEnvironmentVariable("ANDROID_HOME")));
			CurrentSettings.AppendLine(string.Format("JAVA_HOME={0}", Environment.GetEnvironmentVariable("JAVA_HOME")));
			CurrentSettings.AppendLine(string.Format("NDKVersion={0}", ToolChain.GetNdkApiLevel()));
			CurrentSettings.AppendLine(string.Format("SDKVersion={0}", GetSdkApiLevel(ToolChain)));
			CurrentSettings.AppendLine(string.Format("bForDistribution={0}", bForDistribution));
			CurrentSettings.AppendLine(string.Format("bMakeSeparateApks={0}", bMakeSeparateApks));
			CurrentSettings.AppendLine(string.Format("bPackageDataInsideApk={0}", bPackageDataInsideApk));
			CurrentSettings.AppendLine(string.Format("bDisableVerifyOBBOnStartUp={0}", bDisableVerifyOBBOnStartUp));
			CurrentSettings.AppendLine(string.Format("bUseExternalFilesDir={0}", bUseExternalFilesDir));
			CurrentSettings.AppendLine(string.Format("UPLHashCode={0}", UPLHashCode));
			CurrentSettings.AppendLine(string.Format("TemplatesHashCode={0}", TemplatesHashCode));

			// all AndroidRuntimeSettings ini settings in here
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			ConfigHierarchySection Section = Ini.FindSection("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings");
			if (Section != null)
			{
				foreach (string Key in Section.KeyNames)
				{
					// filter out NDK and SDK override since actual resolved versions already written above
					if (Key.Equals("SDKAPILevelOverride") || Key.Equals("NDKAPILevelOverride"))
					{
						continue;
					}

					IReadOnlyList<string> Values;
					Section.TryGetValues(Key, out Values);

					foreach (string Value in Values)
					{
						CurrentSettings.AppendLine(string.Format("{0}={1}", Key, Value));
					}
				}
			}

			Section = Ini.FindSection("/Script/AndroidPlatformEditor.AndroidSDKSettings");
			if (Section != null)
			{
				foreach (string Key in Section.KeyNames)
				{
					// filter out NDK and SDK levels since actual resolved versions already written above
					if (Key.Equals("SDKAPILevel") || Key.Equals("NDKAPILevel"))
					{
						continue;
					}

					IReadOnlyList<string> Values;
					Section.TryGetValues(Key, out Values);
					foreach (string Value in Values)
					{
						CurrentSettings.AppendLine(string.Format("{0}={1}", Key, Value));
					}
				}
			}

			List<string> Arches = ToolChain.GetAllArchitectures();
			foreach (string Arch in Arches)
			{
				CurrentSettings.AppendFormat("Arch={0}{1}", Arch, Environment.NewLine);
			}

			List<string> GPUArchitectures = ToolChain.GetAllGPUArchitectures();
			foreach (string GPUArch in GPUArchitectures)
			{
				CurrentSettings.AppendFormat("GPUArch={0}{1}", GPUArch, Environment.NewLine);
			}

			// Modifying some settings in the GameMapsSettings could trigger the OBB regeneration
			// and make the cached OBBData.java mismatch to the actually data. 
			// So we insert the relevant keys into CurrentSettings to capture the change, to
			// enforce the refreshing of Android java codes
			Section = Ini.FindSection("/Script/EngineSettings.GameMapsSettings");
			if (Section != null)
			{
				foreach (string Key in Section.KeyNames)
				{
					if (!Key.Equals("GameDefaultMap") && 
						!Key.Equals("GlobalDefaultGameMode"))
					{
						continue;
					}

					IReadOnlyList<string> Values;
					Section.TryGetValues(Key, out Values);
					foreach (string Value in Values)
					{
						CurrentSettings.AppendLine(string.Format("{0}={1}", Key, Value));
					}
				}
			}

			return CurrentSettings.ToString();
		}

		private bool CheckDependencies(AndroidToolChain ToolChain, string ProjectName, string ProjectDirectory, string UE4BuildFilesPath, string GameBuildFilesPath, string EngineDirectory, List<string> SettingsFiles,
			string CookFlavor, string OutputPath, bool bMakeSeparateApks, bool bPackageDataInsideApk)
		{
			List<string> Arches = ToolChain.GetAllArchitectures();
			List<string> GPUArchitectures = ToolChain.GetAllGPUArchitectures();

			// check all input files (.so, java files, .ini files, etc)
			bool bAllInputsCurrent = true;
			foreach (string Arch in Arches)
			{
				foreach (string GPUArch in GPUArchitectures)
				{
					string SourceSOName = AndroidToolChain.InlineArchName(OutputPath, Arch, GPUArch);
					// if the source binary was UE4Game, replace it with the new project name, when re-packaging a binary only build
					string ApkFilename = Path.GetFileNameWithoutExtension(OutputPath).Replace("UE4Game", ProjectName);
					string DestApkName = Path.Combine(ProjectDirectory, "Binaries/Android/") + ApkFilename + ".apk";

					// if we making multiple Apks, we need to put the architecture into the name
					if (bMakeSeparateApks)
					{
						DestApkName = AndroidToolChain.InlineArchName(DestApkName, Arch, GPUArch);
					}

					// check to see if it's out of date before trying the slow make apk process (look at .so and all Engine and Project build files to be safe)
					List<String> InputFiles = new List<string>();
					InputFiles.Add(SourceSOName);
					InputFiles.AddRange(Directory.EnumerateFiles(UE4BuildFilesPath, "*.*", SearchOption.AllDirectories));
					if (Directory.Exists(GameBuildFilesPath))
					{
						InputFiles.AddRange(Directory.EnumerateFiles(GameBuildFilesPath, "*.*", SearchOption.AllDirectories));
					}

					// make sure changed java files will rebuild apk
					InputFiles.AddRange(SettingsFiles);

					// rebuild if .pak files exist for OBB in APK case
					if (bPackageDataInsideApk)
					{
						string PAKFileLocation = ProjectDirectory + "/Saved/StagedBuilds/Android" + CookFlavor + "/" + ProjectName + "/Content/Paks";
						if (Directory.Exists(PAKFileLocation))
						{
							IEnumerable<string> PakFiles = Directory.EnumerateFiles(PAKFileLocation, "*.pak", SearchOption.TopDirectoryOnly);
							foreach (string Name in PakFiles)
							{
								InputFiles.Add(Name);
							}
						}
					}

					// look for any newer input file
					DateTime ApkTime = File.GetLastWriteTimeUtc(DestApkName);
					foreach (string InputFileName in InputFiles)
					{
						if (File.Exists(InputFileName))
						{
							// skip .log files
							if (Path.GetExtension(InputFileName) == ".log")
							{
								continue;
							}
							DateTime InputFileTime = File.GetLastWriteTimeUtc(InputFileName);
							if (InputFileTime.CompareTo(ApkTime) > 0)
							{
								bAllInputsCurrent = false;
								Log.TraceInformation("{0} is out of date due to newer input file {1}", DestApkName, InputFileName);
								break;
							}
						}
					}
				}
			}

			return bAllInputsCurrent;
		}

		private int ConvertDepthBufferIniValue(string IniValue)
		{
			switch (IniValue.ToLower())
			{
				case "bits16":
					return 16;
				case "bits24":
					return 24;
				case "bits32":
					return 32;
				default:
					return 0;
			}
		}

		private string ConvertOrientationIniValue(string IniValue)
		{
			switch (IniValue.ToLower())
			{
				case "portrait":
					return "portrait";
				case "reverseportrait":
					return "reversePortrait";
				case "sensorportrait":
					return "sensorPortrait";
				case "landscape":
					return "landscape";
				case "reverselandscape":
					return "reverseLandscape";
				case "sensorlandscape":
					return "sensorLandscape";
				case "sensor":
					return "sensor";
				case "fullsensor":
					return "fullSensor";
				default:
					return "landscape";
			}
		}

		private string GetOrientation(string NDKArch)
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			string Orientation;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "Orientation", out Orientation);

			// check for UPL override
			string OrientationOverride = UPL.ProcessPluginNode(NDKArch, "orientationOverride", "");
			if (!String.IsNullOrEmpty(OrientationOverride))
			{
				Orientation = OrientationOverride;
			}

			return ConvertOrientationIniValue(Orientation);
		}

		private void DetermineScreenOrientationRequirements(string Arch, out bool bNeedPortrait, out bool bNeedLandscape)
		{
			bNeedLandscape = false;
			bNeedPortrait = false;

			switch (GetOrientation(Arch).ToLower())
			{
				case "portrait":
					bNeedPortrait = true;
					break;
				case "reverseportrait":
					bNeedPortrait = true;
					break;
				case "sensorportrait":
					bNeedPortrait = true;
					break;

				case "landscape":
					bNeedLandscape = true;
					break;
				case "reverselandscape":
					bNeedLandscape = true;
					break;
				case "sensorlandscape":
					bNeedLandscape = true;
					break;

				case "sensor":
					bNeedPortrait = true;
					bNeedLandscape = true;
					break;
				case "fullsensor":
					bNeedPortrait = true;
					bNeedLandscape = true;
					break;

				default:
					bNeedPortrait = true;
					bNeedLandscape = true;
					break;
			}
		}

		private void PickDownloaderScreenOrientation(string UE4BuildPath, bool bNeedPortrait, bool bNeedLandscape)
		{
			// Remove unused downloader_progress.xml to prevent missing resource
			if (!bNeedPortrait)
			{
				string LayoutPath = UE4BuildPath + "/res/layout-port/downloader_progress.xml";
				SafeDeleteFile(LayoutPath);
			}
			if (!bNeedLandscape)
			{
				string LayoutPath = UE4BuildPath + "/res/layout-land/downloader_progress.xml";
				SafeDeleteFile(LayoutPath);
			}

			// Loop through each of the resolutions (only /res/drawable/ is required, others are optional)
			string[] Resolutions = new string[] { "/res/drawable/", "/res/drawable-ldpi/", "/res/drawable-mdpi/", "/res/drawable-hdpi/", "/res/drawable-xhdpi/" };
			foreach (string ResolutionPath in Resolutions)
			{
				string PortraitFilename = UE4BuildPath + ResolutionPath + "downloadimagev.png";
				if (bNeedPortrait)
				{
					if (!File.Exists(PortraitFilename) && (ResolutionPath == "/res/drawable/"))
					{
						Log.TraceWarning("Warning: Downloader screen source image {0} not available, downloader screen will not function properly!", PortraitFilename);
					}
				}
				else
				{
					// Remove unused image
					SafeDeleteFile(PortraitFilename);
				}

				string LandscapeFilename = UE4BuildPath + ResolutionPath + "downloadimageh.png";
				if (bNeedLandscape)
				{
					if (!File.Exists(LandscapeFilename) && (ResolutionPath == "/res/drawable/"))
					{
						Log.TraceWarning("Warning: Downloader screen source image {0} not available, downloader screen will not function properly!", LandscapeFilename);
					}
				}
				else
				{
					// Remove unused image
					SafeDeleteFile(LandscapeFilename);
				}
			}
		}
	
		private void PackageForDaydream(string UE4BuildPath)
		{
			bool bPackageForDaydream = IsPackagingForDaydream();

			if (!bPackageForDaydream)
			{
				// If this isn't a Daydream App, we need to make sure to remove
				// Daydream specific assets.

				// Remove the Daydream app  tile background.
				string AppTileBackgroundPath = UE4BuildPath + "/res/drawable-nodpi/vr_icon_background.png";
				SafeDeleteFile(AppTileBackgroundPath);

				// Remove the Daydream app tile icon.
				string AppTileIconPath = UE4BuildPath + "/res/drawable-nodpi/vr_icon.png";
				SafeDeleteFile(AppTileIconPath);
			}
		}

		private void PickSplashScreenOrientation(string UE4BuildPath, bool bNeedPortrait, bool bNeedLandscape)
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool bShowLaunchImage = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bShowLaunchImage", out bShowLaunchImage);
			bool bPackageForOculusMobile = IsPackagingForOculusMobile(Ini); ;
			bool bPackageForDaydream = IsPackagingForDaydream(Ini);
			
			//override the parameters if we are not showing a launch image or are packaging for Oculus Mobile and Daydream
			if (bPackageForOculusMobile || bPackageForDaydream || !bShowLaunchImage)
			{
				bNeedPortrait = bNeedLandscape = false;
			}

			// Remove unused styles.xml to prevent missing resource
			if (!bNeedPortrait)
			{
				string StylesPath = UE4BuildPath + "/res/values-port/styles.xml";
				SafeDeleteFile(StylesPath);
			}
			if (!bNeedLandscape)
			{
				string StylesPath = UE4BuildPath + "/res/values-land/styles.xml";
				SafeDeleteFile(StylesPath);
			}

			// Loop through each of the resolutions (only /res/drawable/ is required, others are optional)
			string[] Resolutions = new string[] { "/res/drawable/", "/res/drawable-ldpi/", "/res/drawable-mdpi/", "/res/drawable-hdpi/", "/res/drawable-xhdpi/" };
			foreach (string ResolutionPath in Resolutions)
			{
				string PortraitFilename = UE4BuildPath + ResolutionPath + "splashscreen_portrait.png";
				if (bNeedPortrait)
				{
					if (!File.Exists(PortraitFilename) && (ResolutionPath == "/res/drawable/"))
					{
						Log.TraceWarning("Warning: Splash screen source image {0} not available, splash screen will not function properly!", PortraitFilename);
					}
				}
				else
				{
					// Remove unused image
					SafeDeleteFile(PortraitFilename);

					// Remove optional extended resource
					string PortraitXmlFilename = UE4BuildPath + ResolutionPath + "splashscreen_p.xml";
					SafeDeleteFile(PortraitXmlFilename);
				}

				string LandscapeFilename = UE4BuildPath + ResolutionPath + "splashscreen_landscape.png";
				if (bNeedLandscape)
				{
					if (!File.Exists(LandscapeFilename) && (ResolutionPath == "/res/drawable/"))
					{
						Log.TraceWarning("Warning: Splash screen source image {0} not available, splash screen will not function properly!", LandscapeFilename);
					}
				}
				else
				{
					// Remove unused image
					SafeDeleteFile(LandscapeFilename);

					// Remove optional extended resource
					string LandscapeXmlFilename = UE4BuildPath + ResolutionPath + "splashscreen_l.xml";
					SafeDeleteFile(LandscapeXmlFilename);
				}
			}
		}

		private string CachedPackageName = null;

		private bool IsLetter(char Input)
		{
			return (Input >= 'A' && Input <= 'Z') || (Input >= 'a' && Input <= 'z');
		}

		private bool IsDigit(char Input)
		{
			return (Input >= '0' && Input <= '9');
		}

		private string GetPackageName(string ProjectName)
		{
			if (CachedPackageName == null)
			{
				ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
				string PackageName;
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "PackageName", out PackageName);

				if (PackageName.Contains("[PROJECT]"))
				{
					// project name must start with a letter
					if (!IsLetter(ProjectName[0]))
					{
						throw new BuildException("Package name segments must all start with a letter. Please replace [PROJECT] with a valid name");
					}

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
						if (c != '.' && c != '_' && !IsDigit(c) && !IsLetter(c))
						{
							throw new BuildException("Project name contains illegal characters (only letters, numbers, and underscore allowed); please replace [PROJECT] with a valid name");
						}
					}

					PackageName = PackageName.Replace("[PROJECT]", ProjectName);
				}

				// verify minimum number of segments
				string[] PackageParts = PackageName.Split('.');
				int SectionCount = PackageParts.Length;
				if (SectionCount < 2)
				{
					throw new BuildException("Package name must have at least 2 segments separated by periods (ex. com.projectname, not projectname); please change in Android Project Settings. Currently set to '" + PackageName + "'");
				}

				// hyphens not allowed
				if (PackageName.Contains("-"))
				{
					throw new BuildException("Package names may not contain hyphens; please change in Android Project Settings. Currently set to '" + PackageName + "'");
				}

				// do not allow special characters
				for (int Index = 0; Index < PackageName.Length; Index++)
				{
					char c = PackageName[Index];
					if (c != '.' && c != '_' && !IsDigit(c) && !IsLetter(c))
					{
						throw new BuildException("Package name contains illegal characters (only letters, numbers, and underscore allowed); please change in Android Project Settings. Currently set to '" + PackageName + "'");
					}
				}

				// validate each segment
				for (int Index = 0; Index < SectionCount; Index++)
				{
					if (PackageParts[Index].Length < 1)
					{
						throw new BuildException("Package name segments must have at least one letter; please change in Android Project Settings. Currently set to '" + PackageName + "'");
					}

					if (!IsLetter(PackageParts[Index][0]))
					{
						throw new BuildException("Package name segments must start with a letter; please change in Android Project Settings. Currently set to '" + PackageName + "'");
					}

					// cannot use Java reserved keywords
					foreach (string Keyword in JavaReservedKeywords)
					{
						if (PackageParts[Index] == Keyword)
						{
							throw new BuildException("Package name segments must not be a Java reserved keyword (" + Keyword + "); please change in Android Project Settings. Currently set to '" + PackageName + "'");
						}
					}
				}

				Log.TraceInformation("Using package name: '{0}'", PackageName);
				CachedPackageName = PackageName;
			}

			return CachedPackageName;
		}

		private string GetPublicKey()
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			string PlayLicenseKey = "";
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "GooglePlayLicenseKey", out PlayLicenseKey);
			return PlayLicenseKey;
		}

		private bool bHaveReadEngineVersion = false;
		private string EngineMajorVersion = "4";
		private string EngineMinorVersion = "0";
		private string EnginePatchVersion = "0";
		private string EngineChangelist = "0";
		private string EngineBranch = "UE4";

		private string ReadEngineVersion()
		{
			if (!bHaveReadEngineVersion)
			{
				ReadOnlyBuildVersion Version = ReadOnlyBuildVersion.Current;

				EngineMajorVersion = Version.MajorVersion.ToString();
				EngineMinorVersion = Version.MinorVersion.ToString();
				EnginePatchVersion = Version.PatchVersion.ToString();
				EngineChangelist = Version.Changelist.ToString();
				EngineBranch = Version.BranchName;

				bHaveReadEngineVersion = true;
			}

			return EngineMajorVersion + "." + EngineMinorVersion + "." + EnginePatchVersion;
		}


		private string GenerateManifest(AndroidToolChain ToolChain, string ProjectName, TargetType InTargetType, string EngineDirectory, bool bIsForDistribution, bool bPackageDataInsideApk, string GameBuildFilesPath, bool bHasOBBFiles, bool bDisableVerifyOBBOnStartUp, string UE4Arch, string GPUArch, string CookFlavor, bool bUseExternalFilesDir, string Configuration, int SDKLevelInt, bool bIsEmbedded, bool bEnableBundle)
		{
			// Read the engine version
			string EngineVersion = ReadEngineVersion();

			int StoreVersion = GetStoreVersion(UE4Arch);

			string Arch = GetNDKArch(UE4Arch);
			int NDKLevelInt = 0;
			int MinSDKVersion = 0;
			int TargetSDKVersion = 0;
			GetMinTargetSDKVersions(ToolChain, UE4Arch, UPL, Arch, bEnableBundle, out MinSDKVersion, out TargetSDKVersion, out NDKLevelInt);

			// get project version from ini
			ConfigHierarchy GameIni = GetConfigCacheIni(ConfigHierarchyType.Game);
			string ProjectVersion;
			GameIni.GetString("/Script/EngineSettings.GeneralProjectSettings", "ProjectVersion", out ProjectVersion);

			// ini file to get settings from
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			string PackageName = GetPackageName(ProjectName);
			string VersionDisplayName = GetVersionDisplayName(bIsEmbedded);
			bool bEnableGooglePlaySupport;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bEnableGooglePlaySupport", out bEnableGooglePlaySupport);
			bool bUseGetAccounts;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bUseGetAccounts", out bUseGetAccounts);
			string DepthBufferPreference;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "DepthBufferPreference", out DepthBufferPreference);
			float MaxAspectRatioValue;
			if (!Ini.TryGetValue("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "MaxAspectRatio", out MaxAspectRatioValue))
			{
				MaxAspectRatioValue = 2.1f;
			}
			string Orientation = ConvertOrientationIniValue(GetOrientation(Arch));
			bool EnableFullScreen;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bFullScreen", out EnableFullScreen);
			bool bUseDisplayCutout;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bUseDisplayCutout", out bUseDisplayCutout);
			bool bRestoreNotificationsOnReboot = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bRestoreNotificationsOnReboot", out bRestoreNotificationsOnReboot);
			List<string> ExtraManifestNodeTags;
			Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ExtraManifestNodeTags", out ExtraManifestNodeTags);
			List<string> ExtraApplicationNodeTags;
			Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ExtraApplicationNodeTags", out ExtraApplicationNodeTags);
			List<string> ExtraActivityNodeTags;
			Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ExtraActivityNodeTags", out ExtraActivityNodeTags);
			string ExtraActivitySettings;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ExtraActivitySettings", out ExtraActivitySettings);
			string ExtraApplicationSettings;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ExtraApplicationSettings", out ExtraApplicationSettings);
			List<string> ExtraPermissions;
			Ini.GetArray("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "ExtraPermissions", out ExtraPermissions);
			bool bPackageForOculusMobile = IsPackagingForOculusMobile(Ini);
			bool bEnableIAP = false;
			Ini.GetBool("OnlineSubsystemGooglePlay.Store", "bSupportsInAppPurchasing", out bEnableIAP);
			bool bShowLaunchImage = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bShowLaunchImage", out bShowLaunchImage);
			string AndroidGraphicsDebugger;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "AndroidGraphicsDebugger", out AndroidGraphicsDebugger);
			bool bSupportAdMob = true;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSupportAdMob", out bSupportAdMob);
			bool bValidateTextureFormats;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bValidateTextureFormats", out bValidateTextureFormats);
			bool bUseNEONForArmV7 = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bUseNEONForArmV7", out bUseNEONForArmV7);
			
			bool bBuildForES31 = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bBuildForES31", out bBuildForES31);
			bool bSupportsVulkan = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSupportsVulkan", out bSupportsVulkan);

			bool bAllowIMU = true;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bAllowIMU", out bAllowIMU);
			if (IsPackagingForDaydream(Ini) && bAllowIMU)
			{
				Log.TraceInformation("Daydream and IMU both enabled, recommend disabling IMU if not needed.");
			}

			bool bExtractNativeLibs = true;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bExtractNativeLibs", out bExtractNativeLibs);

			bool bPublicLogFiles = true;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bPublicLogFiles", out bPublicLogFiles);
			if (!bUseExternalFilesDir)
			{
				bPublicLogFiles = false;
			}

			string InstallLocation;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "InstallLocation", out InstallLocation);
			switch (InstallLocation.ToLower())
			{
				case "preferexternal":
					InstallLocation = "preferExternal";
					break;
				case "auto":
					InstallLocation = "auto";
					break;
				default:
					InstallLocation = "internalOnly";
					break;
			}

			// only apply density to configChanges if using android-24 or higher and minimum sdk is 17
			bool bAddDensity = (SDKLevelInt >= 24) && (MinSDKVersion >= 17);

			// disable Oculus Mobile if not supported platform (in this case only armv7 for now)
			if (UE4Arch != "-armv7" && UE4Arch != "-arm64")
			{
				if (bPackageForOculusMobile)
				{
					Log.TraceInformation("Disabling Package For Oculus Mobile for unsupported architecture {0}", UE4Arch);
					bPackageForOculusMobile = false;
				}
			}

			// disable splash screen for Oculus Mobile (for now)
			if (bPackageForOculusMobile)
			{
				if (bShowLaunchImage)
				{
					Log.TraceInformation("Disabling Show Launch Image for Oculus Mobile enabled application");
					bShowLaunchImage = false;
				}
			}

			bool bPackageForDaydream = IsPackagingForDaydream(Ini);
			// disable splash screen for daydream
			if (bPackageForDaydream)
			{
				if (bShowLaunchImage)
				{
					Log.TraceInformation("Disabling Show Launch Image for Daydream enabled application");
					bShowLaunchImage = false;
				}
			}

			//figure out the app type
			string AppType = InTargetType == TargetType.Game ? "" : InTargetType.ToString();
			if (CookFlavor.EndsWith("Client"))
			{
				CookFlavor = CookFlavor.Substring(0, CookFlavor.Length - 6);
			}
			if (CookFlavor.EndsWith("Server"))
			{
				CookFlavor = CookFlavor.Substring(0, CookFlavor.Length - 6);
			}

			//figure out which texture compressions are supported
			bool bETC2Enabled, bDXTEnabled, bASTCEnabled;
			bETC2Enabled = bDXTEnabled = bASTCEnabled = false;
			if (CookFlavor.Length < 1)
			{
				//All values supported
				bETC2Enabled = bDXTEnabled = bASTCEnabled = true;
			}
			else
			{
				switch (CookFlavor)
				{
					case "_Multi":
						//need to check ini to determine which are supported
						Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bMultiTargetFormat_ETC2", out bETC2Enabled);
						Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bMultiTargetFormat_DXT", out bDXTEnabled);
						Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bMultiTargetFormat_ASTC", out bASTCEnabled);
						break;
					case "_ETC2":
						bETC2Enabled = true;
						break;
					case "_DXT":
						bDXTEnabled = true;
						break;
					case "_ASTC":
						bASTCEnabled = true;
						break;
					default:
						Log.TraceWarning("Invalid or unknown CookFlavor used in GenerateManifest: {0}", CookFlavor);
						break;
				}
			}
			bool bSupportingAllTextureFormats = bETC2Enabled && bDXTEnabled && bASTCEnabled;

			// If it is only ETC2 we need to skip adding the texture format filtering and instead use ES 3.0 as minimum version (it requires ETC2)
			bool bOnlyETC2Enabled = (bETC2Enabled && !(bDXTEnabled || bASTCEnabled));

			string CookedFlavors = (bETC2Enabled ? "ETC2," : "") +
									(bDXTEnabled ? "DXT," : "") +
									(bASTCEnabled ? "ASTC," : "");
			CookedFlavors = (CookedFlavors == "") ? "" : CookedFlavors.Substring(0, CookedFlavors.Length - 1);

			StringBuilder Text = new StringBuilder();
			Text.AppendLine(XML_HEADER);
			Text.AppendLine("<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"");
			Text.AppendLine(string.Format("          package=\"{0}\"", PackageName));
			if (ExtraManifestNodeTags != null)
			{
				foreach (string Line in ExtraManifestNodeTags)
				{
					Text.AppendLine("          " + Line);
				}
			}
			Text.AppendLine(string.Format("          android:installLocation=\"{0}\"", InstallLocation));
			Text.AppendLine(string.Format("          android:versionCode=\"{0}\"", StoreVersion));
			Text.AppendLine(string.Format("          android:versionName=\"{0}\">", VersionDisplayName));

			Text.AppendLine("");

			if (TargetSDKVersion >= 30)
			{
				Text.AppendLine("\t<queries>");
				Text.AppendLine("\t\t<intent>");
				Text.AppendLine("\t\t\t<action android:name=\"android.intent.action.VIEW\" />");
				Text.AppendLine("\t\t\t<category android:name=\"android.intent.category.BROWSABLE\" />");
				Text.AppendLine("\t\t\t<data android:scheme=\"http\" />");
				Text.AppendLine("\t\t</intent>");
				Text.AppendLine("\t\t<intent>");
				Text.AppendLine("\t\t\t<action android:name=\"android.intent.action.VIEW\" />");
				Text.AppendLine("\t\t\t<category android:name=\"android.intent.category.BROWSABLE\" />");
				Text.AppendLine("\t\t\t<data android:scheme=\"https\" />");
				Text.AppendLine("\t\t</intent>");
				Text.AppendLine("\t</queries>");
			}

			Text.AppendLine("\t<!-- Application Definition -->");
			Text.AppendLine("\t<application android:label=\"@string/app_name\"");
			Text.AppendLine("\t             android:icon=\"@drawable/icon\"");

			AndroidToolChain.ClangSanitizer Sanitizer = AndroidToolChain.BuildWithSanitizer(ProjectFile);
			if (Sanitizer != AndroidToolChain.ClangSanitizer.None && Sanitizer != AndroidToolChain.ClangSanitizer.HwAddress)
			{
				Text.AppendLine("\t             android:extractNativeLibs=\"true\"");
			}

			bool bRequestedLegacyExternalStorage = false;
			if (ExtraApplicationNodeTags != null)
			{
				foreach (string Line in ExtraApplicationNodeTags)
				{
					if (Line.Contains("requestLegacyExternalStorage"))
					{
						bRequestedLegacyExternalStorage = true;
					}
					Text.AppendLine("\t             " + Line);
				}
			}
			Text.AppendLine("\t             android:hardwareAccelerated=\"true\"");
			Text.AppendLine(string.Format("\t             android:extractNativeLibs=\"{0}\"", bExtractNativeLibs ? "true" : "false"));
			Text.AppendLine("\t				android:name=\"com.epicgames.ue4.GameApplication\"");
			if (!bIsForDistribution && SDKLevelInt >= 29 && !bRequestedLegacyExternalStorage)
			{
				// work around scoped storage for non-distribution for SDK 29; add to ExtraApplicationNodeTags if you need it for distribution
				Text.AppendLine("\t				android:requestLegacyExternalStorage=\"true\"");
			}
			Text.AppendLine("\t             android:hasCode=\"true\">");
			if (bShowLaunchImage)
			{
				// normal application settings
				Text.AppendLine("\t\t<activity android:name=\"com.epicgames.ue4.SplashActivity\"");
				Text.AppendLine("\t\t          android:label=\"@string/app_name\"");
				Text.AppendLine("\t\t          android:theme=\"@style/UE4SplashTheme\"");
				Text.AppendLine("\t\t          android:launchMode=\"singleTask\"");
				Text.AppendLine(string.Format("\t\t          android:screenOrientation=\"{0}\"", Orientation));
				Text.AppendLine(string.Format("\t\t          android:debuggable=\"{0}\">", bIsForDistribution ? "false" : "true"));
				Text.AppendLine("\t\t\t<intent-filter>");
				Text.AppendLine("\t\t\t\t<action android:name=\"android.intent.action.MAIN\" />");
				Text.AppendLine(string.Format("\t\t\t\t<category android:name=\"android.intent.category.LAUNCHER\" />"));
				Text.AppendLine("\t\t\t</intent-filter>");
				Text.AppendLine("\t\t</activity>");
				Text.AppendLine("\t\t<activity android:name=\"com.epicgames.ue4.GameActivity\"");
				Text.AppendLine("\t\t          android:label=\"@string/app_name\"");
				Text.AppendLine("\t\t          android:theme=\"@style/UE4SplashTheme\"");
				Text.AppendLine(bAddDensity ? "\t\t          android:configChanges=\"mcc|mnc|uiMode|density|screenSize|smallestScreenSize|screenLayout|orientation|keyboardHidden|keyboard\""
											: "\t\t          android:configChanges=\"mcc|mnc|uiMode|screenSize|smallestScreenSize|screenLayout|orientation|keyboardHidden|keyboard\"");
			}
			else
			{
				Text.AppendLine("\t\t<activity android:name=\"com.epicgames.ue4.GameActivity\"");
				Text.AppendLine("\t\t          android:label=\"@string/app_name\"");
				Text.AppendLine("\t\t          android:theme=\"@android:style/Theme.Black.NoTitleBar.Fullscreen\"");
				Text.AppendLine(bAddDensity ? "\t\t          android:configChanges=\"mcc|mnc|uiMode|density|screenSize|smallestScreenSize|screenLayout|orientation|keyboardHidden|keyboard\""
											: "\t\t          android:configChanges=\"mcc|mnc|uiMode|screenSize|smallestScreenSize|screenLayout|orientation|keyboardHidden|keyboard\"");

			}
			if (SDKLevelInt >= 24)
			{
				Text.AppendLine("\t\t          android:resizeableActivity=\"false\"");
			}
			Text.AppendLine("\t\t          android:launchMode=\"singleTask\"");
			Text.AppendLine(string.Format("\t\t          android:screenOrientation=\"{0}\"", Orientation));
			if (ExtraActivityNodeTags != null)
			{
				foreach (string Line in ExtraActivityNodeTags)
				{
					Text.AppendLine("\t\t          " + Line);
				}
			}
			Text.AppendLine(string.Format("\t\t          android:debuggable=\"{0}\">", bIsForDistribution ? "false" : "true"));
			Text.AppendLine("\t\t\t<meta-data android:name=\"android.app.lib_name\" android:value=\"UE4\"/>");
			if (!bShowLaunchImage)
			{
				Text.AppendLine("\t\t\t<intent-filter>");
				Text.AppendLine("\t\t\t\t<action android:name=\"android.intent.action.MAIN\" />");
				Text.AppendLine(string.Format("\t\t\t\t<category android:name=\"android.intent.category.LAUNCHER\" />"));
				Text.AppendLine("\t\t\t</intent-filter>");
			}
			if (!string.IsNullOrEmpty(ExtraActivitySettings))
			{
				ExtraActivitySettings = ExtraActivitySettings.Replace("\\n", "\n");
				foreach (string Line in ExtraActivitySettings.Split("\r\n".ToCharArray()))
				{
					Text.AppendLine("\t\t\t" + Line);
				}
			}
			string ActivityAdditionsFile = Path.Combine(GameBuildFilesPath, "ManifestActivityAdditions.txt");
			if (File.Exists(ActivityAdditionsFile))
			{
				foreach (string Line in File.ReadAllLines(ActivityAdditionsFile))
				{
					Text.AppendLine("\t\t\t" + Line);
				}
			}
			Text.AppendLine("\t\t</activity>");

			// For OBB download support
			if (bShowLaunchImage)
			{
				Text.AppendLine("\t\t<activity android:name=\".DownloaderActivity\"");
				Text.AppendLine(string.Format("\t\t          android:screenOrientation=\"{0}\"", Orientation));
				Text.AppendLine(bAddDensity ? "\t\t          android:configChanges=\"mcc|mnc|uiMode|density|screenSize|orientation|keyboardHidden|keyboard\""
											: "\t\t          android:configChanges=\"mcc|mnc|uiMode|screenSize|orientation|keyboardHidden|keyboard\"");
				Text.AppendLine("\t\t          android:theme=\"@style/UE4SplashTheme\" />");
			}
			else
			{
				Text.AppendLine("\t\t<activity android:name=\".DownloaderActivity\" />");
			}

			// Figure out the required startup permissions if targetting devices supporting runtime permissions
			String StartupPermissions = "";
			if (TargetSDKVersion >= 23)
			{
				if (Configuration != "Shipping" || !bUseExternalFilesDir)
				{
					StartupPermissions = StartupPermissions + (StartupPermissions.Length > 0 ? "," : "") + "android.permission.WRITE_EXTERNAL_STORAGE";
				}
				if (bEnableGooglePlaySupport && bUseGetAccounts)
				{
					StartupPermissions = StartupPermissions + (StartupPermissions.Length > 0 ? "," : "") + "android.permission.GET_ACCOUNTS";
				}
			}

			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.EngineVersion\" android:value=\"{0}\"/>", EngineVersion));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.EngineBranch\" android:value=\"{0}\"/>", EngineBranch));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.ProjectVersion\" android:value=\"{0}\"/>", ProjectVersion));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.DepthBufferPreference\" android:value=\"{0}\"/>", ConvertDepthBufferIniValue(DepthBufferPreference)));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bPackageDataInsideApk\" android:value=\"{0}\"/>", bPackageDataInsideApk ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bVerifyOBBOnStartUp\" android:value=\"{0}\"/>", (bIsForDistribution && !bDisableVerifyOBBOnStartUp) ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bShouldHideUI\" android:value=\"{0}\"/>", EnableFullScreen ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.ProjectName\" android:value=\"{0}\"/>", ProjectName));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.AppType\" android:value=\"{0}\"/>", AppType));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bHasOBBFiles\" android:value=\"{0}\"/>", bHasOBBFiles ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.BuildConfiguration\" android:value=\"{0}\"/>", Configuration));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.CookedFlavors\" android:value=\"{0}\"/>", CookedFlavors));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bValidateTextureFormats\" android:value=\"{0}\"/>", bValidateTextureFormats ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bUseExternalFilesDir\" android:value=\"{0}\"/>", bUseExternalFilesDir ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bPublicLogFiles\" android:value=\"{0}\"/>", bPublicLogFiles ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bUseDisplayCutout\" android:value=\"{0}\"/>", bUseDisplayCutout ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bAllowIMU\" android:value=\"{0}\"/>", bAllowIMU ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bSupportsVulkan\" android:value=\"{0}\"/>", bSupportsVulkan ? "true" : "false"));
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.StartupPermissions\" android:value=\"{0}\"/>", StartupPermissions));
			if (bUseNEONForArmV7)
			{
				Text.AppendLine("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bUseNEONForArmV7\" android:value=\"{true}\"/>");
			}
			if (bPackageForDaydream)
			{
				Text.AppendLine(string.Format("\t\t<meta-data android:name=\"com.epicgames.ue4.GameActivity.bDaydream\" android:value=\"true\"/>"));
			}
			Text.AppendLine("\t\t<meta-data android:name=\"com.google.android.gms.games.APP_ID\"");
			Text.AppendLine("\t\t           android:value=\"@string/app_id\" />");
			Text.AppendLine("\t\t<meta-data android:name=\"com.google.android.gms.version\"");
			Text.AppendLine("\t\t           android:value=\"@integer/google_play_services_version\" />");
			if (bSupportAdMob)
			{
			Text.AppendLine("\t\t<activity android:name=\"com.google.android.gms.ads.AdActivity\"");
			Text.AppendLine("\t\t          android:configChanges=\"keyboard|keyboardHidden|orientation|screenLayout|uiMode|screenSize|smallestScreenSize\"/>");
			}
			if (!string.IsNullOrEmpty(ExtraApplicationSettings))
			{
				ExtraApplicationSettings = ExtraApplicationSettings.Replace("\\n", "\n");
				foreach (string Line in ExtraApplicationSettings.Split("\r\n".ToCharArray()))
				{
					Text.AppendLine("\t\t" + Line);
				}
			}
			string ApplicationAdditionsFile = Path.Combine(GameBuildFilesPath, "ManifestApplicationAdditions.txt");
			if (File.Exists(ApplicationAdditionsFile))
			{
				foreach (string Line in File.ReadAllLines(ApplicationAdditionsFile))
				{
					Text.AppendLine("\t\t" + Line);
				}
			}

			// Required for OBB download support
			Text.AppendLine("\t\t<service android:name=\"OBBDownloaderService\" />");
			Text.AppendLine("\t\t<receiver android:name=\"AlarmReceiver\" />");

			Text.AppendLine("\t\t<receiver android:name=\"com.epicgames.ue4.LocalNotificationReceiver\" />");

			if (bRestoreNotificationsOnReboot)
			{
				Text.AppendLine("\t\t<receiver android:name=\"com.epicgames.ue4.BootCompleteReceiver\" android:exported=\"true\">");
				Text.AppendLine("\t\t\t<intent-filter>");
				Text.AppendLine("\t\t\t\t<action android:name=\"android.intent.action.BOOT_COMPLETED\" />");
				Text.AppendLine("\t\t\t\t<action android:name=\"android.intent.action.QUICKBOOT_POWERON\" />");
				Text.AppendLine("\t\t\t\t<action android:name=\"com.htc.intent.action.QUICKBOOT_POWERON\" />");
				Text.AppendLine("\t\t\t</intent-filter>");
				Text.AppendLine("\t\t</receiver>");
			}

			Text.AppendLine("\t\t<receiver android:name=\"com.epicgames.ue4.MulticastBroadcastReceiver\" android:exported=\"true\">");
			Text.AppendLine("\t\t\t<intent-filter>");
			Text.AppendLine("\t\t\t\t<action android:name=\"com.android.vending.INSTALL_REFERRER\" />");
			Text.AppendLine("\t\t\t</intent-filter>");
			Text.AppendLine("\t\t</receiver>");

			// Max supported aspect ratio
			string MaxAspectRatioString = MaxAspectRatioValue.ToString("f", System.Globalization.CultureInfo.InvariantCulture);
			Text.AppendLine(string.Format("\t\t<meta-data android:name=\"android.max_aspect\" android:value=\"{0}\" />", MaxAspectRatioString));
					
			Text.AppendLine("\t</application>");

			Text.AppendLine("");
			Text.AppendLine("\t<!-- Requirements -->");

			// check for an override for the requirements section of the manifest
			string RequirementsOverrideFile = Path.Combine(GameBuildFilesPath, "ManifestRequirementsOverride.txt");
			if (File.Exists(RequirementsOverrideFile))
			{
				foreach (string Line in File.ReadAllLines(RequirementsOverrideFile))
				{
					Text.AppendLine("\t" + Line);
				}
			}
			else
			{
				Text.AppendLine("\t<uses-feature android:glEsVersion=\"" + AndroidToolChain.GetGLESVersion(bBuildForES31) + "\" android:required=\"true\" />");
				Text.AppendLine("\t<uses-permission android:name=\"android.permission.INTERNET\"/>");
				if (Configuration != "Shipping" || !bUseExternalFilesDir)
				{
					Text.AppendLine("\t<uses-permission android:name=\"android.permission.WRITE_EXTERNAL_STORAGE\"/>");
				}
				Text.AppendLine("\t<uses-permission android:name=\"android.permission.ACCESS_NETWORK_STATE\"/>");
				Text.AppendLine("\t<uses-permission android:name=\"android.permission.WAKE_LOCK\"/>");
			//	Text.AppendLine("\t<uses-permission android:name=\"android.permission.READ_PHONE_STATE\"/>");
				Text.AppendLine("\t<uses-permission android:name=\"com.android.vending.CHECK_LICENSE\"/>");
				Text.AppendLine("\t<uses-permission android:name=\"android.permission.ACCESS_WIFI_STATE\"/>");

				if (bRestoreNotificationsOnReboot)
				{
					Text.AppendLine("\t<uses-permission android:name=\"android.permission.RECEIVE_BOOT_COMPLETED\"/>");
				}

				if (bEnableGooglePlaySupport && bUseGetAccounts)
				{
					Text.AppendLine("\t<uses-permission android:name=\"android.permission.GET_ACCOUNTS\"/>");
				}

				if(!bPackageForOculusMobile)
				{
					Text.AppendLine("\t<uses-permission android:name=\"android.permission.MODIFY_AUDIO_SETTINGS\"/>");
					Text.AppendLine("\t<uses-permission android:name=\"android.permission.VIBRATE\"/>");
				}

				//			Text.AppendLine("\t<uses-permission android:name=\"android.permission.DISABLE_KEYGUARD\"/>");

				if (bEnableIAP)
				{
					Text.AppendLine("\t<uses-permission android:name=\"com.android.vending.BILLING\"/>");
				}
				if (ExtraPermissions != null)
				{
					foreach (string Permission in ExtraPermissions)
					{
						string TrimmedPermission = Permission.Trim(' ');
						if (TrimmedPermission != "")
						{
							string PermissionString = string.Format("\t<uses-permission android:name=\"{0}\"/>", TrimmedPermission);
							if (!Text.ToString().Contains(PermissionString))
							{
								Text.AppendLine(PermissionString);
							}
						}
					}
				}
				string RequirementsAdditionsFile = Path.Combine(GameBuildFilesPath, "ManifestRequirementsAdditions.txt");
				if (File.Exists(RequirementsAdditionsFile))
				{
					foreach (string Line in File.ReadAllLines(RequirementsAdditionsFile))
					{
						Text.AppendLine("\t" + Line);
					}
				}
				if (AndroidGraphicsDebugger.ToLower() == "adreno")
				{
					string PermissionString = "\t<uses-permission android:name=\"com.qti.permission.PROFILER\"/>";
					if (!Text.ToString().Contains(PermissionString))
					{
						Text.AppendLine(PermissionString);
					}
				}

				if (!bSupportingAllTextureFormats)
				{
					Text.AppendLine("\t<!-- Supported texture compression formats (cooked) -->");
					if (bETC2Enabled && !bOnlyETC2Enabled)
					{
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_COMPRESSED_RGB8_ETC2\" />");
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_COMPRESSED_RGBA8_ETC2_EAC\" />");
					}
					if (bDXTEnabled)
					{
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_EXT_texture_compression_dxt1\" />");
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_EXT_texture_compression_s3tc\" />");
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_NV_texture_compression_s3tc\" />");
					}
					if (bASTCEnabled)
					{
						Text.AppendLine("\t<supports-gl-texture android:name=\"GL_KHR_texture_compression_astc_ldr\" />");
					}
				}
			}

			Text.AppendLine("</manifest>");

			// allow plugins to modify final manifest HERE
			XDocument XDoc;
			try
			{
				XDoc = XDocument.Parse(Text.ToString());
			}
			catch (Exception e)
			{
				throw new BuildException("AndroidManifest.xml is invalid {0}\n{1}", e, Text.ToString());
			}

			UPL.ProcessPluginNode(Arch, "androidManifestUpdates", "", ref XDoc);
			return XDoc.ToString();
		}

		private string GenerateProguard(string Arch, string EngineSourcePath, string GameBuildFilesPath)
		{
			StringBuilder Text = new StringBuilder();

			string ProguardFile = Path.Combine(EngineSourcePath, "proguard-project.txt");
			if (File.Exists(ProguardFile))
			{
				foreach (string Line in File.ReadAllLines(ProguardFile))
				{
					Text.AppendLine(Line);
				}
			}

			string ProguardAdditionsFile = Path.Combine(GameBuildFilesPath, "ProguardAdditions.txt");
			if (File.Exists(ProguardAdditionsFile))
			{
				foreach (string Line in File.ReadAllLines(ProguardAdditionsFile))
				{
					Text.AppendLine(Line);
				}
			}

			// add plugin additions
			return UPL.ProcessPluginNode(Arch, "proguardAdditions", Text.ToString());
		}

		private void ValidateGooglePlay(string UE4BuildPath)
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool bEnableGooglePlaySupport;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bEnableGooglePlaySupport", out bEnableGooglePlaySupport);

			if (!bEnableGooglePlaySupport)
			{
				// do not need to do anything; it is fine
				return;
			}

			string IniAppId;
			bool bInvalidIniAppId = false;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "GamesAppID", out IniAppId);

			//validate the value found in the AndroidRuntimeSettings
			Int64 Value;
			if (IniAppId.Length == 0 || !Int64.TryParse(IniAppId, out Value))
			{
				bInvalidIniAppId = true;
			}

			bool bInvalid = false;
			string ReplacementId = "";
			String Filename = Path.Combine(UE4BuildPath, "res", "values", "GooglePlayAppID.xml");
			if (File.Exists(Filename))
			{
				string[] FileContent = File.ReadAllLines(Filename);
				int LineIndex = -1;
				foreach (string Line in FileContent)
				{
					++LineIndex;

					int StartIndex = Line.IndexOf("\"app_id\">");
					if (StartIndex < 0)
						continue;

					StartIndex += 9;
					int EndIndex = Line.IndexOf("</string>");
					if (EndIndex < 0)
						continue;

					string XmlAppId = Line.Substring(StartIndex, EndIndex - StartIndex);

					//validate that the AppId matches the .ini value for the GooglePlay AppId, assuming it's valid
					if (!bInvalidIniAppId &&  IniAppId.CompareTo(XmlAppId) != 0)
					{
						Log.TraceInformation("Replacing Google Play AppID in GooglePlayAppID.xml with AndroidRuntimeSettings .ini value");

						bInvalid = true;
						ReplacementId = IniAppId;
						
					}					
					else if(XmlAppId.Length == 0 || !Int64.TryParse(XmlAppId, out Value))
					{
						Log.TraceWarning("\nWARNING: GooglePlay Games App ID is invalid! Replacing it with \"1\"");

						//write file with something which will fail but not cause an exception if executed
						bInvalid = true;
						ReplacementId = "1";
					}	

					if(bInvalid)
					{
						// remove any read only flags if invalid so it can be replaced
						FileInfo DestFileInfo = new FileInfo(Filename);
						DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;

						//preserve the rest of the file, just fix up this line
						string NewLine = Line.Replace("\"app_id\">" + XmlAppId + "</string>", "\"app_id\">" + ReplacementId + "</string>");
						FileContent[LineIndex] = NewLine;

						File.WriteAllLines(Filename, FileContent);
					}

					break;
				}
			}
			else
			{
				string NewAppId;
				// if we don't have an appID to use from the config, write file with something which will fail but not cause an exception if executed
				if (bInvalidIniAppId)
				{
					Log.TraceWarning("\nWARNING: Creating GooglePlayAppID.xml using a Google Play AppID of \"1\" because there was no valid AppID in AndroidRuntimeSettings!");
					NewAppId = "1";
				}
				else
				{
					Log.TraceInformation("Creating GooglePlayAppID.xml with AndroidRuntimeSettings .ini value");
					NewAppId = IniAppId;
				}

				File.WriteAllText(Filename, XML_HEADER + "\n<resources>\n\t<string name=\"app_id\">" + NewAppId + "</string>\n</resources>\n");
			}
		}

		private bool FilesAreDifferent(string SourceFilename, string DestFilename)
		{
			// source must exist
			FileInfo SourceInfo = new FileInfo(SourceFilename);
			if (!SourceInfo.Exists)
			{
				throw new BuildException("Can't make an APK without file [{0}]", SourceFilename);
			}

			// different if destination doesn't exist
			FileInfo DestInfo = new FileInfo(DestFilename);
			if (!DestInfo.Exists)
			{
				return true;
			}

			// file lengths differ?
			if (SourceInfo.Length != DestInfo.Length)
			{
				return true;
			}

			// validate timestamps
			TimeSpan Diff = DestInfo.LastWriteTimeUtc - SourceInfo.LastWriteTimeUtc;
			if (Diff.TotalSeconds < -1 || Diff.TotalSeconds > 1)
			{
				return true;
			}

			// could check actual bytes just to be sure, but good enough
			return false;
		}

		private bool FilesAreIdentical(string SourceFilename, string DestFilename)
		{
			// source must exist
			FileInfo SourceInfo = new FileInfo(SourceFilename);
			if (!SourceInfo.Exists)
			{
				throw new BuildException("Can't make an APK without file [{0}]", SourceFilename);
			}

			// different if destination doesn't exist
			FileInfo DestInfo = new FileInfo(DestFilename);
			if (!DestInfo.Exists)
			{
				return false;
			}

			// file lengths differ?
			if (SourceInfo.Length != DestInfo.Length)
			{
				return false;
			}

			using (FileStream SourceStream = new FileStream(SourceFilename, FileMode.Open, FileAccess.Read, FileShare.Read))
			using (FileStream DestStream = new FileStream(DestFilename, FileMode.Open, FileAccess.Read, FileShare.Read))
			{
				using(BinaryReader SourceReader = new BinaryReader(SourceStream))
				using(BinaryReader DestReader = new BinaryReader(DestStream))
				{
					bool bEOF = false;
					while (!bEOF)
					{
						byte[] SourceData = SourceReader.ReadBytes(32768);
						if (SourceData.Length == 0)
						{
							bEOF = true;
							break;
						}

						byte[] DestData = DestReader.ReadBytes(32768);
						if (!SourceData.SequenceEqual(DestData))
						{
							return false;
						}
					}
					return true;
				}
			}
			
		}

		private bool RequiresOBB(bool bDisallowPackageInAPK, string OBBLocation)
		{
			if (bDisallowPackageInAPK)
			{
				Log.TraceInformation("APK contains data.");
				return false;
			}
			else if (!String.IsNullOrEmpty(Environment.GetEnvironmentVariable("uebp_LOCAL_ROOT")))
			{
				Log.TraceInformation("On build machine.");
				return true;
			}
			else
			{
				Log.TraceInformation("Looking for OBB.");
				return File.Exists(OBBLocation);
			}
		}

		private bool CreateRunGradle(string GradlePath)
		{
			string RunGradleBatFilename = Path.Combine(GradlePath, "rungradle.bat");

			// check for an unused drive letter
			string UnusedDriveLetter = "";
			bool bFound = true;
			DriveInfo[] AllDrives = DriveInfo.GetDrives();
			for (char DriveLetter = 'Z'; DriveLetter >= 'A'; DriveLetter--)
			{
				UnusedDriveLetter = Char.ToString(DriveLetter) + ":";
				bFound = false;
				for (int DriveIndex = AllDrives.Length - 1; DriveIndex >= 0; DriveIndex--)
				{
					if (AllDrives[DriveIndex].Name.ToUpper().StartsWith(UnusedDriveLetter))
					{
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					break;
				}
			}

			if (bFound)
			{
				Log.TraceInformation("\nUnable to apply subst, using gradlew.bat directly (all drive letters in use!)");
				return false;
			}

			Log.TraceInformation("\nCreating rungradle.bat to work around commandline length limit (using unused drive letter {0})", UnusedDriveLetter);

			// make sure rungradle.bat isn't read-only
			if (File.Exists(RunGradleBatFilename))
			{
				FileAttributes Attribs = File.GetAttributes(RunGradleBatFilename);
				if (Attribs.HasFlag(FileAttributes.ReadOnly))
				{
					File.SetAttributes(RunGradleBatFilename, Attribs & ~FileAttributes.ReadOnly);
				}
			}

			// generate new rungradle.bat with an unused drive letter for subst
			string RunGradleBatText =
					"@echo off\n" +
					"setlocal\n" +
					"set GRADLEPATH=%~dp0\n" +
					"set GRADLE_CMD_LINE_ARGS=\n" +
					":setupArgs\n" +
					"if \"\"%1\"\"==\"\"\"\" goto doneStart\n" +
					"set GRADLE_CMD_LINE_ARGS=%GRADLE_CMD_LINE_ARGS% %1\n" +
					"shift\n" +
					"goto setupArgs\n\n" +
					":doneStart\n" +
					"subst " + UnusedDriveLetter + " \"%CD%\"\n" +
					"pushd " + UnusedDriveLetter + "\n" +
					"call \"%GRADLEPATH%\\gradlew.bat\" %GRADLE_CMD_LINE_ARGS%\n" +
					"set GRADLEERROR=%ERRORLEVEL%\n" +
					"popd\n" +
					"subst " + UnusedDriveLetter + " /d\n" +
					"exit /b %GRADLEERROR%\n";

			File.WriteAllText(RunGradleBatFilename, RunGradleBatText);

			return true;
		}

		private bool GradleEnabled()
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool bEnableGradle = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bEnableGradle", out bEnableGradle);
			return bEnableGradle;
		}
		private bool BundleEnabled()
		{
			if (ForceAPKGeneration)
			{
				return false;
			}
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			bool bEnableBundle = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bEnableBundle", out bEnableBundle);
			return bEnableBundle;
		}

		private bool IsLicenseAgreementValid()
		{
			string LicensePath = Environment.ExpandEnvironmentVariables("%ANDROID_HOME%/licenses");

			// directory must exist
			if (!Directory.Exists(LicensePath))
			{
				Log.TraceInformation("Directory doesn't exist {0}", LicensePath);
				return false;
			}

			// license file must exist
			string LicenseFilename = Path.Combine(LicensePath, "android-sdk-license");
			if (!File.Exists(LicenseFilename))
			{
				Log.TraceInformation("File doesn't exist {0}", LicenseFilename);
				return false;
			}

			// ignore contents of hash for now (Gradle will report if it isn't valid)
			return true;
		}

		private void GetMinTargetSDKVersions(AndroidToolChain ToolChain, string Arch, UnrealPluginLanguage UPL, string NDKArch, bool bEnableBundle, out int MinSDKVersion, out int TargetSDKVersion, out int NDKLevelInt)
		{
			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
			Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "MinSDKVersion", out MinSDKVersion);
			TargetSDKVersion = MinSDKVersion;
			Ini.GetInt32("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "TargetSDKVersion", out TargetSDKVersion);

			// Check for targetSDKOverride from UPL
			string TargetOverride = UPL.ProcessPluginNode(NDKArch, "targetSDKOverride", "");
			if (!String.IsNullOrEmpty(TargetOverride))
			{
				int OverrideInt = 0;
				if (int.TryParse(TargetOverride, out OverrideInt))
				{
					TargetSDKVersion = OverrideInt;
				}
			}

			if ((AndroidToolChain.BuildWithSanitizer(ProjectFile) != AndroidToolChain.ClangSanitizer.None) && (MinSDKVersion < 27))
			{
				MinSDKVersion = 27;
				Log.TraceInformation("Fixing minSdkVersion; requires minSdkVersion of {0} for Clang's Sanitizers", 27);
			}

			if (bEnableBundle && MinSDKVersion < MinimumSDKLevelForBundle)
			{
				MinSDKVersion = MinimumSDKLevelForBundle;
				Log.TraceInformation("Fixing minSdkVersion; requires minSdkVersion of {0} for App Bundle support", MinimumSDKLevelForBundle);
			}

			// Make sure minSdkVersion is at least 13 (need this for appcompat-v13 used by AndroidPermissions)
			// this may be changed by active plugins (Google Play Services 11.0.4 needs 14 for example)
			if (MinSDKVersion < MinimumSDKLevelForGradle)
			{
				MinSDKVersion = MinimumSDKLevelForGradle;
				Log.TraceInformation("Fixing minSdkVersion; requires minSdkVersion of {0} with Gradle based on active plugins", MinimumSDKLevelForGradle);
			}

			// 64-bit targets must be android-21 or higher
			NDKLevelInt = ToolChain.GetNdkApiLevelInt();
			if (NDKLevelInt < 21)
			{
				// 21 is requred for GL ES3.1
				NDKLevelInt = 21;
			}

			// fix up the MinSdkVersion
			if (NDKLevelInt > 19)
			{
				if (MinSDKVersion < 21)
				{
					MinSDKVersion = 21;
					Log.TraceInformation("Fixing minSdkVersion; NDK level above 19 requires minSdkVersion of 21 (arch={0})", Arch.Substring(1));
				}
			}

			if (TargetSDKVersion < MinSDKVersion)
			{
				TargetSDKVersion = MinSDKVersion;
			}
		}

		private void CreateGradlePropertiesFiles(string Arch, int MinSDKVersion, int TargetSDKVersion, string CompileSDKVersion, string BuildToolsVersion, string PackageName,
			string DestApkName, string NDKArch,	string UE4BuildFilesPath, string GameBuildFilesPath, string UE4BuildGradleAppPath, string UE4BuildPath, string UE4BuildGradlePath,
			bool bForDistribution, bool bIsEmbedded, List<string> OBBFiles)
		{
			// Create gradle.properties
			StringBuilder GradleProperties = new StringBuilder();

			int StoreVersion = GetStoreVersion(GetUE4Arch(NDKArch));
			string VersionDisplayName = GetVersionDisplayName(bIsEmbedded);

			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);

			bool bEnableUniversalAPK = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bEnableUniversalAPK", out bEnableUniversalAPK);

			GradleProperties.AppendLine("org.gradle.daemon=false");
			GradleProperties.AppendLine("org.gradle.jvmargs=-XX:MaxHeapSize=4096m -Xmx9216m");
			GradleProperties.AppendLine("android.injected.testOnly=false");
			GradleProperties.AppendLine("android.useAndroidX=true");
			GradleProperties.AppendLine("android.enableJetifier=true");
			GradleProperties.AppendLine(string.Format("COMPILE_SDK_VERSION={0}", CompileSDKVersion));
			GradleProperties.AppendLine(string.Format("BUILD_TOOLS_VERSION={0}", BuildToolsVersion));
			GradleProperties.AppendLine(string.Format("PACKAGE_NAME={0}", PackageName));
			GradleProperties.AppendLine(string.Format("MIN_SDK_VERSION={0}", MinSDKVersion.ToString()));
			GradleProperties.AppendLine(string.Format("TARGET_SDK_VERSION={0}", TargetSDKVersion.ToString()));
			GradleProperties.AppendLine(string.Format("STORE_VERSION={0}", StoreVersion.ToString()));
			GradleProperties.AppendLine(string.Format("VERSION_DISPLAY_NAME={0}", VersionDisplayName));

			if (DestApkName != null)
			{
				GradleProperties.AppendLine(string.Format("OUTPUT_PATH={0}", Path.GetDirectoryName(DestApkName).Replace("\\", "/")));
				GradleProperties.AppendLine(string.Format("OUTPUT_FILENAME={0}", Path.GetFileName(DestApkName)));

				string BundleFilename = Path.GetFileName(DestApkName).Replace(".apk", ".aab");
				GradleProperties.AppendLine(string.Format("OUTPUT_BUNDLEFILENAME={0}", BundleFilename));

				if (bEnableUniversalAPK)
				{
					string UniversalAPKFilename = Path.GetFileName(DestApkName).Replace(".apk", "_universal.apk");
					GradleProperties.AppendLine("OUTPUT_UNIVERSALFILENAME=" + UniversalAPKFilename);
				}
			}

			int OBBFileIndex = 0;
			GradleProperties.AppendLine(string.Format("OBB_FILECOUNT={0}", OBBFiles.Count));
			foreach (string OBBFile in OBBFiles)
			{
				GradleProperties.AppendLine(string.Format("OBB_FILE{0}={1}", OBBFileIndex++, OBBFile.Replace("\\", "/")));
			}

			GradleProperties.AppendLine("ANDROID_TOOLS_BUILD_GRADLE_VERSION={0}", ANDROID_TOOLS_BUILD_GRADLE_VERSION);
			GradleProperties.AppendLine("BUNDLETOOL_JAR=" + Path.GetFullPath(Path.Combine(UE4BuildFilesPath, "..", "Prebuilt", "bundletool", BUNDLETOOL_JAR)).Replace("\\", "/"));
			GradleProperties.AppendLine("GENUNIVERSALAPK_JAR=" + Path.GetFullPath(Path.Combine(UE4BuildFilesPath, "..", "Prebuilt", "GenUniversalAPK", "bin", "GenUniversalAPK.jar")).Replace("\\", "/"));

			// add any Gradle properties from UPL
			string GradlePropertiesUPL = UPL.ProcessPluginNode(NDKArch, "gradleProperties", "");
			GradleProperties.AppendLine(GradlePropertiesUPL);

			// Create abi.gradle
			StringBuilder ABIGradle = new StringBuilder();
			ABIGradle.AppendLine("android {");
			ABIGradle.AppendLine("\tdefaultConfig {");
			ABIGradle.AppendLine("\t\tndk {");
			ABIGradle.AppendLine(string.Format("\t\t\tabiFilter \"{0}\"", NDKArch));
			ABIGradle.AppendLine("\t\t}");
			ABIGradle.AppendLine("\t}");
			ABIGradle.AppendLine("}");
			string ABIGradleFilename = Path.Combine(UE4BuildGradleAppPath, "abi.gradle");
			File.WriteAllText(ABIGradleFilename, ABIGradle.ToString());

			StringBuilder GradleBuildAdditionsContent = new StringBuilder();
			GradleBuildAdditionsContent.AppendLine("apply from: 'aar-imports.gradle'");
			GradleBuildAdditionsContent.AppendLine("apply from: 'projects.gradle'");
			GradleBuildAdditionsContent.AppendLine("apply from: 'abi.gradle'");

			bool bEnableBundle, bBundleABISplit, bBundleLanguageSplit, bBundleDensitySplit;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bEnableBundle", out bEnableBundle);
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bBundleABISplit", out bBundleABISplit);
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bBundleLanguageSplit", out bBundleLanguageSplit);
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bBundleDensitySplit", out bBundleDensitySplit);

			GradleBuildAdditionsContent.AppendLine("android {");
			if (!ForceAPKGeneration && bEnableBundle)
			{
				GradleBuildAdditionsContent.AppendLine("\tbundle {");
				GradleBuildAdditionsContent.AppendLine("\t\tabi { enableSplit = " + (bBundleABISplit ? "true" : "false") + " }");
				GradleBuildAdditionsContent.AppendLine("\t\tlanguage { enableSplit = " + (bBundleLanguageSplit ? "true" : "false") + " }");
				GradleBuildAdditionsContent.AppendLine("\t\tdensity { enableSplit = " + (bBundleDensitySplit ? "true" : "false") + " }");
				GradleBuildAdditionsContent.AppendLine("\t}");
			}

			if (bForDistribution)
			{
				bool bDisableV2Signing = false;

				if (GetTargetOculusMobileDevices().Contains("Go"))
				{
					bDisableV2Signing = true;
					Log.TraceInformation("Disabling v2Signing for Oculus Go");
				}

				string KeyAlias, KeyStore, KeyStorePassword, KeyPassword;
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "KeyStore", out KeyStore);
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "KeyAlias", out KeyAlias);
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "KeyStorePassword", out KeyStorePassword);
				Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "KeyPassword", out KeyPassword);

				if (string.IsNullOrEmpty(KeyStore) || string.IsNullOrEmpty(KeyAlias) || string.IsNullOrEmpty(KeyStorePassword))
				{
					throw new BuildException("DistributionSigning settings are not all set. Check the DistributionSettings section in the Android tab of Project Settings");
				}

				if (string.IsNullOrEmpty(KeyPassword) || KeyPassword == "_sameaskeystore_")
				{
					KeyPassword = KeyStorePassword;
				}

				// Make sure the keystore file exists
				string KeyStoreFilename = Path.Combine(UE4BuildPath, KeyStore);
				if (!File.Exists(KeyStoreFilename))
				{
					throw new BuildException("Keystore file is missing. Check the DistributionSettings section in the Android tab of Project Settings");
				}

				GradleProperties.AppendLine(string.Format("STORE_FILE={0}", KeyStoreFilename.Replace("\\", "/")));
				GradleProperties.AppendLine(string.Format("STORE_PASSWORD={0}", KeyStorePassword));
				GradleProperties.AppendLine(string.Format("KEY_ALIAS={0}", KeyAlias));
				GradleProperties.AppendLine(string.Format("KEY_PASSWORD={0}", KeyPassword));

				GradleBuildAdditionsContent.AppendLine("\tsigningConfigs {");
				GradleBuildAdditionsContent.AppendLine("\t\trelease {");
				GradleBuildAdditionsContent.AppendLine(string.Format("\t\t\tstoreFile file('{0}')", KeyStoreFilename.Replace("\\", "/")));
				GradleBuildAdditionsContent.AppendLine(string.Format("\t\t\tstorePassword '{0}'", KeyStorePassword));
				GradleBuildAdditionsContent.AppendLine(string.Format("\t\t\tkeyAlias '{0}'", KeyAlias));
				GradleBuildAdditionsContent.AppendLine(string.Format("\t\t\tkeyPassword '{0}'", KeyPassword));
				if (bDisableV2Signing)
				{
					GradleBuildAdditionsContent.AppendLine("\t\t\tv2SigningEnabled false");
				}
				GradleBuildAdditionsContent.AppendLine("\t\t}");
				GradleBuildAdditionsContent.AppendLine("\t}");

				// Generate the Proguard file contents and write it
				string ProguardContents = GenerateProguard(NDKArch, UE4BuildFilesPath, GameBuildFilesPath);
				string ProguardFilename = Path.Combine(UE4BuildGradleAppPath, "proguard-rules.pro");
				SafeDeleteFile(ProguardFilename);
				File.WriteAllText(ProguardFilename, ProguardContents);
			}
			else
			{
				// empty just for Gradle not to complain
				GradleProperties.AppendLine("STORE_FILE=");
				GradleProperties.AppendLine("STORE_PASSWORD=");
				GradleProperties.AppendLine("KEY_ALIAS=");
				GradleProperties.AppendLine("KEY_PASSWORD=");

				// empty just for Gradle not to complain
				GradleBuildAdditionsContent.AppendLine("\tsigningConfigs {");
				GradleBuildAdditionsContent.AppendLine("\t\trelease {");
				GradleBuildAdditionsContent.AppendLine("\t\t}");
				GradleBuildAdditionsContent.AppendLine("\t}");
			}

			GradleBuildAdditionsContent.AppendLine("\tbuildTypes {");
			GradleBuildAdditionsContent.AppendLine("\t\trelease {");
			GradleBuildAdditionsContent.AppendLine("\t\t\tsigningConfig signingConfigs.release");
			if (GradlePropertiesUPL.Contains("DISABLE_MINIFY=1"))
			{
				GradleBuildAdditionsContent.AppendLine("\t\t\tminifyEnabled false");
			}
			else
			{
				GradleBuildAdditionsContent.AppendLine("\t\t\tminifyEnabled true");
			}
			if (GradlePropertiesUPL.Contains("DISABLE_PROGUARD=1"))
			{
				GradleBuildAdditionsContent.AppendLine("\t\t\tuseProguard false");
			}
			else
			{
				GradleBuildAdditionsContent.AppendLine("\t\t\tproguardFiles getDefaultProguardFile('proguard-android.txt'), 'proguard-rules.pro'");
			}
			GradleBuildAdditionsContent.AppendLine("\t\t}");
			GradleBuildAdditionsContent.AppendLine("\t\tdebug {");
			GradleBuildAdditionsContent.AppendLine("\t\t\tdebuggable true");
			GradleBuildAdditionsContent.AppendLine("\t\t}");
			GradleBuildAdditionsContent.AppendLine("\t}");
			GradleBuildAdditionsContent.AppendLine("}");

			// Add any UPL app buildGradleAdditions
			GradleBuildAdditionsContent.Append(UPL.ProcessPluginNode(NDKArch, "buildGradleAdditions", ""));

			string GradleBuildAdditionsFilename = Path.Combine(UE4BuildGradleAppPath, "buildAdditions.gradle");
			File.WriteAllText(GradleBuildAdditionsFilename, GradleBuildAdditionsContent.ToString());

			string GradlePropertiesFilename = Path.Combine(UE4BuildGradlePath, "gradle.properties");
			File.WriteAllText(GradlePropertiesFilename, GradleProperties.ToString());

			// Add lint if requested (note depreciation warnings can be suppressed with @SuppressWarnings("deprecation")
			string GradleBaseBuildAdditionsContents = "";
			bool bEnableLint = false;
			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bEnableLint", out bEnableLint);
			if (bEnableLint)
			{
				GradleBaseBuildAdditionsContents =
					"allprojects {\n" +
					"\ttasks.withType(JavaCompile) {\n" +
					"\t\toptions.compilerArgs << \"-Xlint:unchecked\" << \"-Xlint:deprecation\"\n" +
					"\t}\n" +
					"}\n\n";
			}

			// Create baseBuildAdditions.gradle from plugins baseBuildGradleAdditions
			string GradleBaseBuildAdditionsFilename = Path.Combine(UE4BuildGradlePath, "baseBuildAdditions.gradle");
			File.WriteAllText(GradleBaseBuildAdditionsFilename, UPL.ProcessPluginNode(NDKArch, "baseBuildGradleAdditions", GradleBaseBuildAdditionsContents));

			// Create buildscriptAdditions.gradle from plugins buildscriptGradleAdditions
			string GradleBuildScriptAdditionsFilename = Path.Combine(UE4BuildGradlePath, "buildscriptAdditions.gradle");
			File.WriteAllText(GradleBuildScriptAdditionsFilename, UPL.ProcessPluginNode(NDKArch, "buildscriptGradleAdditions", ""));
		}

		private void MakeApk(AndroidToolChain ToolChain, string ProjectName, TargetType InTargetType, string ProjectDirectory, string OutputPath, string EngineDirectory, bool bForDistribution, string CookFlavor, 
			UnrealTargetConfiguration Configuration, bool bMakeSeparateApks, bool bIncrementalPackage, bool bDisallowPackagingDataInApk, bool bDisallowExternalFilesDir, bool bSkipGradleBuild)
		{
			Log.TraceInformation("\n===={0}====PREPARING TO MAKE APK=================================================================", DateTime.Now.ToString());

			// Get list of all architecture and GPU targets for build
			List<string> Arches = ToolChain.GetAllArchitectures();
			List<string> GPUArchitectures = ToolChain.GetAllGPUArchitectures();

			// we do not need to really build an engine UE4Game.apk so short-circuit it
			if (ProjectName == "UE4Game" && OutputPath.Replace("\\", "/").Contains("/Engine/Binaries/Android/") && Path.GetFileNameWithoutExtension(OutputPath).StartsWith("UE4Game"))
			{
				if (!bSkipGradleBuild)
				{
					/*
					IEnumerable<Tuple<string, string>> TargetList = null;

					TargetList = from Arch in Arches
								from GPUArch in GPUArchitectures
								select Tuple.Create(Arch, GPUArch);

					string DestApkDirectory = Path.Combine(ProjectDirectory, "Binaries/Android");
					string ApkFilename = Path.GetFileNameWithoutExtension(OutputPath).Replace("UE4Game", ProjectName);

					foreach (Tuple<string, string> target in TargetList)
					{
						string Arch = target.Item1;
						string GPUArchitecture = target.Item2;
						string DestApkName = Path.Combine(DestApkDirectory, ApkFilename + ".apk");
						DestApkName = AndroidToolChain.InlineArchName(DestApkName, Arch, GPUArchitecture);

						// create a dummy APK if doesn't exist
						if (!File.Exists(DestApkName))
						{
							File.WriteAllText(DestApkName, "dummyfile");
						}
					}
					*/
				}
				Log.TraceInformation("APK generation not needed for project {0} with {1}", ProjectName, OutputPath);
				Log.TraceInformation("\n===={0}====COMPLETED MAKE APK=======================================================================", DateTime.Now.ToString());
				return;
			}

			if (!GradleEnabled())
			{
				throw new BuildException("Support for building APK without Gradle is depreciated; please update your project Engine.ini.");
			}

			if (UPL.GetLastError() != null)
			{
				throw new BuildException("Cannot make APK with UPL errors");
			}

			// make sure it is cached (clear unused warning)
			string EngineVersion = ReadEngineVersion();
			if (EngineVersion == null)
			{
				throw new BuildException("No engine version!");
			}

			SetMinimumSDKLevelForGradle();

			// Verify license agreement since we require Gradle
			if (!IsLicenseAgreementValid())
			{
				throw new BuildException("Android SDK license file not found.  Please agree to license in Android project settings in the editor.");
			}

			LogBuildSetup();

			// bundles disabled for launch-on
			bool bEnableBundle = BundleEnabled() && !bDisallowPackagingDataInApk;

			bool bIsBuildMachine = Environment.GetEnvironmentVariable("IsBuildMachine") == "1";

			// do this here so we'll stop early if there is a problem with the SDK API level (cached so later calls will return the same)
			string SDKAPILevel = GetSdkApiLevel(ToolChain);
			int SDKLevelInt = GetApiLevelInt(SDKAPILevel);
			string BuildToolsVersion = GetBuildToolsVersion();

			// cache some tools paths
			//string NDKBuildPath = Environment.ExpandEnvironmentVariables("%NDKROOT%/ndk-build" + (Utils.IsRunningOnMono ? "" : ".cmd"));
			//bool HasNDKPath = File.Exists(NDKBuildPath);

			// set up some directory info
			string IntermediateAndroidPath = Path.Combine(ProjectDirectory, "Intermediate", "Android");
			string UE4JavaFilePath = Path.Combine(ProjectDirectory, "Build", "Android", GetUE4JavaSrcPath());
			string UE4BuildFilesPath = GetUE4BuildFilePath(EngineDirectory);
			string UE4BuildFilesPath_NFL = GetUE4BuildFilePath(Path.Combine(EngineDirectory, "Restricted/NotForLicensees"));
			string UE4BuildFilesPath_NR = GetUE4BuildFilePath(Path.Combine(EngineDirectory, "Restricted/NoRedist"));
			string GameBuildFilesPath = Path.Combine(ProjectDirectory, "Build", "Android");
			string GameBuildFilesPath_NFL = Path.Combine(Path.Combine(ProjectDirectory, "Restricted/NotForLicensees"), "Build", "Android");
			string GameBuildFilesPath_NR = Path.Combine(Path.Combine(ProjectDirectory, "Restricted/NoRedist"), "Build", "Android");

			// get a list of unique NDK architectures enabled for build
			List<string> NDKArches = new List<string>();
			foreach (string Arch in Arches)
			{
				string NDKArch = GetNDKArch(Arch);
				if (!NDKArches.Contains(NDKArch))
				{
					NDKArches.Add(NDKArch);
				}
			}

			// force create from scratch if on build machine
			bool bCreateFromScratch = bIsBuildMachine;

			// see if last time matches the skipGradle setting
			string BuildTypeFilename = Path.Combine(IntermediateAndroidPath, "BuildType.txt");
			string BuildTypeID = bSkipGradleBuild ? "Embedded" : "Standalone";
			if (File.Exists(BuildTypeFilename))
			{
				string BuildTypeContents = File.ReadAllText(BuildTypeFilename);
				if (BuildTypeID != BuildTypeContents)
				{
					Log.TraceInformation("Build type changed, forcing clean");
					bCreateFromScratch = true;
				}
			}

			// check if the enabled plugins has changed
			string PluginListFilename = Path.Combine(IntermediateAndroidPath, "ActiveUPL.txt");
			string PluginListContents = ActiveUPLFiles.ToString();
			if (File.Exists(PluginListFilename))
			{
				string PreviousPluginListContents = File.ReadAllText(PluginListFilename);
				if (PluginListContents != PreviousPluginListContents)
				{
					Log.TraceInformation("Active UPL files changed, forcing clean");
					bCreateFromScratch = true;
				}
			}

			if (bCreateFromScratch)
			{
				Log.TraceInformation("Cleaning {0}", IntermediateAndroidPath);
				DeleteDirectory(IntermediateAndroidPath);
				Directory.CreateDirectory(IntermediateAndroidPath);
			}
			
			if (!System.IO.Directory.Exists(IntermediateAndroidPath))
			{
				System.IO.Directory.CreateDirectory(IntermediateAndroidPath);
			}

			// write enabled plugins list
			File.WriteAllText(PluginListFilename, PluginListContents);

			// write build type
			File.WriteAllText(BuildTypeFilename, BuildTypeID);

			// cache if we want data in the Apk
			bool bPackageDataInsideApk = bDisallowPackagingDataInApk ? false : GetPackageDataInsideApk();
			bool bDisableVerifyOBBOnStartUp = DisableVerifyOBBOnStartUp();
			bool bUseExternalFilesDir = UseExternalFilesDir(bDisallowExternalFilesDir);

			// Generate Java files
			string PackageName = GetPackageName(ProjectName);
			string TemplateDestinationBase = Path.Combine(ProjectDirectory, "Build", "Android", "src", PackageName.Replace('.', Path.DirectorySeparatorChar));
			MakeDirectoryIfRequired(TemplateDestinationBase);

			// We'll be writing the OBB data into the same location as the download service files
			string UE4OBBDataFileName = GetUE4JavaOBBDataFileName(TemplateDestinationBase);
			string UE4DownloadShimFileName = GetUE4JavaDownloadShimFileName(UE4JavaFilePath);

			// Template generated files
			string JavaTemplateSourceDir = GetUE4TemplateJavaSourceDir(EngineDirectory);
			IEnumerable<TemplateFile> templates = from template in Directory.EnumerateFiles(JavaTemplateSourceDir, "*.template")
							let RealName = Path.GetFileNameWithoutExtension(template)
							select new TemplateFile { SourceFile = template, DestinationFile = GetUE4TemplateJavaDestination(TemplateDestinationBase, RealName) };

			// Generate the OBB and Shim files here
			string ObbFileLocation = ProjectDirectory + "/Saved/StagedBuilds/Android" + CookFlavor + ".obb";
			string PatchFileLocation = ProjectDirectory + "/Saved/StagedBuilds/Android" + CookFlavor + ".patch.obb";
			List<string> RequiredOBBFiles = new List<String> { ObbFileLocation };
			if (File.Exists(PatchFileLocation))
			{
				RequiredOBBFiles.Add(PatchFileLocation);
			}

			// If we are not skipping Gradle build this is done per architecture so store version may be different
			if (bSkipGradleBuild)
			{
				// Generate the OBBData.java file if out of date (can skip rewriting it if packaging inside Apk in some cases)
				WriteJavaOBBDataFile(UE4OBBDataFileName, PackageName, RequiredOBBFiles, CookFlavor, bPackageDataInsideApk, Arches[0].Substring(1));
			}

			// Make sure any existing proguard file in project is NOT used (back it up)
			string ProjectBuildProguardFile = Path.Combine(GameBuildFilesPath, "proguard-project.txt");
			if (File.Exists(ProjectBuildProguardFile))
			{
				string ProjectBackupProguardFile = Path.Combine(GameBuildFilesPath, "proguard-project.backup");
				File.Move(ProjectBuildProguardFile, ProjectBackupProguardFile);
			}

			WriteJavaDownloadSupportFiles(UE4DownloadShimFileName, templates, new Dictionary<string, string>{
				{ "$$GameName$$", ProjectName },
				{ "$$PublicKey$$", GetPublicKey() }, 
				{ "$$PackageName$$",PackageName }
			});

			// Sometimes old files get left behind if things change, so we'll do a clean up pass
			foreach (string NDKArch in NDKArches)
			{
				string UE4BuildPath = Path.Combine(IntermediateAndroidPath, GetUE4Arch(NDKArch).Substring(1).Replace("-", "_"));

				string CleanUpBaseDir = Path.Combine(ProjectDirectory, "Build", "Android", "src");
				string ImmediateBaseDir = Path.Combine(UE4BuildPath, "src");
				IEnumerable<string> files = Directory.EnumerateFiles(CleanUpBaseDir, "*.java", SearchOption.AllDirectories);

				Log.TraceInformation("Cleaning up files based on template dir {0}", TemplateDestinationBase);

				// Make a set of files that are okay to clean up
				HashSet<string> cleanFiles = new HashSet<string>();
				cleanFiles.Add("OBBData.java");
				foreach (TemplateFile template in templates)
				{
					cleanFiles.Add(Path.GetFileName(template.DestinationFile));
				}

				foreach (string filename in files)
				{
					if (filename == UE4DownloadShimFileName)  // we always need the shim, and it'll get rewritten if needed anyway
						continue;

					string filePath = Path.GetDirectoryName(filename);  // grab the file's path
					if (filePath != TemplateDestinationBase)             // and check to make sure it isn't the same as the Template directory we calculated earlier
					{
						// Only delete the files in the cleanup set
						if (!cleanFiles.Contains(Path.GetFileName(filename)))
							continue;

						Log.TraceInformation("Cleaning up file {0}", filename);
						SafeDeleteFile(filename, false);

						// Check to see if this file also exists in our target destination, and if so nuke it too
						string DestFilename = Path.Combine(ImmediateBaseDir, Utils.MakePathRelativeTo(filename, CleanUpBaseDir));
						if (File.Exists(DestFilename))
						{
							Log.TraceInformation("Cleaning up file {0}", DestFilename);
							SafeDeleteFile(DestFilename, false);
						}
					}
				}

				// Directory clean up code (Build/Android/src)
				try
				{
					IEnumerable<string> BaseDirectories = Directory.EnumerateDirectories(CleanUpBaseDir, "*", SearchOption.AllDirectories).OrderByDescending(x => x);
					foreach (string directory in BaseDirectories)
					{
						if (Directory.Exists(directory) && Directory.GetFiles(directory, "*.*", SearchOption.AllDirectories).Count() == 0)
						{
							Log.TraceInformation("Cleaning Directory {0} as empty.", directory);
							Directory.Delete(directory, true);
						}
					}
				}
				catch (Exception)
				{
					// likely System.IO.DirectoryNotFoundException, ignore it
				}

				// Directory clean up code (Intermediate/APK/src)
				try
				{
					IEnumerable<string> ImmediateDirectories = Directory.EnumerateDirectories(ImmediateBaseDir, "*", SearchOption.AllDirectories).OrderByDescending(x => x);
					foreach (string directory in ImmediateDirectories)
					{
						if (Directory.Exists(directory) && Directory.GetFiles(directory, "*.*", SearchOption.AllDirectories).Count() == 0)
						{
							Log.TraceInformation("Cleaning Directory {0} as empty.", directory);
							Directory.Delete(directory, true);
						}
					}
				}
				catch (Exception)
				{
					// likely System.IO.DirectoryNotFoundException, ignore it
				}
			}

			// check to see if any "meta information" is newer than last time we build
			string TemplatesHashCode = GenerateTemplatesHashCode(EngineDirectory);
			string CurrentBuildSettings = GetAllBuildSettings(ToolChain, bForDistribution, bMakeSeparateApks, bPackageDataInsideApk, bDisableVerifyOBBOnStartUp, bUseExternalFilesDir, TemplatesHashCode);
			string BuildSettingsCacheFile = Path.Combine(IntermediateAndroidPath, "UEBuildSettings.txt");

			// Architecture remapping
			Dictionary<string, string> ArchRemapping = new Dictionary<string, string>();
			ArchRemapping.Add("armeabi-v7a", "armv7");
			ArchRemapping.Add("arm64-v8a", "arm64");
			ArchRemapping.Add("x86", "x86");
			ArchRemapping.Add("x86_64", "x64");

			// do we match previous build settings?
			bool bBuildSettingsMatch = true;

			// get application name and whether it changed, needing to force repackage
			string ApplicationDisplayName;
			if (CheckApplicationName(Path.Combine(IntermediateAndroidPath, ArchRemapping[NDKArches[0]]), ProjectName, out ApplicationDisplayName))
			{
				bBuildSettingsMatch = false;
				Log.TraceInformation("Application display name is different than last build, forcing repackage.");
			}

			// if the manifest matches, look at other settings stored in a file
			if (bBuildSettingsMatch)
			{
				if (File.Exists(BuildSettingsCacheFile))
				{
					string PreviousBuildSettings = File.ReadAllText(BuildSettingsCacheFile);
					if (PreviousBuildSettings != CurrentBuildSettings)
					{
						bBuildSettingsMatch = false;
						Log.TraceInformation("Previous .apk file(s) were made with different build settings, forcing repackage.");
					}
				}
			}

			// only check input dependencies if the build settings already match (if we don't run gradle, there is no Apk file to check against)
			if (bBuildSettingsMatch && !bSkipGradleBuild)
			{
				// check if so's are up to date against various inputs
				List<string> JavaFiles = new List<string>{
                                                    UE4OBBDataFileName,
                                                    UE4DownloadShimFileName
                                                };
				// Add the generated files too
				JavaFiles.AddRange(from t in templates select t.SourceFile);
				JavaFiles.AddRange(from t in templates select t.DestinationFile);

				bBuildSettingsMatch = CheckDependencies(ToolChain, ProjectName, ProjectDirectory, UE4BuildFilesPath, GameBuildFilesPath,
					EngineDirectory, JavaFiles, CookFlavor, OutputPath, bMakeSeparateApks, bPackageDataInsideApk);

			}

			// Initialize UPL contexts for each architecture enabled
			UPL.Init(NDKArches, bForDistribution, EngineDirectory, IntermediateAndroidPath, ProjectDirectory, Configuration.ToString(), bSkipGradleBuild, bPerArchBuildDir:true, ArchRemapping:ArchRemapping);

			IEnumerable<Tuple<string, string, string>> BuildList = null;

			bool bRequiresOBB = RequiresOBB(bDisallowPackagingDataInApk, ObbFileLocation);
			if (!bBuildSettingsMatch)
			{
				BuildList = from Arch in Arches
							from GPUArch in GPUArchitectures
							let manifest = GenerateManifest(ToolChain, ProjectName, InTargetType, EngineDirectory, bForDistribution, bPackageDataInsideApk, GameBuildFilesPath, bRequiresOBB, bDisableVerifyOBBOnStartUp, Arch, GPUArch, CookFlavor, bUseExternalFilesDir, Configuration.ToString(), SDKLevelInt, bSkipGradleBuild, bEnableBundle)
							select Tuple.Create(Arch, GPUArch, manifest);
			}
			else
			{
				BuildList = from Arch in Arches
							from GPUArch in GPUArchitectures
							let manifestFile = Path.Combine(IntermediateAndroidPath, Arch + (GPUArch.Length > 0 ? ("_" + GPUArch.Substring(1)) : "") + "_AndroidManifest.xml")
							let manifest = GenerateManifest(ToolChain, ProjectName, InTargetType, EngineDirectory, bForDistribution, bPackageDataInsideApk, GameBuildFilesPath, bRequiresOBB, bDisableVerifyOBBOnStartUp, Arch, GPUArch, CookFlavor, bUseExternalFilesDir, Configuration.ToString(), SDKLevelInt, bSkipGradleBuild, bEnableBundle)
							let OldManifest = File.Exists(manifestFile) ? File.ReadAllText(manifestFile) : ""
							where manifest != OldManifest
							select Tuple.Create(Arch, GPUArch, manifest);
			}

			// Now we have to spin over all the arch/gpu combinations to make sure they all match
			int BuildListComboTotal = BuildList.Count();
			if (BuildListComboTotal == 0)
			{
				Log.TraceInformation("Output .apk file(s) are up to date (dependencies and build settings are up to date)");
				return;
			}

			// at this point, we can write out the cached build settings to compare for a next build
			File.WriteAllText(BuildSettingsCacheFile, CurrentBuildSettings);

			// make up a dictionary of strings to replace in xml files (strings.xml)
			Dictionary<string, string> Replacements = new Dictionary<string, string>();
			Replacements.Add("${EXECUTABLE_NAME}", ApplicationDisplayName);
			Replacements.Add("${PY_VISUALIZER_PATH}", Path.GetFullPath(Path.Combine(EngineDirectory, "Extras", "LLDBDataFormatters", "UE4DataFormatters_2ByteChars.py")));

			// steps run for each build combination (note: there should only be one GPU in future)
			foreach (Tuple<string, string, string> build in BuildList)
			{
				string Arch = build.Item1;
				string GPUArchitecture = build.Item2;
				string Manifest = build.Item3;
				string NDKArch = GetNDKArch(Arch);

				Log.TraceInformation("\n===={0}====PREPARING NATIVE CODE====={1}============================================================", DateTime.Now.ToString(), Arch);

				string UE4BuildPath = Path.Combine(IntermediateAndroidPath, Arch.Substring(1).Replace("-", "_"));

				// If we are packaging for Amazon then we need to copy the  file to the correct location
				Log.TraceInformation("bPackageDataInsideApk = {0}", bPackageDataInsideApk);
				if (bPackageDataInsideApk)
				{
					Log.TraceInformation("Obb location {0}", ObbFileLocation);
					string ObbFileDestination = UE4BuildPath + "/assets";
					Log.TraceInformation("Obb destination location {0}", ObbFileDestination);
					if (File.Exists(ObbFileLocation))
					{
						Directory.CreateDirectory(UE4BuildPath);
						Directory.CreateDirectory(ObbFileDestination);
						Log.TraceInformation("Obb file exists...");
						string DestFileName = Path.Combine(ObbFileDestination, "main.obb.png"); // Need a rename to turn off compression
						string SrcFileName = ObbFileLocation;
						CopyIfDifferent(SrcFileName, DestFileName, true, false);
					}
				}
				else // try to remove the file it we aren't packaging inside the APK
				{
					string ObbFileDestination = UE4BuildPath + "/assets";
					string DestFileName = Path.Combine(ObbFileDestination, "main.obb.png");
					SafeDeleteFile(DestFileName);
				}

				// See if we need to stage a UE4CommandLine.txt file in assets
				string CommandLineSourceFileName = Path.Combine(Path.GetDirectoryName(ObbFileLocation), Path.GetFileNameWithoutExtension(ObbFileLocation), "UE4CommandLine.txt");
				string CommandLineDestFileName = Path.Combine(UE4BuildPath, "assets", "UE4CommandLine.txt");
				if (File.Exists(CommandLineSourceFileName))
				{
					Directory.CreateDirectory(UE4BuildPath);
					Directory.CreateDirectory(Path.Combine(UE4BuildPath, "assets"));
					Console.WriteLine("UE4CommandLine.txt exists...");
					CopyIfDifferent(CommandLineSourceFileName, CommandLineDestFileName, true, true);
				}
				else // try to remove the file if we aren't packaging one
				{
					SafeDeleteFile(CommandLineDestFileName);
				}

				//Copy build files to the intermediate folder in this order (later overrides earlier):
				//	- Shared Engine
				//  - Shared Engine NoRedist (for Epic secret files)
				//  - Game
				//  - Game NoRedist (for Epic secret files)
				CopyFileDirectory(UE4BuildFilesPath, UE4BuildPath, Replacements);
				CopyFileDirectory(UE4BuildFilesPath_NFL, UE4BuildPath, Replacements);
				CopyFileDirectory(UE4BuildFilesPath_NR, UE4BuildPath, Replacements);
				CopyFileDirectory(GameBuildFilesPath, UE4BuildPath, Replacements);
				CopyFileDirectory(GameBuildFilesPath_NFL, UE4BuildPath, Replacements);
				CopyFileDirectory(GameBuildFilesPath_NR, UE4BuildPath, Replacements);

				// Parse Gradle filters (may have been replaced by above copies)
				ParseFilterFile(Path.Combine(UE4BuildPath, "GradleFilter.txt"));

				//Generate Gradle AAR dependencies
				GenerateGradleAARImports(EngineDirectory, UE4BuildPath, NDKArches);

				//Now validate GooglePlay app_id if enabled
				ValidateGooglePlay(UE4BuildPath);

				//determine which orientation requirements this app has
				bool bNeedLandscape = false;
				bool bNeedPortrait = false;
				DetermineScreenOrientationRequirements(NDKArches[0], out bNeedPortrait, out bNeedLandscape);

				//Now keep the splash screen images matching orientation requested
				PickSplashScreenOrientation(UE4BuildPath, bNeedPortrait, bNeedLandscape);

				//Now package the app based on Daydream packaging settings 
				PackageForDaydream(UE4BuildPath);

				//Similarly, keep only the downloader screen image matching the orientation requested
				PickDownloaderScreenOrientation(UE4BuildPath, bNeedPortrait, bNeedLandscape);

				// use Gradle for compile/package
				string UE4BuildGradlePath = Path.Combine(UE4BuildPath, "gradle");
				string UE4BuildGradleAppPath = Path.Combine(UE4BuildGradlePath, "app");
				string UE4BuildGradleMainPath = Path.Combine(UE4BuildGradleAppPath, "src", "main");
				string CompileSDKVersion = SDKAPILevel.Replace("android-", "");

				// Write the manifest to the correct locations (cache and real)
				String ManifestFile = Path.Combine(IntermediateAndroidPath, Arch + (GPUArchitecture.Length > 0 ? ("_" + GPUArchitecture.Substring(1)) : "") + "_AndroidManifest.xml");
				File.WriteAllText(ManifestFile, Manifest);
				ManifestFile = Path.Combine(UE4BuildPath, "AndroidManifest.xml");
				File.WriteAllText(ManifestFile, Manifest);

				// copy prebuild plugin files
				UPL.ProcessPluginNode(NDKArch, "prebuildCopies", "");

				XDocument AdditionalBuildPathFilesDoc = new XDocument(new XElement("files"));
				UPL.ProcessPluginNode(NDKArch, "additionalBuildPathFiles", "", ref AdditionalBuildPathFilesDoc);

				// Generate the OBBData.java file since different architectures may have different store version
				UE4OBBDataFileName = GetUE4JavaOBBDataFileName(Path.Combine(UE4BuildPath, "src", PackageName.Replace('.', Path.DirectorySeparatorChar)));
				WriteJavaOBBDataFile(UE4OBBDataFileName, PackageName, RequiredOBBFiles, CookFlavor, bPackageDataInsideApk, Arch);

				// update GameActivity.java and GameApplication.java if out of date
				UpdateGameActivity(Arch, NDKArch, EngineDirectory, UE4BuildPath);
				UpdateGameApplication(Arch, NDKArch, EngineDirectory, UE4BuildPath);

				// we don't actually need the SO for the bSkipGradleBuild case
				string FinalSOName = null;
				string DestApkDirectory = Path.Combine(ProjectDirectory, "Binaries/Android");
				string DestApkName = null;
				if (bSkipGradleBuild)
				{
					FinalSOName = OutputPath;
					if (!File.Exists(FinalSOName))
					{
						Log.TraceWarning("Did not find compiled .so [{0}]", FinalSOName);
					}
				}
				else
				{
					string SourceSOName = AndroidToolChain.InlineArchName(OutputPath, Arch, GPUArchitecture);
					// if the source binary was UE4Game, replace it with the new project name, when re-packaging a binary only build
					string ApkFilename = Path.GetFileNameWithoutExtension(OutputPath).Replace("UE4Game", ProjectName);
					DestApkName = Path.Combine(DestApkDirectory, ApkFilename + ".apk");

					// As we are always making seperate APKs we need to put the architecture into the name
					DestApkName = AndroidToolChain.InlineArchName(DestApkName, Arch, GPUArchitecture);

					if (!File.Exists(SourceSOName))
					{
						throw new BuildException("Can't make an APK without the compiled .so [{0}]", SourceSOName);
					}
					if (!Directory.Exists(UE4BuildPath + "/jni"))
					{
						throw new BuildException("Can't make an APK without the jni directory [{0}/jni]", UE4BuildFilesPath);
					}

					string JniDir = UE4BuildPath + "/jni/" + NDKArch;
					FinalSOName = JniDir + "/libUE4.so";

					// clear out libs directory like ndk-build would have
					string LibsDir = Path.Combine(UE4BuildPath, "libs");
					DeleteDirectory(LibsDir);
					MakeDirectoryIfRequired(LibsDir);

					// check to see if libUE4.so needs to be copied
					if (BuildListComboTotal > 1 || FilesAreDifferent(SourceSOName, FinalSOName))
					{
						Log.TraceInformation("\nCopying new .so {0} file to jni folder...", SourceSOName);
						Directory.CreateDirectory(JniDir);
						// copy the binary to the standard .so location
						File.Copy(SourceSOName, FinalSOName, true);
						File.SetLastWriteTimeUtc(FinalSOName, File.GetLastWriteTimeUtc(SourceSOName));
					}

					// remove any read only flags
					FileInfo DestFileInfo = new FileInfo(FinalSOName);
					DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
					File.SetLastWriteTimeUtc(FinalSOName, File.GetLastWriteTimeUtc(SourceSOName));
				}

				// after ndk-build is called, we can now copy in the stl .so (ndk-build deletes old files)
				// copy libc++_shared.so to library
				CopySTL(ToolChain, UE4BuildPath, Arch, NDKArch, bForDistribution);
				CopyGfxDebugger(UE4BuildPath, Arch, NDKArch);
				CopyVulkanValidationLayers(UE4BuildPath, Arch, NDKArch, Configuration.ToString());
				AndroidToolChain.ClangSanitizer Sanitizer = AndroidToolChain.BuildWithSanitizer(ProjectFile);
				if (Sanitizer != AndroidToolChain.ClangSanitizer.None && Sanitizer != AndroidToolChain.ClangSanitizer.HwAddress)
				{
					CopyClangSanitizerLib(UE4BuildPath, Arch, NDKArch, Sanitizer);
				}

				// copy postbuild plugin files
				UPL.ProcessPluginNode(NDKArch, "resourceCopies", "");

				CreateAdditonalBuildPathFiles(NDKArch, UE4BuildPath, AdditionalBuildPathFilesDoc);

				Log.TraceInformation("\n===={0}====PERFORMING FINAL APK PACKAGE OPERATION====={1}===========================================", DateTime.Now.ToString(), Arch);

				// check if any plugins want to increase the required compile SDK version
				string CompileSDKMin = UPL.ProcessPluginNode(NDKArch, "minimumSDKAPI", "");
				if (CompileSDKMin != "")
				{
					int CompileSDKVersionInt;
					if (!int.TryParse(CompileSDKVersion, out CompileSDKVersionInt))
					{
						CompileSDKVersionInt = 23;
					}

					bool bUpdatedCompileSDK = false;
					string[] CompileSDKLines = CompileSDKMin.Split(new[] { "\r\n", "\r", "\n" }, StringSplitOptions.None);
					foreach (string CompileLine in CompileSDKLines)
					{
						//string VersionString = CompileLine.Replace("android-", "");
						int VersionInt;
						if (int.TryParse(CompileLine, out VersionInt))
						{
							if (VersionInt > CompileSDKVersionInt)
							{
								CompileSDKVersionInt = VersionInt;
								bUpdatedCompileSDK = true;
							}
						}
					}

					if (bUpdatedCompileSDK)
					{
						CompileSDKVersion = CompileSDKVersionInt.ToString();
						Log.TraceInformation("Building Java with SDK API Level 'android-{0}' due to enabled plugin requirements", CompileSDKVersion);
					}
				}

				// stage files into gradle app directory
				string GradleManifest = Path.Combine(UE4BuildGradleMainPath, "AndroidManifest.xml");
				MakeDirectoryIfRequired(GradleManifest);
				CopyIfDifferent(Path.Combine(UE4BuildPath, "AndroidManifest.xml"), GradleManifest, true, true);

				string[] Excludes;
				switch (NDKArch)
				{
					default:
					case "armeabi-v7a":
						Excludes = new string[] { "arm64-v8a", "x86", "x86-64" };
						break;

					case "arm64-v8a":
						Excludes = new string[] { "armeabi-v7a", "x86", "x86-64" };
						break;

					case "x86":
						Excludes = new string[] { "armeabi-v7a", "arm64-v8a", "x86-64" };
						break;

					case "x86_64":
						Excludes = new string[] { "armeabi-v7a", "arm64-v8a", "x86" };
						break;
				}

				CleanCopyDirectory(Path.Combine(UE4BuildPath, "jni"), Path.Combine(UE4BuildGradleMainPath, "jniLibs"), Excludes);  // has debug symbols
				CleanCopyDirectory(Path.Combine(UE4BuildPath, "libs"), Path.Combine(UE4BuildGradleMainPath, "libs"), Excludes);
				if (Sanitizer != AndroidToolChain.ClangSanitizer.None && Sanitizer != AndroidToolChain.ClangSanitizer.HwAddress)
				{
					CleanCopyDirectory(Path.Combine(UE4BuildPath, "resources"), Path.Combine(UE4BuildGradleMainPath, "resources"), Excludes);
				}

				CleanCopyDirectory(Path.Combine(UE4BuildPath, "assets"), Path.Combine(UE4BuildGradleMainPath, "assets"));
				CleanCopyDirectory(Path.Combine(UE4BuildPath, "res"), Path.Combine(UE4BuildGradleMainPath, "res"));
				CleanCopyDirectory(Path.Combine(UE4BuildPath, "src"), Path.Combine(UE4BuildGradleMainPath, "java"));

				// do any plugin requested copies
				UPL.ProcessPluginNode(NDKArch, "gradleCopies", "");

				// get min and target SDK versions
				int MinSDKVersion = 0;
				int TargetSDKVersion = 0;
				int NDKLevelInt = 0;
				GetMinTargetSDKVersions(ToolChain, Arch, UPL, NDKArch, bEnableBundle, out MinSDKVersion, out TargetSDKVersion, out NDKLevelInt);
					
				// move JavaLibs into subprojects
				string JavaLibsDir = Path.Combine(UE4BuildPath, "JavaLibs");
				PrepareJavaLibsForGradle(JavaLibsDir, UE4BuildGradlePath, MinSDKVersion.ToString(), TargetSDKVersion.ToString(), CompileSDKVersion, BuildToolsVersion, NDKArch);

				// Create local.properties
				String LocalPropertiesFilename = Path.Combine(UE4BuildGradlePath, "local.properties");
				StringBuilder LocalProperties = new StringBuilder();
				LocalProperties.AppendLine(string.Format("ndk.dir={0}", Environment.GetEnvironmentVariable("NDKROOT").Replace("\\", "/")));
				LocalProperties.AppendLine(string.Format("sdk.dir={0}", Environment.GetEnvironmentVariable("ANDROID_HOME").Replace("\\", "/")));
				File.WriteAllText(LocalPropertiesFilename, LocalProperties.ToString());

				CreateGradlePropertiesFiles(Arch, MinSDKVersion, TargetSDKVersion, CompileSDKVersion, BuildToolsVersion, PackageName, DestApkName, NDKArch,
					UE4BuildFilesPath, GameBuildFilesPath, UE4BuildGradleAppPath, UE4BuildPath, UE4BuildGradlePath, bForDistribution, bSkipGradleBuild, RequiredOBBFiles);

				if (!bSkipGradleBuild)
				{
					string GradleScriptPath = Path.Combine(UE4BuildGradlePath, "gradlew");
					if (Utils.IsRunningOnMono)
					{
						// fix permissions for Mac/Linux
						RunCommandLineProgramWithException(UE4BuildGradlePath, "/bin/sh", string.Format("-c 'chmod 0755 \"{0}\"'", GradleScriptPath.Replace("'", "'\"'\"'")), "Fix gradlew permissions");
					}
					else
					{
						if (CreateRunGradle(UE4BuildGradlePath))
						{
							GradleScriptPath = Path.Combine(UE4BuildGradlePath, "rungradle.bat");
						}
						else
						{
							GradleScriptPath = Path.Combine(UE4BuildGradlePath, "gradlew.bat");
						}
					}

					if (!bEnableBundle)
					{
						string GradleBuildType = bForDistribution ? ":app:assembleRelease" : ":app:assembleDebug";

						// collect optional additional Gradle parameters from plugins
						string GradleOptions = UPL.ProcessPluginNode(NDKArch, "gradleParameters", GradleBuildType); //  "--stacktrace --debug " + GradleBuildType);
						string GradleSecondCallOptions = UPL.ProcessPluginNode(NDKArch, "gradleSecondCallParameters", "");

						// check for Android Studio project, call Gradle if doesn't exist (assume user will build with Android Studio)
						string GradleAppImlFilename = Path.Combine(UE4BuildGradlePath, "app.iml");
						if (!File.Exists(GradleAppImlFilename))
						{
							// make sure destination exists
							Directory.CreateDirectory(Path.GetDirectoryName(DestApkName));

							// Use gradle to build the .apk file
							string ShellExecutable = Utils.IsRunningOnMono ? "/bin/sh" : "cmd.exe";
							string ShellParametersBegin = Utils.IsRunningOnMono ? "-c '" : "/c ";
							string ShellParametersEnd = Utils.IsRunningOnMono ? "'" : "";
							RunCommandLineProgramWithExceptionAndFiltering(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleOptions + ShellParametersEnd, "Making .apk with Gradle...");

							if (GradleSecondCallOptions != "")
							{
								RunCommandLineProgramWithExceptionAndFiltering(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleSecondCallOptions + ShellParametersEnd, "Additional Gradle steps...");
							}

							// For build machine run a clean afterward to clean up intermediate files (does not remove final APK)
							if (bIsBuildMachine)
							{
								//GradleOptions = "tasks --all";
								//RunCommandLineProgramWithException(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleOptions + ShellParametersEnd, "Listing all tasks...");

								GradleOptions = "clean";
								RunCommandLineProgramWithExceptionAndFiltering(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleOptions + ShellParametersEnd, "Cleaning Gradle intermediates...");
							}
						}
						else
						{
							Log.TraceInformation("=============================================================================================");
							Log.TraceInformation("Android Studio project found, skipping Gradle; complete creation of APK in Android Studio!!!!");
							Log.TraceInformation("Delete '{0} if you want to have UnrealBuildTool run Gradle for future runs.", GradleAppImlFilename);
							Log.TraceInformation("=============================================================================================");
						}
					}
				}

				bool bBuildWithHiddenSymbolVisibility = false;
				bool bSaveSymbols = false;

				ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
				Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bBuildWithHiddenSymbolVisibility", out bBuildWithHiddenSymbolVisibility);
				Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSaveSymbols", out bSaveSymbols);
				bSaveSymbols = true;
				if (bSaveSymbols || (Configuration == UnrealTargetConfiguration.Shipping && bBuildWithHiddenSymbolVisibility))
				{
					// Copy .so with symbols to 
					int StoreVersion = GetStoreVersion(Arch);
					string SymbolSODirectory = Path.Combine(DestApkDirectory, ProjectName + "_Symbols_v" + StoreVersion + "/" + ProjectName + Arch + GPUArchitecture);
					string SymbolifiedSOPath = Path.Combine(SymbolSODirectory, Path.GetFileName(FinalSOName));
					MakeDirectoryIfRequired(SymbolifiedSOPath);
					Log.TraceInformation("Writing symbols to {0}", SymbolifiedSOPath);

					File.Copy(FinalSOName, SymbolifiedSOPath, true);
				}
			}

			// Deal with generating an App Bundle
			if (bEnableBundle && !bSkipGradleBuild)
			{
				bool bCombinedBundleOK = true;

				// try to make a combined Gradle project for all architectures (may fail if incompatible configuration)
				string UE4GradleDest = Path.Combine(IntermediateAndroidPath, "gradle");

				// start fresh each time for now
				DeleteDirectory(UE4GradleDest);

				// make sure destination exists
				Directory.CreateDirectory(UE4GradleDest);

				String ABIFilter = "";

				// loop through and merge the different architecture gradle directories
				foreach (Tuple<string, string, string> build in BuildList)
				{
					string Arch = build.Item1;
					string GPUArchitecture = build.Item2;
					string Manifest = build.Item3;
					string NDKArch = GetNDKArch(Arch);

					string UE4BuildPath = Path.Combine(IntermediateAndroidPath, Arch.Substring(1).Replace("-", "_"));
					string UE4BuildGradlePath = Path.Combine(UE4BuildPath, "gradle");

					if (!Directory.Exists(UE4BuildGradlePath))
					{
						Log.TraceInformation("Source directory missing: {0}", UE4BuildGradlePath);
						bCombinedBundleOK = false;
						break;
					}

					ABIFilter += ", \"" + NDKArch + "\"";

					string[] SourceFiles = Directory.GetFiles(UE4BuildGradlePath, "*.*", SearchOption.AllDirectories);
					foreach (string Filename in SourceFiles)
					{
						// make the dest filename with the same structure as it was in SourceDir
						string DestFilename = Path.Combine(UE4GradleDest, Utils.MakePathRelativeTo(Filename, UE4BuildGradlePath));

						// skip the build directories
						string Workname = Filename.Replace("\\", "/");
						string DirectoryName = Path.GetDirectoryName(Filename);
						if (DirectoryName.Contains("build") || Workname.Contains("/."))
						{
							continue;
						}

						// if destination doesn't exist, just copy it
						if (!File.Exists(DestFilename))
						{
							string DestSubdir = Path.GetDirectoryName(DestFilename);
							if (!Directory.Exists(DestSubdir))
							{
								Directory.CreateDirectory(DestSubdir);
							}

							// copy it
							File.Copy(Filename, DestFilename);

							// preserve timestamp and clear read-only flags
							FileInfo DestFileInfo = new FileInfo(DestFilename);
							DestFileInfo.Attributes = DestFileInfo.Attributes & ~FileAttributes.ReadOnly;
							File.SetLastWriteTimeUtc(DestFilename, File.GetLastWriteTimeUtc(Filename));

							Log.TraceInformation("Copied file {0}.", DestFilename);
							continue;
						}

						if (FilesAreIdentical(Filename, DestFilename))
						{
							continue;
						}

						// ignore abi.gradle, we're going to generate a new one
						if (Filename.EndsWith("abi.gradle"))
						{
							continue;
						}

						// ignore OBBData.java, we won't use it
						if (Filename.EndsWith("OBBData.java"))
						{
							continue;
						}

						// deal with AndroidManifest.xml
						if (Filename.EndsWith("AndroidManifest.xml"))
						{
							// only allowed to differ by versionCode
							string[] SourceManifest = File.ReadAllLines(Filename);
							string[] DestManifest = File.ReadAllLines(DestFilename);

							if (SourceManifest.Length == DestManifest.Length)
							{
								bool bDiffers = false;
								for (int Index = 0; Index < SourceManifest.Length; Index++)
								{
									if (SourceManifest[Index] == DestManifest[Index])
									{
										continue;
									}

									int SourceVersionIndex = SourceManifest[Index].IndexOf("android:versionCode=");
									if (SourceVersionIndex < 0)
									{
										bDiffers = true;
										break;
									}

									int DestVersionIndex = DestManifest[Index].IndexOf("android:versionCode=");
									if (DestVersionIndex < 0)
									{
										bDiffers = true;
										break;
									}

									int SourceVersionIndex2 = SourceManifest[Index].Substring(SourceVersionIndex + 22).IndexOf("\"");
									string FixedSource = SourceManifest[Index].Substring(0, SourceVersionIndex + 21) + SourceManifest[Index].Substring(SourceVersionIndex + 22 + SourceVersionIndex2);

									int DestVersionIndex2 = DestManifest[Index].Substring(DestVersionIndex + 22).IndexOf("\"");
									string FixedDest = SourceManifest[Index].Substring(0, DestVersionIndex + 21) + DestManifest[Index].Substring(DestVersionIndex + 22 + DestVersionIndex2);

									if (FixedSource != FixedDest)
									{
										bDiffers = true;
										break;
									}
								}
								if (!bDiffers)
								{
									continue;
								}
							}

							// differed too much
							bCombinedBundleOK = false;
							Log.TraceInformation("AndroidManifest.xml files differ too much to combine for single AAB: '{0}' != '{1}'", Filename, DestFilename);
							break;
						}

						// deal with buildAdditions.gradle
						if (Filename.EndsWith("buildAdditions.gradle"))
						{
							// allow store filepath to differ
							string[] SourceProperties = File.ReadAllLines(Filename);
							string[] DestProperties = File.ReadAllLines(DestFilename);

							if (SourceProperties.Length == DestProperties.Length)
							{
								bool bDiffers = false;
								for (int Index = 0; Index < SourceProperties.Length; Index++)
								{
									if (SourceProperties[Index] == DestProperties[Index])
									{
										continue;
									}

									if (SourceProperties[Index].Contains("storeFile file(") && DestProperties[Index].Contains("storeFile file("))
									{
										continue;
									}

									bDiffers = true;
									break;
								}
								if (!bDiffers)
								{
									continue;
								}
							}

							// differed too much
							bCombinedBundleOK = false;
							Log.TraceInformation("buildAdditions.gradle files differ too much to combine for single AAB: '{0}' != '{1}'", Filename, DestFilename);
							break;
						}

						// deal with gradle.properties
						if (Filename.EndsWith("gradle.properties"))
						{
							// allow STORE_VERSION and OUTPUT_FILENAME to differ
							// only allowed to differ by versionCode
							string[] SourceProperties = File.ReadAllLines(Filename);
							string[] DestProperties = File.ReadAllLines(DestFilename);

							if (SourceProperties.Length == DestProperties.Length)
							{
								bool bDiffers = false;
								for (int Index = 0; Index < SourceProperties.Length; Index++)
								{
									if (SourceProperties[Index] == DestProperties[Index])
									{
										continue;
									}

									if (SourceProperties[Index].StartsWith("STORE_VERSION=") && DestProperties[Index].StartsWith("STORE_VERSION="))
									{
										continue;
									}

									if (SourceProperties[Index].StartsWith("STORE_FILE=") && DestProperties[Index].StartsWith("STORE_FILE="))
									{
										continue;
									}

									if (SourceProperties[Index].StartsWith("OUTPUT_FILENAME=") && DestProperties[Index].StartsWith("OUTPUT_FILENAME="))
									{
										continue;
									}

									if (SourceProperties[Index].StartsWith("OUTPUT_BUNDLEFILENAME=") && DestProperties[Index].StartsWith("OUTPUT_BUNDLEFILENAME="))
									{
										continue;
									}

									if (SourceProperties[Index].StartsWith("OUTPUT_UNIVERSALFILENAME=") && DestProperties[Index].StartsWith("OUTPUT_UNIVERSALFILENAME="))
									{
										continue;
									}

									bDiffers = true;
									break;
								}
								if (!bDiffers)
								{
									continue;
								}
							}

							// differed too much
							bCombinedBundleOK = false;
							Log.TraceInformation("gradle.properties files differ too much to combine for single AAB: '{0}' != '{1}'", Filename, DestFilename);
							break;
						}

						// there are unknown differences, cannot make a single AAB
						bCombinedBundleOK = false;
						Log.TraceInformation("Gradle projects differ too much to combine for single AAB: '{0}' != '{1}'", Filename, DestFilename);
						break;
					}
				}

				if (bCombinedBundleOK)
				{
					string NDKArch = NDKArches[0];

					string UE4BuildGradlePath = UE4GradleDest;

					// write a new abi.gradle
					StringBuilder ABIGradle = new StringBuilder();
					ABIGradle.AppendLine("android {");
					ABIGradle.AppendLine("\tdefaultConfig {");
					ABIGradle.AppendLine("\t\tndk {");
					ABIGradle.AppendLine(string.Format("\t\t\tabiFilters{0}", ABIFilter.Substring(1)));
					ABIGradle.AppendLine("\t\t}");
					ABIGradle.AppendLine("\t}");
					ABIGradle.AppendLine("}");
					string ABIGradleFilename = Path.Combine(UE4GradleDest, "app", "abi.gradle");
					File.WriteAllText(ABIGradleFilename, ABIGradle.ToString());

					// update manifest to use versionCode properly
					string BaseStoreVersion = GetStoreVersion("default").ToString();
					string ManifestFilename = Path.Combine(UE4BuildGradlePath, "app", "src", "main", "AndroidManifest.xml");
					string[] ManifestContents = File.ReadAllLines(ManifestFilename);
					for (int Index = 0; Index < ManifestContents.Length; Index++)
					{
						int ManifestVersionIndex = ManifestContents[Index].IndexOf("android:versionCode=");
						if (ManifestVersionIndex < 0)
						{
							continue;
						}

						int ManifestVersionIndex2 = ManifestContents[Index].Substring(ManifestVersionIndex + 22).IndexOf("\"");
						ManifestContents[Index] = ManifestContents[Index].Substring(0, ManifestVersionIndex + 21) + BaseStoreVersion + ManifestContents[Index].Substring(ManifestVersionIndex + 22 + ManifestVersionIndex2);
						break;
					}
					File.WriteAllLines(ManifestFilename, ManifestContents);

					ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);
					bool bEnableUniversalAPK = false;
					Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bEnableUniversalAPK", out bEnableUniversalAPK);

					// update gradle.properties to set STORE_VERSION properly, and OUTPUT_BUNDLEFILENAME
					string GradlePropertiesFilename = Path.Combine(UE4BuildGradlePath, "gradle.properties");
					string GradlePropertiesContent = File.ReadAllText(GradlePropertiesFilename);
					GradlePropertiesContent += string.Format("\nSTORE_VERSION={0}\nOUTPUT_BUNDLEFILENAME={1}\n", BaseStoreVersion,
						Path.GetFileNameWithoutExtension(OutputPath).Replace("UE4Game", ProjectName) + ".aab");
					if (bEnableUniversalAPK)
					{
						GradlePropertiesContent += string.Format("OUTPUT_UNIVERSALFILENAME={0}\n",
							Path.GetFileNameWithoutExtension(OutputPath).Replace("UE4Game", ProjectName) + "_universal.apk");
					}
					File.WriteAllText(GradlePropertiesFilename, GradlePropertiesContent);

					string GradleScriptPath = Path.Combine(UE4BuildGradlePath, "gradlew");
					if (Utils.IsRunningOnMono)
					{
						// fix permissions for Mac/Linux
						RunCommandLineProgramWithException(UE4BuildGradlePath, "/bin/sh", string.Format("-c 'chmod 0755 \"{0}\"'", GradleScriptPath.Replace("'", "'\"'\"'")), "Fix gradlew permissions");
					}
					else
					{
						if (CreateRunGradle(UE4BuildGradlePath))
						{
							GradleScriptPath = Path.Combine(UE4BuildGradlePath, "rungradle.bat");
						}
						else
						{
							GradleScriptPath = Path.Combine(UE4BuildGradlePath, "gradlew.bat");
						}
					}

					string GradleBuildType = bForDistribution ? ":app:bundleRelease" : ":app:bundleDebug";

					// collect optional additional Gradle parameters from plugins
					string GradleOptions = UPL.ProcessPluginNode(NDKArch, "gradleParameters", GradleBuildType); //  "--stacktrace --debug " + GradleBuildType);
					string GradleSecondCallOptions = UPL.ProcessPluginNode(NDKArch, "gradleSecondCallParameters", "");

					// make sure destination exists
					//Directory.CreateDirectory(Path.GetDirectoryName(DestApkName));

					// Use gradle to build the .apk file
					string ShellExecutable = Utils.IsRunningOnMono ? "/bin/sh" : "cmd.exe";
					string ShellParametersBegin = Utils.IsRunningOnMono ? "-c '" : "/c ";
					string ShellParametersEnd = Utils.IsRunningOnMono ? "'" : "";
					RunCommandLineProgramWithExceptionAndFiltering(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleOptions + ShellParametersEnd, "Making .aab with Gradle...");

					if (GradleSecondCallOptions != "")
					{
						RunCommandLineProgramWithExceptionAndFiltering(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleSecondCallOptions + ShellParametersEnd, "Additional Gradle steps...");
					}

					// For build machine run a clean afterward to clean up intermediate files (does not remove final APK)
					if (bIsBuildMachine)
					{
						//GradleOptions = "tasks --all";
						//RunCommandLineProgramWithException(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleOptions + ShellParametersEnd, "Listing all tasks...");

						GradleOptions = "clean";
						RunCommandLineProgramWithExceptionAndFiltering(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleOptions + ShellParametersEnd, "Cleaning Gradle intermediates...");
					}
				}
				else
				{
					// generate an AAB for each architecture separately, was unable to merge
					foreach (Tuple<string, string, string> build in BuildList)
					{
						string Arch = build.Item1;
						string GPUArchitecture = build.Item2;
						string Manifest = build.Item3;
						string NDKArch = GetNDKArch(Arch);

						Log.TraceInformation("\n===={0}====GENERATING BUNDLE====={1}================================================================", DateTime.Now.ToString(), Arch);

						string UE4BuildPath = Path.Combine(IntermediateAndroidPath, Arch.Substring(1).Replace("-", "_"));
						string UE4BuildGradlePath = Path.Combine(UE4BuildPath, "gradle");

						string GradleScriptPath = Path.Combine(UE4BuildGradlePath, "gradlew");
						if (Utils.IsRunningOnMono)
						{
							// fix permissions for Mac/Linux
							RunCommandLineProgramWithException(UE4BuildGradlePath, "/bin/sh", string.Format("-c 'chmod 0755 \"{0}\"'", GradleScriptPath.Replace("'", "'\"'\"'")), "Fix gradlew permissions");
						}
						else
						{
							if (CreateRunGradle(UE4BuildGradlePath))
							{
								GradleScriptPath = Path.Combine(UE4BuildGradlePath, "rungradle.bat");
							}
							else
							{
								GradleScriptPath = Path.Combine(UE4BuildGradlePath, "gradlew.bat");
							}
						}

						string GradleBuildType = bForDistribution ? ":app:bundleRelease" : ":app:bundleDebug";

						// collect optional additional Gradle parameters from plugins
						string GradleOptions = UPL.ProcessPluginNode(NDKArch, "gradleParameters", GradleBuildType); //  "--stacktrace --debug " + GradleBuildType);
						string GradleSecondCallOptions = UPL.ProcessPluginNode(NDKArch, "gradleSecondCallParameters", "");

						// make sure destination exists
						//Directory.CreateDirectory(Path.GetDirectoryName(DestApkName));

						// Use gradle to build the .apk file
						string ShellExecutable = Utils.IsRunningOnMono ? "/bin/sh" : "cmd.exe";
						string ShellParametersBegin = Utils.IsRunningOnMono ? "-c '" : "/c ";
						string ShellParametersEnd = Utils.IsRunningOnMono ? "'" : "";
						RunCommandLineProgramWithExceptionAndFiltering(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleOptions + ShellParametersEnd, "Making .aab with Gradle...");

						if (GradleSecondCallOptions != "")
						{
							RunCommandLineProgramWithExceptionAndFiltering(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleSecondCallOptions + ShellParametersEnd, "Additional Gradle steps...");
						}

						// For build machine run a clean afterward to clean up intermediate files (does not remove final APK)
						if (bIsBuildMachine)
						{
							//GradleOptions = "tasks --all";
							//RunCommandLineProgramWithException(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleOptions + ShellParametersEnd, "Listing all tasks...");

							GradleOptions = "clean";
							RunCommandLineProgramWithExceptionAndFiltering(UE4BuildGradlePath, ShellExecutable, ShellParametersBegin + "\"" + GradleScriptPath + "\" " + GradleOptions + ShellParametersEnd, "Cleaning Gradle intermediates...");
						}
					}
				}
			}

			Log.TraceInformation("\n===={0}====COMPLETED MAKE APK=======================================================================", DateTime.Now.ToString());
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
			IEnumerable<ReceiptProperty> Results = Receipt.AdditionalProperties.Where(x => x.Name == "AndroidPlugin");
			foreach (ReceiptProperty Property in Results)
			{
				// Keep only unique paths
				string PluginPath = Property.Value;
				if (PluginExtras.FirstOrDefault(x => x == PluginPath) == null)
				{
					PluginExtras.Add(PluginPath);
					Log.TraceInformation("AndroidPlugin: {0}", PluginPath);
				}
			}
			return PluginExtras;
		}

		public override bool PrepTargetForDeployment(TargetReceipt Receipt)
		{
			//Log.TraceInformation("$$$$$$$$$$$$$$ PrepTargetForDeployment $$$$$$$$$$$$$$$$$");

			DirectoryReference ProjectDirectory = DirectoryReference.FromFile(Receipt.ProjectFile) ?? UnrealBuildTool.EngineDirectory;
			string TargetName = (Receipt.ProjectFile == null ? Receipt.TargetName : Receipt.ProjectFile.GetFileNameWithoutAnyExtensions());

			AndroidToolChain ToolChain = ((AndroidPlatform)UEBuildPlatform.GetBuildPlatform(Receipt.Platform)).CreateTempToolChainForProject(Receipt.ProjectFile) as AndroidToolChain;

			// get the receipt
			SetAndroidPluginData(ToolChain.GetAllArchitectures(), CollectPluginDataPaths(Receipt));

			bool bShouldCompileAsDll = Receipt.HasValueForAdditionalProperty("CompileAsDll", "true");

			SavePackageInfo(TargetName, ProjectDirectory.FullName, Receipt.TargetType, bShouldCompileAsDll);

			// Get the output paths
			BuildProductType ProductType = bShouldCompileAsDll ? BuildProductType.DynamicLibrary : BuildProductType.Executable;
			List<FileReference> OutputPaths = Receipt.BuildProducts.Where(x => x.Type == ProductType).Select(x => x.Path).ToList();
			if (OutputPaths.Count < 1)
			{
				throw new BuildException("Target file does not contain either executable or dynamic library .so");
			}

			// we need to strip architecture from any of the output paths
			string BaseSoName = ToolChain.RemoveArchName(OutputPaths[0].FullName);

			// make an apk at the end of compiling, so that we can run without packaging (debugger, cook on the fly, etc)
			string RelativeEnginePath = UnrealBuildTool.EngineDirectory.MakeRelativeTo(DirectoryReference.GetCurrentDirectory());

			MakeApk(ToolChain, TargetName, Receipt.TargetType, ProjectDirectory.FullName, BaseSoName, RelativeEnginePath, bForDistribution: false, CookFlavor: "", Configuration: Receipt.Configuration,
				bMakeSeparateApks: ShouldMakeSeparateApks(), bIncrementalPackage: true, bDisallowPackagingDataInApk: false, bDisallowExternalFilesDir: true, bSkipGradleBuild: bShouldCompileAsDll);

			// if we made any non-standard .apk files, the generated debugger settings may be wrong
			if (ShouldMakeSeparateApks() && (OutputPaths.Count > 1 || !OutputPaths[0].FullName.Contains("-armv7")))
			{
				Log.TraceInformation("================================================================================================================================");
				Log.TraceInformation("Non-default apk(s) have been made: If you are debugging, you will need to manually select one to run in the debugger properties!");
				Log.TraceInformation("================================================================================================================================");
			}
			return true;
		}

		// Store generated package name in a text file for builds that do not generate an apk file 
		public bool SavePackageInfo(string TargetName, string ProjectDirectory, TargetType InTargetType, bool bIsEmbedded)
		{
			string PackageName = GetPackageName(TargetName);
			string DestPackageNameFileName = Path.Combine(ProjectDirectory, "Binaries", "Android", "packageInfo.txt");

			string[] PackageInfoSource = new string[4];
			PackageInfoSource[0] = PackageName;
			PackageInfoSource[1] = GetStoreVersion("").ToString();
			PackageInfoSource[2] = GetVersionDisplayName(bIsEmbedded);
			PackageInfoSource[3] = string.Format("name='com.epicgames.ue4.GameActivity.AppType' value='{0}'", InTargetType == TargetType.Game ? "" : InTargetType.ToString());

			Log.TraceInformation("Writing packageInfo pkgName:{0} storeVersion:{1} versionDisplayName:{2} to {3}", PackageInfoSource[0], PackageInfoSource[1], PackageInfoSource[2], DestPackageNameFileName);

			string DestDirectory = Path.GetDirectoryName(DestPackageNameFileName);
			if (!Directory.Exists(DestDirectory))
			{
				Directory.CreateDirectory(DestDirectory);
			}

			File.WriteAllLines(DestPackageNameFileName, PackageInfoSource);

			return true;
		}

		public static bool ShouldMakeSeparateApks()
		{
			// @todo android fat binary: Currently, there isn't much utility in merging multiple .so's into a single .apk except for debugging,
			// but we can't properly handle multiple GPU architectures in a single .apk, so we are disabling the feature for now
			// The user will need to manually select the apk to run in their Visual Studio debugger settings (see Override APK in TADP, for instance)
			// If we change this, pay attention to <OverrideAPKPath> in AndroidProjectGenerator
			return true;

			// check to see if the project wants separate apks
			// 			ConfigCacheIni Ini = nGetConfigCacheIni("Engine");
			// 			bool bSeparateApks = false;
			// 			Ini.GetBool("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "bSplitIntoSeparateApks", out bSeparateApks);
			// 
			// 			return bSeparateApks;
		}

		public bool PrepForUATPackageOrDeploy(FileReference ProjectFile, string ProjectName, DirectoryReference ProjectDirectory, string ExecutablePath, string EngineDirectory, bool bForDistribution, string CookFlavor, UnrealTargetConfiguration Configuration, bool bIsDataDeploy, bool bSkipGradleBuild)
		{
			//Log.TraceInformation("$$$$$$$$$$$$$$ PrepForUATPackageOrDeploy $$$$$$$$$$$$$$$$$");

			TargetType Type = TargetType.Game;
			if (CookFlavor.EndsWith("Client"))
			{
				Type = TargetType.Client;			
			}
			else if (CookFlavor.EndsWith("Server"))
			{
				Type = TargetType.Server;
			}

			// note that we cannot allow the data packaged into the APK if we are doing something like Launch On that will not make an obb
			// file and instead pushes files directly via deploy
			AndroidToolChain ToolChain = new AndroidToolChain(ProjectFile, false, null, null);

			SavePackageInfo(ProjectName, ProjectDirectory.FullName, Type, bSkipGradleBuild);

			MakeApk(ToolChain, ProjectName, Type, ProjectDirectory.FullName, ExecutablePath, EngineDirectory, bForDistribution: bForDistribution, CookFlavor: CookFlavor, Configuration: Configuration,
				bMakeSeparateApks: ShouldMakeSeparateApks(), bIncrementalPackage: false, bDisallowPackagingDataInApk: bIsDataDeploy, bDisallowExternalFilesDir: !bForDistribution || bIsDataDeploy, bSkipGradleBuild:bSkipGradleBuild);
			return true;
		}

		public static void OutputReceivedDataEventHandler(Object Sender, DataReceivedEventArgs Line)
		{
			if ((Line != null) && (Line.Data != null))
			{
				Log.TraceInformation(Line.Data);
			}
		}

		private string GenerateTemplatesHashCode(string EngineDir)
		{
			string SourceDirectory = Path.Combine(EngineDir, "Build", "Android", "Java");

			if (!Directory.Exists(SourceDirectory))
			{
				return "badpath";
			}

			MD5 md5 = MD5.Create();
			byte[] TotalHashBytes = null;

			string[] SourceFiles = Directory.GetFiles(SourceDirectory, "*.*", SearchOption.AllDirectories);
			foreach (string Filename in SourceFiles)
			{
				using (FileStream stream = File.OpenRead(Filename))
				{
					byte[] FileHashBytes = md5.ComputeHash(stream);
					if (TotalHashBytes != null)
					{
						int index = 0;
						foreach (byte b in FileHashBytes)
						{
							TotalHashBytes[index] ^= b;
							index++;
						}
					}
					else
					{
						TotalHashBytes = FileHashBytes;
					}
				}
			}

			if (TotalHashBytes != null)
			{
				string HashCode = "";
				foreach (byte b in TotalHashBytes)
				{
					HashCode += b.ToString("x2");
				}
				return HashCode;
			}

			return "empty";
		}

		private void UpdateGameActivity(string UE4Arch, string NDKArch, string EngineDir, string UE4BuildPath)
		{
			string SourceFilename = Path.Combine(EngineDir, "Build", "Android", "Java", "src", "com", "epicgames", "ue4", "GameActivity.java.template");
			string DestFilename = Path.Combine(UE4BuildPath, "src", "com", "epicgames", "ue4", "GameActivity.java");

			// check for GameActivity.java.template override
			SourceFilename = UPL.ProcessPluginNode(NDKArch, "gameActivityReplacement", SourceFilename);

			ConfigHierarchy Ini = GetConfigCacheIni(ConfigHierarchyType.Engine);

			string LoadLibraryDefaults = "";

			string SuperClassDefault;
			if (!Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "GameActivitySuperClass", out SuperClassDefault))
			{
				SuperClassDefault = UPL.ProcessPluginNode(NDKArch, "gameActivitySuperClass", "");
				if (String.IsNullOrEmpty(SuperClassDefault))
				{
					SuperClassDefault = "NativeActivity";
				}
			}

			string AndroidGraphicsDebugger;
			Ini.GetString("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings", "AndroidGraphicsDebugger", out AndroidGraphicsDebugger);

			switch (AndroidGraphicsDebugger.ToLower())
			{
				case "mali":
					LoadLibraryDefaults += "\t\ttry\n" +
											"\t\t{\n" +
											"\t\t\tSystem.loadLibrary(\"MGD\");\n" +
											"\t\t}\n" +
											"\t\tcatch (java.lang.UnsatisfiedLinkError e)\n" +
											"\t\t{\n" +
											"\t\t\tLog.debug(\"libMGD.so not loaded.\");\n" +
											"\t\t}\n";
					break;
			}

			Dictionary<string, string> Replacements = new Dictionary<string, string>{
				{ "//$${gameActivityImportAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityImportAdditions", "")},
				{ "//$${gameActivityPostImportAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityPostImportAdditions", "")},
				{ "//$${gameActivityImplementsAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityImplementsAdditions", "")},
				{ "//$${gameActivityClassAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityClassAdditions", "")},
				{ "//$${gameActivityReadMetadataAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityReadMetadataAdditions", "")},
				{ "//$${gameActivityOnCreateAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnCreateAdditions", "")},
				{ "//$${gameActivityOnCreateFinalAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnCreateFinalAdditions", "")},
				{ "//$${gameActivityOverrideAPKOBBPackaging}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOverrideAPKOBBPackaging", "")},
				{ "//$${gameActivityOnDestroyAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnDestroyAdditions", "")},
				{ "//$${gameActivityOnStartAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnStartAdditions", "")},
				{ "//$${gameActivityOnStopAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnStopAdditions", "")},
				{ "//$${gameActivityOnRestartAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnRestartAdditions", "")},
				{ "//$${gameActivityOnSaveInstanceStateAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnSaveInstanceStateAdditions", "")},
				{ "//$${gameActivityOnRequestPermissionsResultAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnRequestPermissionsResultAdditions", "")},
				{ "//$${gameActivityOnPauseAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnPauseAdditions", "")},
				{ "//$${gameActivityOnResumeAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnResumeAdditions", "")},
				{ "//$${gameActivityOnNewIntentAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnNewIntentAdditions", "")},
  				{ "//$${gameActivityOnActivityResultAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnActivityResultAdditions", "")},
				{ "//$${gameActivityOnActivityResultIapStoreHelperHandler}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnActivityResultIapStoreHelperHandler", "")},
  				{ "//$${gameActivityPreConfigRulesParseAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityPreConfigRulesParseAdditions", "")},
  				{ "//$${gameActivityPostConfigRulesAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityPostConfigRulesAdditions", "")},
  				{ "//$${gameActivityFinalizeConfigRulesAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityFinalizeConfigRulesAdditions", "")},
				{ "//$${gameActivityBeforeConfigRulesAppliedAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityBeforeConfigRulesAppliedAdditions", "")},
				{ "//$${gameActivityAfterMainViewCreatedAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityAfterMainViewCreatedAdditions", "")},
				{ "//$${gameActivityResizeKeyboardAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityResizeKeyboardAdditions", "")},
				{ "//$${gameActivityLoggerCallbackAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityLoggerCallbackAdditions", "")},
				{ "//$${gameActivityGetCommandLineAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityGetCommandLineAdditions", "")},
				{ "//$${gameActivityGetLoginIdAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityGetLoginIdAdditions", "")},
				{ "//$${gameActivityGetFunnelIdAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityGetFunnelIdAdditions", "")},
				{ "//$${gameActivityAllowedRemoteNotificationsAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityAllowedRemoteNotificationsAdditions", "")},
				{ "//$${gameActivityAndroidThunkJavaIapBeginPurchase}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityAndroidThunkJavaIapBeginPurchase", "")},
				{ "//$${gameActivityIapSetupServiceAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityIapSetupServiceAdditions", "")},
				{ "//$${gameActivityOnRestartApplicationAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityOnRestartApplicationAdditions", "")},
				{ "//$${gameActivityForceQuitAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameActivityForceQuitAdditions", "")},
				{ "//$${soLoadLibrary}$$", UPL.ProcessPluginNode(NDKArch, "soLoadLibrary", LoadLibraryDefaults)},
				{ "$${gameActivitySuperClass}$$", SuperClassDefault},
			};

			string[] TemplateSrc = File.ReadAllLines(SourceFilename);
			string[] TemplateDest = File.Exists(DestFilename) ? File.ReadAllLines(DestFilename) : null;

			bool TemplateChanged = false;
			for (int LineIndex = 0; LineIndex < TemplateSrc.Length; ++LineIndex)
			{
				string SrcLine = TemplateSrc[LineIndex];
				bool Changed = false;
				foreach (KeyValuePair<string, string> KVP in Replacements)
				{
					if(SrcLine.Contains(KVP.Key))
					{
						SrcLine = SrcLine.Replace(KVP.Key, KVP.Value);
						Changed = true;
					}
				}
				if (Changed)
				{
					TemplateSrc[LineIndex] = SrcLine;
					TemplateChanged = true;
				}
			}

			if (TemplateChanged)
			{
				// deal with insertions of newlines
				TemplateSrc = string.Join("\n", TemplateSrc).Split(new[] { "\r\n", "\r", "\n" }, StringSplitOptions.None);
			}

			if (TemplateDest == null || TemplateSrc.Length != TemplateDest.Length || !TemplateSrc.SequenceEqual(TemplateDest))
			{
				Log.TraceInformation("\n==== Writing new GameActivity.java file to {0} ====", DestFilename);
				File.WriteAllLines(DestFilename, TemplateSrc);
			}
		}

		private void UpdateGameApplication(string UE4Arch, string NDKArch, string EngineDir, string UE4BuildPath)
		{
			string SourceFilename = Path.Combine(EngineDir, "Build", "Android", "Java", "src", "com", "epicgames", "ue4", "GameApplication.java.template");
			string DestFilename = Path.Combine(UE4BuildPath, "src", "com", "epicgames", "ue4", "GameApplication.java");

			Dictionary<string, string> Replacements = new Dictionary<string, string>{
				{ "//$${gameApplicationImportAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameApplicationImportAdditions", "")},
				{ "//$${gameApplicationOnCreateAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameApplicationOnCreateAdditions", "")},
				{ "//$${gameApplicationAttachBaseContextAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameApplicationAttachBaseContextAdditions", "")},
				{ "//$${gameApplicationOnLowMemoryAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameApplicationOnLowMemoryAdditions", "")},
				{ "//$${gameApplicationOnTrimMemoryAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameApplicationOnTrimMemoryAdditions", "")},
				{ "//$${gameApplicationOnConfigurationChangedAdditions}$$", UPL.ProcessPluginNode(NDKArch, "gameApplicationOnConfigurationChangedAdditions", "")},
			};

			string[] TemplateSrc = File.ReadAllLines(SourceFilename);
			string[] TemplateDest = File.Exists(DestFilename) ? File.ReadAllLines(DestFilename) : null;

			bool TemplateChanged = false;
			for (int LineIndex = 0; LineIndex < TemplateSrc.Length; ++LineIndex)
			{
				string SrcLine = TemplateSrc[LineIndex];
				bool Changed = false;
				foreach (KeyValuePair<string, string> KVP in Replacements)
				{
					if (SrcLine.Contains(KVP.Key))
					{
						SrcLine = SrcLine.Replace(KVP.Key, KVP.Value);
						Changed = true;
					}
				}
				if (Changed)
				{
					TemplateSrc[LineIndex] = SrcLine;
					TemplateChanged = true;
				}
			}

			if (TemplateChanged)
			{
				// deal with insertions of newlines
				TemplateSrc = string.Join("\n", TemplateSrc).Split(new[] { "\r\n", "\r", "\n" }, StringSplitOptions.None);
			}

			if (TemplateDest == null || TemplateSrc.Length != TemplateDest.Length || !TemplateSrc.SequenceEqual(TemplateDest))
			{
				Log.TraceInformation("\n==== Writing new GameApplication.java file to {0} ====", DestFilename);
				File.WriteAllLines(DestFilename, TemplateSrc);
			}
		}

		private void CreateAdditonalBuildPathFiles(string NDKArch, string UE4BuildPath, XDocument FilesToAdd)
		{
			Dictionary<string, string> PathsAndRootEls = new Dictionary<string, string>();

			foreach (XElement Element in FilesToAdd.Root.Elements())
			{
				string RelPath = Element.Value;
				if (RelPath != null)
				{
					XAttribute TypeAttr = Element.Attribute("rootEl");
					PathsAndRootEls[RelPath] = TypeAttr == null ? null : TypeAttr.Value;
				}
			}

			foreach (KeyValuePair<string, string> Entry in PathsAndRootEls)
			{
				string UPLNodeName = Entry.Key.Replace("/", "__").Replace(".", "__");
				string Content;
				if (Entry.Value == null)
				{
					// no root element, assume not XML
					Content = UPL.ProcessPluginNode(NDKArch, UPLNodeName, "");
				}
				else
				{
					XDocument ContentDoc = new XDocument(new XElement(Entry.Value));
					UPL.ProcessPluginNode(NDKArch, UPLNodeName, "", ref ContentDoc);
					Content = XML_HEADER + "\n" + ContentDoc.ToString();
				}

				string DestPath = Path.Combine(UE4BuildPath, Entry.Key);
				if (!File.Exists(DestPath) || File.ReadAllText(DestPath) != Content)
				{
					File.WriteAllText(DestPath, Content);
				}
			}
		}

		private AndroidAARHandler CreateAARHandler(string EngineDir, string UE4BuildPath, List<string> NDKArches, bool HandleDependencies=true)
		{
			AndroidAARHandler AARHandler = new AndroidAARHandler();
			string ImportList = "";

			// Get some common paths
			string AndroidHome = Environment.ExpandEnvironmentVariables("%ANDROID_HOME%").TrimEnd('/', '\\');
			EngineDir = EngineDir.TrimEnd('/', '\\');

			// Add the AARs from the default aar-imports.txt
			// format: Package,Name,Version
			string ImportsFile = Path.Combine(UE4BuildPath, "aar-imports.txt");
			if (File.Exists(ImportsFile))
			{
				ImportList = File.ReadAllText(ImportsFile);
			}

			// Run the UPL imports section for each architecture and add any new imports (duplicates will be removed)
			foreach (string NDKArch in NDKArches)
			{
				ImportList = UPL.ProcessPluginNode(NDKArch, "AARImports", ImportList);
			}

			// Add the final list of imports and get dependencies
			foreach (string Line in ImportList.Split('\n'))
			{
				string Trimmed = Line.Trim(' ', '\r');

				if (Trimmed.StartsWith("repository "))
				{
					string DirectoryPath = Trimmed.Substring(11).Trim(' ').TrimEnd('/', '\\');
					DirectoryPath = DirectoryPath.Replace("$(ENGINEDIR)", EngineDir);
					DirectoryPath = DirectoryPath.Replace("$(ANDROID_HOME)", AndroidHome);
					DirectoryPath = DirectoryPath.Replace('\\', Path.DirectorySeparatorChar).Replace('/', Path.DirectorySeparatorChar);
					AARHandler.AddRepository(DirectoryPath);
				}
				else if (Trimmed.StartsWith("repositories "))
				{
					string DirectoryPath = Trimmed.Substring(13).Trim(' ').TrimEnd('/', '\\');
					DirectoryPath = DirectoryPath.Replace("$(ENGINEDIR)", EngineDir);
					DirectoryPath = DirectoryPath.Replace("$(ANDROID_HOME)", AndroidHome);
					DirectoryPath = DirectoryPath.Replace('\\', Path.DirectorySeparatorChar).Replace('/', Path.DirectorySeparatorChar);
					AARHandler.AddRepositories(DirectoryPath, "m2repository");
				}
				else
				{
					string[] Sections = Trimmed.Split(',');
					if (Sections.Length == 3)
					{
						string PackageName = Sections[0].Trim(' ');
						string BaseName = Sections[1].Trim(' ');
						string Version = Sections[2].Trim(' ');
						Log.TraceInformation("AARImports: {0}, {1}, {2}", PackageName, BaseName, Version);
						AARHandler.AddNewAAR(PackageName, BaseName, Version, HandleDependencies);
					}
				}
			}

			return AARHandler;
		}

		private void PrepareJavaLibsForGradle(string JavaLibsDir, string UE4BuildGradlePath, string InMinSdkVersion, string InTargetSdkVersion, string CompileSDKVersion, string BuildToolsVersion, string NDKArch)
		{
			StringBuilder SettingsGradleContent = new StringBuilder();
			StringBuilder ProjectDependencyContent = new StringBuilder();

			SettingsGradleContent.AppendLine("rootProject.name='app'");
			SettingsGradleContent.AppendLine("include ':app'");
			ProjectDependencyContent.AppendLine("dependencies {");

			string[] LibDirs = Directory.GetDirectories(JavaLibsDir);
			foreach (string LibDir in LibDirs)
			{
				string RelativePath = Path.GetFileName(LibDir);

				SettingsGradleContent.AppendLine(string.Format("include ':{0}'", RelativePath));
				ProjectDependencyContent.AppendLine(string.Format("\timplementation project(':{0}')", RelativePath));

				string GradleProjectPath = Path.Combine(UE4BuildGradlePath, RelativePath);
				string GradleProjectMainPath = Path.Combine(GradleProjectPath, "src", "main");

				string ManifestFilename = Path.Combine(LibDir, "AndroidManifest.xml");
				string GradleManifest = Path.Combine(GradleProjectMainPath, "AndroidManifest.xml");
				MakeDirectoryIfRequired(GradleManifest);

				// Copy parts were they need to be
				CleanCopyDirectory(Path.Combine(LibDir, "assets"), Path.Combine(GradleProjectPath, "assets"));
				CleanCopyDirectory(Path.Combine(LibDir, "libs"), Path.Combine(GradleProjectPath, "libs"));
				CleanCopyDirectory(Path.Combine(LibDir, "res"), Path.Combine(GradleProjectMainPath, "res"));

				// If our lib already has a src/main/java folder, don't put things into a java folder
				string SrcDirectory = Path.Combine(LibDir, "src", "main");
				if (Directory.Exists(Path.Combine(SrcDirectory, "java")))
				{
					CleanCopyDirectory(SrcDirectory, GradleProjectMainPath);
				}
				else
				{
					CleanCopyDirectory(Path.Combine(LibDir, "src"), Path.Combine(GradleProjectMainPath, "java"));
				}

				// Now generate a build.gradle from the manifest
				StringBuilder BuildGradleContent = new StringBuilder();
				BuildGradleContent.AppendLine("apply plugin: 'com.android.library'");
				BuildGradleContent.AppendLine("android {");
				BuildGradleContent.AppendLine(string.Format("\tcompileSdkVersion {0}", CompileSDKVersion));
				BuildGradleContent.AppendLine("\tdefaultConfig {");

				// Try to get the SDK target from the AndroidManifest.xml
				string VersionCode = "";
				string VersionName = "";
				string MinSdkVersion = InMinSdkVersion;
				string TargetSdkVersion = InTargetSdkVersion;
				XDocument ManifestXML;
				if (File.Exists(ManifestFilename))
				{
					try
					{
						ManifestXML = XDocument.Load(ManifestFilename);

						XAttribute VersionCodeAttr = ManifestXML.Root.Attribute(XName.Get("versionCode", "http://schemas.android.com/apk/res/android"));
						if (VersionCodeAttr != null)
						{
							VersionCode = VersionCodeAttr.Value;
						}

						XAttribute VersionNameAttr = ManifestXML.Root.Attribute(XName.Get("versionName", "http://schemas.android.com/apk/res/android"));
						if (VersionNameAttr != null)
						{
							VersionName = VersionNameAttr.Value;
						}

						XElement UseSDKNode = null;
						foreach (XElement WorkNode in ManifestXML.Elements().First().Descendants("uses-sdk"))
						{
							UseSDKNode = WorkNode;

							XAttribute MinSdkVersionAttr = WorkNode.Attribute(XName.Get("minSdkVersion", "http://schemas.android.com/apk/res/android"));
							if (MinSdkVersionAttr != null)
							{
								MinSdkVersion = MinSdkVersionAttr.Value;
							}

							XAttribute TargetSdkVersionAttr = WorkNode.Attribute(XName.Get("targetSdkVersion", "http://schemas.android.com/apk/res/android"));
							if (TargetSdkVersionAttr != null)
							{
								TargetSdkVersion = TargetSdkVersionAttr.Value;
							}
						}

						if (UseSDKNode != null)
						{
							UseSDKNode.Remove();
						}

						// rewrite the manifest if different
						String NewManifestText = ManifestXML.ToString();
						String OldManifestText = "";
						if (File.Exists(GradleManifest))
						{
							OldManifestText = File.ReadAllText(GradleManifest);
						}
						if (NewManifestText != OldManifestText)
						{
							File.WriteAllText(GradleManifest, NewManifestText);
						}
					}
					catch (Exception e)
					{
						Log.TraceError("AAR Manifest file {0} parsing error! {1}", ManifestFilename, e);
					}
				}

				if (VersionCode != "")
				{
					BuildGradleContent.AppendLine(string.Format("\t\tversionCode {0}", VersionCode));
				}
				if (VersionName != "")
				{
					BuildGradleContent.AppendLine(string.Format("\t\tversionName \"{0}\"", VersionName));
				}
				if (MinSdkVersion != "")
				{
					BuildGradleContent.AppendLine(string.Format("\t\tminSdkVersion = {0}", MinSdkVersion));
				}
				if (TargetSdkVersion != "")
				{
					BuildGradleContent.AppendLine(string.Format("\t\ttargetSdkVersion = {0}", TargetSdkVersion));
				}
				BuildGradleContent.AppendLine("\t}");
				BuildGradleContent.AppendLine("}");

				string AdditionsGradleFilename = Path.Combine(LibDir, "additions.gradle");
				if (File.Exists(AdditionsGradleFilename))
				{
					string[] AdditionsLines = File.ReadAllLines(AdditionsGradleFilename);
					foreach (string LineContents in AdditionsLines)
					{
						BuildGradleContent.AppendLine(LineContents);
					}
				}

				// rewrite the build.gradle if different
				string BuildGradleFilename = Path.Combine(GradleProjectPath, "build.gradle");
				String NewBuildGradleText = BuildGradleContent.ToString();
				String OldBuildGradleText = "";
				if (File.Exists(BuildGradleFilename))
				{
					OldBuildGradleText = File.ReadAllText(BuildGradleFilename);
				}
				if (NewBuildGradleText != OldBuildGradleText)
				{
					File.WriteAllText(BuildGradleFilename, NewBuildGradleText);
				}
			}
			ProjectDependencyContent.AppendLine("}");

			// Add any UPL settingsGradleAdditions
			SettingsGradleContent.Append(UPL.ProcessPluginNode(NDKArch, "settingsGradleAdditions", ""));

			string SettingsGradleFilename = Path.Combine(UE4BuildGradlePath, "settings.gradle");
			File.WriteAllText(SettingsGradleFilename, SettingsGradleContent.ToString());

			string ProjectsGradleFilename = Path.Combine(UE4BuildGradlePath, "app", "projects.gradle");
			File.WriteAllText(ProjectsGradleFilename, ProjectDependencyContent.ToString());
		}

		private void GenerateGradleAARImports(string EngineDir, string UE4BuildPath, List<string> NDKArches)
		{
			AndroidAARHandler AARHandler = CreateAARHandler(EngineDir, UE4BuildPath, NDKArches, false);
			StringBuilder AARImportsContent = new StringBuilder();

			// Add repositories
			AARImportsContent.AppendLine("repositories {");
			foreach (string Repository in AARHandler.Repositories)
			{
				string RepositoryPath = Path.GetFullPath(Repository).Replace('\\', '/');
				AARImportsContent.AppendLine("\tmaven { url uri('" + RepositoryPath + "') }");
			}
			AARImportsContent.AppendLine("}");

			// Add dependencies
			AARImportsContent.AppendLine("dependencies {");
			foreach (AndroidAARHandler.AndroidAAREntry Dependency in AARHandler.AARList)
			{
				AARImportsContent.AppendLine(string.Format("\timplementation '{0}:{1}:{2}'", Dependency.Filename, Dependency.BaseName, Dependency.Version));
			}
			AARImportsContent.AppendLine("}");

			string AARImportsFilename = Path.Combine(UE4BuildPath, "gradle", "app", "aar-imports.gradle");
			File.WriteAllText(AARImportsFilename, AARImportsContent.ToString());
		}
	}
}
