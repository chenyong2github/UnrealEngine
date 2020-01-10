// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using Tools.DotNETCommon;
using UnrealBuildTool;


namespace UnrealBuildTool
{
	partial struct UnrealTargetPlatform
	{
		/// <summary>
		/// XXX
		/// </summary>
		public static UnrealTargetPlatform XXX = FindOrAddByName("XXX");
	}

	partial struct UnrealPlatformGroup
	{
		/// <summary>
		/// Fake group for putting fake platforms into
		/// </summary>
		public static UnrealPlatformGroup Fake = FindOrAddByName("Fake");
	}

	/// <summary>
	/// 
	/// </summary>
	public class XXXTargetRules
	{
		/// <summary>
		/// Some setting
		/// </summary>
		public bool bSomeSetting = false;
	}

	/// <summary>
	/// 
	/// </summary>
	public class ReadOnlyXXXTargetRules
	{
		private XXXTargetRules Inner;

		/// <summary>
		/// Some setting
		/// </summary>
		public bool bSomeSetting
		{
			get { return Inner.bSomeSetting; }
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="Inner"></param>
		public ReadOnlyXXXTargetRules(XXXTargetRules Inner)
		{
			this.Inner = Inner;

			UnrealPlatformGroup Group = UnrealPlatformGroup.Android;
		}
	}

	abstract partial class TargetRules
	{
		/// <summary>
		/// XXX-specific target settings.
		/// </summary>
		public XXXTargetRules XXXPlatform = new XXXTargetRules();
	}


	public partial class ReadOnlyTargetRules
	{
		private ReadOnlyXXXTargetRules _XXXPlatform = null;
		/// <summary>
		/// 
		/// </summary>
		public ReadOnlyXXXTargetRules XXXPlatform
		{
			get
			{
				if (_XXXPlatform == null)
				{
					_XXXPlatform = new ReadOnlyXXXTargetRules(Inner.XXXPlatform);
				}
				return _XXXPlatform;
			}
		}
	}
}


class XXXPlatform : UEBuildPlatform
{
	// the SDK instance
	XXXPlatformSDK SDK;

	public XXXPlatform(XXXPlatformSDK InSDK)
		: base(UnrealTargetPlatform.XXX)
	{
		SDK = InSDK;
	}

	public override SDKStatus HasRequiredSDKsInstalled()
	{
		return SDK.HasRequiredSDKsInstalled();
	}

	public override void ResetTarget(TargetRules Target)
	{
		// set a dummy setting
		Target.XXXPlatform.bSomeSetting = true;

		// @TODO NEW PLATFORM: Delete this line:
		Target.bUsePCHFiles = false;
	}

	public override void ValidateTarget(TargetRules Target)
	{
		Target.bDeployAfterCompile = true;
		Target.bCompileNvCloth = true;
	}

