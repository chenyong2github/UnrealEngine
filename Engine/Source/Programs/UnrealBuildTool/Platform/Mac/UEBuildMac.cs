// Copyright Epic Games, Inc. All Rights Reserved.


using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using Tools.DotNETCommon;
using System.Linq;

namespace UnrealBuildTool
{
	/// <summary>
	/// Mac-specific target settings
	/// </summary>
	public class MacTargetRules
	{
		/// <summary>
		/// Enables address sanitizer (ASan).
		/// </summary>
		[CommandLine("-EnableASan")]
		[XmlConfigFile(Category = "BuildConfiguration", Name = "bEnableAddressSanitizer")]
		public bool bEnableAddressSanitizer = false;

		/// <summary>
		/// Enables thread sanitizer (TSan).
		/// </summary>
		[CommandLine("-EnableTSan")]
		[XmlConfigFile(Category = "BuildConfiguration", Name = "bEnableThreadSanitizer")]
		public bool bEnableThreadSanitizer = false;

		/// <summary>
		/// Enables undefined behavior sanitizer (UBSan).
		/// </summary>
		[CommandLine("-EnableUBSan")]
		[XmlConfigFile(Category = "BuildConfiguration", Name = "bEnableUndefinedBehaviorSanitizer")]
		public bool bEnableUndefinedBehaviorSanitizer = false;
	}

	/// <summary>
	/// Read-only wrapper for Mac-specific target settings
	/// </summary>
	public class ReadOnlyMacTargetRules
	{
		/// <summary>
		/// The private mutable settings object
		/// </summary>
		private MacTargetRules Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The settings object to wrap</param>
		public ReadOnlyMacTargetRules(MacTargetRules Inner)
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

		public bool bEnableAddressSanitizer
		{
			get { return Inner.bEnableAddressSanitizer; }
		}

		public bool bEnableThreadSanitizer
		{
			get { return Inner.bEnableThreadSanitizer; }
		}

		public bool bEnableUndefinedBehaviorSanitizer
		{
			get { return Inner.bEnableUndefinedBehaviorSanitizer; }
		}		

		#if !__MonoCS__
		#pragma warning restore CS1591
		#endif
		#endregion
	}

	class MacPlatform : UEBuildPlatform
	{
		MacPlatformSDK SDK;

		public MacPlatform(MacPlatformSDK InSDK) : base(UnrealTargetPlatform.Mac)
		{
			SDK = InSDK;
		}

		public override SDKStatus HasRequiredSDKsInstalled()
		{
			return SDK.HasRequiredSDKsInstalled();
		}

		public override bool CanUseXGE()
		{
			return false;
		}

		public override bool CanUseDistcc()
		{
			return true;
		}

		public override bool CanUseFASTBuild()
		{
			return true;
		}

		public override void ResetTarget(TargetRules Target)
		{			
		}

		public override void ValidateTarget(TargetRules Target)
		{
			if (BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Mac)
			{
				// @todo: Temporarily disable precompiled header files when building remotely due to errors
				Target.bUsePCHFiles = false;
			}

			// Needs OS X 10.11 for Metal. The remote toolchain has not been initialized yet, so just assume it's a recent SDK.
			if ((BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Mac || MacToolChain.Settings.MacOSSDKVersionFloat >= 10.11f) && Target.bCompileAgainstEngine)
			{
				Target.GlobalDefinitions.Add("HAS_METAL=1");
				Target.ExtraModuleNames.Add("MetalRHI");
			}
			else
			{
				Target.GlobalDefinitions.Add("HAS_METAL=0");
			}

			// Force using the ANSI allocator if ASan is enabled
			string AddressSanitizer = Environment.GetEnvironmentVariable("ENABLE_ADDRESS_SANITIZER");
			if(Target.MacPlatform.bEnableAddressSanitizer || (AddressSanitizer != null && AddressSanitizer == "YES"))
			{
				Target.GlobalDefinitions.Add("FORCE_ANSI_ALLOCATOR=1");
			}

			Target.GlobalDefinitions.Add("GL_SILENCE_DEPRECATION=1");

			Target.bUsePDBFiles = !Target.bDisableDebugInfo && ShouldCreateDebugInfo(new ReadOnlyTargetRules(Target));

			// we always deploy - the build machines need to be able to copy the files back, which needs the full bundle
			Target.bDeployAfterCompile = true;

			Target.bCheckSystemHeadersForModification = BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Mac;

			// Mac-Arm todo - Do we need to compile in two passes so we can set this differently?
			bool bCompilingForArm = Target.Architecture.IndexOf("arm", StringComparison.OrdinalIgnoreCase) >= 0;
			bool bCompilingMultipleArchitectures = Target.Architecture.Contains("+");
			Target.bCompileISPC = !bCompilingForArm;
			Target.bUsePCHFiles = !bCompilingMultipleArchitectures;
		}

