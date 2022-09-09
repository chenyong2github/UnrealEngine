// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Perforce;
using Horde.Build.Jobs.Templates;
using Horde.Build.Streams;
using Horde.Build.Users;

namespace Horde.Build.Perforce
{
	/// <summary>
	/// Result from checking a shelved change status
	/// </summary>
	public enum CheckShelfResult
	{
		/// <summary>
		/// The shelf is ok
		/// </summary>
		Ok,

		/// <summary>
		/// Changelist does not exist
		/// </summary>
		NoChange,

		/// <summary>
		/// The change does not contain any shelved files
		/// </summary>
		NoShelvedFiles,

		/// <summary>
		/// The shelf contains *some* files from a different stream
		/// </summary>
		MixedStream,

		/// <summary>
		/// The shelf contains only files from a different stream
		/// </summary>
		WrongStream,
	}

	/// <summary>
	/// A connection returned by the Perforce service
	/// </summary>
	public interface IPooledPerforceConnection : IPerforceConnection, IDisposable
	{
		/// <summary>
		/// The client used by the connection
		/// </summary>
		ClientRecord? Client { get; }

		/// <summary>
		/// Gets the server information, returning a cached copy if possible
		/// </summary>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		ValueTask<InfoRecord> GetInfoAsync(CancellationToken cancellationToken);
	}

	/// <summary>
	/// Wrapper around Perforce functionality. Can use a local p4.exe client for development purposes, or a separate HordePerforceBridge instance over REST for deployments.
	/// </summary>
	public interface IPerforceService
	{
		/// <summary>
		/// Connect to the given Perforce cluster
		/// </summary>
		/// <param name="clusterName">Name of the cluster</param>
		/// <param name="userName">Name of the user to connect as. Uses the service account if null.</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Connection information</returns>
		public Task<IPooledPerforceConnection> ConnectAsync(string clusterName, string? userName = null, CancellationToken cancellationToken = default);

		/// <summary>
		/// Finds or adds a user from the given Perforce server, adding the user (and populating their profile with Perforce data) if they do not currently exist
		/// </summary>
		/// <param name="clusterName"></param>
		/// <param name="userName"></param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public ValueTask<IUser> FindOrAddUserAsync(string clusterName, string userName, CancellationToken cancellationToken = default);

		/// <summary>
		/// Checks a shelf is valid for the given stream
		/// </summary>
		/// <param name="clusterName">Name of the Perforce cluster</param>
		/// <param name="streamName">The stream to query</param>
		/// <param name="changeNumber">Shelved changelist number</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Whether the shelf is valid, plus the description for it</returns>
		public Task<(CheckShelfResult, string?)> CheckShelfAsync(string clusterName, string streamName, int changeNumber, CancellationToken cancellationToken = default);

		/// <summary>
		/// Gets the latest change for a particular stream
		/// </summary>
		/// <param name="stream">Stream to query</param>
		/// <param name="changeNumber">Change numbers to query</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Commit details</returns>
		public Task<ICommit> GetChangeDetailsAsync(IStream stream, int changeNumber, CancellationToken cancellationToken = default);

		/// <summary>
		/// Duplicates a shelved changelist
		/// </summary>
		/// <param name="clusterName">Name of the Perforce cluster</param>
		/// <param name="shelvedChange">The shelved changelist</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The duplicated changelist</returns>
		public Task<int> DuplicateShelvedChangeAsync(string clusterName, int shelvedChange, CancellationToken cancellationToken = default);

		/// <summary>
		/// Submit a shelved changelist
		/// </summary>
		/// <param name="clusterName">Name of the Perforce cluster</param>
		/// <param name="shelvedChange">The shelved changelist number, created by <see cref="DuplicateShelvedChangeAsync(String,Int32,CancellationToken)"/></param>
		/// <param name="originalChange">The original changelist number</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Tuple consisting of the submitted changelist number and message</returns>
		public Task<(int? Change, string Message)> SubmitShelvedChangeAsync(string clusterName, int shelvedChange, int originalChange, CancellationToken cancellationToken = default);

		/// <summary>
		/// Deletes a changelist containing shelved files.
		/// </summary>
		/// <param name="clusterName">Name of the Perforce cluster</param>
		/// <param name="shelvedChange">The changelist containing shelved files</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Async tasy</returns>
		public Task DeleteShelvedChangeAsync(string clusterName, int shelvedChange, CancellationToken cancellationToken = default);

		/// <summary>
		/// Updates a changelist description
		/// </summary>
		/// <param name="clusterName">Name of the Perforce cluster</param>
		/// <param name="change">The change to update</param>
		/// <param name="description">The new description</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Async task</returns>
		public Task UpdateChangelistDescription(string clusterName, int change, string description, CancellationToken cancellationToken = default);

		/// <summary>
		/// Creates a commit source for the given stream
		/// </summary>
		/// <param name="stream">Stream to create a commit source for</param>
		/// <returns>Commit source instance</returns>
		public ICommitCollection GetCommits(IStream stream);
	}
}
