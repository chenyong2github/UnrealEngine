// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class CQTest_HoloLens : CQTest
	{
		public CQTest_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
			throw new System.Exception("Should not be here - HoloLens in the uplugin's PlatformDenyList");
		}
	}
}
