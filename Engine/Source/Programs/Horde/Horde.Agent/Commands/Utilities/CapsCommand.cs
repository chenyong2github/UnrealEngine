// Copyright Epic Games, Inc. All Rights Reserved.

using Grpc.Net.Client;
using HordeAgent.Utility;
using HordeCommon.Rpc;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using HordeCommon.Rpc.Messages;
using HordeAgent.Services;

namespace HordeAgent.Commands.Utilities
{
	/// <summary>
	/// Shows capabilities of this agent
	/// </summary>
	[Command("Caps", "Lists detected capabilities of this agent")]
	class CapsCommand : Command
	{
		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger Logger)
		{
			AgentCapabilities Capabilities = await WorkerService.GetAgentCapabilities(DirectoryReference.GetCurrentDirectory(), Logger);
			foreach (DeviceCapabilities Device in Capabilities.Devices)
			{
				Logger.LogInformation("Device: {Name}", Device.Handle);
				foreach (string Property in Device.Properties)
				{
					Logger.LogInformation("  {Property}", Property);
				}
			}
			return 0;
		}
	}
}
