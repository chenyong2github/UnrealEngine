// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Android-specific target settings
	/// </summary>
	public partial class AndroidTargetRules
	{
		/// <summary>
		/// Lists Architectures that you want to build
		/// </summary>
		[CommandLine("-Architectures=", ListSeparator = '+')]
		public List<string> Architectures = new List<string>();

		/// <summary>
		/// Lists GPU Architectures that you want to build (mostly used for mobile etc.)
		/// </summary>
		[CommandLine("-GPUArchitectures=", ListSeparator = '+')]
		public List<string> GPUArchitectures = new List<string>();
	}

	/// <summary>
	/// Read-only wrapper for Android-specific target settings
	/// </summary>
	public partial class ReadOnlyAndroidTargetRules
	{
		/// <summary>
		/// The private mutable settings object
		/// </summary>
		private AndroidTargetRules Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The settings object to wrap</param>
		public ReadOnlyAndroidTargetRules(AndroidTargetRules Inner)
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

		public IReadOnlyList<string> Architectures
		{
			get { return Inner.Architectures.AsReadOnly(); }
		}

		public IReadOnlyList<string> GPUArchitectures
		{
			get { return Inner.GPUArchitectures.AsReadOnly(); }
		}

		#if !__MonoCS__
		#pragma warning restore CS1591
		#endif
		#endregion
	}

	class AndroidPlatform : UEBuildPlatform
	{
		UEBuildPlatformSDK SDK;

		public AndroidPlatform(UnrealTargetPlatform InTargetPlatform, UEBuildPlatformSDK InSDK) 
			: base(InTargetPlatform, InSDK)
		{
			SDK = InSDK;
		}

		public AndroidPlatform(AndroidPlatformSDK InSDK) : this(UnrealTargetPlatform.Android, InSDK)
		{
		}

		public override void ResetTarget(TargetRules Target)
		{
			ValidateTarget(Target);

			Target.bDeployAfterCompile = true;
		}

		public override void ValidateTarget(TargetRules Target)
		{
			Target.bCompilePhysX = true;
			Target.bCompileAPEX = false;
			Target.bCompileNvCloth = false;

			Target.bCompileRecast = true;
			Target.bCompileISPC = false;
		}

		public override bool CanUseXGE()
		{
			// Disable when building on Linux
			return BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Linux;
		}

		public override bool IsBuildProduct(string FileName, string[] NamePrefixes, string[] NameSuffixes)
		{
			return IsBuildProductWithArch(FileName, NamePrefixes, NameSuffixes, ".so")
				|| IsBuildProductWithArch(FileName, NamePrefixes, NameSuffixes, ".apk")
				|| IsBuildProductWithArch(FileName, NamePrefixes, NameSuffixes, ".a");
		}

		static bool IsBuildProductWithArch(string Name, string[] NamePrefixes, string[] NameSuffixes, string Extension)
		{
			// Strip off the extension, then a GPU suffix, then a CPU suffix, before testing whether it matches a build product name.
			if (Name.EndsWith(Extension, StringComparison.InvariantCultureIgnoreCase))
			{
				int ExtensionEndIdx = Name.Length - Extension.Length;
				foreach (string GpuSuffix in AndroidToolChain.AllGpuSuffixes)
				{
					int GpuIdx = ExtensionEndIdx - GpuSuffix.Length;
					if (GpuIdx > 0 && String.Compare(Name, GpuIdx, GpuSuffix, 0, GpuSuffix.Length, StringComparison.InvariantCultureIgnoreCase) == 0)
					{
						foreach (string CpuSuffix in AndroidToolChain.AllCpuSuffixes)
						{
							int CpuIdx = GpuIdx - CpuSuffix.Length;
							if (CpuIdx > 0 && String.Compare(Name, CpuIdx, CpuSuffix, 0, CpuSuffix.Length, StringComparison.InvariantCultureIgnoreCase) == 0)
							{
								return IsBuildProductName(Name, 0, CpuIdx, NamePrefixes, NameSuffixes);
							}
						}
					}
				}
			}
			return false;
		}

		public override string GetBinaryExtension(UEBuildBinaryType InBinaryType)
		{
			switch (InBinaryType)
			{
				case UEBuildBinaryType.DynamicLinkLibrary:
					return ".so";
				case UEBuildBinaryType.Executable:
					return ".so";
				case UEBuildBinaryType.StaticLibrary:
					return ".a";
			}
			return base.GetBinaryExtension(InBinaryType);
		}

		public override string[] GetDebugInfoExtensions(ReadOnlyTargetRules InTarget, UEBuildBinaryType InBinaryType)
		{
			return new string [] {};
		}

		public override void FindAdditionalBuildProductsToClean(ReadOnlyTargetRules Target, List<FileReference> FilesToDelete, List<DirectoryReference> DirectoriesToDelete)
		{
			base.FindAdditionalBuildProductsToClean(Target, FilesToDelete, DirectoriesToDelete);

			if(Target.ProjectFile != null)
			{
				DirectoriesToDelete.Add(DirectoryReference.Combine(DirectoryReference.FromFile(Target.ProjectFile), "Intermediate", "Android"));
			}
		}

		public virtual bool HasSpecificDefaultBuildConfig(UnrealTargetPlatform Platform, DirectoryReference ProjectPath)
		{
			string[] BoolKeys = new string[] {
				"bBuildForArmV7", "bBuildForArm64", "bBuildForX86", "bBuildForX8664", 
				"bBuildForES31", "bBuildWithHiddenSymbolVisibility", "bUseNEONForArmV7", "bSaveSymbols"
			};
			string[] StringKeys = new string[] {
				"NDKAPILevelOverride"
			};

			// look up Android specific settings
			if (!DoProjectSettingsMatchDefault(Platform, ProjectPath, "/Script/AndroidRuntimeSettings.AndroidRuntimeSettings",
				BoolKeys, null, StringKeys))
			{
				return false;
			}
			return true;
		}

		public override bool HasDefaultBuildConfig(UnrealTargetPlatform Platform, DirectoryReference ProjectPath)
		{
			// @todo Lumin: This is kinda messy - better way?
			if (HasSpecificDefaultBuildConfig(Platform, ProjectPath) == false)
			{
				return false;
			}
			
			// any shared-between-all-androids would be here

			// check the base settings
			return base.HasDefaultBuildConfig(Platform, ProjectPath);
		}

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
							Rules.DynamicallyLoadedModuleNames.Add("AndroidTargetPlatform");
						}
					}
					else if (ModuleName == "TargetPlatform")
					{
						bBuildShaderFormats = true;
						Rules.DynamicallyLoadedModuleNames.Add("TextureFormatDXT");
						Rules.DynamicallyLoadedModuleNames.Add("TextureFormatASTC");
						Rules.DynamicallyLoadedModuleNames.Add("TextureFormatETC2");  // ETC2 
						if (Target.bBuildDeveloperTools)
						{
							//Rules.DynamicallyLoadedModuleNames.Add("AudioFormatADPCM");	//@todo android: android audio
						}
					}
				}

				// allow standalone tools to use targetplatform modules, without needing Engine
				if (ModuleName == "TargetPlatform")
				{
					if (Target.bForceBuildTargetPlatforms)
					{
						Rules.DynamicallyLoadedModuleNames.Add("AndroidTargetPlatform");
					}

					if (bBuildShaderFormats)
					{
						//Rules.DynamicallyLoadedModuleNames.Add("ShaderFormatAndroid");		//@todo android: ShaderFormatAndroid
					}
				}
			}
		}

		public override List<FileReference> FinalizeBinaryPaths(FileReference BinaryName, FileReference ProjectFile, ReadOnlyTargetRules Target)
		{
			AndroidToolChain ToolChain = CreateToolChain(Target) as AndroidToolChain;

			List<string> Architectures = ToolChain.GetAllArchitectures();
			List<string> GPUArchitectures = ToolChain.GetAllGPUArchitectures();

			// make multiple output binaries
			List<FileReference> AllBinaries = new List<FileReference>();
			foreach (string Architecture in Architectures)
			{
				foreach (string GPUArchitecture in GPUArchitectures)
				{
					string BinaryPath;
					if (Target.bShouldCompileAsDLL)
					{
						BinaryPath = Path.Combine(BinaryName.Directory.FullName, Target.Configuration.ToString(), "libUE4.so");
					}
					else
					{
						BinaryPath = AndroidToolChain.InlineArchName(BinaryName.FullName, Architecture, GPUArchitecture);
					}

					AllBinaries.Add(new FileReference(BinaryPath));
				}
			}

			return AllBinaries;
		}

		public override void AddExtraModules(ReadOnlyTargetRules Target, List<string> PlatformExtraModules)
		{
			if (Target.Type != TargetType.Program)
			{
				PlatformExtraModules.Add("VulkanRHI");
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
		}

		public virtual void SetUpSpecificEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment CompileEnvironment, LinkEnvironment LinkEnvironment)
		{
			string NDKPath = Environment.GetEnvironmentVariable("NDKROOT");
			NDKPath = NDKPath.Replace("\"", "");

			AndroidToolChain ToolChain = new AndroidToolChain(Target.ProjectFile, false, Target.AndroidPlatform.Architectures, Target.AndroidPlatform.GPUArchitectures);

			// figure out the NDK version
			string NDKToolchainVersion = SDK.GetInstalledVersion();
			UInt64 NDKVersionInt;
			SDK.TryConvertVersionToInt(NDKToolchainVersion, out NDKVersionInt);

			// PLATFORM_ANDROID_NDK_VERSION is in the form 150100, where 15 is major version, 01 is the letter (1 is 'a'), 00 indicates beta revision if letter is 00
			CompileEnvironment.Definitions.Add(string.Format("PLATFORM_ANDROID_NDK_VERSION={0}", NDKVersionInt));

			Log.TraceInformation("NDK toolchain: {0}, NDK version: {1}, ClangVersion: {2}", NDKToolchainVersion, NDKVersionInt, ToolChain.GetClangVersionString());

			CompileEnvironment.Definitions.Add("PLATFORM_DESKTOP=0");
			CompileEnvironment.Definitions.Add("PLATFORM_CAN_SUPPORT_EDITORONLY_DATA=0");

			CompileEnvironment.Definitions.Add("WITH_OGGVORBIS=1");

			CompileEnvironment.Definitions.Add("UNICODE");
			CompileEnvironment.Definitions.Add("_UNICODE");

			CompileEnvironment.Definitions.Add("PLATFORM_ANDROID=1");
			CompileEnvironment.Definitions.Add("ANDROID=1");

			CompileEnvironment.Definitions.Add("WITH_EDITOR=0");
			CompileEnvironment.Definitions.Add("USE_NULL_RHI=0");

			DirectoryReference NdkDir = new DirectoryReference(NDKPath);
			//CompileEnvironment.SystemIncludePaths.Add(DirectoryReference.Combine(NdkDir, "sources/cxx-stl/llvm-libc++/include"));

			// the toolchain will actually filter these out
			LinkEnvironment.SystemLibraryPaths.Add(DirectoryReference.Combine(NdkDir, "sources/cxx-stl/llvm-libc++/libs/armeabi-v7a"));
			LinkEnvironment.SystemLibraryPaths.Add(DirectoryReference.Combine(NdkDir, "sources/cxx-stl/llvm-libc++/libs/arm64-v8a"));
			LinkEnvironment.SystemLibraryPaths.Add(DirectoryReference.Combine(NdkDir, "sources/cxx-stl/llvm-libc++/libs/x86"));
			LinkEnvironment.SystemLibraryPaths.Add(DirectoryReference.Combine(NdkDir, "sources/cxx-stl/llvm-libc++/libs/x86_64"));

			CompileEnvironment.SystemIncludePaths.Add(DirectoryReference.Combine(NdkDir, "sources/android/native_app_glue"));
			CompileEnvironment.SystemIncludePaths.Add(DirectoryReference.Combine(NdkDir, "sources/android/cpufeatures"));

			//@TODO: Tegra Gfx Debugger - standardize locations - for now, change the hardcoded paths and force this to return true to test
			if (UseTegraGraphicsDebugger(Target))
			{
				//LinkEnvironment.LibraryPaths.Add("ThirdParty/NVIDIA/TegraGfxDebugger");
				//LinkEnvironment.LibraryPaths.Add("F:/NVPACK/android-kk-egl-t124-a32/stub");
				//LinkEnvironment.AdditionalLibraries.Add("Nvidia_gfx_debugger_stub");
			}

			if (!UseTegraGraphicsDebugger(Target))
			{
				LinkEnvironment.SystemLibraries.Add("GLESv3");
				LinkEnvironment.SystemLibraries.Add("EGL");
			}
			LinkEnvironment.SystemLibraries.Add("android");
			LinkEnvironment.SystemLibraries.Add("OpenSLES");
		}

		public override void SetUpEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment CompileEnvironment, LinkEnvironment LinkEnvironment)
		{

			CompileEnvironment.Definitions.Add("PLATFORM_DESKTOP=0");
			CompileEnvironment.Definitions.Add("PLATFORM_CAN_SUPPORT_EDITORONLY_DATA=0");

			CompileEnvironment.Definitions.Add("WITH_OGGVORBIS=1");

			CompileEnvironment.Definitions.Add("UNICODE");
			CompileEnvironment.Definitions.Add("_UNICODE");

			CompileEnvironment.Definitions.Add("PLATFORM_ANDROID=1");
			CompileEnvironment.Definitions.Add("ANDROID=1");

			CompileEnvironment.Definitions.Add("WITH_EDITOR=0");
			CompileEnvironment.Definitions.Add("USE_NULL_RHI=0");

			SetUpSpecificEnvironment(Target, CompileEnvironment, LinkEnvironment);

			// deliberately not linking stl or stdc++ here (c++_shared is default)
			LinkEnvironment.SystemLibraries.Add("c");
			LinkEnvironment.SystemLibraries.Add("dl");
			LinkEnvironment.SystemLibraries.Add("log");
			LinkEnvironment.SystemLibraries.Add("m");
			LinkEnvironment.SystemLibraries.Add("z");
			LinkEnvironment.SystemLibraries.Add("atomic");
		}

		private bool UseTegraGraphicsDebugger(ReadOnlyTargetRules Target)
		{
			// Disable for now
			return false;
		}

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

		public override UEToolChain CreateToolChain(ReadOnlyTargetRules Target)
		{
			bool bUseLdGold = Target.bUseUnityBuild;
			return new AndroidToolChain(Target.ProjectFile, bUseLdGold, Target.AndroidPlatform.Architectures, Target.AndroidPlatform.GPUArchitectures);
		}
		public virtual UEToolChain CreateTempToolChainForProject(FileReference ProjectFile)
		{
			return new AndroidToolChain(ProjectFile, true, null, null);
		}

		/// <summary>
		/// Deploys the given target
		/// </summary>
		/// <param name="Receipt">Receipt for the target being deployed</param>
		public override void Deploy(TargetReceipt Receipt)
		{
			// do not package data if building via UBT
			new UEDeployAndroid(Receipt.ProjectFile, false).PrepTargetForDeployment(Receipt);
		}
	}


	class AndroidPlatformFactory : UEBuildPlatformFactory
	{
		public override UnrealTargetPlatform TargetPlatform
		{
			get { return UnrealTargetPlatform.Android; }
		}

		public override void RegisterBuildPlatforms()
		{
			AndroidPlatformSDK SDK = new AndroidPlatformSDK();

			// Register this build platform
			UEBuildPlatform.RegisterBuildPlatform(new AndroidPlatform(SDK));
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Android, UnrealPlatformGroup.Android);
		}
	}
}