	/// <summary>
	/// If this platform can be compiled with XGE
	/// </summary>
	public override bool CanUseXGE()
	{
		// @TODO NEW PLATFORM: You probably want this to return true
		return false;
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
		return IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".xdll")
			|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".xexe")
			|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".xa");
	}

	/// <summary>
	/// Get the extension to use for the given binary type
	/// </summary>
	/// <param name="InBinaryType"> The binrary type being built</param>
	/// <returns>string    The binary extenstion (ie 'exe' or 'dll')</returns>
	public override string GetBinaryExtension(UEBuildBinaryType InBinaryType)
	{
		switch (InBinaryType)
		{
			case UEBuildBinaryType.DynamicLinkLibrary:
				return ".xdll";
			case UEBuildBinaryType.Executable:
				return ".xexe";
			case UEBuildBinaryType.StaticLibrary:
				return ".xa";
		}
		return base.GetBinaryExtension(InBinaryType);
	}

	/// <summary>
	/// Get the extensions to use for debug info for the given binary type
	/// </summary>
	/// <param name="Target">Rules for the target being built</param>
	/// <param name="InBinaryType"> The binary type being built</param>
	/// <returns>string[]    The debug info extensions (i.e. 'pdb')</returns>
	public override string[] GetDebugInfoExtensions(ReadOnlyTargetRules Target, UEBuildBinaryType InBinaryType)
	{
		return new string[] { };
	}

	/// <summary>
	/// Whether this platform should build a monolithic binary
	/// </summary>
	public override bool ShouldCompileMonolithicBinary(UnrealTargetPlatform InPlatform)
	{
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
		// set PLATFORM_XXX to 0 in modules that XXX will set to 1, so that #if checks don't trigger the error of being undefined,
		// and we don't need to set PLATFORM_XXX to 0 in Core somewhere
		if (Rules.bAllowConfidentialPlatformDefines)
		{
			Rules.PublicDefinitions.Add("PLATFORM_XXX=0");
		}

		if (ModuleName == "RHI")
		{
			// these must be mirrored down below
			Rules.AppendStringToPublicDefinition("DDPI_EXTRA_SHADERPLATFORMS", "SP_XXX=32, ");
			Rules.AppendStringToPublicDefinition("DDPI_SHADER_PLATFORM_NAME_MAP", "{ TEXT(\"XXX\"), SP_XXX },");
		}

		// don't do any target platform stuff if SDK is not available
		if (!UEBuildPlatform.IsPlatformAvailable(Platform))
		{
			return;
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			bool bIsMonolithic = true;
			UEBuildPlatform BuildPlatform = GetBuildPlatform(Target.Platform);

			if (BuildPlatform != null)
			{
				bIsMonolithic = BuildPlatform.ShouldCompileMonolithicBinary(Target.Platform);
			}

			if ((Target.bBuildEditor == false) || !bIsMonolithic)
			{
				bool bBuildShaderFormats = Target.bForceBuildShaderFormats;

				if (!Target.bBuildRequiresCookedData)
				{
					if (ModuleName == "Engine")
					{
						if (Target.bBuildDeveloperTools)
						{
 							Rules.DynamicallyLoadedModuleNames.Add("XXXTargetPlatform");
						}
					}
				}

				// allow standalone tools to use target platform modules, without needing Engine
				if (ModuleName == "TargetPlatform")
				{
					if (Target.bForceBuildTargetPlatforms)
					{
 						Rules.DynamicallyLoadedModuleNames.Add("XXXTargetPlatform");
					}

					if (bBuildShaderFormats)
					{
// 						Rules.DynamicallyLoadedModuleNames.Add("XXXShaderFormat");
					}
				}
			}
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
		if (Rules.bAllowConfidentialPlatformDefines)
		{
			Rules.PublicDefinitions.Add("PLATFORM_XXX=1");
		}

		if (ModuleName == "Core")
		{
// 			Rules.PublicIncludePaths.Add("Runtime/Core/Public/XXX");
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
		// read a setting from the read only target rules, specific to XXX
		CompileEnvironment.Definitions.Add(string.Format("SOMESETTING={0}", Target.XXXPlatform.bSomeSetting));

		CompileEnvironment.Definitions.Add("PLATFORM_DESKTOP=0");
		CompileEnvironment.Definitions.Add("PLATFORM_64BITS=1");
		CompileEnvironment.Definitions.Add("EXCEPTIONS_DISABLED=0");
		CompileEnvironment.Definitions.Add("UNICODE");
		CompileEnvironment.Definitions.Add("_UNICODE");
	}

	/// <summary>
	/// Setup the configuration environment for building
	/// </summary>
	/// <param name="Target"> The target being built</param>
	/// <param name="GlobalCompileEnvironment">The global compile environment</param>
	/// <param name="GlobalLinkEnvironment">The global link environment</param>
	public override void SetUpConfigurationEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment GlobalCompileEnvironment, LinkEnvironment GlobalLinkEnvironment)
	{
		base.SetUpConfigurationEnvironment(Target, GlobalCompileEnvironment, GlobalLinkEnvironment);
		GlobalLinkEnvironment.bCreateDebugInfo = Target.Configuration != UnrealTargetConfiguration.Shipping;
	}

	/// <summary>
	/// Whether this platform should create debug information or not
	/// </summary>
	/// <param name="Target">The target being built</param>
	/// <returns>bool    true if debug info should be generated, false if not</returns>
	public override bool ShouldCreateDebugInfo(ReadOnlyTargetRules Target)
	{
		switch (Target.Configuration)
		{
			case UnrealTargetConfiguration.Development:
			case UnrealTargetConfiguration.Shipping:
			case UnrealTargetConfiguration.Test:
			case UnrealTargetConfiguration.Debug:
			default:
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
	}

	/// <summary>
	/// Creates a toolchain instance for the given platform.
	/// </summary>
	/// <param name="Target">The target being built</param>
	/// <returns>New toolchain instance.</returns>
	public override UEToolChain CreateToolChain(ReadOnlyTargetRules Target)
	{
		return new XXXToolChain();
	}

	/// <summary>
	/// Deploys the given target
	/// </summary>
	/// <param name="Receipt">Receipt for the target being deployed</param>
	public override void Deploy(TargetReceipt Receipt)
	{
	}
}

class XXXPlatformSDK : UEBuildPlatformSDK
{
	// Platform name for finding SDK under //CarefullyRedist
	public const string TargetPlatformName = "XXX";

	/// <summary>
	/// Whether platform supports switching SDKs during runtime
	/// </summary>
	/// <returns>true if supports</returns>
	protected override bool PlatformSupportsAutoSDKs()
	{
		return true;
	}

	/// <summary>
	/// Returns platform-specific name used in SDK repository
	/// </summary>
	/// <returns>path to SDK Repository</returns>
	public override string GetSDKTargetPlatformName()
	{
		return TargetPlatformName;
	}

	/// <summary>
	/// Returns SDK string as required by the platform
	/// </summary>
	/// <returns>Valid SDK string</returns>
	protected override string GetRequiredSDKString()
	{
		return "1.0.0";
	}

	protected override String GetRequiredScriptVersionString()
	{
		// major.minor.bumps (bumps used to force reapplication of an SDK update)
		return "1.0.0";
	}

	/// <summary>
	/// Gets the version of the SDK installed by looking at SCE_ORBIS_SDK_VERSION in sdk_version.h
	/// </summary>
	/// <returns>SDK version</returns>
	static public uint GetInstalledSDKVersion()
	{
		return 100;
	}

	/// <summary>
	/// Converts a hex SDK version to a printable string
	/// </summary>
	/// <returns>SDK version as a string</returns>
	static public string SDKVersionToString(uint SDKVersion)
	{
		return "1.0.0";
	}

	/// <summary>
	/// Converts an SDK version string to a uint
	/// </summary>
	/// <returns>SDK version as a uint</returns>
	static public uint SDKVersionStringToUint(string VersionString)
	{
		return 100;
	}


	/// <summary>
	/// Whether the required external SDKs are installed for this platform
	/// </summary>
	protected override SDKStatus HasRequiredManualSDKInternal()
	{
		return SDKStatus.Valid;
	}
}


class XXXPlatformFactory : UEBuildPlatformFactory
{
	public override UnrealTargetPlatform TargetPlatform
	{
		get { return UnrealTargetPlatform.XXX; }
	}

	/// <summary>
	/// Register the platform with the UEBuildPlatform class
	/// </summary>
	public override void RegisterBuildPlatforms()
	{
 		XXXPlatformSDK SDK = new XXXPlatformSDK();
 		SDK.ManageAndValidateSDK();

		// Register this build platform for XXX
		UEBuildPlatform.RegisterBuildPlatform(new XXXPlatform(SDK));
		UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.XXX, UnrealPlatformGroup.Fake);
	}
}