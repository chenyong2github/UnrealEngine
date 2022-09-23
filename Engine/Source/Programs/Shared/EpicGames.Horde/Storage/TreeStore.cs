// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using K4os.Compression.LZ4;
using Microsoft.Extensions.Caching.Memory;

#if WITH_OODLE
using EpicGames.Compression;
#endif

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Options for configuring a bundle serializer
	/// </summary>
	public class TreeOptions
	{
		/// <summary>
		/// Maximum payload size fo a blob
		/// </summary>
		public int MaxBlobSize { get; set; } = 10 * 1024 * 1024;

		/// <summary>
		/// Compression format to use
		/// </summary>
		public BundleCompressionFormat CompressionFormat { get; set; } = BundleCompressionFormat.LZ4;

		/// <summary>
		/// Minimum size of a block to be compressed
		/// </summary>
		public int MinCompressionPacketSize { get; set; } = 16 * 1024;

		/// <summary>
		/// Number of nodes to retain in the working set after performing a partial flush
		/// </summary>
		public int TrimIgnoreCount { get; set; } = 150;
	}

	/// <summary>
	/// Implementation of tree storage which packs nodes together into <see cref="Bundle"/> objects, then stores them
	/// in a <see cref="IStorageClient"/>.
	/// </summary>
	public class TreeStore
	{
		/// <summary>
		/// Information about a node within a bundle.
		/// </summary>
		[DebuggerDisplay("{Hash}")]
		sealed class NodeInfo : ITreeBlobRef
		{
			public readonly TreeStore Owner;
			public readonly IoHash Hash;
			public NodeState State { get; private set; }

			IoHash ITreeBlobRef.Hash => Hash;

			public NodeInfo(TreeStore owner, IoHash hash, NodeState state)
			{
				Owner = owner;
				Hash = hash;
				State = state;
			}

			/// <summary>
			/// Updates the current state. The only allowed transition is to an exported node state.
			/// </summary>
			/// <param name="state">The new state</param>
			public void SetState(ExportedNodeState state)
			{
				State = state;
			}

			/// <inheritdoc/>
			public ValueTask<ITreeBlob> GetTargetAsync(CancellationToken cancellationToken = default) => Owner.GetNodeAsync(this, cancellationToken);
		}

		/// <summary>
		/// Metadata about a compression packet within a bundle
		/// </summary>
		class PacketInfo
		{
			public readonly int Index;
			public readonly int Offset;
			public readonly int DecodedLength;
			public readonly int EncodedLength;

			public PacketInfo(int index, int offset, BundlePacket packet)
			{
				Index = index;
				Offset = offset;
				DecodedLength = packet.DecodedLength;
				EncodedLength = packet.EncodedLength;
			}
		}

		/// <summary>
		/// Base class for the state of a node
		/// </summary>
		abstract class NodeState
		{
		}

		/// <summary>
		/// State for data imported from a bundle, but whose location within it is not yet known because the bundle's header has not been read yet.
		/// Can be transitioned to ExportedNodeState.
		/// </summary>
		class ImportedNodeState : NodeState
		{
			public readonly BundleInfo BundleInfo;
			public readonly int ExportIdx;

			public ImportedNodeState(BundleInfo bundleInfo, int exportIdx)
			{
				BundleInfo = bundleInfo;
				ExportIdx = exportIdx;
			}
		}

		/// <summary>
		/// State for data persisted to a bundle. Decoded data is cached outside this structure in the store's MemoryCache.
		/// Cannot be transitioned to any other state.
		/// </summary>
		class ExportedNodeState : ImportedNodeState
		{
			public readonly PacketInfo PacketInfo;
			public readonly int Offset;
			public readonly int Length;
			public readonly IReadOnlyList<NodeInfo> References;

			public ExportedNodeState(BundleInfo bundleInfo, int exportIdx, PacketInfo packetInfo, int offset, int length, IReadOnlyList<NodeInfo> references)
				: base(bundleInfo, exportIdx)
			{
				PacketInfo = packetInfo;
				Offset = offset;
				Length = length;
				References = references;
			}
		}

		/// <summary>
		/// Data for a node which exists in memory, but which does NOT exist in storage yet. Nodes which 
		/// are read in from storage are represented by ExportedNodeState and a MemoryCache. Can transition to an ExportedNodeState only.
		/// </summary>
		class InMemoryNodeState : NodeState, ITreeBlob
		{
			public readonly ReadOnlySequence<byte> Data;
			public readonly IReadOnlyList<NodeInfo> References;

			public BundleWriter? _writer;
			public bool _writing; // This node is currently being written asynchronously

			ReadOnlySequence<byte> ITreeBlob.Data => Data;
			IReadOnlyList<ITreeBlobRef> ITreeBlob.Refs => References;

			public InMemoryNodeState(ReadOnlySequence<byte> data, IReadOnlyList<NodeInfo> references)
			{
				Data = data;
				References = references;
			}
		}

		/// <summary>
		/// Stores a lookup from blob id to bundle info, and from there to the nodes it contains.
		/// </summary>
		class BundleContext
		{
			readonly ConcurrentDictionary<BlobLocator, BundleInfo> _blobIdToBundle = new ConcurrentDictionary<BlobLocator, BundleInfo>();

			public BundleInfo FindOrAddBundle(BlobLocator blobId, int exportCount)
			{
				BundleInfo bundleInfo = _blobIdToBundle.GetOrAdd(blobId, new BundleInfo(this, blobId, exportCount));
				Debug.Assert(bundleInfo.Exports.Length == exportCount);
				return bundleInfo;
			}
		}

		/// <summary>
		/// Information about an imported bundle
		/// </summary>
		[DebuggerDisplay("{BlobId}")]
		class BundleInfo
		{
			public readonly BundleContext Context;
			public readonly BlobLocator Locator;
			public readonly NodeInfo?[] Exports;

			public bool Mounted;
			public Task MountTask = Task.CompletedTask;

			public BundleInfo(BundleContext context, BlobLocator locator, int exportCount)
			{
				Context = context;
				Locator = locator;
				Exports = new NodeInfo?[exportCount];
			}
		}

		class BundleWriter : ITreeWriter
		{
			readonly object _lockObject = new object();

			readonly TreeStore _owner;
			readonly BundleContext _context;
			readonly BundleWriter? _parent;
			readonly TreeOptions _options;
			readonly Utf8String _prefix;

			public readonly ConcurrentDictionary<IoHash, NodeInfo> HashToNode = new ConcurrentDictionary<IoHash, NodeInfo>(); // TODO: this needs to include some additional state from external sources.

			// List of child writers that need to be flushed
			readonly List<BundleWriter> _children = new List<BundleWriter>();

			// Task which includes all active writes, and returns the last blob
			Task<NodeInfo> _flushTask = Task.FromResult<NodeInfo>(null!);

			// Nodes which have been queued to be written, but which are not yet part of any active write
			readonly List<NodeInfo> _queueNodes = new List<NodeInfo>();

			// Number of nodes in _queueNodes that are ready to be written (ie. all their dependencies have been written)
			int _readyCount;

			// Sum of lengths for nodes in _queueNodes up to _readyCount
			long _readyLength;

			/// <summary>
			/// Constructor
			/// </summary>
			public BundleWriter(TreeStore owner, BundleContext context, BundleWriter? parent, TreeOptions options, Utf8String prefix)
			{
				_owner = owner;
				_context = context;
				_parent = parent;
				_options = options;
				_prefix = prefix;
			}

			/// <inheritdoc/>
			public ITreeWriter CreateChildWriter()
			{
				BundleWriter writer = new BundleWriter(_owner, _context, this, _options, _prefix);
				lock (_lockObject)
				{
					_children.Add(writer);
				}
				return writer;
			}

			/// <inheritdoc/>
			public Task<ITreeBlobRef> WriteNodeAsync(ReadOnlySequence<byte> data, IReadOnlyList<ITreeBlobRef> refs, CancellationToken cancellationToken = default)
			{
				IReadOnlyList<NodeInfo> typedRefs = refs.Select(x => (NodeInfo)x).ToList();

				IoHash hash = ComputeHash(data, typedRefs);

				NodeInfo? node;
				if (!HashToNode.TryGetValue(hash, out node))
				{
					InMemoryNodeState state = new InMemoryNodeState(data, typedRefs);
					NodeInfo newNode = new NodeInfo(_owner, hash, state);

					// Try to add the node again. If we succeed, check if we need to flush the current bundle being built.
					node = HashToNode.GetOrAdd(hash, newNode);
					if (node == newNode)
					{
						WriteNode(node);
					}
				}

				return Task.FromResult<ITreeBlobRef>(node);
			}

			void WriteNode(NodeInfo node)
			{
				lock (_lockObject)
				{
					// Add these nodes to the queue for writing
					AddToQueue(node);

					// Update the list of nodes which are ready to be written. We need to make sure that any child writers have flushed before
					// we can reference the blobs containing their nodes.
					UpdateReady();
				}
			}

			void AddToQueue(NodeInfo root)
			{
				if (root.State is InMemoryNodeState state && state._writer == null)
				{
					lock (state)
					{
						// Check again to avoid race
						if (state._writer == null) 
						{
							// Write all the dependencies first
							foreach (NodeInfo reference in state.References)
							{
								AddToQueue(reference);
							}

							state._writer = this;
							_queueNodes.Add(root);
						}
					}
				}
			}

			void UpdateReady()
			{
				// Update the number of nodes which can be written
				while (_readyCount < _queueNodes.Count)
				{
					NodeInfo nextNode = _queueNodes[_readyCount];
					InMemoryNodeState nextState = (InMemoryNodeState)nextNode.State;

					if (_readyCount > 0 && _readyLength + nextState.Data.Length > _options.MaxBlobSize)
					{
						WriteReady();
					}
					else
					{
						foreach (NodeInfo other in nextState.References)
						{
							if (other.State is InMemoryNodeState otherState)
							{
								// Need to wait for nodes in another writer to be flushed first
								if (otherState._writer != this)
								{
									return;
								}

								// Need to wait for previous bundles from the current writer to complete before we can reference them
								if (otherState._writing)
								{
									return;
								}
							}
						}

						_readyCount++;
						_readyLength += nextState.Data.Length;
					}
				}
			}

			void WriteReady()
			{
				if (_readyCount > 0)
				{
					NodeInfo[] writeNodes = _queueNodes.Slice(0, _readyCount).ToArray();
					_queueNodes.RemoveRange(0, _readyCount);

					// Mark the nodes as writing so nothing else will be flushed until they're ready
					foreach (NodeInfo writeNode in writeNodes)
					{
						InMemoryNodeState writeState = (InMemoryNodeState)writeNode.State;
						writeState._writing = true;
					}

					_readyCount = 0;
					_readyLength = 0;

					Task prevFlushTask = _flushTask ?? Task.CompletedTask;
					_flushTask = Task.Run(() => WriteNodesAsync(writeNodes, prevFlushTask, CancellationToken.None));
				}
			}

			/// <inheritdoc/>
			public async Task WriteRefAsync(RefName refName, ITreeBlobRef root, CancellationToken cancellationToken)
			{
				if (_parent != null)
				{
					throw new InvalidOperationException("Flushing a child writer is not permitted.");
				}

				// Make sure the last written node is the desired root. If not, we'll need to write it to a new blob so it can be last.
				NodeInfo? last = await _flushTask;
				if (last != root)
				{
					if (_queueNodes.Count == 0 || _queueNodes[^1] != root)
					{
						ITreeBlob blob = await root.GetTargetAsync(cancellationToken);
						InMemoryNodeState state = new InMemoryNodeState(blob.Data, blob.Refs.ConvertAll(x => (NodeInfo)x));
						WriteNode(new NodeInfo(_owner, root.Hash, state));
					}
					last = await FlushInternalAsync(cancellationToken);
				}

				// Write a reference to the blob containing this node
				ImportedNodeState importedState = (ImportedNodeState)last.State;
				await _owner._blobStore.WriteRefTargetAsync(refName, new RefTarget(importedState.BundleInfo.Locator, 0), cancellationToken);
			}

			async Task<NodeInfo> FlushInternalAsync(CancellationToken cancellationToken)
			{
				// Get all the child writers and flush them
				BundleWriter[] children;
				lock (_lockObject)
				{
					children = _children.ToArray();
				}

				Task[] tasks = new Task[children.Length];
				for (int idx = 0; idx < children.Length; idx++)
				{
					tasks[idx] = children[idx].FlushInternalAsync(cancellationToken);
				}
				await Task.WhenAll(tasks);

				// Wait for any current writes to finish. _flushTask may be updated through calls to UpdateReady() during its execution, so loop until it's complete.
				while (!_flushTask.IsCompleted)
				{
					await Task.WhenAll(_flushTask, Task.Delay(0, cancellationToken));
				}

				// Write the last batch
				if (_queueNodes.Count > 0)
				{
					lock (_lockObject)
					{
						WriteReady();
					}
					await Task.WhenAll(_flushTask, Task.Delay(0, cancellationToken));
				}

				// Return the identifier of the blob containing the root node
				return await _flushTask;
			}

			async Task<NodeInfo> WriteNodesAsync(NodeInfo[] writeNodes, Task prevWriteTask, CancellationToken cancellationToken)
			{
				// Create the bundle
				Bundle bundle = CreateBundle(writeNodes);
				BundleHeader header = bundle.Header;

				// Write it to storage
				BlobLocator locator = await _owner._blobStore.WriteBundleAsync(bundle, _prefix, cancellationToken);

				// Create a BundleInfo for it
				BundleInfo bundleInfo = _context.FindOrAddBundle(locator, writeNodes.Length);
				for (int idx = 0; idx < writeNodes.Length; idx++)
				{
					bundleInfo.Exports[idx] = writeNodes[idx];
				}

				// Update the node states to reference the written bundle
				int exportIdx = 0;
				int packetOffset = 0;
				for (int packetIdx = 0; packetIdx < bundle.Header.Packets.Count; packetIdx++)
				{
					BundlePacket packet = bundle.Header.Packets[packetIdx];
					PacketInfo packetInfo = new PacketInfo(packetIdx, packetOffset, packet);

					int nodeOffset = 0;
					for (; exportIdx < header.Exports.Count && nodeOffset + bundle.Header.Exports[exportIdx].Length <= packet.DecodedLength; exportIdx++)
					{
						InMemoryNodeState inMemoryState = (InMemoryNodeState)writeNodes[exportIdx].State;

						BundleExport export = header.Exports[exportIdx];
						writeNodes[exportIdx].SetState(new ExportedNodeState(bundleInfo, exportIdx, packetInfo, nodeOffset, export.Length, inMemoryState.References));

						nodeOffset += header.Exports[exportIdx].Length;
					}

					packetOffset += packet.EncodedLength;
				}

				// Now that we've written some nodes to storage, update any parent writers that may be dependent on us
				lock (_lockObject)
				{
					UpdateReady();
				}

				// Also update the parent write queue
				if (_parent != null)
				{
					lock (_parent._lockObject)
					{
						_parent.UpdateReady();
					}
				}

				// Wait for other writes to finish
				await prevWriteTask;
				return writeNodes[^1];
			}

			/// <summary>
			/// Creates a Bundle containing a set of nodes. 
			/// </summary>
			Bundle CreateBundle(NodeInfo[] nodes)
			{
				TreeOptions options = _options;

				// Create a set from the nodes to be written. We use this to determine references that are imported.
				HashSet<NodeInfo> nodeSet = new HashSet<NodeInfo>(nodes);

				// Find all the imported nodes by bundle
				Dictionary<BundleInfo, List<NodeInfo>> bundleToImportedNodes = new Dictionary<BundleInfo, List<NodeInfo>>();
				foreach (NodeInfo node in nodes)
				{
					InMemoryNodeState state = (InMemoryNodeState)node.State;
					foreach (NodeInfo reference in state.References)
					{
						if (nodeSet.Add(reference))
						{
							// Get the persisted node info
							ImportedNodeState importedState = (ImportedNodeState)reference.State;
							BundleInfo bundleInfo = importedState.BundleInfo;

							// Get the list of nodes within it
							List<NodeInfo>? importedNodes;
							if (!bundleToImportedNodes.TryGetValue(bundleInfo, out importedNodes))
							{
								importedNodes = new List<NodeInfo>();
								bundleToImportedNodes.Add(bundleInfo, importedNodes);
							}
							importedNodes.Add(reference);
						}
					}
				}

				// Create the import list
				List<BundleImport> imports = new List<BundleImport>();

				// Add all the imports and assign them identifiers
				Dictionary<NodeInfo, int> nodeToIndex = new Dictionary<NodeInfo, int>();
				foreach ((BundleInfo bundleInfo, List<NodeInfo> importedNodes) in bundleToImportedNodes)
				{
					(int, IoHash)[] exportInfos = new (int, IoHash)[importedNodes.Count];
					for (int idx = 0; idx < importedNodes.Count; idx++)
					{
						NodeInfo importedNode = importedNodes[idx];
						nodeToIndex.Add(importedNode, nodeToIndex.Count);

						ImportedNodeState importedNodeState = (ImportedNodeState)importedNode.State;
						exportInfos[idx] = (importedNodeState.ExportIdx, importedNode.Hash);
					}
					imports.Add(new BundleImport(bundleInfo.Locator, bundleInfo.Exports.Length, exportInfos));
				}

				// Create the export list
				List<BundleExport> exports = new List<BundleExport>();
				List<BundlePacket> packets = new List<BundlePacket>();

				// Size of data currently stored in the block buffer
				int blockSize = 0;

				// List of data packets
				List<ReadOnlyMemory<byte>> packetBlocks = new List<ReadOnlyMemory<byte>>();

				// Segments of data in the current block
				List<ReadOnlyMemory<byte>> blockSegments = new List<ReadOnlyMemory<byte>>();

				// Compress all the nodes into the output buffer buffer
				foreach (NodeInfo node in nodes)
				{
					InMemoryNodeState nodeState = (InMemoryNodeState)node.State;
					ReadOnlySequence<byte> nodeData = nodeState.Data;

					// If we can't fit this data into the current block, flush the contents of it first
					if (blockSize > 0 && blockSize + nodeData.Length > options.MinCompressionPacketSize)
					{
						FlushPacket(_options.CompressionFormat, blockSegments, blockSize, packets, packetBlocks);
						blockSize = 0;
					}

					// Create the export for this node
					int[] references = nodeState.References.Select(x => nodeToIndex[x]).ToArray();
					BundleExport export = new BundleExport(node.Hash, (int)nodeData.Length, references);
					exports.Add(export);
					nodeToIndex[node] = nodeToIndex.Count;

					// Write out the new block
					if (nodeData.Length < options.MinCompressionPacketSize || !nodeData.IsSingleSegment)
					{
						blockSize += AddSegments(nodeData, blockSegments);
					}
					else
					{
						FlushPacket(options.CompressionFormat, nodeData.First, packets, packetBlocks);
					}
				}
				FlushPacket(options.CompressionFormat, blockSegments, blockSize, packets, packetBlocks);

				// Flush the data
				BundleHeader header = new BundleHeader(options.CompressionFormat, imports, exports, packets);
				return new Bundle(header, packetBlocks);
			}

			static int AddSegments(ReadOnlySequence<byte> sequence, List<ReadOnlyMemory<byte>> segments)
			{
				int size = 0;
				foreach (ReadOnlyMemory<byte> segment in sequence)
				{
					segments.Add(segment);
					size += segment.Length;
				}
				return size;
			}

			static void FlushPacket(BundleCompressionFormat format, List<ReadOnlyMemory<byte>> blockSegments, int blockSize, List<BundlePacket> packets, List<ReadOnlyMemory<byte>> packetBlocks)
			{
				if (blockSize > 0)
				{
					if (blockSegments.Count == 1)
					{
						FlushPacket(format, blockSegments[0], packets, packetBlocks);
					}
					else
					{
						using IMemoryOwner<byte> buffer = MemoryPool<byte>.Shared.Rent(blockSize);

						Memory<byte> output = buffer.Memory;
						foreach (ReadOnlyMemory<byte> blockSegment in blockSegments)
						{
							blockSegment.CopyTo(output);
							output = output.Slice(blockSegment.Length);
						}

						FlushPacket(format, buffer.Memory.Slice(0, blockSize), packets, packetBlocks);
					}
				}
				blockSegments.Clear();
			}

			static void FlushPacket(BundleCompressionFormat format, ReadOnlyMemory<byte> inputData, List<BundlePacket> packets, List<ReadOnlyMemory<byte>> packetBlocks)
			{
				if (inputData.Length > 0)
				{
					ReadOnlyMemory<byte> encodedData = BundleData.Compress(format, inputData);
					packets.Add(new BundlePacket(encodedData.Length, inputData.Length));
					packetBlocks.Add(encodedData);
				}
			}
		}

		readonly IStorageClient _blobStore;

		/// <summary>
		/// Constructor
		/// </summary>
		public TreeStore(IStorageClient blobStore)
		{
			_blobStore = blobStore;
		}

		/// <inheritdoc/>
		public ITreeWriter CreateTreeWriter(TreeOptions options, Utf8String prefix = default) => new BundleWriter(this, new BundleContext(), null, options, prefix);

		/// <inheritdoc/>
		public Task DeleteTreeAsync(RefName name, CancellationToken cancellationToken) => _blobStore.DeleteRefAsync(name, cancellationToken);

		/// <inheritdoc/>
		public Task<bool> HasTreeAsync(RefName name, CancellationToken cancellationToken) => _blobStore.HasRefAsync(name, cancellationToken: cancellationToken);

		/// <inheritdoc/>
		public async Task<ITreeBlob?> TryReadTreeAsync(RefName name, TimeSpan maxAge = default, CancellationToken cancellationToken = default)
		{
			// Read the blob referenced by this ref
			RefValue? refValue = await _blobStore.TryReadRefValueAsync(name, maxAge, cancellationToken);
			if (refValue == null)
			{
				return null;
			}

			// Create a new context for this bundle and its references
			BundleContext context = new BundleContext();

			// Add the new bundle to it
			BundleInfo bundleInfo = context.FindOrAddBundle(refValue.Target.Locator, refValue.Bundle.Header.Exports.Count);
			MountBundle(bundleInfo, refValue.Bundle.Header);

			// Return the last node from the bundle as the root
			return await GetNodeAsync(bundleInfo.Exports[^1]!, cancellationToken);
		}

		/// <summary>
		/// Gets the data for a given node
		/// </summary>
		/// <param name="node">The node to get the data for</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The node data</returns>
		async ValueTask<ITreeBlob> GetNodeAsync(NodeInfo node, CancellationToken cancellationToken)
		{
			if (node.State is InMemoryNodeState inMemoryState)
			{
				return inMemoryState;
			}

			ExportedNodeState exportedState = await GetExportedStateAsync(node, cancellationToken);

			ReadOnlyMemory<byte> packetData = await _blobStore.ReadBundlePacketAsync(exportedState.BundleInfo.Locator, exportedState.PacketInfo.Index, cancellationToken);
			ReadOnlyMemory<byte> nodeData = packetData.Slice(exportedState.Offset, exportedState.Length);

			return new InMemoryNodeState(new ReadOnlySequence<byte>(nodeData), exportedState.References);
		}

		async ValueTask<ExportedNodeState> GetExportedStateAsync(NodeInfo node, CancellationToken cancellationToken)
		{
			ExportedNodeState? exportedState = node.State as ExportedNodeState;
			if (exportedState == null)
			{
				ImportedNodeState importedState = (ImportedNodeState)node.State;
				await MountBundleAsync(importedState.BundleInfo, cancellationToken);
				exportedState = (ExportedNodeState)node.State;
			}
			return exportedState;
		}

		#region Reading bundles

		async Task MountBundleAsync(BundleInfo bundleInfo, CancellationToken cancellationToken)
		{
			if (!bundleInfo.Mounted)
			{
				Task mountTask;
				lock (bundleInfo)
				{
					Task prevMountTask = bundleInfo.MountTask.ContinueWith(x => { }, cancellationToken, TaskContinuationOptions.None, TaskScheduler.Default);
					mountTask = Task.Run(() => MountBundleInternalAsync(prevMountTask, bundleInfo, cancellationToken), cancellationToken);
					bundleInfo.MountTask = mountTask;
				}
				await mountTask;
			}
		}

		async Task MountBundleInternalAsync(Task prevMountTask, BundleInfo bundleInfo, CancellationToken cancellationToken)
		{
			// Wait for the previous mount task to complete. This may succeed or be cancelled.
			await prevMountTask;

			// If it didn't succeed, try again
			if (!bundleInfo.Mounted)
			{
				BundleHeader header = await _blobStore.ReadBundleHeaderAsync(bundleInfo.Locator, cancellationToken);
				MountBundle(bundleInfo, header);
			}
		}

		void MountBundle(BundleInfo bundleInfo, BundleHeader header)
		{
			// Create all the imported nodes
			List<NodeInfo> nodes = CreateImports(bundleInfo.Context, header);

			// Create the exported nodes, or update the state of any imported nodes to exported
			int exportIdx = 0;
			int packetOffset = 0;
			for (int packetIdx = 0; packetIdx < header.Packets.Count; packetIdx++)
			{
				BundlePacket packet = header.Packets[packetIdx];
				PacketInfo packetInfo = new PacketInfo(packetIdx, packetOffset, packet);

				int nodeOffset = 0;
				for (; exportIdx < header.Exports.Count && nodeOffset + header.Exports[exportIdx].Length <= packet.DecodedLength; exportIdx++)
				{
					BundleExport export = header.Exports[exportIdx];
					List<NodeInfo> nodeRefs = export.References.ConvertAll(x => nodes[x]);

					ExportedNodeState state = new ExportedNodeState(bundleInfo, exportIdx, packetInfo, nodeOffset, export.Length, nodeRefs);

					NodeInfo? node = bundleInfo.Exports[exportIdx];
					if (node == null)
					{
						node = bundleInfo.Exports[exportIdx] = new NodeInfo(this, export.Hash, state);
					}
					else
					{
						node.SetState(state);
					}
					nodes.Add(node);

					nodeOffset += export.Length;
				}

				packetOffset += packet.EncodedLength;
			}

			// Mark the bundle as mounted
			bundleInfo.Mounted = true;
		}

		List<NodeInfo> CreateImports(BundleContext context, BundleHeader header)
		{
			List<NodeInfo> indexedNodes = new List<NodeInfo>(header.Imports.Sum(x => x.Exports.Count) + header.Exports.Count);
			foreach (BundleImport import in header.Imports)
			{
				BundleInfo importBundle = context.FindOrAddBundle(import.Locator, import.ExportCount);
				foreach ((int exportIdx, IoHash exportHash) in import.Exports)
				{
					NodeInfo? node = importBundle.Exports[exportIdx];
					if (node == null)
					{
						node = new NodeInfo(this, exportHash, new ImportedNodeState(importBundle, exportIdx));
						importBundle.Exports[exportIdx] = node;
					}
					indexedNodes.Add(node);
				}
			}
			return indexedNodes;
		}

		#endregion

		#region Writing bundles

		static IoHash ComputeHash(ReadOnlySequence<byte> data, IReadOnlyList<NodeInfo> references)
		{
			byte[] buffer = new byte[IoHash.NumBytes * (references.Count + 1)];

			Span<byte> span = buffer;
			for (int idx = 0; idx < references.Count; idx++)
			{
				references[idx].Hash.CopyTo(span);
				span = span[IoHash.NumBytes..];
			}
			IoHash.Compute(data).CopyTo(span);

			return IoHash.Compute(buffer);
		}

		#endregion
	}
}
