// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using MongoDB.Bson;
using Perforce;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using HordeServer.Storage;
using HordeServer.Storage.Primitives;
using HordeServer.Services;
using EpicGames.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using System.Collections.Concurrent;
using System.Globalization;
using Microsoft.Extensions.Hosting;

namespace HordeServer.Commits.Impl
{
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Service which mirrors changes from Perforce
	/// </summary>
	class CommitService : ICommitService, IHostedService, IDisposable
	{
		class StreamReplicationState
		{
			[BsonElement("s")]
			public StreamId StreamId { get; set; }

			[BsonElement("min")]
			public int MinChange { get; set; } // Inclusive

			[BsonElement("max")]
			public int MaxChange { get; set; } // Exclusive
		}

		[SingletonDocument("60f59cfa4cc1474f19bcc161")]
		class ReplicationState : SingletonBase
		{
			public List<StreamReplicationState> Streams { get; set; } = new List<StreamReplicationState>();
		}

		ICommitCollection CommitCollection;
		IStreamCollection StreamCollection;
		IPerforceService PerforceService;
		IUserCollection UserCollection;
		ISingletonDocument<Globals> GlobalsDocument;
		ISingletonDocument<ReplicationState> StateDocument;
		ILogger<CommitService> Logger;
		ElectedTick Ticker;

		/// <summary>
		/// Constructor
		/// </summary>
		public CommitService(ICommitCollection CommitCollection, IStreamCollection StreamCollection, IPerforceService PerforceService, IUserCollection UserCollection, DatabaseService DatabaseService, ILogger<CommitService> Logger)
		{
			this.CommitCollection = CommitCollection;
			this.StreamCollection = StreamCollection;
			this.PerforceService = PerforceService;
			this.UserCollection = UserCollection;
			this.GlobalsDocument = new SingletonDocument<Globals>(DatabaseService);
			this.StateDocument = new SingletonDocument<ReplicationState>(DatabaseService);
			this.Logger = Logger;
			this.Ticker = new ElectedTick(DatabaseService, new ObjectId("60f866c49e7268f71803b6e0"), TickReplicationAsync, TimeSpan.FromSeconds(30.0), Logger);
			TickReplicationAsync(default).Wait();
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			Ticker.Dispose();
		}

		/// <inheritdoc/>
		public Task StartAsync(CancellationToken cancellationToken)
		{
			Ticker.Start();
			return Task.CompletedTask;
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken cancellationToken)
		{
			await Ticker.StopAsync();
		}

		/// <summary>
		/// Updates commits from Perforce
		/// </summary>
		/// <param name="StoppingToken"></param>
		/// <returns></returns>
		async Task TickReplicationAsync(CancellationToken StoppingToken)
		{
			Globals Globals = await GlobalsDocument.GetAsync();
			ReplicationState State = await StateDocument.GetAsync();
			bool bUpdateState = false;

			// Find the last 100 changes from any stream that does not currently have any changes registered
			List<IStream> Streams = await StreamCollection.FindAllAsync();
			foreach (IStream Stream in Streams)
			{
				StreamReplicationState? StreamState = State.Streams.FirstOrDefault(x => x.StreamId == Stream.Id);
				if (StreamState == null)
				{
					List<ChangeSummary> Changes = await PerforceService.GetChangesAsync(Stream.ClusterName, Stream.Name, null, null, 10, null);
					if (Changes.Count > 0)
					{
						StreamState = new StreamReplicationState();
						StreamState.StreamId = Stream.Id;
						StreamState.MinChange = Changes.Min(x => x.Number);
						StreamState.MaxChange = Changes.Max(x => x.Number + 1);

						foreach (ChangeSummary Change in Changes)
						{
							await AddCommitAsync(Stream, Change.Number);
						}

						State.Streams.Add(StreamState);
						if (!await StateDocument.TryUpdateAsync(State))
						{
							return;
						}
					}
				}
			}

			// Update all the clusters
			foreach (IGrouping<string, IStream> Cluster in Streams.GroupBy(x => x.ClusterName, StringComparer.OrdinalIgnoreCase))
			{
				// Get the first change to query from
				int? MinChange = null;
				foreach (IStream Stream in Cluster)
				{
					StreamReplicationState? StreamState = State.Streams.FirstOrDefault(x => x.StreamId == Stream.Id);
					if (StreamState != null)
					{
						if (MinChange == null || StreamState.MaxChange < MinChange.Value)
						{
							MinChange = StreamState.MaxChange + 1;
						}
					}
				}

				// Query all the changes
				List<ChangeSummary> Changes = await PerforceService.GetChangesAsync(Cluster.Key, MinChange, 100);
				if(Changes.Count > 0)
				{
					int MaxChange = Changes.Max(x => x.Number) + 1;
					foreach (IStream Stream in Cluster)
					{
						StreamReplicationState? StreamState = State.Streams.FirstOrDefault(x => x.StreamId == Stream.Id);
						if (StreamState != null && MaxChange > StreamState.MaxChange)
						{
							using (IStreamView StreamView = await PerforceService.GetStreamViewAsync(Cluster.Key, Stream.Name))
							{
								foreach (ChangeSummary Change in Changes)
								{
									if (StreamView.TryGetStreamPath(Change.Path, out _)) // TODO: Need *possible* match with wildcard, not exact match
									{
										await AddCommitAsync(Stream, Change.Number);
									}
								}
							}
							StreamState.MaxChange = MaxChange;
							bUpdateState = true;
						}
					}
				}
			}

			if(bUpdateState)
			{
				await StateDocument.TryUpdateAsync(State);
			}
		}

