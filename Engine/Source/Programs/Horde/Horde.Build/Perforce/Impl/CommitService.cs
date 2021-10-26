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
using EpicGames.Perforce.Managed;
using System.Threading.Channels;
using StackExchange.Redis;
using EpicGames.Perforce;
using EpicGames.Redis;
using System.Diagnostics;
using System.IO.Compression;
using System.IO;

namespace HordeServer.Commits.Impl
{
	using P4 = Perforce.P4;
	using NamespaceId = StringId<INamespace>;
	using BucketId = StringId<IBucket>;
	using StreamId = StringId<IStream>;

	/// <summary>
	/// Service which mirrors changes from Perforce
	/// </summary>
	class CommitService : ICommitService, IHostedService, IDisposable
	{
		bool EnableUpdates = false;

		/// <summary>
		/// Metadata about a stream that needs to have commits mirrored
		/// </summary>
		class StreamInfo
		{
			public IStream Stream { get; set; }
			public ViewMap View { get; }

			public StreamInfo(IStream Stream, ViewMap View)
			{
				this.Stream = Stream;
				this.View = View;
			}
		}

		NamespaceId NamespaceId { get; } = new NamespaceId("default");
		BucketId BucketId { get; } = new BucketId("commits");

		// Redis
		IDatabase Redis;

		// Collections
		ICommitCollection CommitCollection;
		IBlobCollection BlobCollection;
		IObjectCollection ObjectCollection;
		IRefCollection RefCollection;
		IStreamCollection StreamCollection;
		IPerforceService PerforceService;
		IUserCollection UserCollection;
		ISingletonDocument<Globals> GlobalsDocument;
		ILogger<CommitService> Logger;
		ElectedTick UpdateCommitsTicker;

		const int MaxBackgroundTasks = 2;

		/// <summary>
		/// Constructor
		/// </summary>
		public CommitService(IDatabase Redis, ICommitCollection CommitCollection, IBlobCollection BlobCollection, IObjectCollection ObjectCollection, IRefCollection RefCollection, IStreamCollection StreamCollection, IPerforceService PerforceService, IUserCollection UserCollection, DatabaseService DatabaseService, ILogger<CommitService> Logger)
		{
			this.Redis = Redis;
			this.RedisDirtyStreams = new RedisSet<StreamId>(Redis, RedisBaseKey.Append("streams"));
			this.RedisReservations = new RedisSortedSet<StreamId>(Redis, RedisBaseKey.Append("reservations"));

			this.CommitCollection = CommitCollection;
			this.BlobCollection = BlobCollection;
			this.ObjectCollection = ObjectCollection;
			this.RefCollection = RefCollection;
			this.StreamCollection = StreamCollection;
			this.PerforceService = PerforceService;
			this.UserCollection = UserCollection;
			this.GlobalsDocument = new SingletonDocument<Globals>(DatabaseService);
			this.Logger = Logger;
			this.UpdateCommitsTicker = new ElectedTick(DatabaseService, new ObjectId("60f866c49e7268f71803b6ef"), UpdateCommitsAsync, TimeSpan.FromSeconds(30.0), Logger);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			UpdateCommitsTicker.Dispose();
		}

		/// <inheritdoc/>
		public async Task StartAsync(CancellationToken CancellationToken)
		{
			if (EnableUpdates)
			{
				UpdateCommitsTicker.Start();
				await StartTreeUpdatesAsync();
			}
		}

		/// <inheritdoc/>
		public async Task StopAsync(CancellationToken CancellationToken)
		{
			if (EnableUpdates)
			{
				await StopTreeUpdatesAsync();
				await UpdateCommitsTicker.StopAsync();
			}
		}

		#region Commit updates

		/// <summary>
		/// Polls Perforce for submitted changes
		/// </summary>
		/// <param name="CancellationToken"></param>
		/// <returns></returns>
		async Task UpdateCommitsAsync(CancellationToken CancellationToken)
		{
			List<IStream> Streams = await StreamCollection.FindAllAsync();
			foreach (IGrouping<string, IStream> Group in Streams.GroupBy(x => x.ClusterName, StringComparer.OrdinalIgnoreCase))
			{
				try
				{
					await UpdateCommitsForClusterAsync(Group.Key, Group);
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Error while updating cluster {ClusterName}", Group.Key);
				}
			}
		}

