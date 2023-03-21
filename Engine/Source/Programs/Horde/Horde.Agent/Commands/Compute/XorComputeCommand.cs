// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Commands.Compute
{
	/// <summary>
	/// Runs a simple compute command
	/// </summary>
	[Command("xorcompute", "Executes a simple XOR command through the compute API")]
	class XorComputeCommand : ComputeCommand
	{
		readonly ILogger _logger;

		/// <summary>
		/// Constructor
		/// </summary>
		public XorComputeCommand(IServiceProvider serviceProvider, ILogger<XorComputeCommand> logger)
			: base(serviceProvider)
		{
			_logger = logger;
		}

		/// <inheritdoc/>
		protected override async Task<bool> HandleRequestAsync(IComputeLease lease, CancellationToken cancellationToken)
		{
			IComputeSocket socket = lease.Socket;
			await using (IComputeChannel mainChannel = socket.AttachMessageChannel(0))
			{
				_logger.LogInformation("Forking compute channel...");

				await using IComputeChannel channel = socket.AttachMessageChannel(1);
				mainChannel.Fork(1);

				_logger.LogInformation("Sending XOR request");
				channel.XorRequest(new byte[] { 1, 2, 3, 4, 5 }, (byte)123);

				_logger.LogInformation("Waiting for response...");
				IComputeMessage response = await channel.ReceiveAsync(cancellationToken);

				byte[] result = response.Data.ToArray();
				byte[] expectedResult = new byte[] { 1 ^ 123, 2 ^ 123, 3 ^ 123, 4 ^ 123, 5 ^ 123 };

				if (result.SequenceEqual(expectedResult))
				{
					_logger.LogInformation("Received response; data is correct.");
				}
				else
				{
					throw new Exception("Incorrect response data");
				}

				_logger.LogInformation("Closing channel");
			}

			_logger.LogInformation("Closed");
			return true;
		}
	}
}
