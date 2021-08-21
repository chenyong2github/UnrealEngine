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

namespace HordeServer.Commits.Impl
{
	using P4 = Perforce.P4;
	using NamespaceId = StringId<INamespace>;
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

		NamespaceId NamespaceId { get; } = new NamespaceId("default");

		ICommitCollection CommitCollection;
		IObjectCollection ObjectCollection;
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
		public CommitService(ICommitCollection CommitCollection, IObjectCollection ObjectCollection, IStreamCollection StreamCollection, IPerforceService PerforceService, IUserCollection UserCollection, DatabaseService DatabaseService, ILogger<CommitService> Logger)
		{
			this.CommitCollection = CommitCollection;
			this.ObjectCollection = ObjectCollection;
			this.StreamCollection = StreamCollection;
			this.PerforceService = PerforceService;
			this.UserCollection = UserCollection;
			this.GlobalsDocument = new SingletonDocument<Globals>(DatabaseService);
			this.StateDocument = new SingletonDocument<ReplicationState>(DatabaseService);
			this.Logger = Logger;
			this.Ticker = new ElectedTick(DatabaseService, new ObjectId("60f866c49e7268f71803b6e0"), TickReplicationAsync, TimeSpan.FromSeconds(30.0), Logger);
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
			ChangeDetails Details = await PerforceService.GetChangeDetailsAsync(Stream.ClusterName, Stream.Name, Number);

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

		#region Snapshot generation

		/// <summary>
		/// Number of files to query at a time before recursing down the tree
		/// </summary>
		const int BatchSize = 16384;

		/// <summary>
		/// Creates a snapshot of the repository at a particular changelist
		/// </summary>
		/// <param name="Repository">The Perforce repository to mirror</param>
		/// <param name="Stream">Name of the stream to be mirrored</param>
		/// <param name="Change">Changelist to replicate</param>
		Task<StreamTreeRef> CreateTreeAsync(P4.Repository Repository, string Stream, int Change)
		{
			// Get the stream spec
			P4.Stream StreamSpec = Repository.GetStream($"{Stream}@{Change}", new P4.Options { { "-v", "" } });
			ViewMap View = new ViewMap(StreamSpec.View);
			return CreateTreeAsync(Repository, View, Change);
		}

		/// <summary>
		/// Creates a snapshot of a stream at a particular changelist
		/// </summary>
		/// <param name="Repository">The Perforce repository to mirror</param>
		/// <param name="View">View for the stream</param>
		/// <param name="Change">Changelist to replicate</param>
		async Task<StreamTreeRef> CreateTreeAsync(P4.Repository Repository, ViewMap View, int Change)
		{
			StringComparison Comparison = Repository.Server.Metadata.CaseSensitive ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;

			// In order to optimize for the common case where we're adding multiple files to the same directory, we keep an open list of
			// StreamTreeBuilder objects down to the last modified node. Whenever we need to move to a different node, we write the 
			// previous path to the object store.
			List<StreamTreeBuilder> TreePath = new List<StreamTreeBuilder>();
			TreePath.Add(new StreamTreeBuilder());

			// Generate a snapshot using all the root paths
			List<string> RootPaths = View.GetRootPaths(Comparison);
			foreach (string RootPath in RootPaths)
			{
				ViewMap FilteredView = new ViewMap();
				foreach (ViewMapEntry Entry in View.Entries)
				{
					if (Entry.SourcePrefix.StartsWith(RootPath, Comparison))
					{
						FilteredView.Entries.Add(Entry);
					}
				}
				await AddDepotDirToSnapshotAsync(Repository, RootPath, Change, FilteredView, Comparison, TreePath);
			}

			// Encode the tree
			return await EncodeTreeAsync(TreePath[0]);
		}

		/// <summary>
		/// Adds all files under a directory in the depot to a snapshot
		/// </summary>
		/// <param name="Repository">The perforce repository</param>
		/// <param name="DepotDir">Depot directory to recurse through, with a trailing slash</param>
		/// <param name="Change">Changelist number to capture</param>
		/// <param name="View">View for the stream</param>
		/// <param name="Comparison">Comparison type to use for names</param>
		/// <param name="TreePath">The last computed path through the tree</param>
		async Task AddDepotDirToSnapshotAsync(P4.Repository Repository, string DepotDir, int Change, ViewMap View, StringComparison Comparison, List<StreamTreeBuilder> TreePath)
		{
			if (View.MayMatchAnyFilesInSubDirectory(DepotDir, Comparison))
			{
				P4.GetFileMetaDataCmdOptions Options = new P4.GetFileMetaDataCmdOptions(P4.GetFileMetadataCmdFlags.FileSize, null, null, BatchSize, null, null, null);
				IList<P4.FileMetaData> Files = Repository.GetFileMetaData(Options, P4.FileSpec.DepotSpec($"{DepotDir}...", new P4.ChangelistIdVersion(Change)));
				if (Files != null && Files.Count < BatchSize)
				{
					await AddDepotFilesToSnapshotAsync(Files, View, Comparison, TreePath);
					return;
				}

				IList<string> Dirs = Repository.GetDepotDirs(new P4.GetDepotDirsCmdOptions(P4.GetDepotDirsCmdFlags.None, null), $"{DepotDir}*@{Change}");
				foreach (string Dir in Dirs)
				{
					await AddDepotDirToSnapshotAsync(Repository, Dir + "/", Change, View, Comparison, TreePath);
				}
			}

			if (View.MayMatchAnyFilesInDirectory(DepotDir, Comparison))
			{
				P4.GetFileMetaDataCmdOptions Options = new P4.GetFileMetaDataCmdOptions(P4.GetFileMetadataCmdFlags.FileSize, null, null, -1, null, null, null);
				IList<P4.FileMetaData> Files = Repository.GetFileMetaData(Options, P4.FileSpec.DepotSpec($"{DepotDir}*", new P4.ChangelistIdVersion(Change)));
				await AddDepotFilesToSnapshotAsync(Files, View, Comparison, TreePath);
			}
		}

		/// <summary>
		/// Adds a set of files in the depot to a snapshot
		/// </summary>
		/// <param name="Files">The files to add</param>
		/// <param name="View">View for the stream</param>
		/// <param name="Comparison">Comparison type to use for names</param>
		/// <param name="TreePath">The last computed path through the tree</param>
		async Task AddDepotFilesToSnapshotAsync(IList<P4.FileMetaData>? Files, ViewMap View, StringComparison Comparison, List<StreamTreeBuilder> TreePath)
		{
			if (Files != null)
			{
				foreach (P4.FileMetaData File in Files)
				{
					if (File.Digest != null)
					{
						string? TargetFile;
						if (View.TryMapFile(File.DepotPath.Path, Comparison, out TargetFile))
						{
							await AddFileAsync(TargetFile, File, TreePath);
						}
					}
				}
			}
		}

		/// <summary>
		/// Adds a single depot file to a snapshot
		/// </summary>
		/// <param name="Path">The stream path for the file</param>
		/// <param name="MetaData">The file metadata</param>
		/// <param name="TreePath">The last computed path through the tree</param>
		async Task AddFileAsync(string Path, P4.FileMetaData MetaData, List<StreamTreeBuilder> TreePath)
		{
			string[] Segments = Path.Split('/');

			StreamTreeBuilder Node = TreePath[0];
			for (int Idx = 0; Idx < Segments.Length - 1; Idx++)
			{
				Utf8String Segment = Segments[Idx];

				StreamTreeBuilder NextNode = await FindOrAddTreeAsync(Node, Segment);
				if (Idx + 1 < TreePath.Count && TreePath[Idx + 1] != NextNode)
				{
					await CollapseTreeAsync(Node, TreePath[Idx + 1]);
				}
				if (Idx + 1 >= TreePath.Count)
				{
					TreePath.Add(NextNode);
				}

				Node = NextNode;
			}

			Node.NameToFile[Segments[Segments.Length - 1]] = CreateStreamFile(MetaData);
		}

		#endregion

		#region Incremental snapshot generation

		/// <summary>
		/// Creates a snapshot for the given stream, given an initial state and list of modified files
		/// </summary>
		/// <param name="TreeRef">The tree to update</param>
		/// <param name="Repository"></param>
		/// <param name="View"></param>
		/// <param name="Change"></param>
		/// <returns></returns>
		Task<StreamTreeRef> UpdateTreeAsync(StreamTreeRef TreeRef, P4.Repository Repository, ViewMap View, int Change)
		{
			P4.Changelist Changelist = Repository.GetChangelist(Change);
			StringComparison Comparison = Repository.Server.Metadata.CaseSensitive ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;
			return UpdateTreeAsync(Changelist.Files, View, Comparison, TreeRef);
		}

		/// <summary>
		/// Creates a snapshot for the given stream, given an initial state and list of modified files
		/// </summary>
		/// <param name="Files"></param>
		/// <param name="View"></param>
		/// <param name="Comparison"></param>
		/// <param name="PrevTreeRef"></param>
		/// <returns></returns>
		async Task<StreamTreeRef> UpdateTreeAsync(IList<P4.FileMetaData> Files, ViewMap View, StringComparison Comparison, StreamTreeRef PrevTreeRef)
		{
			StreamTreeBuilder Tree = await ReadTreeAsync(PrevTreeRef);

			// Update the tree
			foreach (P4.FileMetaData File in Files)
			{
				string? LocalPathStr;
				if (View.TryMapFile(File.DepotPath.Path, Comparison, out LocalPathStr))
				{
					Utf8String LocalPath = LocalPathStr;

					StreamTreeBuilder Node = Tree;
					for (; ; )
					{
						int NextIdx = LocalPath.IndexOf('/');
						if (NextIdx == -1)
						{
							break;
						}

						Utf8String Name = LocalPath[0..NextIdx];
						Node = await FindOrAddTreeAsync(Node, Name);

						LocalPath = LocalPath[(NextIdx + 1)..];
					}

					Utf8String FileName = LocalPath;
					if (File.HeadRev > 0)
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

			return await EncodeTreeAsync(Tree);
		}

		#endregion

		#region Misc tree manipulation

		/// <summary>
		/// Gets a <see cref="StreamTreeBuilder"/> for a subtree with the given name
		/// </summary>
		/// <param name="Tree"></param>
		/// <param name="Name"></param>
		/// <returns></returns>
		async Task<StreamTreeBuilder> FindOrAddTreeAsync(StreamTreeBuilder Tree, Utf8String Name)
		{
			StreamTreeBuilder? Result;
			if (Tree.NameToTreeBuilder.TryGetValue(Name, out Result))
			{
				return Result;
			}

			StreamTreeRef? TreeRef;
			if (Tree.NameToTree.TryGetValue(Name, out TreeRef))
			{
				Result = await ReadTreeAsync(TreeRef);

				Tree.NameToTree.Remove(Name);
				Tree.NameToTreeBuilder.Add(Name, Result);

				return Result;
			}

			Result = new StreamTreeBuilder();
			Tree.NameToTreeBuilder[Name] = Result;

			return Result;
		}

		/// <summary>
		/// Reads a tree builder for the given tree reference
		/// </summary>
		/// <param name="TreeRef"></param>
		/// <returns></returns>
		async Task<StreamTreeBuilder> ReadTreeAsync(StreamTreeRef TreeRef)
		{
			CbObject? Object = await ObjectCollection.GetAsync(NamespaceId, TreeRef.Hash);
			if (Object == null)
			{
				throw new Exception($"Missing object {TreeRef.Hash} referenced by tree for {TreeRef.Path}");
			}
			return new StreamTreeBuilder(new StreamTree(Object, TreeRef.Path + "/"));
		}

		/// <summary>
		/// Collapse a subtree under a given child
		/// </summary>
		/// <param name="Parent"></param>
		/// <param name="Child"></param>
		/// <returns></returns>
		async Task CollapseTreeAsync(StreamTreeBuilder Parent, StreamTreeBuilder Child)
		{
			foreach ((Utf8String Name, StreamTreeBuilder Node) in Parent.NameToTreeBuilder)
			{
				if (Node == Child)
				{
					StreamTreeRef ChildRef = await EncodeTreeAsync(Child);
					Parent.NameToTreeBuilder.Remove(Name);
					Parent.NameToTree.Add(Name, ChildRef);
					break;
				}
			}
		}

		/// <summary>
		/// Writes a subtree to the object store
		/// </summary>
		/// <param name="TreeBuilder">Root of the tree to write</param>
		/// <returns>New tree reference for the serialized tree</returns>
		async Task<StreamTreeRef> EncodeTreeAsync(StreamTreeBuilder TreeBuilder)
		{
			Dictionary<IoHash, CbObject> Objects = new Dictionary<IoHash, CbObject>();
			StreamTreeRef Ref = TreeBuilder.Encode(Objects);

			foreach ((IoHash Hash, CbObject Object) in Objects)
			{
				await ObjectCollection.AddAsync(NamespaceId, Hash, Object);
			}

			return Ref;
		}

		/// <summary>
		/// Creates a StreamFile from a FileMetaData object
		/// </summary>
		/// <param name="MetaData"></param>
		/// <returns></returns>
		static StreamFile CreateStreamFile(P4.FileMetaData MetaData)
		{
			P4.FileType FileType = MetaData.Type ?? MetaData.HeadType;
			return new StreamFile(MetaData.DepotPath.Path, MetaData.FileSize, new FileContentId(Md5Hash.Parse(MetaData.Digest), FileType.ToString()), MetaData.HeadRev);
		}

		/// <summary>
		/// Print the contents of a snapshot
		/// </summary>
		/// <param name="TreeRef"></param>
		/// <returns></returns>
		Task PrintSnapshotAsync(StreamTreeRef TreeRef)
		{
			return PrintSnapshotInternalAsync(TreeRef, "/");
		}

		/// <summary>
		/// Print the contents of a snapshot
		/// </summary>
		/// <param name="TreeRef"></param>
		/// <param name="Prefix"></param>
		/// <returns></returns>
		async Task PrintSnapshotInternalAsync(StreamTreeRef TreeRef, string Prefix)
		{
			StreamTree Tree = new StreamTree(await ObjectCollection.GetRequiredObjectAsync(NamespaceId, TreeRef.Hash), TreeRef.Path);

			foreach ((Utf8String Name, StreamFile File) in Tree.NameToFile)
			{
				Logger.LogInformation($"{Prefix}/{Name} = {File.Path}#{File.Revision}");
			}
			foreach ((Utf8String Name, StreamTreeRef ChildTreeRef) in Tree.NameToTree)
			{
				await PrintSnapshotInternalAsync(ChildTreeRef, $"{Prefix}/{Name}");
			}
		}

		#endregion
	}
}
