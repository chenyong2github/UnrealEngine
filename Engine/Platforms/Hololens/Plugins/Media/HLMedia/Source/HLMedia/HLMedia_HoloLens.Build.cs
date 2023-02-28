// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class HLMedia_HoloLens : HLMedia
	{
		public HLMedia_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePathModuleNames.Add("D3D11RHI_HoloLens");
		}
	}
}
