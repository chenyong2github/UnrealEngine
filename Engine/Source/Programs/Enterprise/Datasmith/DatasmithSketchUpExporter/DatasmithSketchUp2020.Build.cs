// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	[SupportedPlatforms("Win64")]
	public class DatasmithSketchUp2020 : DatasmithSketchUpBase
	{
		public DatasmithSketchUp2020(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PrivateDefinitions.Add("SKP_SDK_2020");
		}

		public override string GetSketchUpSDKFolder()
		{
			return "SDK_WIN_x64_2020-0-363";
		}

		public override string GetSketchUpEnvVar()
		{
			return "SKP_SDK_2020";
		}
	}
}
