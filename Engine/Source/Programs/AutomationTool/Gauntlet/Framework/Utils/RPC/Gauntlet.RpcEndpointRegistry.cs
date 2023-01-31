// Copyright Epic Games, Inc. All Rights Reserved.

namespace Gauntlet
{
	/// <summary>
	/// All RPC Endpoints that have been set up. For the endpoint to be found, you must use the Name of the RPC here.
	/// </summary>
	public class RpcEndpointRegistry
	{
		// Any Process
		public static string PressApplicationCloseButton { get { return "PressApplicationCloseButton"; } }
		public static string ExecuteCheatCommand { get { return "ExecuteCheatCommand"; } }
	}
}
