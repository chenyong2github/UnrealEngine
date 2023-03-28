// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Represents a remotely executing process
	/// </summary>
	public interface IComputeProcess : IAsyncDisposable
	{
		/// <summary>
		/// Exit code for the process.
		/// </summary>
		int ExitCode { get; }

		/// <summary>
		/// Whether the process has exited yet or not
		/// </summary>
		bool HasExited { get; }

		/// <summary>
		/// Read a line from the child process
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Line that was read</returns>
		ValueTask<string?> ReadLineAsync(CancellationToken cancellationToken = default);

		/// <summary>
		/// Wait until the process exits
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Exit code</returns>
		Task<int> WaitForExitAsync(CancellationToken cancellationToken = default);
	}
}
