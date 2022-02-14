// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Base interface for Perforce clients
	/// </summary>
	public interface IPerforceConnection : IDisposable
	{
		/// <summary>
		/// Connection settings
		/// </summary>
		IPerforceSettings Settings { get; }

		/// <summary>
		/// Logger for this connection
		/// </summary>
		ILogger Logger { get; }

		/// <summary>
		/// Runs a Perforce command
		/// </summary>
		/// <param name="Command">The command name</param>
		/// <param name="Arguments">Arguments for the command</param>
		/// <param name="FileArguments">File arguments (may be put into a response file)</param>
		/// <param name="InputData">Input data to be passed to the command</param>
		/// <param name="InterceptIo">Whether to intercept file I/O and return it in the reponse stream. Only supported by the native client.</param>
		/// <returns>Response object</returns>
		Task<IPerforceOutput> CommandAsync(string Command, IReadOnlyList<string> Arguments, IReadOnlyList<string>? FileArguments, byte[]? InputData, bool InterceptIo);

		/// <summary>
		/// Execute the 'login' command
		/// </summary>
		/// <param name="Password">Password to use to login</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		Task<IPerforceOutput> LoginCommandAsync(string Password, CancellationToken CancellationToken = default);
	}
}
