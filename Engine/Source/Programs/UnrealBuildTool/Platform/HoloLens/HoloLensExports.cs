// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Public HoloLensDeploy wrapper exposed to UAT
	/// </summary>
	public class HoloLensExports
	{
		private HoloLensDeploy InnerDeploy;

		/// <summary>
		/// 
		/// </summary>
		public HoloLensExports()
		{
			InnerDeploy = new HoloLensDeploy();
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="ProjectFile"></param>
		/// <param name="InProjectName"></param>
		/// <param name="InProjectDirectory"></param>
		/// <param name="Architecture"></param>
		/// <param name="InTargetConfigurations"></param>
		/// <param name="InExecutablePaths"></param>
		/// <param name="InEngineDir"></param>
		/// <param name="bForDistribution"></param>
		/// <param name="CookFlavor"></param>
		/// <param name="bIsDataDeploy"></param>
		/// <returns></returns>
		public bool PrepForUATPackageOrDeploy(FileReference ProjectFile, string InProjectName, string InProjectDirectory, WindowsArchitecture Architecture, List<UnrealTargetConfiguration> InTargetConfigurations, List<string> InExecutablePaths, string InEngineDir, bool bForDistribution, string CookFlavor, bool bIsDataDeploy)
		{
			return InnerDeploy.PrepForUATPackageOrDeploy(ProjectFile, InProjectName, InProjectDirectory, Architecture, InTargetConfigurations, InExecutablePaths, InEngineDir, bForDistribution, CookFlavor, bIsDataDeploy);
		}

		/// <summary>
		/// Collect all the WinMD references
		/// </summary>
		/// <param name="Receipt"></param>
		/// <param name="SourceProjectDir"></param>
		/// <param name="DestPackageRoot"></param>
		public void AddWinMDReferencesFromReceipt(TargetReceipt Receipt, DirectoryReference SourceProjectDir, string DestPackageRoot)
		{
			InnerDeploy.AddWinMDReferencesFromReceipt(Receipt, SourceProjectDir, DestPackageRoot);
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="ToolName"></param>
		/// <returns></returns>
		public static FileReference GetWindowsSdkToolPath(string ToolName)
		{
			return HoloLensToolChain.GetWindowsSdkToolPath(ToolName);
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="SdkVersion"></param>
		/// <returns></returns>
		public static bool InitWindowsSdkToolPath(string SdkVersion)
		{
			return HoloLensToolChain.InitWindowsSdkToolPath(SdkVersion);
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="DLCFile"></param>
		/// <param name="OutputDirectory"></param>
		public static void CreateManifestForDLC(FileReference DLCFile, DirectoryReference OutputDirectory)
		{
			string IntermediateDirectory = DirectoryReference.Combine(DLCFile.Directory, "Intermediate", "Deploy").FullName;
			new HoloLensManifestGenerator().CreateManifest(UnrealTargetPlatform.HoloLens, WindowsArchitecture.ARM64, OutputDirectory.FullName, IntermediateDirectory, DLCFile, DLCFile.Directory.FullName, new List<UnrealTargetConfiguration>(), new List<string>(), null);
		}

		/// <summary>
		/// 
		/// </summary>
		/// <returns>CurrentWindowsSdkVersion</returns>
		public static Version GetCurrentWindowsSdkVersion()
		{
			return HoloLensToolChain.GetCurrentWindowsSdkVersion();
		}
	}
}
