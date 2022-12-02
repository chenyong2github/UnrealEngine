// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

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
		// Information about a TreeNodeRef that was written, and needs to be fixed up after flushing
		class NodeRefInfo
		{
			public readonly TreeNodeRef NodeRef;
			public readonly uint Revision;
			public NodeRefInfo? Next;

			public NodeRefInfo(TreeNodeRef nodeRef, NodeRefInfo? next)
			{
				NodeRef = nodeRef;
				Revision = nodeRef.Target!.Revision;
				Next = next;
			}
		}

		// Unique identifier for an output node
		record OutputNodeKey(IoHash Hash, BundleType Type);

		// Information about a unique output node. Note that multiple node refs may de-duplicate to the same output node.
		class OutputNodeInfo : NodeRefInfo
		{
			public readonly OutputNodeKey Key;
			public readonly int Length;
			public readonly IReadOnlyList<TreeNodeRef> Refs;

			public OutputNodeInfo(OutputNodeKey key, int length, IReadOnlyList<TreeNodeRef> refs, TreeNodeRef nodeRef)
				: base(nodeRef, null)
			{
				Key = key;
				Length = length;
				Refs = refs;
			}

			public void Add(TreeNodeRef nodeRef) => Next = new NodeRefInfo(nodeRef, Next);
		}

		class NodeWriter : ITreeNodeWriter
		{
			public ByteArrayBuilder Data = new ByteArrayBuilder();

			readonly IReadOnlyDictionary<TreeNodeRef, int> _refToIndex;

			public int Length => Data.Length;

			public NodeWriter(IReadOnlyList<TreeNodeRef> refs)
			{
				_refToIndex = new Dictionary<TreeNodeRef, int>(refs.Distinct().Select((x, i) => KeyValuePair.Create(x, i)));
			}

			public void WriteRef(TreeNodeRef target)
			{
				if (!_refToIndex.TryGetValue(target, out int index))
				{
					throw new InvalidOperationException($"Cannot serialize value; was not returned by owner's {nameof(target.Target.EnumerateRefs)} method.");
				}
				Data.WriteUnsignedVarInt(index);
				target.SerializeInternal(this);
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

		// Map of keys to nodes in the queue
		readonly Dictionary<OutputNodeKey, OutputNodeInfo> _nodeKeyToInfo = new Dictionary<OutputNodeKey, OutputNodeInfo>();

		// Map of hashes to existing nodes. Empty/invalid locators are used for items that are queued for writing, but not available yet.
		readonly Dictionary<OutputNodeKey, NodeLocator> _nodeKeyToLocator = new Dictionary<OutputNodeKey, NodeLocator>(); // TODO: this needs to include some additional state from external sources.

		// Queue of nodes for the current bundle
		readonly List<OutputNodeInfo> _queue = new List<OutputNodeInfo>();

		// Map of node refs to their serialized state
		readonly HashSet<(TreeNodeRef, uint)> _queuedRefs = new HashSet<(TreeNodeRef, uint)>();

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
		/// Constructor
		/// </summary>
		/// <param name="store">Store to write data to</param>
		/// <param name="refName">Ref being written. Will be used as a prefix for storing blobs.</param>
		/// <param name="options">Options for the writer</param>
		public TreeWriter(IStorageClient store, RefName refName, TreeOptions? options = null)
			: this(store, options, refName.Text)
		{
		}

		/// <summary>
		/// Copies settings from another tree writer
		/// </summary>
		/// <param name="other">Other instance to copy from</param>
		public TreeWriter(TreeWriter other)
			: this(other._store, other._options, other._prefix)
		{
		}

		/// <inheritdoc/>
		public async Task<bool> WriteAsync(TreeNodeRef nodeRef, CancellationToken cancellationToken = default)
		{
			// Check we actually have a target node. If we don't, we don't need to write anything.
			TreeNode? target = nodeRef.Target;
			if (target == null)
			{
				Debug.Assert(nodeRef.Locator.IsValid());
				return false;
			}

			// Early-out if this ref is already queued for writing.
			uint revision = target.Revision;
			if (!_queuedRefs.Add((nodeRef, revision)))
			{
				return true;
			}

			// Write all the nodes it references, and mark the ref as dirty if any of them change.
			List<TreeNodeRef> nextRefs = target.EnumerateRefs().ToList();
			foreach (TreeNodeRef nextRef in nextRefs)
			{
				if (await WriteAsync(nextRef, cancellationToken))
				{
					nodeRef.MarkAsDirty();
				}
			}

			// If the target node hasn't been modified, use the existing serialized state.
			if (!nodeRef.IsDirty())
			{
				// Make sure the locator is valid. The node may be queued for writing but not flushed to disk yet.
				Debug.Assert(nodeRef.Locator.IsValid());
				nodeRef.Collapse();
				return false;
			}

			// Serialize the node
			NodeWriter nodeWriter = new NodeWriter(nextRefs);
			target.Serialize(nodeWriter);

			// Get the hash for the new blob
			ReadOnlySequence<byte> data = nodeWriter.Data.AsSequence();
			target.Hash = IoHash.Compute(data);
			nodeRef.MarkAsPendingWrite();

			OutputNodeKey nodeKey = new OutputNodeKey(target.Hash, target.GetBundleType());

			// Check if we're already tracking a node with the same hash
			OutputNodeInfo? nodeInfo;
			if (_nodeKeyToInfo.TryGetValue(nodeKey, out nodeInfo))
			{
				// This node is already queued for writing to storage
				nodeInfo.Add(nodeRef);
			}
			else if (_nodeKeyToLocator.TryGetValue(nodeKey, out NodeLocator locator))
			{
				// This node has already been written to storage
				nodeRef.MarkAsWritten(locator, nodeRef.Target!.Revision);
			}
			else
			{
				// Write the node if we don't already have it
				_packetWriter.WriteFixedLengthBytes(data);

				nodeInfo = new OutputNodeInfo(nodeKey, (int)data.Length, nextRefs, nodeRef);
				_queue.Add(nodeInfo);
				_nodeKeyToInfo.Add(nodeKey, nodeInfo);

				if (_packetWriter.Length > Math.Min(_options.MinCompressionPacketSize, _options.MaxBlobSize))
				{
					FlushPacket();
				}

				if (_currentBundleLength > _options.MaxBlobSize)
				{
					await FlushAsync(cancellationToken);
				}
			}

			return true;
		}

		/// <summary>
		/// Writes a node to the given ref
		/// </summary>
		/// <param name="name">Name of the ref to write</param>
		/// <param name="node"></param>
		/// <param name="options"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public async Task<NodeLocator> WriteAsync(RefName name, TreeNode node, RefOptions? options = null, CancellationToken cancellationToken = default)
		{
			TreeNodeRef nodeRef = new TreeNodeRef(node);
			await WriteAsync(nodeRef, cancellationToken);
			await FlushAsync(cancellationToken);

			Debug.Assert(nodeRef.Locator.IsValid());
			await _store.WriteRefTargetAsync(name, nodeRef.Locator, options, cancellationToken);
			return nodeRef.Locator;
		}

		/// <summary>
		/// Compresses the current packet and schedule it to be written to storage
		/// </summary>
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
		/// <param name="root">Root for the tree</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async Task<NodeLocator> FlushAsync(TreeNode root, CancellationToken cancellationToken)
		{
			TreeNodeRef rootRef = new TreeNodeRef(root);
			await WriteAsync(rootRef, cancellationToken);
			await FlushAsync(cancellationToken);
			return rootRef.Locator;
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
				HashSet<TreeNodeRef> nodeSet = new HashSet<TreeNodeRef>();
				foreach (OutputNodeInfo nodeInfo in _queue)
				{
					for (NodeRefInfo? nodeRefInfo = nodeInfo; nodeRefInfo != null; nodeRefInfo = nodeRefInfo.Next)
					{
						nodeSet.Add(nodeRefInfo.NodeRef);
					}
				}
				
				// Find all the imported nodes by bundle
				Dictionary<BlobLocator, List<(int, TreeNodeRef)>> bundleToImports = new Dictionary<BlobLocator, List<(int, TreeNodeRef)>>();
				foreach (OutputNodeInfo nodeInfo in _queue)
				{
					foreach (TreeNodeRef refKey in nodeInfo.Refs)
					{
						if (nodeSet.Add(refKey))
						{
							Debug.Assert(refKey.Locator.IsValid());
							NodeLocator refLocator = refKey.Locator;

							List<(int, TreeNodeRef)>? importedNodes;
							if (!bundleToImports.TryGetValue(refLocator.Blob, out importedNodes))
							{
								importedNodes = new List<(int, TreeNodeRef)>();
								bundleToImports.Add(refLocator.Blob, importedNodes);
							}

							importedNodes.Add((refLocator.ExportIdx, refKey));
						}
					}
				}

				// Map from node hash to index, with imported nodes first, ordered by blob, and exported nodes second.
				int nodeIdx = 0;
				Dictionary<TreeNodeRef, int> nodeToIndex = new Dictionary<TreeNodeRef, int>();

				// Add all the imports and assign them identifiers
				List<BundleImport> imports = new List<BundleImport>();
				foreach ((BlobLocator blobLocator, List<(int, TreeNodeRef)> importEntries) in bundleToImports)
				{
					importEntries.SortBy(x => x.Item1);

					int[] entries = new int[importEntries.Count];
					for (int idx = 0; idx < importEntries.Count; idx++)
					{
						TreeNodeRef key = importEntries[idx].Item2;
						nodeToIndex.Add(key, nodeIdx++);
						entries[idx] = importEntries[idx].Item1;
					}

					imports.Add(new BundleImport(blobLocator, entries));
				}

				// List of types in the bundle
				List<BundleType> types = new List<BundleType>();
				Dictionary<BundleType, int> typeToIndex = new Dictionary<BundleType, int>();

				// Create the export list
				List<BundleExport> exports = new List<BundleExport>(_queue.Count);
				foreach (OutputNodeInfo nodeInfo in _queue)
				{
					int typeIdx = FindOrAddType(types, typeToIndex, nodeInfo.Key.Type);
					int[] references = nodeInfo.Refs.Select(x => nodeToIndex[x]).ToArray();
					BundleExport export = new BundleExport(typeIdx, nodeInfo.Key.Hash, nodeInfo.Length, references);
					for (NodeRefInfo? nodeRefInfo = nodeInfo; nodeRefInfo != null; nodeRefInfo = nodeRefInfo.Next)
					{
						nodeToIndex[nodeRefInfo.NodeRef] = nodeIdx;
					}
					exports.Add(export);
					nodeIdx++;
				}

				// Create the bundle
				BundleHeader header = new BundleHeader(_options.CompressionFormat, types, imports, exports, _packets.ToArray());
				Bundle bundle = new Bundle(header, _encodedPackets.ToArray());

				// Write the bundle
				BlobLocator locator = await _store.WriteBundleAsync(bundle, _prefix, cancellationToken);
				for (int idx = 0; idx < _queue.Count; idx++)
				{
					NodeLocator nodeLocator = new NodeLocator(locator, idx);

					OutputNodeInfo nodeInfo = _queue[idx];
					for (NodeRefInfo? nodeRefInfo = nodeInfo; nodeRefInfo != null; nodeRefInfo = nodeRefInfo.Next)
					{
						nodeRefInfo.NodeRef.MarkAsWritten(nodeLocator, nodeRefInfo.Revision);
					}

					_nodeKeyToLocator[nodeInfo.Key] = nodeLocator;
				}

				// Reset the output state
				_packets.Clear();
				_encodedPackets.Clear();
				_queue.Clear();
				_queuedRefs.Clear();
				_nodeKeyToInfo.Clear();
				_currentBundleLength = 0;
			}
		}

		static int FindOrAddType(List<BundleType> types, Dictionary<BundleType, int> typeToIndex, BundleType type)
		{
			int typeIdx;
			if (!typeToIndex.TryGetValue(type, out typeIdx))
			{
				typeIdx = types.Count;
				typeToIndex.Add(type, typeIdx);
				types.Add(type);
			}
			return typeIdx;
		}
	}
}
