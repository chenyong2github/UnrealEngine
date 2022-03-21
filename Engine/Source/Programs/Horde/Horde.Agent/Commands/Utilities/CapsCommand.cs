// Copyright Epic Games, Inc. All Rights Reserved.

using Grpc.Net.Client;
using Horde.Agent.Utility;
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
using Horde.Agent.Services;

namespace Horde.Agent.Commands.Utilities
{
	/// <summary>
	/// Shows capabilities of this agent
	/// </summary>
	[Command("Caps", "Lists detected capabilities of this agent")]
	class CapsCommand : Command
	{
		/// <inheritdoc/>
		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			AgentCapabilities capabilities = await WorkerService.GetAgentCapabilities(DirectoryReference.GetCurrentDirectory(), logger);
			foreach (DeviceCapabilities device in capabilities.Devices)
			{
				logger.LogInformation("Device: {Name}", device.Handle);
				foreach (string property in device.Properties)
				{
					logger.LogInformation("  {Property}", property);
				}
			}
			return 0;
		}
	}
}
