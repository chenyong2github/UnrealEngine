// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace Horde.Build.Perforce
{
	/// <summary>
	/// VCS abstraction. Provides information about commits to a particular stream.
	/// </summary>
	public interface ICommitSource
	{
		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="changeNumber">Change numbers to query</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Commit details</returns>
		Task<ICommit> GetCommitAsync(int changeNumber, CancellationToken cancellationToken = default);

		/// <summary>
		/// Finds changes submitted to a stream, in reverse order.
		/// </summary>
		/// <param name="minChange">The minimum changelist number</param>
		/// <param name="maxChange">The maximum changelist number</param>
		/// <param name="maxResults">Maximum number of results to return</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Changelist information</returns>
		IAsyncEnumerable<ICommit> FindCommitsAsync(int? minChange, int? maxChange, int? maxResults, CancellationToken cancellationToken = default);

		/// <summary>
		/// Gets the latest change number
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		Task<int> LatestNumberAsync(CancellationToken cancellationToken = default);
	}

	/// <summary>
	/// Extension methods for <see cref="ICommitSource"/>
	/// </summary>
	public static class CommitSourceExtensions
	{
		/// <summary>
		/// Finds the latest commit from a source
		/// </summary>
		/// <param name="source">The commit source to query</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The latest commit</returns>
		public static async Task<ICommit> LatestAsync(this ICommitSource source, CancellationToken cancellationToken)
		{
			return await source.FindCommitsAsync(null, null, 1, cancellationToken).FirstAsync(cancellationToken);
		}
	}
}
