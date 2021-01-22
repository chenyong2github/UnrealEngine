// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TextureShareSDK_D3D12 : TextureShareSDKBase
{
	public TextureShareSDK_D3D12(ReadOnlyTargetRules Target) : base(Target)
	{
	}

	public override string GetSDKRenderPlatform() { return "D3D12"; }
}
