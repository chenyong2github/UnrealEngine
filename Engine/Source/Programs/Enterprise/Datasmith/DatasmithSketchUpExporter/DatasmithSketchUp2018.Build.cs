// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	[SupportedPlatforms("Win64")]
	public class DatasmithSketchUp2018 : DatasmithSketchUpBase
	{
		public DatasmithSketchUp2018(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PrivateDefinitions.Add("SKP_SDK_2018");
		}

		public override string GetSketchUpSDKFolder()
		{
			return "SDK_Win_x64_18-0-18664";
		}

		public override string GetSketchUpEnvVar()
		{
			return "SKP_SDK_2018";
		}
	}
}