		/// <summary>
		/// Updates all the streams on a particular server cluster
		/// </summary>
		/// <param name="ClusterName"></param>
		/// <param name="Streams"></param>
		/// <returns></returns>
		async Task UpdateCommitsForClusterAsync(string ClusterName, IEnumerable<IStream> Streams)
		{
			// Create a connection to the server
			IPerforceConnection? Connection = await PerforceService.GetServiceUserConnection(ClusterName);
			if (Connection == null)
			{
				throw new PerforceException($"Unable to create cluster connection for {ClusterName}");
			}

			// Figure out the case settings for the server
			InfoRecord ServerInfo = await Connection.GetInfoAsync(InfoOptions.ShortOutput);

			// Get the view for each stream
			List<StreamInfo> StreamInfoList = new List<StreamInfo>();
			foreach (IStream Stream in Streams)
			{
				ViewMap View = await GetStreamViewAsync(Connection, Stream.Name);
				StreamInfoList.Add(new StreamInfo(Stream, View));
			}

			// Find all the depot roots
			Dictionary<Utf8String, Utf8String> DepotRoots = new Dictionary<Utf8String, Utf8String>(ServerInfo.Utf8PathComparer);
			foreach (ViewMap View in StreamInfoList.Select(x => x.View))
			{
				foreach (ViewMapEntry Entry in View.Entries)
				{
					if (Entry.SourcePrefix.Length >= 3)
					{
						int SlashIdx = Entry.SourcePrefix.IndexOf('/', 2);
						Utf8String Depot = Entry.SourcePrefix.Slice(0, SlashIdx + 1);

						Utf8String DepotRoot;
						if (DepotRoots.TryGetValue(Depot, out DepotRoot))
						{
							DepotRoot = GetCommonPrefix(DepotRoot, Entry.SourcePrefix);
						}
						else
						{
							DepotRoot = Entry.SourcePrefix;
						}

						int LastSlashIdx = DepotRoot.LastIndexOf('/');
						DepotRoots[Depot] = DepotRoot.Slice(0, LastSlashIdx + 1);
					}
				}
			}

			// Find the minimum changelist number to query
			int MinChange = int.MaxValue;
			foreach (StreamInfo StreamInfo in StreamInfoList)
			{
				RedisList<int> Changes = RedisStreamChanges(StreamInfo.Stream.Id);

				int FirstChange = await Changes.GetByIndexAsync(-1);
				if (FirstChange == 0)
				{
					FirstChange = await GetFirstCommitToReplicateAsync(Connection, StreamInfo.View, ServerInfo.Utf8PathComparer);
				}
				else
				{
					FirstChange++;
				}

				if (FirstChange != 0)
				{
					MinChange = Math.Min(MinChange, FirstChange);
				}
			}

			// Find all the changes to consider
			SortedSet<int> ChangeNumbers = new SortedSet<int>();
			foreach (Utf8String DepotRoot in DepotRoots.Values)
			{
				List<ChangesRecord> Changes = await Connection.GetChangesAsync(ChangesOptions.None, null, MinChange, -1, ChangeStatus.Submitted, null, $"{DepotRoot}...");
				ChangeNumbers.UnionWith(Changes.Select(x => x.Number));
			}

			// Add the changes in order
			List<string> RelativePaths = new List<string>();
			foreach (int ChangeNumber in ChangeNumbers)
			{
				DescribeRecord DescribeRecord = await Connection.DescribeAsync(ChangeNumber);
				foreach (StreamInfo StreamInfo in StreamInfoList)
				{
					IStream Stream = StreamInfo.Stream;

					Utf8String BasePath = GetBasePath(DescribeRecord, StreamInfo.View, ServerInfo.Utf8PathComparer);
					if (!BasePath.IsEmpty)
					{
						await AddCommitAsync(Stream, DescribeRecord, BasePath.ToString());

						RedisList<int> StreamCommitsKey = RedisStreamChanges(Stream.Id);
						await StreamCommitsKey.RightPushAsync(DescribeRecord.Number);

						await RedisDirtyStreams.AddAsync(Stream.Id);
						await Redis.PublishAsync(RedisUpdateChannel, Stream.Id);
					}
				}
			}
		}

		/// <summary>
		/// Find the first commit to replicate from a branch
		/// </summary>
		/// <param name="Connection"></param>
		/// <param name="View"></param>
		/// <param name="Comparer"></param>
		/// <returns></returns>
		static async Task<int> GetFirstCommitToReplicateAsync(IPerforceConnection Connection, ViewMap View, Utf8StringComparer Comparer)
		{
			int MinChange = 0;

			List<Utf8String> RootPaths = View.GetRootPaths(Comparer);
			foreach (Utf8String RootPath in RootPaths)
			{
				IList<ChangesRecord> PathChanges = await Connection.GetChangesAsync(ChangesOptions.None, 20, ChangeStatus.Submitted, $"{RootPath}...");
				MinChange = Math.Max(MinChange, PathChanges.Min(x => x.Number));
			}

			return MinChange;
		}

		/// <summary>
		/// Find the base path for a change within a stream
		/// </summary>
		/// <param name="Changelist">Information about the changelist</param>
		/// <param name="View">Mapping from depot syntax to client view</param>
		/// <param name="Comparer">Path comparison type for the server</param>
		/// <returns>The base path for all files in the change</returns>
		static Utf8String GetBasePath(DescribeRecord Changelist, ViewMap View, Utf8StringComparer Comparer)
		{
			Utf8String BasePath = default;
			foreach (DescribeFileRecord File in Changelist.Files)
			{
				Utf8String StreamFile;
				if (View.TryMapFile(File.DepotFile, Comparer, out StreamFile))
				{
					if (BasePath.IsEmpty)
					{
						BasePath = StreamFile;
					}
					else
					{
						BasePath = GetCommonPrefix(BasePath, StreamFile);
					}
				}
			}
			return BasePath;
		}

