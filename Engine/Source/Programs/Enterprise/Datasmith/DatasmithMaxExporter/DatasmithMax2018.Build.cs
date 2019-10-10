// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	[SupportedPlatforms("Win64")]
	public class DatasmithMax2018 : DatasmithMaxBase
	{
		public DatasmithMax2018(ReadOnlyTargetRules Target)
			: base(Target)
		{
		}

		public override string GetMaxVersion() { return "2018"; }
	}
}