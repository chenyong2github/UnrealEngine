// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	partial struct UnrealTargetPlatform
	{
		/// <summary>
		/// HTML5 target platform
		/// </summary>
		public static UnrealTargetPlatform HTML5 = FindOrAddByName("HTML5");
	}

	/// <summary>
	/// HTML5-specific target settings
	/// </summary>
	public class HTML5TargetRules
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public HTML5TargetRules()
		{
			XmlConfig.ApplyTo(this);
		}

		/// <summary>
		/// Use LLVM Wasm backend
		/// </summary>
		public string libExt = HTML5ToolChain.libExt;
	}

	/// <summary>
	/// Read-only wrapper for HTML5-specific target settings
	/// </summary>
	public class ReadOnlyHTML5TargetRules
	{
		/// <summary>
		/// The private mutable settings object
		/// </summary>
		private HTML5TargetRules Inner;

		/// <summary>
		/// The path to the DPX specific ThirdParty source directory.
		/// </summary>
		public string PlatformThirdPartySourceDirectory => DirectoryReference.Combine(UnrealBuildTool.EnginePlatformExtensionsDirectory, "HTML5", "Source", "ThirdParty").FullName;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The settings object to wrap</param>
		public ReadOnlyHTML5TargetRules(HTML5TargetRules Inner)
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

		public string libExt
		{
			get { return Inner.libExt; }
		}

#if !__MonoCS__
#pragma warning restore CS1591
#endif
		#endregion
	}


	partial class TargetRules
	{
		/// <summary>
		/// DPX-specific target settings.
		/// </summary>
		internal HTML5TargetRules HTML5Platform = new HTML5TargetRules();
	}

	partial class ReadOnlyTargetRules
	{
		private ReadOnlyHTML5TargetRules _HTML5Platform = null;

		/// <summary>
		/// HTML5-specific target settings
		/// </summary>
		public ReadOnlyHTML5TargetRules HTML5Platform
		{
			get
			{
				if (_HTML5Platform == null)
				{
					_HTML5Platform = new ReadOnlyHTML5TargetRules(Inner.HTML5Platform);
				}
				return _HTML5Platform;
			}
		}

		/// <summary>
		/// Return an optimization suffix for all standard HTML5 libs based on Target settings
		/// </summary>
		public string HTML5OptimizationSuffix
		{
			get
			{
				if (bCompileForSize)
				{
					return "_Oz";
				}
				else
				{
					if (Configuration == UnrealTargetConfiguration.Development)
					{
						return "_O2";
					}
					else if (Configuration == UnrealTargetConfiguration.Shipping)
					{
						return "_O3";
					}
				}
				return "";
			}
		}
	}


	class HTML5Platform : UEBuildPlatform
	{
		/// <summary>
		/// Architecture to build for.
		/// </summary>
		[XmlConfigFile]
		public static string HTML5Architecture = "";

		HTML5PlatformSDK SDK;

		public HTML5Platform(HTML5PlatformSDK InSDK)
			: base(UnrealTargetPlatform.HTML5)
		{
			SDK = InSDK;
		}

		/// <summary>
		/// Whether the required external SDKs are installed for this platform. Could be either a manual install or an AutoSDK.
		/// </summary>
		public override SDKStatus HasRequiredSDKsInstalled()
		{
			return SDK.HasRequiredSDKsInstalled();
		}

		// The current architecture - affects everything about how UBT operates on HTML5
		public override string GetDefaultArchitecture(FileReference ProjectFile)
		{
			// by default, use an empty architecture (which is really just a modifier to the platform for some paths/names)
			return HTML5Platform.HTML5Architecture;
		}

		public override void ResetTarget(TargetRules Target)
		{
			ValidateTarget(Target);
		}

		public override void ValidateTarget(TargetRules Target)
		{
			Target.bCompileAPEX = false;
			Target.bCompileNvCloth = false;
			Target.bCompilePhysX = true;
			Target.bCompileForSize = false;			// {true:[all:-Oz], false:[developer:-O2, shipping:-O3]}  WARNING: need emscripten version >= 1.37.13
			Target.bUsePCHFiles = false;
			Target.bDeployAfterCompile = true;
		}

		public override bool CanUseXGE()
		{
			return false; // NOTE: setting to true may break CIS builds...
		}

		/// <summary>
		/// Determines if the given name is a build product for a target.
		/// </summary>
		/// <param name="FileName">The name to check</param>
		/// <param name="NamePrefixes">Target or application names that may appear at the start of the build product name (eg. "UE4Editor", "ShooterGameEditor")</param>
		/// <param name="NameSuffixes">Suffixes which may appear at the end of the build product name</param>
		/// <returns>True if the string matches the name of a build product, false otherwise</returns>
		public override bool IsBuildProduct(string FileName, string[] NamePrefixes, string[] NameSuffixes)
		{
			return IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".js")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, HTML5ToolChain.libExt);
		}

		/// <summary>
		/// Get the extension to use for the given binary type
		/// </summary>
		/// <param name="InBinaryType"> The binary type being built</param>
		/// <returns>string    The binary extension (ie 'exe' or 'dll')</returns>
		public override string GetBinaryExtension(UEBuildBinaryType InBinaryType)
		{
			switch (InBinaryType)
			{
				case UEBuildBinaryType.DynamicLinkLibrary:
					return ".js";
				case UEBuildBinaryType.Executable:
					return ".js";
				case UEBuildBinaryType.StaticLibrary:
					return HTML5ToolChain.libExt;
			}

			return base.GetBinaryExtension(InBinaryType);
		}

		/// <summary>
		/// Get the extensions to use for debug info for the given binary type
		/// </summary>
		/// <param name="InTarget">The target being built</param>
		/// <param name="InBinaryType"> The binary type being built</param>
		/// <returns>string[]    The debug info extensions (i.e. 'pdb')</returns>
		public override string[] GetDebugInfoExtensions(ReadOnlyTargetRules InTarget, UEBuildBinaryType InBinaryType)
		{
			return new string [] {};
		}

		/// <summary>
		/// Whether this platform should build a monolithic binary
		/// </summary>
		public override bool ShouldCompileMonolithicBinary(UnrealTargetPlatform InPlatform)
		{
			// This platform currently always compiles monolithic
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
			// set PLATFORM_HTML5 to 0 in modules that HTML5 will set to 1, so that #if checks don't trigger the error of being undefined,
			// and we don't need to set PLATFORM_HTML5 to 0 in Core somewhere
// leaving for future reference...
//			if (Rules.bAllowConfidentialPlatformDefines)
//			{
//				Rules.PublicDefinitions.Add("PLATFORM_HTML5=0");
//			}

			if (Target.Platform == UnrealTargetPlatform.Win32 ||
				Target.Platform == UnrealTargetPlatform.Win64 ||
				Target.Platform == UnrealTargetPlatform.Mac ||
				Target.Platform == UnrealTargetPlatform.Linux)
			{
				// allow standalone tools to use targetplatform modules, without needing Engine
				if ((!Target.bBuildRequiresCookedData
					&& ModuleName == "Engine"
					&& Target.bBuildDeveloperTools)
					|| (Target.bForceBuildTargetPlatforms && ModuleName == "TargetPlatform"))
				{
					Rules.DynamicallyLoadedModuleNames.Add("HTML5TargetPlatform");
				}
            }

			if (ModuleName == "UnrealEd")
			{
				Rules.DynamicallyLoadedModuleNames.Add("HTML5PlatformEditor");
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
			// enable the platform define in modules that explicitly allow it (ie licensee game modules)
// leaving for future reference...
//			if (Rules.bAllowConfidentialPlatformDefines)
//			{
//				Rules.PublicDefinitions.Add("PLATFORM_HTML5=1");
//			}

			// overridable #ifdef's
			Rules.PublicDefinitions.Add("PLATFORM_USE_SHOWFLAGS_ALWAYS_BITFIELD=0"); // i.e. use boolean version
			Rules.PublicDefinitions.Add("ALLOW_LQ_LIGHTMAPS=1");
			Rules.PublicDefinitions.Add("ULANG_PLATFORM_HTML5=1");

			if (ModuleName == "Core")
			{
				Rules.PublicDependencyModuleNames.Add("zlib");
			}
			else if (ModuleName == "Engine")
			{
				Rules.PrivateDependencyModuleNames.Add("zlib");
				Rules.PrivateDependencyModuleNames.Add("UElibPNG");
				Rules.PublicDependencyModuleNames.Add("UEOgg");
				Rules.PublicDependencyModuleNames.Add("Vorbis");
			}
		}

		/// <summary>
		/// Setup the target environment for building
		/// </summary>
		/// <param name="Target">Settings for the target being compiled</param>
		/// <param name="CompileEnvironment">The compile environment for this target</param>
		/// <param name="LinkEnvironment">The link environment for this target</param>
		public override void SetUpEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment CompileEnvironment, LinkEnvironment LinkEnvironment)
		{
			CompileEnvironment.Definitions.Add("PLATFORM_IS_EXTENSION=1");

			// @todo needed?
			CompileEnvironment.Definitions.Add("UNICODE");
			CompileEnvironment.Definitions.Add("_UNICODE");
			CompileEnvironment.Definitions.Add("WITH_AUTOMATION_WORKER=0");
			CompileEnvironment.Definitions.Add("WITH_OGGVORBIS=1");
			CompileEnvironment.Definitions.Add("USE_SCENE_LOCK=0");

		}

		/// <summary>
		/// Whether this platform should create debug information or not
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <returns>true if debug info should be generated, false if not</returns>
		public override bool ShouldCreateDebugInfo(ReadOnlyTargetRules Target)
		{
			switch (Target.Configuration)
			{
				case UnrealTargetConfiguration.Development:
				case UnrealTargetConfiguration.Shipping:
				case UnrealTargetConfiguration.Test:
					return !Target.bOmitPCDebugInfoInDevelopment;
				case UnrealTargetConfiguration.Debug:
				default:
					// We don't need debug info for Emscripten, and it causes a bunch of issues with linking
					return true;
			};
		}

		/// <summary>
		/// Setup the binaries for this specific platform.
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <param name="ExtraModuleNames"></param>
		public override void AddExtraModules(ReadOnlyTargetRules Target, List<string> ExtraModuleNames)
		{
			ExtraModuleNames.Add("HTML5PlatformFeatures");
		}

		/// <summary>
		/// Creates a toolchain instance for the given platform.
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <returns>New toolchain instance.</returns>
		public override UEToolChain CreateToolChain(ReadOnlyTargetRules Target)
		{
			return new HTML5ToolChain(Target.ProjectFile);
		}

		/// <summary>
		/// Deploys the given target
		/// </summary>
		/// <param name="Receipt">Receipt for the target being deployed</param>
		public override void Deploy(TargetReceipt Receipt)
		{
		}
	}

	class HTML5PlatformSDK : UEBuildPlatformSDK
	{
		// platforms can choose if they prefer a correct the the AutoSDK install over the manual install.
		protected override bool PreferAutoSDK()
		{
			// HTML5 build toolchain is found at: Engine/Platforms/HTML5/Build/emsdk/...
			return false;
		}

		public override string GetSDKTargetPlatformName()
		{
			return "HTML5";
		}

		/// <summary>
		/// Whether the required external SDKs are installed for this platform
		/// </summary>
		protected override SDKStatus HasRequiredManualSDKInternal()
		{
			if (!HTML5SDKInfo.IsSDKInstalled())
			{
				return SDKStatus.Invalid;
			}
			return SDKStatus.Valid;
		}
	}

	class HTML5PlatformFactory : UEBuildPlatformFactory
	{
		public override UnrealTargetPlatform TargetPlatform
		{
			get { return UnrealTargetPlatform.HTML5; }
		}

		/// <summary>
		/// Register the platform with the UEBuildPlatform class
		/// </summary>
		public override void RegisterBuildPlatforms()
		{
			HTML5PlatformSDK SDK = new HTML5PlatformSDK();
			SDK.ManageAndValidateSDK();

			// Make sure the SDK is installed
			if ((ProjectFileGenerator.bGenerateProjectFiles == true) || (SDK.HasRequiredSDKsInstalled() == SDKStatus.Valid))
			{
				// make sure we have the HTML5 files; if not, then this user doesn't really have HTML5 access/files, no need to compile HTML5!
				FileReference HTML5TargetPlatformFile = FileReference.Combine(UnrealBuildTool.EnginePlatformExtensionsDirectory, "HTML5", "Source", "Developer", "HTML5TargetPlatform", "HTML5TargetPlatform.Build.cs");
				if (!FileReference.Exists(HTML5TargetPlatformFile))
				{
					Log.TraceWarning("Missing required components (.... HTML5TargetPlatformFile, others here...). Check source control filtering, or try resyncing.");
				}
				else
				{
					// Register this build platform for HTML5
					Log.TraceVerbose("        Registering for {0}", UnrealTargetPlatform.HTML5.ToString());
					UEBuildPlatform.RegisterBuildPlatform(new HTML5Platform(SDK));
				}
			}
		}
	}
}