		/// <summary>
		/// Gets the common prefix between two stringsc
		/// </summary>
		/// <param name="A"></param>
		/// <param name="B"></param>
		/// <returns></returns>
		static Utf8String GetCommonPrefix(Utf8String A, Utf8String B)
		{
			int Index = 0;
			while (Index < A.Length && Index < B.Length && A[Index] == B[Index])
			{
				Index++;
			}
			return A.Substring(0, Index);
		}

		/// <summary>
		/// Adds metadata for a particular commit
		/// </summary>
		/// <param name="Stream"></param>
		/// <param name="Changelist"></param>
		/// <param name="BasePath"></param>
		/// <returns></returns>
		async Task<ICommit> AddCommitAsync(IStream Stream, DescribeRecord Changelist, string BasePath)
		{
			IUser Author = await UserCollection.FindOrAddUserByLoginAsync(Changelist.User);
			IUser Owner = (await ParseRobomergeOwnerAsync(Changelist.Description)) ?? Author;

			int OriginalChange = ParseRobomergeSource(Changelist.Description) ?? Changelist.Number;

			NewCommit NewCommit = new NewCommit(Stream.Id, Changelist.Number, OriginalChange, Author.Id, Owner.Id, Changelist.Description, BasePath, Changelist.Time);
			return await CommitCollection.AddOrReplaceAsync(NewCommit);
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

		async Task<IUser?> ParseRobomergeOwnerAsync(string Description)
		{
			string? OriginalAuthor = ParseRobomergeOwner(Description);
			if (OriginalAuthor != null)
			{
				return await UserCollection.FindOrAddUserByLoginAsync(OriginalAuthor);
			}
			return null;
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
		#endregion

		#region Tree Updates

		RedisKey RedisBaseKey { get; } = new RedisKey("commits/");
		RedisChannel<StreamId> RedisUpdateChannel { get; } = new RedisChannel<StreamId>("commits/streams");
		readonly RedisSet<StreamId> RedisDirtyStreams;
		readonly RedisSortedSet<StreamId> RedisReservations;
		Channel<StreamId>? RedisUpdateStreams;
		RedisChannelSubscription<StreamId>? RedisUpdateSubscription;
		Task? StreamUpdateTask;
		RedisList<int> RedisStreamChanges(StreamId StreamId) => new RedisList<int>(Redis, RedisBaseKey.Append($"stream/{StreamId}/changes"));

		async Task StartTreeUpdatesAsync()
		{
			RedisUpdateStreams = Channel.CreateUnbounded<StreamId>();
			RedisUpdateSubscription = await Redis.Multiplexer.GetSubscriber().SubscribeAsync(RedisUpdateChannel, (_, StreamId) => RedisUpdateStreams.Writer.TryWrite(StreamId));
			StreamUpdateTask = Task.Run(() => UpdateTreesAsync());
		}

		async Task StopTreeUpdatesAsync()
		{
			if (RedisUpdateStreams != null)
			{
				RedisUpdateStreams.Writer.Complete();
				RedisUpdateStreams = null;
			}
			if (RedisUpdateSubscription != null)
			{
				await RedisUpdateSubscription.DisposeAsync();
				RedisUpdateSubscription = null;
			}
			await StreamUpdateTask!;
		}

		async Task UpdateTreesAsync()
		{
			List<(StreamId, Task)> BackgroundTasks = new List<(StreamId, Task)>(MaxBackgroundTasks);
			while (!RedisUpdateStreams!.Reader.Completion.IsCompleted || BackgroundTasks.Count > 0)
			{
				try
				{
					await UpdateTreesInternalAsync(BackgroundTasks);
				}
				catch (Exception Ex)
				{
					Logger.LogError(Ex, "Exception when updating commit service");
				}
			}
		}

		async Task UpdateTreesInternalAsync(List<(StreamId, Task)> BackgroundTasks)
		{
			// Remove any complete background tasks
			for (int Idx = BackgroundTasks.Count - 1; Idx >= 0; Idx--)
			{
				(_, Task BackgroundTask) = BackgroundTasks[Idx];
				if (BackgroundTask.IsCompleted)
				{
					await BackgroundTask;
					BackgroundTasks.RemoveAt(Idx);
				}
			}

			// Create a list of events to wait for
			List<Task> WaitTasks = new List<Task>(BackgroundTasks.Select(x => x.Item2));
			if (!RedisUpdateStreams!.Reader.Completion.IsCompleted)
			{
				WaitTasks.Add(RedisUpdateStreams.Reader.WaitToReadAsync().AsTask());

				// If we have spare slots for executing background tasks, check if there are any dirty streams
				if (BackgroundTasks.Count < MaxBackgroundTasks)
				{
					// Expire any reservations that are no longer valid
					DateTime UtcNow = DateTime.UtcNow;
					await RedisReservations.RemoveRangeByScoreAsync(Double.NegativeInfinity, UtcNow.Ticks);

					// Find the streams that we can wait for
					HashSet<StreamId> CheckStreams = new HashSet<StreamId>(await RedisDirtyStreams.MembersAsync());
					CheckStreams.ExceptWith(BackgroundTasks.Select(x => x.Item1));

					if (CheckStreams.Count > 0)
					{
						// Try to start new background tasks
						double NewScore = (UtcNow + TimeSpan.FromSeconds(30.0)).Ticks;
						foreach (StreamId CheckStream in CheckStreams)
						{
							if (await RedisReservations.AddAsync(CheckStream, NewScore, When.NotExists))
							{
								Task NewTask = Task.Run(() => UpdateStreamTreesAsync(CheckStream));
								BackgroundTasks.Add((CheckStream, NewTask));

								if (BackgroundTasks.Count == MaxBackgroundTasks)
								{
									break;
								}
							}
						}
						CheckStreams.ExceptWith(BackgroundTasks.Select(x => x.Item1));

						// If we still have spare tasks, check how long we should wait for a reservation to expire
						if (BackgroundTasks.Count < MaxBackgroundTasks)
						{
							long WaitTime = long.MaxValue;
							await foreach (SortedSetEntry<StreamId> Entry in RedisReservations.ScanAsync())
							{
								if (CheckStreams.Contains(Entry.Element))
								{
									WaitTime = Math.Min(WaitTime, (long)Entry.Score - UtcNow.Ticks);
								}
							}
							if (WaitTime < int.MaxValue)
							{
								WaitTasks.Add(Task.Delay(new TimeSpan(Math.Max(WaitTime, 0))));
							}
						}
					}
				}
			}

			// Wait until any task has completed
			if (WaitTasks.Count > 0)
			{
				await Task.WhenAny(WaitTasks);
			}
		}

		async Task UpdateStreamTreesAsync(StreamId StreamId)
		{
			Stopwatch Timer = Stopwatch.StartNew();

			RedisList<int> StreamChanges = RedisStreamChanges(StreamId);
			for (; ; )
			{
				// Update the stream, updating the reservation every 30 seconds
				Task InternalTask = Task.Run(() => UpdateStreamTreesInternalAsync(StreamId, StreamChanges));
				while (!InternalTask.IsCompleted)
				{
					Task DelayTask = Task.Delay(TimeSpan.FromSeconds(15.0));
					if (await Task.WhenAny(InternalTask, DelayTask) == DelayTask)
					{
						DateTime NewTime = DateTime.UtcNow + TimeSpan.FromSeconds(30.0);
						await RedisReservations.AddAsync(StreamId, NewTime.Ticks);
						Logger.LogInformation("Extending reservation for tree update of {StreamId} (elapsed: {Time}s)", StreamId, (int)Timer.Elapsed.TotalSeconds);
					}
				}
				await InternalTask;

				// Remove this stream from the dirty list if it's empty
				ITransaction Transaction = Redis.CreateTransaction();
				Transaction.AddCondition(Condition.ListLengthLessThan(StreamChanges.Key, 2));
				_ = Transaction.With(RedisDirtyStreams).RemoveAsync(StreamId);
				if (await Transaction.ExecuteAsync())
				{
					break;
				}
			}
			await RedisReservations.RemoveAsync(StreamId);
		}

		async Task UpdateStreamTreesInternalAsync(StreamId StreamId, RedisList<int> Changes)
		{
			IStream? Stream = await StreamCollection.GetAsync(StreamId);
			if (Stream == null)
			{
				return;
			}

			NativePerforceConnection? Perforce = await PerforceService.GetServiceUserConnection(Stream.ClusterName);
			if (Perforce == null)
			{
				return;
			}

			InfoRecord ServerInfo = await Perforce.GetInfoAsync(InfoOptions.ShortOutput);

			ViewMap View = await GetStreamViewAsync(Perforce, Stream.Name);

#if ENABLE_TREE_MIRRORING
			ICommit? PrevCommit = null;
			StreamTreeRef? PrevRoot = null;
#endif
			ObjectSet ObjectSet = new ObjectSet(BlobCollection, NamespaceId, 256 * 1024, DateTime.UtcNow);
			for (; ; )
			{
				// Get the first two changelists to update
				int[] Values = await Changes.RangeAsync(0, 1);
				if (Values.Length < 2)
				{
					break;
				}
#if ENABLE_TREE_MIRRORING
				// Clear the previous commit if it no longer matches
				if (PrevCommit != null && PrevCommit.Change != Values[0])
				{
					PrevCommit = null;
				}

				// Get the previous tree if we don't have it from the previous iteration
				if (PrevCommit == null)
				{
					PrevCommit = await CommitCollection.GetCommitAsync(Stream.Id, Values[0]);
					if (PrevCommit == null)
					{
						await Changes.LeftPopAsync();
						continue;
					}

					bool HasTree = false;
					if (PrevCommit.TreeRef != null)
					{
						CommitTree? PrevCommitTree = await ReadCommitTreeAsync(PrevCommit);
						if (PrevCommitTree != null)
						{
							try
							{
								throw new NotImplementedException();
//								await Builder.ResetAsync(PrevCommitTree);
//								HasTree = true;
							}
							catch (Exception Ex)
							{
								Logger.LogError(Ex, "Unable to read commit tree for {StreamId} CL {Change}", Stream.Id, PrevCommit.Change);
							}
						}
					}

					if (!HasTree)
					{
						PrevRoot = await AddTreeRefAsync(Perforce, Stream, View, null, null, PrevCommit, ObjectSet, ServerInfo.Utf8PathComparer);
					}
				}

				// Move to the next commit
				ICommit? Commit = await CommitCollection.GetCommitAsync(StreamId, Values[1]);
				StreamTreeRef? Root = null;
				if (Commit != null)
				{
					Root = await AddTreeRefAsync(Perforce, Stream, View, PrevCommit, PrevRoot, Commit, ObjectSet, ServerInfo.Utf8PathComparer);
				}

				// Move to the next commit
				PrevCommit = Commit;
				PrevRoot = Root;
#endif
				// Remove the first change number from this queue
				await Changes.LeftPopAsync();
			}
		}

		async Task<CommitTree?> GetCommitTreeAsync(ICommit Commit)
		{
			if (Commit.TreeRef == null)
			{
				return null;
			}

			IRef? Ref = await RefCollection.GetAsync(NamespaceId, BucketId, Commit.TreeRef);
			if (Ref == null)
			{
				return null;
			}

			return CbSerializer.Deserialize<CommitTree>(Ref.Value.AsField());
		}

		async Task<StreamTreeRef> AddTreeRefAsync(IPerforceConnection Perforce, IStream Stream, ViewMap View, ICommit? PrevCommit, StreamTreeRef? PrevRoot, ICommit Commit, ObjectSet ObjectSet, Utf8StringComparer PathComparer)
		{
			StreamTreeRef Root;
			if (PrevCommit == null || PrevRoot == null)
			{
				Logger.LogInformation("Performing snapshot of {StreamId} at CL {Change}", Commit.StreamId, Commit.Change);
				Root = await CreateTreeAsync(Perforce, View, Commit.Change, ObjectSet, PathComparer);
			}
			else
			{
				Logger.LogInformation("Performing incremental update of {StreamId} from CL {PrevChange} to CL {Change}", Commit.StreamId, PrevCommit.Change, Commit.Change);
				Root = await UpdateTreeAsync(PrevRoot, Perforce, View, Commit.Change, ObjectSet, PathComparer);
			}

			CommitTree Tree = await FlushAsync(ObjectSet, Root);
			string RefName = $"tree_{Commit.StreamId}_{Commit.Change}";

			List<IoHash> MissingHashes = await RefCollection.SetAsync(NamespaceId, BucketId, RefName, Tree.Serialize(Stream.Name));
			if (MissingHashes.Count > 0)
			{
				throw new Exception($"Missing hashes when attempting to add ref: {String.Join(", ", MissingHashes.Select(x => x.ToString()))}");
			}

			NewCommit NewCommit = new NewCommit(Commit);
			NewCommit.TreeRef = RefName;
			await CommitCollection.AddOrReplaceAsync(NewCommit);

			return Tree.Root;
		}

#endregion

#region Tree Snapshots

		/// <summary>
		/// Creates a snapshot of a stream at a particular changelist
		/// </summary>
		/// <param name="Connection">The Perforce repository to mirror</param>
		/// <param name="View">View for the stream</param>
		/// <param name="Change">Changelist to replicate</param>
		/// <param name="ObjectSet"></param>
		/// <param name="PathComparer">Comparison type for paths</param>
		async Task<StreamTreeRef> CreateTreeAsync(IPerforceConnection Connection, ViewMap View, int Change, ObjectSet ObjectSet, Utf8StringComparer PathComparer)
		{
			StreamTreeBuilder Root = new StreamTreeBuilder();

			// Optimize the view by removing unnecessary inclusions of the same path
			View = new ViewMap(View);
			for (int Idx = 0; Idx < View.Entries.Count; Idx++)
			{
				ViewMapEntry Entry = View.Entries[Idx];
				if (Entry.Include && Entry.IsPathWildcard())
				{
					while (Idx + 1 < View.Entries.Count && View.Entries[Idx + 1].Include && View.Entries[Idx + 1].SourcePrefix.StartsWith(Entry.SourcePrefix, PathComparer))
					{
						View.Entries.RemoveAt(Idx + 1);
					}
				}
			}

			// In order to optimize for the common case where we're adding multiple files to the same directory, we keep an open list of
			// StreamTreeBuilder objects down to the last modified node. Whenever we need to move to a different node, we write the 
			// previous path to the object store.
			for(int Idx = 0; Idx < View.Entries.Count; Idx++)
			{
				ViewMapEntry Entry = View.Entries[Idx];
				if (Entry.Include)
				{
					ViewMap FilteredView = new ViewMap();
					FilteredView.Entries.Add(Entry);

					for (int OtherIdx = Idx + 1; OtherIdx < View.Entries.Count; OtherIdx++)
					{
						ViewMapEntry OtherEntry = View.Entries[OtherIdx];
						if (!OtherEntry.Include)
						{
							if (OtherEntry.SourcePrefix.StartsWith(Entry.SourcePrefix, PathComparer) || Entry.SourcePrefix.StartsWith(OtherEntry.SourcePrefix, PathComparer))
							{
								FilteredView.Entries.Add(OtherEntry);
							}
						}
					}

					Logger.LogInformation("Adding {Source}@{Change}", Entry.Source, Change);
					await AddDepotFilesToSnapshotAsync(Root, Connection, $"{Entry.Source}@{Change}", View, PathComparer, ObjectSet);
				}
			}

			return Root.EncodeRef(x => WriteTree(ObjectSet, x));
		}

		/// <summary>
		/// Record returned by the fstat command
		/// </summary>
		public class Utf8FStatRecord
		{
			[PerforceTag("depotFile", Optional = true)]
			public Utf8String DepotFile;

			[PerforceTag("headType", Optional = true)]
			public Utf8String HeadType;

			[PerforceTag("headRev", Optional = true)]
			public int HeadRevision;

			[PerforceTag("digest", Optional = true)]
			public Utf8String Digest;

			[PerforceTag("fileSize", Optional = true)]
			public long FileSize;

			[PerforceTag("type", Optional = true)]
			public Utf8String Type;
		}

		/// <summary>
		/// Adds a set of files in the depot to a snapshot
		/// </summary>
		/// <param name="Root"></param>
		/// <param name="Connection"></param>
		/// <param name="FileSpec">The files to add</param>
		/// <param name="View">View for the stream</param>
		/// <param name="Comparer">Comparison type to use for names</param>
		/// <param name="ObjectSet"></param>
		static async Task AddDepotFilesToSnapshotAsync(StreamTreeBuilder Root, IPerforceConnection Connection, string FileSpec, ViewMap View, Utf8StringComparer Comparer, ObjectSet ObjectSet)
		{
			const string Filter = "^headAction=delete ^headAction=move/delete";
			const string Fields = "depotFile,fileSize,digest,headType,headRev";

			List<string> Arguments = new List<string>();
			Arguments.Add("-Ol");
			Arguments.Add("-Op");
			Arguments.Add("-Os");
			Arguments.Add("-F");
			Arguments.Add(Filter);
			Arguments.Add("-T");
			Arguments.Add(String.Join(",", Fields));
			Arguments.Add(FileSpec);

			List<Utf8String> Segments = new List<Utf8String>();

			IAsyncEnumerable<PerforceResponse<Utf8FStatRecord>> Responses = Connection.StreamCommandAsync<Utf8FStatRecord>("fstat", Arguments);
			await foreach (PerforceResponse<Utf8FStatRecord> Response in Responses)
			{
				Utf8FStatRecord File = Response.Data;
				if (File.Digest != Utf8String.Empty)
				{
					Utf8String TargetFile;
					if (File.DepotFile != Utf8String.Empty && View.TryMapFile(File.DepotFile, Comparer, out TargetFile))
					{
						await AddFileAsync(Root, TargetFile, File, Segments, ObjectSet);
					}
				}
			}
		}

		/// <summary>
		/// Adds a single depot file to a snapshot
		/// </summary>
		/// <param name="Root">The root tree builder</param>
		/// <param name="Path">The stream path for the file</param>
		/// <param name="MetaData">The file metadata</param>
		/// <param name="Segments"></param>
		/// <param name="ObjectSet"></param>
		static async Task AddFileAsync(StreamTreeBuilder Root, Utf8String Path, Utf8FStatRecord MetaData, List<Utf8String> Segments, ObjectSet ObjectSet)
		{
			StreamTreeBuilder Node = Root;

			// Split the path into segments
			Segments.Clear();
			for (int Pos = 0; ;)
			{
				int NextPos = Path.IndexOf('/', Pos);
				if (NextPos == -1)
				{
					Segments.Add(Path.Substring(Pos));
					break;
				}
				else
				{
					Segments.Add(Path.Substring(Pos, NextPos - Pos));
					Pos = NextPos + 1;
				}
			}

			// Match as many nodes as we can from the existing path
			int SegmentIdx = 0;
			for(; SegmentIdx < Segments.Count - 1; SegmentIdx++)
			{
				Utf8String Segment = Segments[SegmentIdx];
				if (!Node.NameToTreeBuilder.TryGetValue(Segment, out StreamTreeBuilder? NextNode))
				{
					break;
				}
				Node = NextNode;
			}

			// Collapse the rest of the current path
			await CollapseChildrenAsync(ObjectSet, Node);

			// Expand the rest of the path
			for(; SegmentIdx < Segments.Count - 1; SegmentIdx++)
			{
				Utf8String Segment = Segments[SegmentIdx];
				Node = await ExpandChildAsync(ObjectSet, Node, Segment);
			}

			// Add the filename
			Utf8String FileName = Segments[Segments.Count - 1];
			Node.NameToFile[FileName] = CreateStreamFile(MetaData);
		}

#endregion

#region Tree Incremental Updates

		/// <summary>
		/// Creates a snapshot for the given stream, given an initial state and list of modified files
		/// </summary>
		/// <param name="RootRef"></param>
		/// <param name="Perforce"></param>
		/// <param name="Change"></param>
		/// <param name="View"></param>
		/// <param name="ObjectSet"></param>
		/// <param name="PathComparer"></param>
		/// <returns></returns>
		static async Task<StreamTreeRef> UpdateTreeAsync(StreamTreeRef RootRef, IPerforceConnection Perforce, ViewMap View, int Change, ObjectSet ObjectSet, Utf8StringComparer PathComparer)
		{
			List<FStatRecord> Records = await Perforce.FStatAsync(-1, Change, null, null, -1, FStatOptions.IncludeFileSizes, FileSpecList.Any);
			return await UpdateTreeAsync(RootRef, Records, View, ObjectSet, PathComparer);
		}

		/// <summary>
		/// Creates a snapshot for the given stream, given an initial state and list of modified files
		/// </summary>
		/// <param name="RootRef"></param>
		/// <param name="Files"></param>
		/// <param name="View"></param>
		/// <param name="ObjectSet"></param>
		/// <param name="PathComparer"></param>
		/// <returns></returns>
		static async Task<StreamTreeRef> UpdateTreeAsync(StreamTreeRef RootRef, IList<FStatRecord> Files, ViewMap View, ObjectSet ObjectSet, Utf8StringComparer PathComparer)
		{
			// Read the root tree
			StreamTreeBuilder Root = new StreamTreeBuilder(await ReadTreeAsync(ObjectSet, RootRef));

			// Update the tree
			foreach (FStatRecord File in Files)
			{
				Utf8String LocalPathStr;
				if (File.DepotFile != null && View.TryMapFile(File.DepotFile, PathComparer, out LocalPathStr))
				{
					Utf8String LocalPath = LocalPathStr;

					StreamTreeBuilder Node = Root;
					for (; ; )
					{
						int NextIdx = LocalPath.IndexOf('/');
						if (NextIdx == -1)
						{
							break;
						}

						Utf8String Name = LocalPath[0..NextIdx];
						Node = await ExpandChildAsync(ObjectSet, Node, Name);

						LocalPath = LocalPath[(NextIdx + 1)..];
					}

					FileAction Action = File.Action;
					if (Action == FileAction.None)
					{
						Action = File.HeadAction;
						if (Action == FileAction.None)
						{
							throw new NotImplementedException();
						}
					}

					Utf8String FileName = LocalPath;
					if (Action != FileAction.Delete && Action != FileAction.MoveDelete)
					{
						Node.NameToFile[FileName] = CreateStreamFile(File);
					}
					else
					{
						if (!Node.NameToFile.Remove(FileName))
						{
							// TODO: Handle mismatched case?
							throw new NotImplementedException();
						}
					}
				}
			}

			return Root.EncodeRef(x => WriteTree(ObjectSet, x));
		}

#endregion

#region Tree Builder

		/// <summary>
		/// Adds or expands a tree under the given node
		/// </summary>
		/// <param name="ObjectSet"></param>
		/// <param name="Tree"></param>
		/// <param name="Name"></param>
		/// <returns></returns>
		static async Task<StreamTreeBuilder> ExpandChildAsync(ObjectSet ObjectSet, StreamTreeBuilder Tree, Utf8String Name)
		{
			StreamTreeBuilder? Result;
			if (Tree.NameToTreeBuilder.TryGetValue(Name, out Result))
			{
				return Result;
			}

			StreamTreeRef? TreeRef;
			if (Tree.NameToTree.TryGetValue(Name, out TreeRef))
			{
				Result = new StreamTreeBuilder(await ReadTreeAsync(ObjectSet, TreeRef));

				Tree.NameToTree.Remove(Name);
				Tree.NameToTreeBuilder.Add(Name, Result);

				return Result;
			}

			Result = new StreamTreeBuilder();
			Tree.NameToTreeBuilder[Name] = Result;

			return Result;
		}

		/// <summary>
		/// Collapse children underneath the given tree builder, allowing data to be purged from memory and pushed
		/// into persistent storage.
		/// </summary>
		/// <param name="ObjectSet"></param>
		/// <param name="Tree"></param>
		/// <returns></returns>
		static async Task CollapseChildrenAsync(ObjectSet ObjectSet, StreamTreeBuilder Tree)
		{
			foreach ((Utf8String ChildName, StreamTreeBuilder ChildBuilder) in Tree.NameToTreeBuilder)
			{
				await CollapseChildrenAsync(ObjectSet, ChildBuilder);
				if (ChildBuilder.NameToFile.Count > 0 || ChildBuilder.NameToTree.Count > 0)
				{
					StreamTreeRef ChildTreeRef = ChildBuilder.EncodeRef(x => WriteTree(ObjectSet, x));
					Tree.NameToTree[ChildName] = ChildTreeRef;
				}
			}
			Tree.NameToTreeBuilder.Clear();
		}

		/// <summary>
		/// Flush all the current data to storage, optimizing the underlying blobs if necessary
		/// </summary>
		/// <returns>The new tree object</returns>
		static async Task<CommitTree> FlushAsync(ObjectSet ObjectSet, StreamTreeRef RootRef)
		{
			// Find the live set of objects
			ObjectSet.RootSet.Clear();
			ObjectSet.RootSet.Add(RootRef.Hash);

			// Flush all the remaining objects
			await ObjectSet.FlushAsync();

			// Wait for all the blobs to finish writing and write the new tree
			return new CommitTree(RootRef, new List<CbBinaryAttachment>(), ObjectSet.PackIndexes.ConvertAll(x => (CbBinaryAttachment)x.DataHash));
		}

		/// <summary>
		/// Reads a referenced tree from storage
		/// </summary>
		/// <param name="ObjectSet">The object packer</param>
		/// <param name="TreeRef">The tree reference</param>
		/// <returns></returns>
		static async Task<StreamTree> ReadTreeAsync(ObjectSet ObjectSet, StreamTreeRef TreeRef)
		{
			ReadOnlyMemory<byte> Data = await ObjectSet.GetObjectDataAsync(TreeRef.Hash);
			if (Data.Length == 0)
			{
				throw new Exception($"Unable to find tree with hash {TreeRef.Hash} for {TreeRef.Path}");
			}
			return new StreamTree(TreeRef.Path, new CbObject(Data));
		}

		/// <summary>
		/// Serializes a tree to an object packer
		/// </summary>
		/// <param name="ObjectSet"></param>
		/// <param name="Tree"></param>
		/// <returns></returns>
		static IoHash WriteTree(ObjectSet ObjectSet, StreamTree Tree)
		{
			CbObject Object = Tree.ToCbObject();
			ReadOnlyMemory<byte> Data = Object.GetView();

			List<IoHash> Refs = new List<IoHash>();
			Object.IterateAttachments(Field => Refs.Add(Field.AsHash()));

			IoHash Hash = IoHash.Compute(Data.Span);
			ObjectSet.Add(Hash, Data.Span, Refs.ToArray());
			return Hash;
		}

		/// <summary>
		/// Prints the state of the tree
		/// </summary>
		/// <param name="ObjectSet"></param>
		/// <param name="TreeRef"></param>
		/// <param name="Logger"></param>
		/// <returns></returns>
		public Task PrintAsync(ObjectSet ObjectSet, StreamTreeRef TreeRef, ILogger Logger) => PrintInternalAsync(ObjectSet, TreeRef, String.Empty, Logger);

		async Task PrintInternalAsync(ObjectSet ObjectSet, StreamTreeRef TreeRef, string Prefix, ILogger Logger)
		{
			StreamTree Tree = await ReadTreeAsync(ObjectSet, TreeRef);

			foreach ((Utf8String Name, StreamFile File) in Tree.NameToFile)
			{
				Logger.LogInformation($"{Prefix}/{Name} = {File.Path}#{File.Revision}");
			}
			foreach ((Utf8String Name, StreamTreeRef ChildTreeRef) in Tree.NameToTree)
			{
				await PrintInternalAsync(ObjectSet, ChildTreeRef, $"{Prefix}/{Name}", Logger);
			}
		}

#endregion

#region Tree Storage

		/// <summary>
		/// Reads the tree for a given commit
		/// </summary>
		/// <param name="Commit"></param>
		async Task<CommitTree> ReadCommitTreeAsync(ICommit Commit)
		{
			IRef? Ref = await RefCollection.GetAsync(NamespaceId, BucketId, Commit.TreeRef!);
			return CbSerializer.Deserialize<CommitTree>(Ref!.Value.AsField());
		}

#endregion

#region Misc tree manipulation

		/// <summary>
		/// Gets the view for a stream
		/// </summary>
		/// <param name="Connection">The Perforce connection</param>
		/// <param name="StreamName">Name of the stream to be mirrored</param>
		static async Task<ViewMap> GetStreamViewAsync(IPerforceConnection Connection, string StreamName)
		{
			StreamRecord Record = await Connection.GetStreamAsync(StreamName, true);

			ViewMap View = new ViewMap();
			foreach (string ViewLine in Record.View)
			{
				Match Match = Regex.Match(ViewLine, "^(-?)([^ ]+) *([^ ]+)$");
				if (!Match.Success)
				{
					throw new PerforceException($"Unable to parse stream view: '{ViewLine}'");
				}
				View.Entries.Add(new ViewMapEntry(Match.Groups[1].Length == 0, Match.Groups[2].Value, Match.Groups[3].Value));
			}

			return View;
		}

		/// <summary>
		/// Creates a StreamFile from a FileMetaData object
		/// </summary>
		/// <param name="MetaData"></param>
		/// <returns></returns>
		static StreamFile CreateStreamFile(Utf8FStatRecord MetaData)
		{
			Utf8String FileType = Utf8String.Empty;
			if (MetaData.Type != Utf8String.Empty)
			{
				FileType = MetaData.Type;
			}
			else if (MetaData.HeadType != Utf8String.Empty)
			{
				FileType = MetaData.HeadType;
			}
			return new StreamFile(MetaData.DepotFile!, MetaData.FileSize, new FileContentId(Md5Hash.Parse(MetaData.Digest!), FileType), MetaData.HeadRevision);
		}

		/// <summary>
		/// Creates a StreamFile from a FileMetaData object
		/// </summary>
		/// <param name="MetaData"></param>
		/// <returns></returns>
		static StreamFile CreateStreamFile(FStatRecord MetaData)
		{
			string FileType = MetaData.Type ?? MetaData.HeadType ?? String.Empty;
			return new StreamFile(MetaData.DepotFile!, MetaData.FileSize, new FileContentId(Md5Hash.Parse(MetaData.Digest!), FileType.ToString()), MetaData.HeadRevision);
		}
#endregion
	}
}
