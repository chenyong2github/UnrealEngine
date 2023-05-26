// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml.Linq;
using EpicGames.Core;
using UnrealBuildBase;
using Microsoft.Extensions.Logging;

namespace UnrealBuildTool
{
	/// <summary>
	/// Public Apple functions exposed to UAT
	/// </summary>
	public static class AppleExports
	{
		private static bool bForceModernXcode = Environment.CommandLine.Contains("-modernxcode", StringComparison.OrdinalIgnoreCase);
		private static bool bForceLegacyXcode = Environment.CommandLine.Contains("-legacyxcode", StringComparison.OrdinalIgnoreCase);

		/// <summary>
		/// Is the current project using modern xcode?
		/// </summary>
		/// <param name="ProjectFile"></param>
		/// <returns></returns>
		public static bool UseModernXcode(FileReference? ProjectFile)
		{
			if (bForceModernXcode)
			{
				if (bForceLegacyXcode)
				{
					throw new BuildException("Both -modernxcode and -legacyxcode were specified, please use one or the other.");
				}
				return true;
			}
			if (bForceLegacyXcode)
			{
				return false;
			}

			// Modern Xcode mode does this now
			bool bUseModernXcode = false;
			if (OperatingSystem.IsMacOS())
			{
				ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, ProjectFile?.Directory, UnrealTargetPlatform.Mac);
				Ini.TryGetValue("/Script/MacTargetPlatform.XcodeProjectSettings", "bUseModernXcode", out bUseModernXcode);
			}
			return bUseModernXcode;
		}

		/// <summary>
		/// Different ways that xcodebuild is run, so the scripts can behave appropriately
		/// </summary>
		public enum XcodeBuildMode
		{
			/// <summary>
			/// This is when hitting Build from in Xcode
			/// </summary>
			Default = 0,
			/// <summary>
			/// This runs after building when building on commandline directly with UBT
			/// </summary>
			PostBuildSync = 1,
			/// <summary>
			/// This runs when making a fully made .app in the Staged directory
			/// </summary>
			Stage = 2,
			/// <summary>
			/// This runs when packaging a full made .app into Project/Binaries
			/// </summary>
			Package = 3,
			/// <summary>
			/// This runs when packaging a .xcarchive for distribution
			/// </summary>
			Distribute = 4,
		}

		/// <summary>
		/// Generates a stub xcode project for the given project/platform/target combo, then builds or archives it
		/// </summary>
		/// <param name="ProjectFile">Project to build</param>
		/// <param name="Platform">Platform to build</param>
		/// <param name="Configuration">Configuration to build</param>
		/// <param name="TargetName">Target to build</param>
		/// <param name="BuildMode">Sets an envvar used inside the xcode project to control certain features</param>
		/// <param name="Logger"></param>
		/// <param name="ExtraOptions">Any extra options to pass to xcodebuild</param>
		/// <returns>xcode's exit code</returns>
		public static int BuildWithStubXcodeProject(FileReference? ProjectFile, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration,
			string TargetName, XcodeBuildMode BuildMode, ILogger Logger, string ExtraOptions = "")
		{
			DirectoryReference? GeneratedProjectFile;
			// we don't use distro flag when making a modern project
			AppleExports.GenerateRunOnlyXcodeProject(ProjectFile, Platform, TargetName, bForDistribution: false, Logger, out GeneratedProjectFile);

			if (GeneratedProjectFile == null)
			{
				return 1;
			}

			// run xcodebuild on the generated project to make the .app
			string XcodeBuildAction = BuildMode == XcodeBuildMode.Distribute ? "archive" : "build";
			return AppleExports.FinalizeAppWithXcode(GeneratedProjectFile!, Platform, TargetName, Configuration.ToString(), XcodeBuildAction, ExtraOptions + $" UE_XCODE_BUILD_MODE={BuildMode}", Logger);
		}




		/// <summary>
		/// Genearate an run-only Xcode project, that is not meant to be used for anything else besides code-signing/running/etc of the native .app bundle
		/// </summary>
		/// <param name="UProjectFile">Location of .uproject file (or null for the engine project</param>
		/// <param name="Platform">The platform to generate a project for</param>
		/// <param name="TargetName">The name of the target being built, so we can generate a more minimal project</param>
		/// <param name="bForDistribution">True if this is making a bild for uploading to app store</param>
		/// <param name="Logger">Logging object</param>
		/// <param name="GeneratedProjectFile">Returns the .xcworkspace that was made</param>
		public static void GenerateRunOnlyXcodeProject(FileReference? UProjectFile, UnrealTargetPlatform Platform, string TargetName, bool bForDistribution, ILogger Logger, out DirectoryReference? GeneratedProjectFile)
		{
			AppleToolChain.GenerateRunOnlyXcodeProject(UProjectFile, Platform, TargetName, bForDistribution, Logger, out GeneratedProjectFile);
		}

		/// <summary>
		/// Version of FinalizeAppWithXcode that is meant for modern xcode mode, where we assume all codesigning is setup already in the project, so nothing else is needed
		/// </summary>
		/// <param name="XcodeProject">The .xcworkspace file to build</param>
		/// <param name="Platform">THe platform to make the .app for</param>
		/// <param name="SchemeName">The name of the scheme (basically the target on the .xcworkspace)</param>
		/// <param name="Configuration">Which configuration to make (Debug, etc)</param>
		/// <param name="Action">Action (build, archive, etc)</param>
		/// <param name="ExtraOptions">Extra options to pass to xcodebuild</param>
		/// <param name="Logger">Logging object</param>
		/// <returns>xcode's exit code</returns>
		public static int FinalizeAppWithXcode(DirectoryReference XcodeProject, UnrealTargetPlatform Platform, string SchemeName, string Configuration, string Action, string ExtraOptions, ILogger Logger)
		{
			return AppleToolChain.FinalizeAppWithXcode(XcodeProject, Platform, SchemeName, Configuration, Action, ExtraOptions, Logger);
		}
	}
}
