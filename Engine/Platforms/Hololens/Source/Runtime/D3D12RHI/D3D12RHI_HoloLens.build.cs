// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class D3D12RHI_HoloLens : D3D12RHI
	{
		protected override bool bUsesWindowsD3D12 { get => true; }

		public D3D12RHI_HoloLens(ReadOnlyTargetRules Target) : base(Target)
		{
			PrecompileForTargets = PrecompileTargetsType.None;

			PrivateDefinitions.Add("D3D12RHI_USE_D3DDISASSEMBLE=0");
		}
	}
}
