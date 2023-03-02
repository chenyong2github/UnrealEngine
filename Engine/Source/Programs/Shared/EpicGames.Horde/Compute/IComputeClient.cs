// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Interface for uploading compute work to remote machines
	/// </summary>
	public interface IComputeClient : IAsyncDisposable
	{
		/// <summary>
		/// Adds a new remote request
		/// </summary>
		/// <param name="clusterId">Cluster to execute the request</param>
		/// <param name="requirements">Requirements for the agent</param>
		/// <param name="handler">Handler for the connection</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public Task<TResult> ExecuteAsync<TResult>(ClusterId clusterId, Requirements? requirements, Func<IComputeChannel, CancellationToken, Task<TResult>> handler, CancellationToken cancellationToken);
	}
}
