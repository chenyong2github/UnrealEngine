// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Base class for platform-specific project generators
	/// </summary>
	class LuminProjectGenerator : PlatformProjectGenerator
	{
		static bool VSSupportChecked = false;       // Don't want to check multiple times
		static bool VSDebuggingEnabled = false;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Arguments">Command line arguments passed to the project generator</param>
		public LuminProjectGenerator(CommandLineArguments Arguments)
			: base(Arguments)
		{
		}

		private bool IsVSLuminSupportInstalled(VCProjectFileFormat ProjectFileFormat)
		{
			if (!VSSupportChecked)
			{
				// TODO: add a registry check or file exists check to confirm if MLExtension is installed on the given ProjectFileFormat.
				VSDebuggingEnabled = (ProjectFileFormat == VCProjectFileFormat.VisualStudio2015 || ProjectFileFormat == VCProjectFileFormat.VisualStudio2017);
				VSSupportChecked = true;
			}
            return VSDebuggingEnabled;
		}
		
		/// <summary>
		/// Enumerate all the platforms that this generator supports
		/// </summary>
		public override IEnumerable<UnrealTargetPlatform> GetPlatforms()
		{
			yield return UnrealTargetPlatform.Lumin;
		}

		/// <summary>
		/// Whether this build platform has native support for VisualStudio
		/// </summary>
		/// <param name="InPlatform">  The UnrealTargetPlatform being built</param>
		/// <param name="InConfiguration"> The UnrealTargetConfiguration being built</param>
		/// <param name="ProjectFileFormat"> Which version of VS</param>
		/// <returns>bool    true if native VisualStudio support (or custom VSI) is available</returns>
		public override bool HasVisualStudioSupport(UnrealTargetPlatform InPlatform, UnrealTargetConfiguration InConfiguration, VCProjectFileFormat ProjectFileFormat)
		{
			// TODO: Returning true from here is resulting in VS erroring out when trying to build a game project for Lumin.
			return false; //IsVSLuminSupportInstalled(ProjectFileFormat);
		}

		/// <summary>
		/// Return the platform toolset string to write into the project configuration
		/// </summary>
		/// <param name="InPlatform">  The UnrealTargetPlatform being built</param>
		/// <param name="InConfiguration"> The UnrealTargetConfiguration being built</param>
		/// <param name="InProjectFileFormat"> The verison of VS</param> 
		/// <param name="ProjectFileBuilder">String builder for the project file</param>
		/// <returns>string    The custom configuration section for the project file; Empty string if it doesn't require one</returns>
		public override void GetVisualStudioPlatformToolsetString(UnrealTargetPlatform InPlatform, UnrealTargetConfiguration InConfiguration, VCProjectFileFormat InProjectFileFormat, StringBuilder ProjectFileBuilder)
		{
			ProjectFileBuilder.AppendLine("    <PlatformToolset>" + VCProjectFileGenerator.GetProjectFilePlatformToolsetVersionString(InProjectFileFormat) + "</PlatformToolset>");
		}

		/// <summary>
		/// Return any custom paths for VisualStudio this platform requires
		/// This include ReferencePath, LibraryPath, LibraryWPath, IncludePath and ExecutablePath.
		/// </summary>
		/// <param name="InPlatform">The UnrealTargetPlatform being built</param>
		/// <param name="InConfiguration">The configuration being built</param>
		/// <param name="TargetType">The type of target (game or program)</param>
		/// <param name="TargetRulesPath">Path to the target.cs file</param>
		/// <param name="ProjectFilePath">Path to the project file</param>
		/// <param name="NMakeOutputPath"></param>
		/// <param name="InProjectFileFormat">Format for the generated project files</param>
		/// <param name="ProjectFileBuilder">The project file content</param>
		/// <returns>The custom path lines for the project file; Empty string if it doesn't require one</returns>
		public override void GetVisualStudioPathsEntries(UnrealTargetPlatform InPlatform, UnrealTargetConfiguration InConfiguration, TargetType TargetType, FileReference TargetRulesPath, FileReference ProjectFilePath, FileReference NMakeOutputPath, VCProjectFileFormat InProjectFileFormat, StringBuilder ProjectFileBuilder)
		{
			base.GetVisualStudioPathsEntries(InPlatform, InConfiguration, TargetType, TargetRulesPath, ProjectFilePath, NMakeOutputPath, InProjectFileFormat, ProjectFileBuilder);

			if (IsVSLuminSupportInstalled(InProjectFileFormat) && TargetType == TargetType.Game && InPlatform == UnrealTargetPlatform.Lumin)
			{
				string MLSDK = Utils.CleanDirectorySeparators(Environment.GetEnvironmentVariable("MLSDK"), '\\');

				// TODO: Check if MPK name can be other than the project name.
				string GameName = TargetRulesPath.GetFileNameWithoutExtension();
				GameName = Path.GetFileNameWithoutExtension(GameName);

				string PackageFile = Utils.MakePathRelativeTo(NMakeOutputPath.Directory.FullName, ProjectFilePath.Directory.FullName);
				string PackageName = GameName;
				if (InConfiguration != UnrealTargetConfiguration.Development)
				{
					PackageName = GameName + "-" + InPlatform.ToString() + "-" + InConfiguration.ToString();
				}
				PackageFile = Path.Combine(PackageFile, PackageName + ".mpk");

                // Can't use $(NMakeOutput) directly since that is defined after <ELFFile> tag and thus ends up being translated as an empty string.
                string ELFFile = Utils.MakePathRelativeTo(NMakeOutputPath.Directory.FullName, ProjectFilePath.Directory.FullName);
				// Provide path to stripped executable so all symbols are resolved from the external sym file instead.
                ELFFile = Path.Combine(ELFFile, "..\\..\\Intermediate\\Lumin\\Mabu\\Binaries", GetElfName(NMakeOutputPath));
				string DebuggerFlavor = "MLDebugger";

				string SymFile = Utils.MakePathRelativeTo(NMakeOutputPath.Directory.FullName, ProjectFilePath.Directory.FullName);
				SymFile = Path.Combine(SymFile, "..\\..\\Intermediate\\Lumin\\Mabu\\Binaries", Path.ChangeExtension(NMakeOutputPath.GetFileName(), ".sym"));

				// following are defaults for debugger options
				string Attach = "false";
				string EnableAutoStop = "true";
				string AutoStopAtFunction = "main";
				string EnablePrettyPrinting = "true";
				string MLDownloadOnStart = "true";

				string CustomPathEntriesTemplate = "<MLSDK>{0}</MLSDK>" + ProjectFileGenerator.NewLine +
													"<PackageFile>{1}</PackageFile>" + ProjectFileGenerator.NewLine +
													"<ELFFile>{2}</ELFFile>" + ProjectFileGenerator.NewLine +
													"<DebuggerFlavor>{3}</DebuggerFlavor>" + ProjectFileGenerator.NewLine +
													"<Attach>{4}</Attach>" + ProjectFileGenerator.NewLine +
													"<EnableAutoStop>{5}</EnableAutoStop>" + ProjectFileGenerator.NewLine +
													"<AutoStopAtFunction>{6}</AutoStopAtFunction>" + ProjectFileGenerator.NewLine +
													"<EnablePrettyPrinting>{7}</EnablePrettyPrinting>" + ProjectFileGenerator.NewLine +
													"<MLDownloadOnStart>{8}</MLDownloadOnStart>" + ProjectFileGenerator.NewLine;
													// No need to provide SymFile path. The file is automatically picked up when it resides in the same folder and has the same name as the ElfFile
													// "<SymFile>{9}</SymFile>" + ProjectFileGenerator.NewLine;
				ProjectFileBuilder.Append(ProjectFileGenerator.NewLine + string.Format(CustomPathEntriesTemplate, MLSDK, PackageFile, ELFFile, DebuggerFlavor, Attach, EnableAutoStop, AutoStopAtFunction, EnablePrettyPrinting, MLDownloadOnStart));
			}
		}

		private string GetElfName(FileReference InNMakeOutputPath)
		{
			ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, InNMakeOutputPath.Directory.ParentDirectory.ParentDirectory, UnrealTargetPlatform.Lumin);
			bool bUseMobileRendering = false;
			Ini.GetBool("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "bUseMobileRendering", out bUseMobileRendering);
			bool bUseVulkan = false;
			Ini.GetBool("/Script/LuminRuntimeSettings.LuminRuntimeSettings", "bUseVulkan", out bUseVulkan);

			string OutputFileName = string.Format("{0}-arm64-{1}{2}", InNMakeOutputPath.GetFileNameWithoutExtension(), !(bUseMobileRendering || bUseVulkan) ? "lumingl4" : "lumin", InNMakeOutputPath.GetExtension());

			return OutputFileName;
		}
	}
}
