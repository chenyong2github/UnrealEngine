// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class WebSocketNetworking  : ModuleRules
	{
		public WebSocketNetworking(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"NetCore",
					"Engine",
					"EngineSettings",
					"ImageCore",
					"Sockets",
					"PacketHandler",
					"OpenSSL",
					"libWebSockets",
					"zlib"
				}
			);

			PublicDefinitions.Add("USE_LIBWEBSOCKET=1");
		}
	}
}