		/// <summary>
		/// Returns true since we can do this on Mac (with some caveats, that may necessitate this being an option)
		/// </summary>
		/// <param name="InArchitectures">Architectures that are being built</param>
		public override bool CanBuildArchitecturesInSinglePass(IEnumerable<string> InArchitectures)
		{
			return true;
		}

		/// <summary>
		/// Allows the platform to override whether the architecture name should be appended to the name of binaries.
		/// </summary>
		/// <returns>True if the architecture name should be appended to the binary</returns>
		public override bool RequiresArchitectureSuffix()
		{
			return false;
		}

		/// <summary>
		/// Get the default architecture for a project. This may be overriden on the command line to UBT.
		/// </summary>
		/// <param name="ProjectFile">Optional project to read settings from </param>
		public override string GetDefaultArchitecture(FileReference ProjectFile)
		{
			// by default use Intel.
			return MacExports.DefaultArchitecture;
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
			return IsBuildProductName(FileName, NamePrefixes, NameSuffixes, "")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".dsym")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".dylib")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".a")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".app");
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
					return ".dylib";
				case UEBuildBinaryType.Executable:
					return "";
				case UEBuildBinaryType.StaticLibrary:
					return ".a";
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
			switch (InBinaryType)
			{
				case UEBuildBinaryType.DynamicLinkLibrary:
				case UEBuildBinaryType.Executable:
					return Target.bUsePDBFiles ? new string[] {".dSYM"} : new string[] {};
				case UEBuildBinaryType.StaticLibrary:
				default:
					return new string [] {};
			}
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
		}

		public override DirectoryReference GetBundleDirectory(ReadOnlyTargetRules Rules, List<FileReference> OutputFiles)
		{
			if (Rules.bIsBuildingConsoleApplication)
			{
				return null;
			}
			else
			{
				return OutputFiles[0].Directory.ParentDirectory.ParentDirectory;
			}
		}

		/// <summary>
		/// For platforms that need to output multiple files per binary (ie Android "fat" binaries)
		/// this will emit multiple paths. By default, it simply makes an array from the input
		/// </summary>
		public override List<FileReference> FinalizeBinaryPaths(FileReference BinaryName, FileReference ProjectFile, ReadOnlyTargetRules Target)
		{
			List<FileReference> BinaryPaths = new List<FileReference>();
			if (Target.bIsBuildingConsoleApplication || !String.IsNullOrEmpty(BinaryName.GetExtension()))
			{
				BinaryPaths.Add(BinaryName);
			}
			else
			{
				BinaryPaths.Add(new FileReference(BinaryName.FullName + ".app/Contents/MacOS/" + BinaryName.GetFileName()));
			}
			return BinaryPaths;
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
			bool bBuildShaderFormats = Target.bForceBuildShaderFormats;

			if (!Target.bBuildRequiresCookedData)
			{
				if (ModuleName == "TargetPlatform")
				{
					bBuildShaderFormats = true;
				}
			}

			// allow standalone tools to use target platform modules, without needing Engine
			if (ModuleName == "TargetPlatform")
			{
				if (Target.bForceBuildTargetPlatforms)
				{
					Rules.DynamicallyLoadedModuleNames.Add("MacTargetPlatform");
					Rules.DynamicallyLoadedModuleNames.Add("MacNoEditorTargetPlatform");
					Rules.DynamicallyLoadedModuleNames.Add("MacClientTargetPlatform");
					Rules.DynamicallyLoadedModuleNames.Add("MacServerTargetPlatform");
					Rules.DynamicallyLoadedModuleNames.Add("AllDesktopTargetPlatform");
				}

				if (bBuildShaderFormats)
				{
					// Rules.DynamicallyLoadedModuleNames.Add("ShaderFormatD3D");
					Rules.DynamicallyLoadedModuleNames.Add("ShaderFormatOpenGL");
					Rules.DynamicallyLoadedModuleNames.Add("MetalShaderFormat");
					Rules.DynamicallyLoadedModuleNames.Add("ShaderFormatVectorVM");

					Rules.DynamicallyLoadedModuleNames.Remove("VulkanRHI");
					Rules.DynamicallyLoadedModuleNames.Add("VulkanShaderFormat");
				}
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
			CompileEnvironment.Definitions.Add("PLATFORM_MAC=1");
			CompileEnvironment.Definitions.Add("PLATFORM_APPLE=1");

			CompileEnvironment.Definitions.Add("WITH_TTS=0");
			CompileEnvironment.Definitions.Add("WITH_SPEECH_RECOGNITION=0");
		}

		/// <summary>
		/// Whether this platform should create debug information or not
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <returns>true if debug info should be generated, false if not</returns>
		public override bool ShouldCreateDebugInfo(ReadOnlyTargetRules Target)
		{
			// Always generate debug symbols on the build machines.
			bool IsBuildMachine = Environment.GetEnvironmentVariable("IsBuildMachine") == "1";

			switch (Target.Configuration)
			{
				case UnrealTargetConfiguration.Development:
				case UnrealTargetConfiguration.Shipping:
				case UnrealTargetConfiguration.Test:
					return !Target.bOmitPCDebugInfoInDevelopment || IsBuildMachine;
				case UnrealTargetConfiguration.DebugGame:
				case UnrealTargetConfiguration.Debug:
				default:
					return true;
			};
		}

		/// <summary>
		/// Creates a toolchain instance for the given platform.
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <returns>New toolchain instance.</returns>
		public override UEToolChain CreateToolChain(ReadOnlyTargetRules Target)
		{
			MacToolChainOptions Options = MacToolChainOptions.None;

			string AddressSanitizer = Environment.GetEnvironmentVariable("ENABLE_ADDRESS_SANITIZER");
			string ThreadSanitizer = Environment.GetEnvironmentVariable("ENABLE_THREAD_SANITIZER");
			string UndefSanitizerMode = Environment.GetEnvironmentVariable("ENABLE_UNDEFINED_BEHAVIOR_SANITIZER");

			if(Target.MacPlatform.bEnableAddressSanitizer || (AddressSanitizer != null && AddressSanitizer == "YES"))
			{
				Options |= MacToolChainOptions.EnableAddressSanitizer;
			}
			if(Target.MacPlatform.bEnableThreadSanitizer || (ThreadSanitizer != null && ThreadSanitizer == "YES"))
			{
				Options |= MacToolChainOptions.EnableThreadSanitizer;
			}
			if(Target.MacPlatform.bEnableUndefinedBehaviorSanitizer || (UndefSanitizerMode != null && UndefSanitizerMode == "YES"))
			{
				Options |= MacToolChainOptions.EnableUndefinedBehaviorSanitizer;
			}
			if(Target.bShouldCompileAsDLL)
			{
				Options |= MacToolChainOptions.OutputDylib;
			}

			return new MacToolChain(Target.ProjectFile, Options);
		}

		/// <summary>
		/// Deploys the given target
		/// </summary>
		/// <param name="Receipt">Receipt for the target being deployed</param>
		public override void Deploy(TargetReceipt Receipt)
		{
			new UEDeployMac().PrepTargetForDeployment(Receipt);
		}
	}

	class MacPlatformSDK : UEBuildPlatformSDK
	{
		protected override SDKStatus HasRequiredManualSDKInternal()
		{
			return SDKStatus.Valid;
		}
	}

	class MacPlatformFactory : UEBuildPlatformFactory
	{
		public override UnrealTargetPlatform TargetPlatform
		{
			get { return UnrealTargetPlatform.Mac; }
		}

		/// <summary>
		/// Register the platform with the UEBuildPlatform class
		/// </summary>
		public override void RegisterBuildPlatforms()
		{
			MacPlatformSDK SDK = new MacPlatformSDK();
			SDK.ManageAndValidateSDK();

			// Register this build platform for Mac
			UEBuildPlatform.RegisterBuildPlatform(new MacPlatform(SDK));
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Mac, UnrealPlatformGroup.Apple);
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Mac, UnrealPlatformGroup.Desktop);
		}
	}
}
