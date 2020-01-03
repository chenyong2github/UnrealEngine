// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	[SupportedPlatforms("Win64")]
	public class DatasmithSketchUp2019 : DatasmithSketchUpBase
	{
		public DatasmithSketchUp2019(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PrivateDefinitions.Add("SKP_SDK_2019");
		}

		public override string GetSketchUpSDKFolder()
		{
			return "SDK_WIN_x64_2019-0-753_0";
		}

		public override string GetSketchUpEnvVar()
		{
			return "SKP_SDK_2019";
		}
	}
}
