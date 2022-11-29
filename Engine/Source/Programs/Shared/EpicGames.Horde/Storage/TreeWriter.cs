// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Horde.Compute.Impl;

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
	}

	/// <summary>
	/// Writes nodes of a tree to an <see cref="IStorageClient"/>, packed into bundles.
	/// </summary>
	public class TreeWriter
	{
		class NodeInfo
		{
			public readonly BundleType Type;
			public readonly IoHash Hash;
			public readonly int Length;
			public readonly IReadOnlyList<IoHash> RefHashes;
			public readonly TreeNodeRef NodeRef;
			public readonly uint Revision;

			public NodeInfo(BundleType type, IoHash hash, int length, IReadOnlyList<IoHash> refs, TreeNodeRef nodeRef)
			{
				Type = type;
				Hash = hash;
				Length = length;
				RefHashes = refs;
				NodeRef = nodeRef;
				Revision = nodeRef.Target!.Revision;
			}
		}

		class NodeWriter : ITreeNodeWriter
		{
			public ByteArrayBuilder Data = new ByteArrayBuilder();

			readonly IReadOnlyDictionary<TreeNodeRef, IoHash> _refToHash;

			public int Length => Data.Length;

			public NodeWriter(IReadOnlyDictionary<TreeNodeRef, IoHash> refToHash)
			{
				_refToHash = refToHash;
			}

			public void WriteRef(TreeNodeRef target)
			{
				IoHash hash;
				if (!_refToHash.TryGetValue(target, out hash))
				{
					throw new InvalidOperationException($"Cannot serialize value; was not returned by owner's {nameof(target.Target.EnumerateRefs)} method.");
				}
				Data.WriteIoHash(hash);
			}

			public Memory<byte> GetMemory(int minSize = 1) => Data.GetMemory(minSize);

			public void Advance(int length) => Data.Advance(length);
		}

		static readonly TreeOptions s_defaultOptions = new TreeOptions();

		readonly IStorageClient _store;
		readonly TreeOptions _options;
		readonly Utf8String _prefix;

		// Writer used to create packets
		readonly ArrayMemoryWriter _packetWriter;

		// Map of hashes to existing nodes. Empty/invalid locators are used for items that are queued for writing, but not available yet.
		readonly Dictionary<IoHash, NodeLocator> _hashToNode = new Dictionary<IoHash, NodeLocator>(); // TODO: this needs to include some additional state from external sources.

		// Queue of nodes for the current bundle
		readonly List<NodeInfo> _queue = new List<NodeInfo>();

		// Queue of nodes that are already scheduled to be written elsewhere
		readonly List<NodeInfo> _secondaryQueue = new List<NodeInfo>();

		// List of packets in the current bundle
		readonly List<BundlePacket> _packets = new List<BundlePacket>();

		// List of compressed packets
		readonly List<ReadOnlyMemory<byte>> _encodedPackets = new List<ReadOnlyMemory<byte>>();

		// Total size of compressed packets in the current bundle
		int _currentBundleLength;

		/// <summary>
		/// Accessor for the store backing this writer
		/// </summary>
		public IStorageClient Store => _store;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="store">Store to write data to</param>
		/// <param name="options">Options for the writer</param>
		/// <param name="prefix">Prefix for blobs written to the store</param>
		public TreeWriter(IStorageClient store, TreeOptions? options = null, Utf8String prefix = default)
		{
			_store = store;
			_options = options ?? s_defaultOptions;
			_prefix = prefix;
			_packetWriter = new ArrayMemoryWriter(_options.MinCompressionPacketSize);
		}

		/// <summary>
		/// Copies settings from another tree writer
		/// </summary>
		/// <param name="other">Other instance to copy from</param>
		public TreeWriter(TreeWriter other)
			: this(other._store, other._options, other._prefix)
		{
		}

		/// <summary>
		/// Attempt to get the locator for the node with a given hash
		/// </summary>
		/// <param name="hash">Hash of the node</param>
		/// <returns>True if the locator was found</returns>
		public NodeLocator GetLocator(IoHash hash) => _hashToNode[hash];

		/// <summary>
		/// Attempt to get the locator for the node with a given hash
		/// </summary>
		/// <param name="hash">Hash of the node</param>
		/// <param name="locator">Receives the locator for the node</param>
		/// <returns>True if the locator was found</returns>
		public bool TryGetLocator(IoHash hash, out NodeLocator locator) => _hashToNode.TryGetValue(hash, out locator);

		/// <inheritdoc/>
		public async Task<IoHash> WriteAsync(TreeNodeRef nodeRef, CancellationToken cancellationToken = default)
		{
			// Check we actually have a target node. If we don't, we don't need to write anything.
			TreeNode? node = nodeRef.Target;
			if (node == null)
			{
				Debug.Assert(nodeRef.Hash != IoHash.Zero);
				_hashToNode.TryAdd(nodeRef.Hash, nodeRef.Locator);
				return nodeRef.Hash;
			}

			// Write all the nodes it references, and mark the ref as dirty if any of them change.
			Dictionary<TreeNodeRef, IoHash> nextRefToHash = new Dictionary<TreeNodeRef, IoHash>();
			foreach (TreeNodeRef nextRef in node.EnumerateRefs())
			{
				IoHash prevHash = nextRef.Hash;
				IoHash nextHash = await WriteAsync(nextRef, cancellationToken);
				Debug.Assert(nextHash != IoHash.Zero);

				nextRefToHash.Add(nextRef, nextHash);

				if (prevHash != nextHash)
				{
					nodeRef.MarkAsDirty();
				}
			}

			// If the target node hasn't been modified, use the existing serialized state.
			if (!nodeRef.IsDirty())
			{
				// Make sure the locator is valid. The node may be queued for writing but not flushed to disk yet.
				if (nodeRef.Locator.IsValid())
				{
					_hashToNode.TryAdd(nodeRef.Hash, nodeRef.Locator);
					nodeRef.Collapse();
				}
				return nodeRef.Hash;
			}

			// Serialize the node
			NodeWriter nodeWriter = new NodeWriter(nextRefToHash);
			nodeRef.Target!.Serialize(nodeWriter);
			BundleType bundleType = nodeRef.Target.GetBundleType();

			// Get the hash for the new blob
			ReadOnlySequence<byte> data = nodeWriter.Data.AsSequence();
			IoHash hash = IoHash.Compute(data);
			nodeRef.MarkAsPendingWrite(hash);

			// Check if we're already tracking a node with the same hash
			NodeInfo nodeInfo = new NodeInfo(bundleType, hash, (int)data.Length, nextRefToHash.Values.Distinct().ToArray(), nodeRef);
			if (_hashToNode.TryGetValue(hash, out NodeLocator locator))
			{
				// If the node is in the lookup but not currently valid, it's already queued for writing. Add this ref to the list of refs that needs fixing up,
				// so we can update it after flushing.
				if (locator.IsValid())
				{
					nodeRef.MarkAsWritten(hash, locator, nodeInfo.Revision);
				}
				else
				{
					_secondaryQueue.Add(nodeInfo);
				}
			}
			else
			{
				// Write the node if we don't already have it
				_packetWriter.WriteFixedLengthBytes(data);

				_queue.Add(nodeInfo);
				_hashToNode.Add(hash, default);

				if (_packetWriter.Length > Math.Min(_options.MinCompressionPacketSize, _options.MaxBlobSize))
				{
					FlushPacket();
				}

				if (_currentBundleLength > _options.MaxBlobSize)
				{
					await FlushAsync(cancellationToken);
				}
			}

			return hash;
		}

		/// <summary>
		/// Writes a node to the given ref
		/// </summary>
		/// <param name="name">Name of the ref to write</param>
		/// <param name="node"></param>
		/// <param name="options"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public async Task<NodeLocator> WriteRefAsync(RefName name, TreeNode node, RefOptions? options = null, CancellationToken cancellationToken = default)
		{
			TreeNodeRef nodeRef = new TreeNodeRef(node);
			await WriteAsync(nodeRef, cancellationToken);
			await FlushAsync(cancellationToken);

			Debug.Assert(nodeRef.Locator.IsValid());
			await _store.WriteRefTargetAsync(name, new RefTarget(nodeRef.Hash, nodeRef.Locator), options, cancellationToken);
			return nodeRef.Locator;
		}

		/// <summary>
		/// Writes a ref to the node with the given hash
		/// </summary>
		/// <param name="name">Name of the ref to write</param>
		/// <param name="root">Hash of the root node</param>
		/// <param name="options"></param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public async Task<NodeLocator> WriteAsync(RefName name, IoHash root, RefOptions? options = null, CancellationToken cancellationToken = default)
		{
			await FlushAsync(cancellationToken);
			NodeLocator locator = GetLocator(root);
			await _store.WriteRefTargetAsync(name, new RefTarget(root, locator), options, cancellationToken);
			return locator;
		}

		void FlushPacket()
		{
			if (_packetWriter.Length > 0)
			{
				ReadOnlyMemory<byte> encodedData = BundleData.Compress(_options.CompressionFormat, _packetWriter.WrittenMemory);
				_encodedPackets.Add(encodedData);

				BundlePacket packet = new BundlePacket(encodedData.Length, _packetWriter.Length);
				_packets.Add(packet);

				_packetWriter.Clear();
				_currentBundleLength += encodedData.Length;
			}
		}

		/// <summary>
		/// Flushes all the current nodes to storage
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async Task FlushAsync(CancellationToken cancellationToken)
		{
			FlushPacket();

			if (_queue.Count > 0)
			{
				// Create a set from the nodes to be written. We use this to determine references that are imported.
				HashSet<IoHash> nodeSet = new HashSet<IoHash>(_queue.Select(x => x.Hash));

				// Find all the imported nodes by bundle
				Dictionary<BlobLocator, List<(int, IoHash)>> bundleToImportedNodes = new Dictionary<BlobLocator, List<(int, IoHash)>>();
				foreach (NodeInfo nodeInfo in _queue)
				{
					foreach (IoHash refHash in nodeInfo.RefHashes)
					{
						if (nodeSet.Add(refHash))
						{
							NodeLocator refLocator = _hashToNode[refHash];

							List<(int, IoHash)>? importedNodes;
							if (!bundleToImportedNodes.TryGetValue(refLocator.Blob, out importedNodes))
							{
								importedNodes = new List<(int, IoHash)>();
								bundleToImportedNodes.Add(refLocator.Blob, importedNodes);
							}

							importedNodes.Add((refLocator.ExportIdx, refHash));
						}
					}
				}

				// Map from node hash to index, with imported nodes first, ordered by blob, and exported nodes second.
				Dictionary<IoHash, int> nodeToIndex = new Dictionary<IoHash, int>();

				// Add all the imports and assign them identifiers
				List<BundleImport> imports = new List<BundleImport>();
				foreach ((BlobLocator blobLocator, List<(int, IoHash)> importedNodes) in bundleToImportedNodes)
				{
					importedNodes.SortBy(x => x.Item1);

					foreach ((_, IoHash hash) in importedNodes)
					{
						nodeToIndex[hash] = nodeToIndex.Count;
					}

					imports.Add(new BundleImport(blobLocator, importedNodes));
				}

				// List of types in the bundle
				List<BundleType> types = new List<BundleType>();
				Dictionary<BundleType, int> typeToIndex = new Dictionary<BundleType, int>();

				// Create the export list
				List<BundleExport> exports = new List<BundleExport>(_queue.Count);
				foreach (NodeInfo nodeInfo in _queue)
				{
					int typeIdx;
					if (!typeToIndex.TryGetValue(nodeInfo.Type, out typeIdx))
					{
						typeIdx = types.Count;
						typeToIndex.Add(nodeInfo.Type, typeIdx);
						types.Add(nodeInfo.Type);
					}

					int[] references = nodeInfo.RefHashes.Select(x => nodeToIndex[x]).ToArray();
					BundleExport export = new BundleExport(typeIdx, nodeInfo.Hash, nodeInfo.Length, references);
					nodeToIndex.Add(nodeInfo.Hash, nodeToIndex.Count);
					exports.Add(export);
				}

				// Create the bundle
				BundleHeader header = new BundleHeader(_options.CompressionFormat, types, imports, exports, _packets.ToArray());
				Bundle bundle = new Bundle(header, _encodedPackets.ToArray());

				// Write the bundle
				BlobLocator locator = await _store.WriteBundleAsync(bundle, _prefix, cancellationToken);
				for (int idx = 0; idx < _queue.Count; idx++)
				{
					NodeLocator nodeLocator = new NodeLocator(locator, idx);

					NodeInfo nodeInfo = _queue[idx];
					nodeInfo.NodeRef.MarkAsWritten(nodeInfo.Hash, nodeLocator, nodeInfo.Revision);

					_hashToNode[nodeInfo.Hash] = nodeLocator;
				}

				// Update any refs with their target locator
				int refIdx = 0;
				for (; refIdx < _secondaryQueue.Count; refIdx++)
				{
					NodeInfo nodeInfo = _secondaryQueue[refIdx];
					if (_hashToNode.TryGetValue(nodeInfo.Hash, out NodeLocator refLocator))
					{
						nodeInfo.NodeRef.MarkAsWritten(nodeInfo.Hash, refLocator, nodeInfo.Revision);
					}
				}
				_secondaryQueue.RemoveRange(0, refIdx);

				// Reset the output state
				_packets.Clear();
				_encodedPackets.Clear();
				_queue.Clear();
				_currentBundleLength = 0;
			}
		}
	}
}
