// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using System.Xml;
using System.Linq;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Lumin-specific target settings
	/// </summary>	
	public class LuminTargetRules
	{
		/// <summary>
		/// Lists GPU Architectures that you want to build (mostly used for mobile etc.)
		/// </summary>
		[CommandLine("-GPUArchitectures=", ListSeparator = '+')]
		public List<string> GPUArchitectures = new List<string>();

		/// <summary>
		/// If -distribution was passed on the commandline, this build is for distribution.
		/// </summary>
		[CommandLine("-distribution")]
		public bool bForDistribution = false;
	}

	/// <summary>
	/// Read-only wrapper for Android-specific target settings
	/// </summary>
	public class ReadOnlyLuminTargetRules
	{
		/// <summary>
		/// The private mutable settings object
		/// </summary>
		private LuminTargetRules Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The settings object to wrap</param>
		public ReadOnlyLuminTargetRules(LuminTargetRules Inner)
		{
			this.Inner = Inner;
		}

		/// <summary>
		/// Accessors for fields on the inner TargetRules instance
		/// </summary>
		#region Read-only accessor properties 
#if !__MonoCS__
#pragma warning disable CS1591
#endif

		public IReadOnlyList<string> GPUArchitectures
		{
			get { return Inner.GPUArchitectures.AsReadOnly(); }
		}
		public bool bForDistribution
		{
			get { return Inner.bForDistribution; }
		}

#if !__MonoCS__
#pragma warning restore CS1591
#endif
		#endregion
	}

	/// <summary>
	/// Helper class for shared lumin platform functions and members
	/// </summary>
	public class LuminSDKVersionHelper
	{
		/// <summary>
		/// This is the minimum SDK version we support.
		/// </summary>
		static uint MinimumSDKVersionMajor = 0;
		static uint MinimumSDKVersionMinor = 24;

		/// <summary>
		/// Gets a string defining the minimum required sdk version.
		/// </summary>
		/// <returns></returns>
		public string GetRequiredSDKString()
		{
			return string.Format("{0}.{1}", MinimumSDKVersionMajor, MinimumSDKVersionMinor);
		}

		/// <summary>
		/// checks if the sdk is installed or has been synced, sets environment variable
		/// </summary>
		/// <returns></returns>
		public bool HasAnySDK()
		{
			string EnvVarKey = "MLSDK";

			string MLSDKPath = Environment.GetEnvironmentVariable(EnvVarKey);
			if (String.IsNullOrEmpty(MLSDKPath))
			{
				Log.TraceLog("*** Unable to determine MLSDK location ***");
				return false;
			}

			// detected SDK version is < minimum major/minor
			uint DetectedMajorVersion;
			uint DetectedMinorVersion;
			String VersionFile = string.Format("{0}/include/ml_version.h", MLSDKPath).Replace('/', Path.DirectorySeparatorChar);
			if (File.Exists(VersionFile))
			{
				string[] VersionText = File.ReadAllLines(VersionFile);

				String MajorVersion = FindVersionNumber("MLSDK_VERSION_MAJOR", VersionText);
				String MinorVersion = FindVersionNumber("MLSDK_VERSION_MINOR", VersionText);
				DetectedMajorVersion = Convert.ToUInt32(MajorVersion);
				DetectedMinorVersion = Convert.ToUInt32(MinorVersion);
			}
			else
			{
				Log.TraceLog("*** Unable to locate MLSDK version file ml_version.h ***");
				return false;
			}

			if (DetectedMajorVersion < MinimumSDKVersionMajor || (DetectedMajorVersion == MinimumSDKVersionMajor && DetectedMinorVersion < MinimumSDKVersionMinor))
			{
				Log.TraceLog("*** Found installed MLSDK version {0}.{1} at '{2}' but require at least {3}.{4} ***",
					DetectedMajorVersion, DetectedMinorVersion, MLSDKPath, MinimumSDKVersionMajor, MinimumSDKVersionMinor);
				return false;
			}

			Log.TraceLog("*** Found installed MLSDK version {0}.{1} at '{2}' ***", DetectedMajorVersion, DetectedMinorVersion, MLSDKPath);
			return true;
		}

		private string FindVersionNumber(string StringToFind, string[] AllLines)
		{
			string FoundVersion = "Unknown";
			foreach (string CurrentLine in AllLines)
			{
				int Index = CurrentLine.IndexOf(StringToFind);
				if (Index != -1)
				{
					FoundVersion = CurrentLine.Substring(Index + StringToFind.Length);
					break;
				}
			}
			return FoundVersion.Trim();
		}
	}

	class LuminPlatform : AndroidPlatform
	{
		public LuminPlatform(AndroidPlatformSDK InSDK)
			: base(UnrealTargetPlatform.Lumin, InSDK)
		{
		}

		public override bool CanUseXGE()
		{
			return true;
		}

		public override bool HasSpecificDefaultBuildConfig(UnrealTargetPlatform Platform, DirectoryReference ProjectPath)
		{
			ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, ProjectPath, UnrealTargetPlatform.Lumin);
			bool bUseVulkan = true;
			Ini.GetBool("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "bUseVulkan", out bUseVulkan);

			List<string> ConfigBoolKeys = new List<string>();
			if (!bUseVulkan)
			{
				ConfigBoolKeys.Add("bUseMobileRendering");
			}

			// look up Android specific settings
			if (!DoProjectSettingsMatchDefault(UnrealTargetPlatform.Lumin, ProjectPath, "/Script/LuminRuntimeSettings.LuminRuntimeSettings", ConfigBoolKeys.ToArray(), null, null))
			{
				return false;
			}

			return true;
		}


		/// <summary>
		/// Modify the rules for a newly created module, where the target is a different host platform.
		/// This is not required - but allows for hiding details of a particular platform.
		/// </summary>
		/// <param name="ModuleName">The name of the module</param>
		/// <param name="Rules">The module rules</param>
		/// <param name="Target">The target being build</param>
		public override void ModifyModuleRulesForOtherPlatform(string ModuleName, ModuleRules Rules, ReadOnlyTargetRules Target)
		{
			// don't do any target platform stuff if SDK is not available
			if (!UEBuildPlatform.IsPlatformAvailable(Platform))
			{
				return;
			}

			if ((Target.Platform == UnrealTargetPlatform.Win32) || (Target.Platform == UnrealTargetPlatform.Win64) || (Target.Platform == UnrealTargetPlatform.Mac) || (Target.Platform == UnrealTargetPlatform.Linux))
			{
				bool bBuildShaderFormats = Target.bForceBuildShaderFormats;
				if (!Target.bBuildRequiresCookedData)
				{
					if (ModuleName == "Engine")
					{
						if (Target.bBuildDeveloperTools)
						{
							Rules.DynamicallyLoadedModuleNames.Add("LuminTargetPlatform");
						}
					}
					else if (ModuleName == "TargetPlatform")
					{
						bBuildShaderFormats = true;
						Rules.DynamicallyLoadedModuleNames.Add("TextureFormatASTC");
					}
				}

				// allow standalone tools to use targetplatform modules, without needing Engine
				if (ModuleName == "TargetPlatform")
				{
					if (Target.bForceBuildTargetPlatforms)
					{
						Rules.DynamicallyLoadedModuleNames.Add("LuminTargetPlatform");
					}
				}
			}
		}

		public override void AddExtraModules(ReadOnlyTargetRules Target, List<string> PlatformExtraModules)
		{
			// 			PlatformExtraModules.Add("VulkanRHI");
			PlatformExtraModules.Add("MagicLeapAudio");
			PlatformExtraModules.Add("MagicLeapAudioCapture");

			// Hack: GoogleOboe is an explicit dependency of AudioCaptureAndroid, which is built when you compile for Lumin with the -allmodules flag.
			// Doing this suppresses a warning that GoogleOboe isn't supported for Lumin, even though it is.
			if(Target.bBuildAllModules)
			{
				PlatformExtraModules.Add("GoogleOboe");
			}
		}

		/// <summary>
		/// Modify the rules for a newly created module, in a target that's being built for this platform.
		/// This is not required - but allows for hiding details of a particular platform.
		/// </summary>
		/// <param name="ModuleName">The name of the module</param>
		/// <param name="Rules">The module rules</param>
		/// <param name="Target">The target being build</param>
		public override void ModifyModuleRulesForActivePlatform(string ModuleName, ModuleRules Rules, ReadOnlyTargetRules Target)
		{
			// may need to put some stuff in here to keep Lumin out of other module .cs files
		}

		public override void SetUpSpecificEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment CompileEnvironment, LinkEnvironment LinkEnvironment)
		{
			CompileEnvironment.Definitions.Add("PLATFORM_LUMIN=1");
			CompileEnvironment.Definitions.Add("USE_ANDROID_JNI=0");
			CompileEnvironment.Definitions.Add("USE_ANDROID_AUDIO=0");
			CompileEnvironment.Definitions.Add("USE_ANDROID_FILE=0");
			CompileEnvironment.Definitions.Add("USE_ANDROID_INPUT=0");
			CompileEnvironment.Definitions.Add("USE_ANDROID_LAUNCH=0");
			CompileEnvironment.Definitions.Add("USE_ANDROID_EVENTS=0");
			CompileEnvironment.Definitions.Add("USE_ANDROID_OPENGL=0");
			CompileEnvironment.Definitions.Add("WITH_OGGVORBIS=1");

			DirectoryReference MLSDKDir = new DirectoryReference(Environment.GetEnvironmentVariable("MLSDK"));
			CompileEnvironment.SystemIncludePaths.Add(DirectoryReference.Combine(MLSDKDir, "lumin/stl/libc++-8/include"));
			CompileEnvironment.SystemIncludePaths.Add(DirectoryReference.Combine(MLSDKDir, "include"));
			CompileEnvironment.SystemIncludePaths.Add(DirectoryReference.Combine(MLSDKDir, "tools/toolchains/llvm-8/sysroot/usr/include"));

			LinkEnvironment.SystemLibraryPaths.Add(DirectoryReference.Combine(MLSDKDir, "lib/lumin"));
			LinkEnvironment.SystemLibraryPaths.Add(DirectoryReference.Combine(MLSDKDir, "lumin/stl/libc++-8/lib"));

			LinkEnvironment.SystemLibraries.Add("GLESv3");
			LinkEnvironment.SystemLibraries.Add("EGL");

			LinkEnvironment.SystemLibraries.Add("ml_lifecycle");
			LinkEnvironment.SystemLibraries.Add("ml_ext_logging");
			LinkEnvironment.SystemLibraries.Add("ml_dispatch");
			//LinkEnvironment.SystemLibraries.Add("android_support");
		}

		public override bool ShouldCreateDebugInfo(ReadOnlyTargetRules Target)
		{
			// Don't create debug info for shipping distribution builds
			if (Target.Configuration == UnrealTargetConfiguration.Shipping && Target.LuminPlatform.bForDistribution)
			{
				return false;
			}

			return base.ShouldCreateDebugInfo(Target);
		}

		public override UEToolChain CreateToolChain(ReadOnlyTargetRules Target)
		{
			bool bUseLdGold = Target.bUseUnityBuild;
			return new LuminToolChain(Target.ProjectFile, false, null, Target.LuminPlatform.GPUArchitectures, true);
		}
		public override UEToolChain CreateTempToolChainForProject(FileReference ProjectFile)
		{
			return new LuminToolChain(ProjectFile);
		}

		/// <summary>
		/// Deploys the given target
		/// </summary>
		/// <param name="Receipt">Receipt for the target being deployed</param>
		public override void Deploy(TargetReceipt Receipt)
		{
			new UEDeployLumin(Receipt.ProjectFile).PrepTargetForDeployment(Receipt);
		}

		public override void ValidateTarget(TargetRules Target)
		{
			base.ValidateTarget(Target);

			// #todo: ICU is crashing on startup, this is a workaround
			Target.bCompileICU = true;
		}
	}

	class LuminPlatformSDK : AndroidPlatformSDK
	{
		/// <summary>
		/// This is the minimum SDK version we support.
		/// </summary>
		LuminSDKVersionHelper SDKVersionHelper = new LuminSDKVersionHelper();

		public override string GetSDKTargetPlatformName()
		{
			return "Lumin";
		}

		public override string GetRequiredSDKString()
		{
			return SDKVersionHelper.GetRequiredSDKString();
		}

		protected override String GetRequiredScriptVersionString()
		{
			return "Lumin_15";
		}

		/// <summary>
		/// checks if the sdk is installed or has been synced, sets environment variable
		/// </summary>
		/// <returns></returns>
		protected override bool HasAnySDK()
		{
			return SDKVersionHelper.HasAnySDK();
		}

		protected override SDKStatus HasRequiredManualSDKInternal()
		{
			// if any autosdk setup has been done then the local process environment is suspect
			if (HasSetupAutoSDK())
			{
				return SDKStatus.Invalid;
			}

			if (SDKVersionHelper.HasAnySDK())
			{
				return SDKStatus.Valid;
			}

			return SDKStatus.Invalid;
		}
	}

	class LuminPlatformFactory : UEBuildPlatformFactory
	{
		public override UnrealTargetPlatform TargetPlatform
		{
			get { return UnrealTargetPlatform.Lumin; }
		}

		public override void RegisterBuildPlatforms()
		{
			LuminPlatformSDK SDK = new LuminPlatformSDK();
			SDK.ManageAndValidateSDK();

			// Register this build platform
			UEBuildPlatform.RegisterBuildPlatform(new LuminPlatform(SDK));
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Lumin, UnrealPlatformGroup.Android);
		}
	}

}
