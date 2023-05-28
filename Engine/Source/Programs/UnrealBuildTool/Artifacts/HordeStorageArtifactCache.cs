// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage;
using EpicGames.Horde.Storage.Backends;
using EpicGames.Horde.Storage.Nodes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealBuildTool.Artifacts
{
	/// <summary>
	/// Horde specific artifact mapping structure that also contains the file nodes for the outputs
	/// </summary>
	readonly struct HordeArtifactMapping
	{

		/// <summary>
		/// Artifact mapping
		/// </summary>
		public readonly ArtifactMapping ArtifactMapping;

		/// <summary>
		/// Collection of output file references.  There should be exactly the same number
		/// of file references as outputs in the mapping
		/// </summary>
		public readonly NodeRef<ChunkedDataNode>[] OutputRefs;

		/// <summary>
		/// Construct a new horde artifact number
		/// </summary>
		/// <param name="artifactMapping">Artifact mapping</param>
		/// <exception cref="ArgumentException"></exception>
		public HordeArtifactMapping(ArtifactMapping artifactMapping)
		{
			ArtifactMapping = artifactMapping;
			OutputRefs = new NodeRef<ChunkedDataNode>[ArtifactMapping.Outputs.Length];
		}

		/// <summary>
		/// Construct a new artifact mapping from the reader
		/// </summary>
		/// <param name="reader">Source reader</param>
		public HordeArtifactMapping(NodeReader reader)
		{
			ArtifactMapping = reader.ReadArtifactMapping();
			OutputRefs = reader.ReadVariableLengthArray(() => new NodeRef<ChunkedDataNode>(reader));
		}

		/// <summary>
		/// Serialize the artifact mapping 
		/// </summary>
		/// <param name="writer">Destination writer</param>
		public void Serialize(ITreeNodeWriter writer)
		{
			writer.WriteArtifactMapping(ArtifactMapping);
			writer.WriteVariableLengthArray(OutputRefs, x => x.Serialize(writer));
		}

		/// <summary>
		/// Return an enumeration of the references
		/// </summary>
		/// <returns></returns>
		public IEnumerable<NodeRef> EnumerateRefs() => OutputRefs;

		/// <summary>
		/// Write all the files to disk
		/// </summary>
		/// <param name="writer">Destination writer</param>
		/// <param name="cancellationToken">Cancellation token</param>
		/// <returns>Task</returns>
		public async Task WriteFilesAsync(TreeWriter writer, CancellationToken cancellationToken)
		{
			ChunkingOptionsForNodeType nodeTypeOptions = new(512 * 1024, 2 * 1024 * 1024, 1 * 1024 * 1024);
			ChunkingOptions options = new(){ LeafOptions = nodeTypeOptions, InteriorOptions = nodeTypeOptions };

			ChunkedDataWriter fileWriter = new(writer, options);
			int index = 0;
			foreach (Artifact artifact in ArtifactMapping.Outputs)
			{
				string outputName = artifact.GetFullPath();
				using FileStream stream = new(outputName, FileMode.Open, FileAccess.Read, FileShare.Read);
				OutputRefs[index++] = new NodeRef<ChunkedDataNode>(await fileWriter.CreateAsync(stream, nodeTypeOptions.TargetSize, cancellationToken)); 
			}
		}
	}

	/// <summary>
	/// Series of helper methods for serialization
	/// </summary>
	static class HordeArtifactReaderWriterExtensions
	{

		/// <summary>
		/// Read a horde artifact mapping
		/// </summary>
		/// <param name="reader">Source reader</param>
		/// <returns>Created artifact mapping</returns>
		public static HordeArtifactMapping ReadHordeArtifactMapping(this NodeReader reader)
		{
			return new HordeArtifactMapping(reader);
		}

		/// <summary>
		/// Write a horde artifact mapping
		/// </summary>
		/// <param name="writer">Destination writer</param>
		/// <param name="artifactMapping">Artifact mapping to write</param>
		public static void WriteHordeArtifactMapping(this ITreeNodeWriter writer, HordeArtifactMapping artifactMapping)
		{
			artifactMapping.Serialize(writer);
		}
	}

	/// <summary>
	/// Horde node that represents a collection of mapping nodes 
	/// </summary>
	[NodeType("{E8DBCD77-861D-4CAE-B77F-5807D26E2533}")]
	class ArtifactMappingCollectionNode : Node
	{

		/// <summary>
		/// Collection of mappings
		/// </summary>
		public Dictionary<IoHash, HordeArtifactMapping> Mappings = new();

		/// <summary>
		/// Construct a new collection
		/// </summary>
		public ArtifactMappingCollectionNode()
		{
		}

		/// <summary>
		/// Construct a new mapping collection from the source reader
		/// </summary>
		/// <param name="reader">Source reader</param>
		public ArtifactMappingCollectionNode(NodeReader reader)
		{
			Mappings = reader.ReadDictionary<IoHash, HordeArtifactMapping>(() => reader.ReadIoHash(), () => reader.ReadHordeArtifactMapping());
		}

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			writer.WriteDictionary<IoHash, HordeArtifactMapping>(Mappings, (x) => writer.WriteIoHash(x), (x) => writer.WriteHordeArtifactMapping(x));
		}

		/// <inheritdoc/>
		public override IEnumerable<NodeRef> EnumerateRefs()
		{
			foreach (HordeArtifactMapping artifactMapping in Mappings.Values)
			{
				foreach (NodeRef outputRef in artifactMapping.OutputRefs)
				{
					yield return outputRef;
				}
			}
		}

		/// <summary>
		/// Mark the node as dirty
		/// </summary>
		public new void MarkAsDirty()
		{
			base.MarkAsDirty();
		}
	}

	/// <summary>
	/// Class for managing artifacts using horde storage
	/// </summary>
	public class HordeStorageArtifactCache : IArtifactCache
	{
		/// <summary>
		/// Defines the theoretical max number of pending mappings to write
		/// </summary>
		const int MaxPendingSize = 128;

		/// <summary>
		/// Underlying storage object
		/// </summary>
		private IStorageClient? _store = null;

		/// <summary>
		/// Logger to be used
		/// </summary>
		private readonly ILogger _logger;

		/// <summary>
		/// Task used to wait on ready state
		/// </summary>
		private Task<ArtifactCacheState>? _readyTask = null;

		/// <summary>
		/// Ready state
		/// </summary>
		private int _state = (int)ArtifactCacheState.Pending;

		/// <summary>
		/// Collection of mappings waiting to be written
		/// </summary>
		private readonly List<ArtifactMapping> _pendingMappings;

		/// <summary>
		/// Task for any pending flush
		/// </summary>
		private Task? _pendingFlushTask = null;

		/// <summary>
		/// Controls access to shared data structures
		/// </summary>
		private readonly SemaphoreSlim _semaphore = new(1);

		/// <summary>
		/// Test to see if the cache is ready
		/// </summary>
		public ArtifactCacheState State
		{
			get => (ArtifactCacheState) Interlocked.Add(ref _state, 0);
			private set => Interlocked.Exchange(ref _state, (int)value);
		}

		/// <summary>
		/// Create a memory only cache
		/// </summary>
		/// <returns>Storage client instance</returns>
		public static IArtifactCache CreateMemoryCache(ILogger logger)
		{
			HordeStorageArtifactCache cache = new(new MemoryStorageClient(), logger)
			{
				State = ArtifactCacheState.Available
			};
			return cache;
		}

		/// <summary>
		/// Create a file based cache
		/// </summary>
		/// <param name="directory">Destination directory</param>
		/// <param name="logger">Logging object</param>
		/// <param name="cleanDirectory">If true, clean the directory</param>
		/// <returns>Storage client instance</returns>
		public static IArtifactCache CreateFileCache(DirectoryReference directory, ILogger logger, bool cleanDirectory)
		{
			HordeStorageArtifactCache cache = new(null, logger);
			cache._readyTask = Task.Run(() => cache.InitFileCache(directory, NullLogger.Instance, cleanDirectory));
			return cache;
		}

		static HordeStorageArtifactCache()
		{
			Node.RegisterType<ArtifactMappingCollectionNode>();
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="storage">Storage object to use</param>
		/// <param name="logger">Logging destination</param>
		private HordeStorageArtifactCache(IStorageClient? storage, ILogger logger)
		{
			_store = storage;
			_logger = logger;
			_pendingMappings = new(MaxPendingSize);
		}

		/// <inheritdoc/>
		public Task<ArtifactCacheState> WaitForReadyAsync()
		{
			return _readyTask ?? Task.FromResult<ArtifactCacheState>(State);
		}

		/// <inheritdoc/>
		public async Task<ArtifactMapping[]> QueryArtifactMappingsAsync(IoHash[] partialKeys, CancellationToken cancellationToken)
		{
			if (State != ArtifactCacheState.Available || _store == null)
			{
				return Array.Empty<ArtifactMapping>();
			}

			List<ArtifactMapping> mappings = new();
			await _semaphore.WaitAsync(cancellationToken);
			try
			{
				TreeReader reader = CreateTreeReader();
				foreach (IoHash key in partialKeys)
				{
					lock (_pendingMappings)
					{
						mappings.AddRange(_pendingMappings.Where(x => x.Key == key));
					}
					ArtifactMappingCollectionNode? node = await reader.TryReadNodeAsync<ArtifactMappingCollectionNode>(GetRefName(key), default, cancellationToken);
					if (node != null)
					{
						foreach (HordeArtifactMapping mapping in node.Mappings.Values)
						{
							mappings.Add(mapping.ArtifactMapping);
						}
					}
				}
			}
			finally
			{
				_semaphore.Release();
			}
			return mappings.ToArray();
		}

		/// <inheritdoc/>
		public async Task<bool[]?> QueryArtifactOutputsAsync(ArtifactMapping[] mappings, CancellationToken cancellationToken)
		{
			if (State != ArtifactCacheState.Available || _store == null)
			{
				return null;
			}

			TreeReader reader = CreateTreeReader();
			bool[] output = new bool[mappings.Length];
			Array.Fill(output, false);

			for (int index = 0; index < mappings.Length; index++)
			{
				output[index] = false;
				ArtifactMapping mapping = mappings[index];
				ArtifactMappingCollectionNode? node = await reader.TryReadNodeAsync<ArtifactMappingCollectionNode>(GetRefName(mapping.Key), default, cancellationToken);
				if (node != null)
				{
					if (node.Mappings.TryGetValue(mapping.MappingKey, out HordeArtifactMapping hordeArtifactMapping))
					{
						output[index] = true;

						int refIndex = 0;
						foreach (NodeRef<ChunkedDataNode> artifactRef in hordeArtifactMapping.OutputRefs)
						{
							if (artifactRef.Handle == null)
							{
								output[index] = false;
								break;
							}
							try
							{
								string outputName = hordeArtifactMapping.ArtifactMapping.Outputs[refIndex++].GetFullPath(mapping.DirectoryMapping);
								using FileStream stream = new(outputName, FileMode.Create, FileAccess.Write, FileShare.ReadWrite);
								await ChunkedDataNode.CopyToStreamAsync(reader, artifactRef.Handle.Locator, stream, cancellationToken);
							}
							catch (Exception)
							{
								output[index] = false;
								break;
							}
						}

						if (!output[index])
						{
							foreach (Artifact artifact in hordeArtifactMapping.ArtifactMapping.Outputs)
							{
								string outputName = artifact.GetFullPath(mapping.DirectoryMapping);
								if (File.Exists(outputName))
								{
									try
									{
										File.Delete(outputName);
									}
									catch (Exception)
									{
									}
								}
							}
						}
					}
				}
			}
			return output;
		}

		/// <inheritdoc/>
		public async Task SaveArtifactMappingsAsync(ArtifactMapping[] artifactMappings, CancellationToken cancellationToken)
		{
			if (State != ArtifactCacheState.Available || _store == null)
			{
				return;
			}

			lock (_pendingMappings)
			{
				_pendingMappings.AddRange(artifactMappings);
			}

			Task? task = FlushChangesInternalAsync(false, cancellationToken);
			if (task != null)
			{
				await task;
			}
		}

		/// <inheritdoc/>
		public async Task FlushChangesAsync(CancellationToken cancellationToken)
		{
			if (State != ArtifactCacheState.Available || _store == null)
			{
				return;
			}

			Task? task = FlushChangesInternalAsync(true, cancellationToken);
			if (task != null)
			{
				await task;
			}
		}

		/// <summary>
		/// Optionally flush all pending mappings
		/// </summary>
		/// <param name="force">If true, force a flush</param>
		/// <param name="cancellationToken">Cancellation token</param>
		private Task? FlushChangesInternalAsync(bool force, CancellationToken cancellationToken)
		{
			Task? pendingFlushTask = null;
			lock (_pendingMappings)
			{

				// If any prior flush task has completed, then forget it
				if (_pendingFlushTask != null && _pendingFlushTask.IsCompleted)
				{
					_pendingFlushTask = null;
				}

				// We start a new flush under the following condition
				//
				// 1) Mappings must be pending
				// 2) Create a new task if force is specified
				// 3) -OR- Create a new task if there is no current task and we have reached the limit 
				if (_pendingMappings.Count > 0 && (force || (_pendingMappings.Count >= MaxPendingSize && _pendingFlushTask == null)))
				{
					ArtifactMapping[] mappingsToFlush = _pendingMappings.ToArray();
					Task? priorTask = _pendingFlushTask;
					_pendingMappings.Clear();
					async Task action()
					{

						// When forcing, we might have a prior flush task in progress.  Wait for it to complete
						if (priorTask != null)
						{
							await priorTask;
						}

						// Block reading while we update the mappings
						await _semaphore.WaitAsync(cancellationToken);
						try
						{
							List<Task> tasks = CommitMappings(mappingsToFlush, cancellationToken);
							await Task.WhenAll(tasks);
						}
						finally
						{
							_semaphore.Release();
						}
					}
					pendingFlushTask = _pendingFlushTask = new(() => action().Wait(), cancellationToken);
				}
			}

			// Start the task outside of the lock
			pendingFlushTask?.Start();
			return pendingFlushTask;
		}

		/// <summary>
		/// Add a group of mappings to a new or existing source
		/// </summary>
		/// <param name="mappings">New mappings to add</param>
		/// <param name="cancellationToken">Token to be used to cancel operations</param>
		/// <returns>List of tasks</returns>
		private List<Task> CommitMappings(ArtifactMapping[] mappings, CancellationToken cancellationToken)
		{
			List<Task> tasks = new();
			if (mappings.Length == 0)
			{
				return tasks;
			}

			TreeReader reader = CreateTreeReader();

			// Loop through the mappings
			foreach (ArtifactMapping mapping in mappings)
			{

				// Create the task to write the files
				tasks.Add(Task.Run(async () =>
				{
					RefName refName = GetRefName(mapping.Key);

					// Locate the destination collection for this key
					ArtifactMappingCollectionNode? node = reader.TryReadNodeAsync<ArtifactMappingCollectionNode>(refName, default, cancellationToken).Result;
					node ??= new ArtifactMappingCollectionNode();

					// Update the mapping collection
					HordeArtifactMapping hordeArtifactMapping = new(mapping);
					node.Mappings[mapping.MappingKey] = hordeArtifactMapping;
					node.MarkAsDirty();

					// Save the mapping file
					using TreeWriter writer = CreateTreeWriter();
					await hordeArtifactMapping.WriteFilesAsync(writer, cancellationToken);

					// Save the collection
					NodeHandle _ = await writer.WriteAsync(refName, node, null, cancellationToken);
				}, cancellationToken));
			}
			return tasks;
		}

		/// <summary>
		/// Initialize a file based cache
		/// </summary>
		/// <param name="directory">Destination directory</param>
		/// <param name="logger">Logger</param>
		/// <param name="cleanDirectory">If true, clean the directory</param>
		/// <returns>Cache state</returns>
		private ArtifactCacheState InitFileCache(DirectoryReference directory, ILogger logger, bool cleanDirectory)
		{
			try
			{
				if (cleanDirectory)
				{
					// Clear the output directory
					try
					{
						Directory.Delete(directory.FullName, true);
					}
					catch (Exception)
					{ }
				}
				Directory.CreateDirectory(directory.FullName);

				_store = new FileStorageClient(directory, logger);

				State = ArtifactCacheState.Available;
				return State;
			}
			catch (Exception)
			{
				State = ArtifactCacheState.Unavailable;
				throw;
			}
		}

		/// <summary>
		/// Return the ref name for horde storage given a mapping collection key
		/// </summary>
		/// <param name="key">Mapping collection key</param>
		/// <returns>The reference name</returns>
		private static RefName GetRefName(IoHash key)
		{
			return new RefName($"action_artifact_v2_{key}");
		}

		/// <summary>
		/// Create a new tree reader
		/// </summary>
		/// <returns>TreeReader</returns>
		private TreeReader CreateTreeReader()
		{
			return new(_store!, null, NullLogger.Instance);
		}

		/// <summary>
		/// Create a new tree writer 
		/// </summary>
		/// <returns>TreeWriter</returns>
		private TreeWriter CreateTreeWriter()
		{
			return new(_store!, null, default, null, NullLogger.Instance);
		}
	}
}
