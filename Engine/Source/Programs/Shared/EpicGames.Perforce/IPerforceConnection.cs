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
	public interface IPerforceConnection
	{
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
		/// <returns>Response object</returns>
		Task<IPerforceOutput> CommandAsync(string Command, IReadOnlyList<string> Arguments, IReadOnlyList<string>? FileArguments, byte[]? InputData);

		/// <summary>
		/// Execute the 'login' command
		/// </summary>
		/// <param name="Password">Password to use to login</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		Task LoginAsync(string Password, CancellationToken CancellationToken = default);

		/// <summary>
		/// Sets an environment variable
		/// </summary>
		/// <param name="Name">Name of the variable to set</param>
		/// <param name="Value">Value for the variable</param>
		/// <param name="CancellationToken">Token used to cancel the operation</param>
		/// <returns>Response from the server</returns>
		Task SetAsync(string Name, string Value, CancellationToken CancellationToken = default);

		/// <summary>
		/// Gets the setting of a Perforce variable
		/// </summary>
		/// <param name="Name">Name of the variable to get</param>
		/// <param name="CancellationToken">Cancellation token for the request</param>
		/// <returns>Value of the variable</returns>
		Task<string?> TryGetSettingAsync(string Name, CancellationToken CancellationToken = default);
	}
}
