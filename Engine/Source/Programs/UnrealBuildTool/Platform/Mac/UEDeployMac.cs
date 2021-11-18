// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Xml;
using System.IO;
using Ionic.Zip;
using EpicGames.Core;

namespace UnrealBuildTool
{
	class UEDeployMac : UEBuildDeploy
	{
		public override bool PrepTargetForDeployment(TargetReceipt Receipt)
		{
			Log.TraceInformation("Deploying now!");
			return true;
		}

		public static bool GeneratePList(string ProjectDirectory, bool bIsUnrealGame, string GameName, string ProjectName, string InEngineDir, string ExeName)
		{
			string IntermediateDirectory = (bIsUnrealGame ? InEngineDir : ProjectDirectory) + "/Intermediate/Mac";
			string DestPListFile = IntermediateDirectory + "/" + ExeName + "-Info.plist";
			string SrcPListFile = (bIsUnrealGame ? (InEngineDir + "Source/Programs/") : (ProjectDirectory + "/Source/")) + GameName + "/Resources/Mac/Info.plist";
			if (!File.Exists(SrcPListFile))
			{
				SrcPListFile = InEngineDir + "/Source/Runtime/Launch/Resources/Mac/Info.plist";
			}

			string PListData;
			if (File.Exists(SrcPListFile))
			{
				PListData = File.ReadAllText(SrcPListFile);
			}
			else
			{
				return false;
			}

            // bundle identifier
            // plist replacements
            DirectoryReference? DirRef = bIsUnrealGame ? (!string.IsNullOrEmpty(UnrealBuildTool.GetRemoteIniPath()) ? new DirectoryReference(UnrealBuildTool.GetRemoteIniPath()!) : null) : new DirectoryReference(ProjectDirectory);
            ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, DirRef, UnrealTargetPlatform.IOS);

            string BundleIdentifier;
            Ini.GetString("/Script/IOSRuntimeSettings.IOSRuntimeSettings", "BundleIdentifier", out BundleIdentifier);

            string BundleVersion = MacToolChain.LoadEngineDisplayVersion();
			PListData = PListData.Replace("${EXECUTABLE_NAME}", ExeName).Replace("${APP_NAME}", BundleIdentifier.Replace("[PROJECT_NAME]", ProjectName).Replace("_", "")).Replace("${ICON_NAME}", GameName).Replace("${MACOSX_DEPLOYMENT_TARGET}", MacToolChain.Settings.MinMacOSVersion).Replace("${BUNDLE_VERSION}", BundleVersion);

			if (!Directory.Exists(IntermediateDirectory))
			{
				Directory.CreateDirectory(IntermediateDirectory);
			}
			File.WriteAllText(DestPListFile, PListData);

			return true;
		}
	}
}
