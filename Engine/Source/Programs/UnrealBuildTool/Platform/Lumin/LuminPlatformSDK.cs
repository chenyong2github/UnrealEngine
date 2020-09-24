// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using Tools.DotNETCommon;
using System.Text.RegularExpressions;

namespace UnrealBuildTool
{
	class LuminPlatformSDK : UEBuildPlatformSDK
	{

		public override string GetMainVersion()
		{
			return "0.24";
		}

 		public override void GetValidVersionRange(out string MinVersion, out string MaxVersion)
		{
			MinVersion = MaxVersion = GetMainVersion();
		}

		public override void GetValidSoftwareVersionRange(out string MinVersion, out string MaxVersion)
		{
			MinVersion = MaxVersion = null;
		}

		public override string GetInstalledSDKVersion()
		{
			string EnvVarKey = "MLSDK";

			string MLSDKPath = Environment.GetEnvironmentVariable(EnvVarKey);
			if (String.IsNullOrEmpty(MLSDKPath))
			{
				return null;
			}

			String VersionFile = Path.Combine(MLSDKPath, "include/ml_version.h");
			if (File.Exists(VersionFile))
			{
				string[] VersionText = File.ReadAllLines(VersionFile);

				String MajorVersion = FindVersionNumber("MLSDK_VERSION_MAJOR", VersionText);
				String MinorVersion = FindVersionNumber("MLSDK_VERSION_MINOR", VersionText);

				return string.Format("{0}.{1}", MajorVersion, MinorVersion);
			}

			return null;
		}


		public override bool TryConvertVersionToInt(string StringValue, out UInt64 OutValue)
		{
			// make major high 16 bits, minor low 16
			Match Result = Regex.Match(StringValue, @"^(\d+).(\d+)$");
			if (Result.Success)
			{
				OutValue = UInt64.Parse(Result.Groups[1].Value) << 16 | UInt64.Parse(Result.Groups[2].Value) << 0;
				return true;
			}
			OutValue = 0;
			return false;
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


		protected override bool PlatformSupportsAutoSDKs()
		{
			return true;
		}

		protected override String GetRequiredScriptVersionString()
		{
			return "Lumin_15";
		}

		// prefer auto sdk on android as correct 'manual' sdk detection isn't great at the moment.
		protected override bool PreferAutoSDK()
		{
			return true;
		}
	}
}