		/// <summary>
		/// Adds metadata for a particular commit
		/// </summary>
		/// <param name="Stream"></param>
		/// <param name="Number"></param>
		/// <returns></returns>
		async Task AddCommitAsync(IStream Stream, int Number)
		{
			ChangeDetails Details = await PerforceService.GetChangeDetailsAsync(Stream.ClusterName, Number);

			IUser Author = await UserCollection.FindOrAddUserByLoginAsync(Details.Author);
			int? OriginalChange = ParseRobomergeSource(Details.Description);

			IUser Owner = Author;

			string? OriginalAuthor = ParseRobomergeOwner(Details.Description);
			if (OriginalAuthor != null)
			{
				Owner = await UserCollection.FindOrAddUserByLoginAsync(OriginalAuthor);
			}

			NewCommit Commit = new NewCommit(Stream.Id, Details.Number, OriginalChange ?? Details.Number, Author.Id, Owner.Id, Details.Description, Details.Path, Details.Date);
			await CommitCollection.AddOrReplaceAsync(Commit);
		}

		/// <inheritdoc/>
		public async Task<List<ICommit>> FindCommitsAsync(StreamId StreamId, int? MinChange = null, int? MaxChange = null, string[]? Paths = null, int? Index = null, int? Count = null)
		{
			if(Paths != null)
			{
				throw new NotImplementedException();
			}

			return await CommitCollection.FindCommitsAsync(StreamId, MinChange, MaxChange, Index, Count);
		}

		/// <summary>
		/// Attempts to parse the Robomerge source from this commit information
		/// </summary>
		/// <param name="Description">Description text to parse</param>
		/// <returns>The parsed source changelist, or null if no #ROBOMERGE-SOURCE tag was present</returns>
		static int? ParseRobomergeSource(string Description)
		{
			// #ROBOMERGE-SOURCE: CL 13232051 in //Fortnite/Release-12.60/... via CL 13232062 via CL 13242953
			Match Match = Regex.Match(Description, @"^#ROBOMERGE-SOURCE: CL (\d+)", RegexOptions.Multiline);
			if (Match.Success)
			{
				return int.Parse(Match.Groups[1].Value, CultureInfo.InvariantCulture);
			}
			else
			{
				return null;
			}
		}

		/// <summary>
		/// Attempts to parse the Robomerge owner from this commit information
		/// </summary>
		/// <param name="Description">Description text to parse</param>
		/// <returns>The Robomerge owner, or null if no #ROBOMERGE-OWNER tag was present</returns>
		static string? ParseRobomergeOwner(string Description)
		{
			// #ROBOMERGE-OWNER: ben.marsh
			Match Match = Regex.Match(Description, @"^#ROBOMERGE-OWNER:\s*([^\s]+)", RegexOptions.Multiline);
			if (Match.Success)
			{
				return Match.Groups[1].Value;
			}
			else
			{
				return null;
			}
		}
	}
}
