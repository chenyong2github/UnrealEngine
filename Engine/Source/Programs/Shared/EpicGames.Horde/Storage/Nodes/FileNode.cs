// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage.Nodes;
using EpicGames.Serialization;
using Microsoft.Extensions.Options;
using System;
using System.Buffers;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Metadata;
using System.Threading;
using System.Threading.Tasks;
using System.Xml.Linq;

namespace EpicGames.Horde.Storage.Nodes
{
	/// <summary>
	/// Options for creating file nodes
	/// </summary>
	public class ChunkingOptions
	{
		/// <summary>
		/// Options for creating leaf nodes
		/// </summary>
		public ChunkingOptionsForNodeType LeafOptions { get; set; }

		/// <summary>
		/// Options for creating interior nodes
		/// </summary>
		public ChunkingOptionsForNodeType InteriorOptions { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public ChunkingOptions()
		{
			LeafOptions = new ChunkingOptionsForNodeType(32 * 1024, 256 * 1024, 64 * 1024);
			InteriorOptions = new ChunkingOptionsForNodeType(32 * 1024, 256 * 1024, 64 * 1024);
		}
	}

	/// <summary>
	/// Options for creating a specific type of file nodes
	/// </summary>
	public class ChunkingOptionsForNodeType
	{
		/// <summary>
		/// Minimum chunk size
		/// </summary>
		public int MinSize { get; set; }

		/// <summary>
		/// Maximum chunk size. Chunks will be split on this boundary if another match is not found.
		/// </summary>
		public int MaxSize { get; set; }

		/// <summary>
		/// Target chunk size for content-slicing
		/// </summary>
		public int TargetSize { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="size">Fixed size chunks to use</param>
		public ChunkingOptionsForNodeType(int size)
			: this(size, size, size)
		{
		}

		/// <summary>
		/// Default constructor
		/// </summary>
		public ChunkingOptionsForNodeType(int minSize, int maxSize, int targetSize)
		{
			MinSize = minSize;
			MaxSize = maxSize;
			TargetSize = targetSize;
		}
	}

	/// <summary>
	/// Representation of a data stream, split into chunks along content-aware boundaries using a rolling hash (<see cref="BuzHash"/>).
	/// Chunks are pushed into a tree hierarchy as data is appended to the root, with nodes of the tree also split along content-aware boundaries with <see cref="IoHash.NumBytes"/> granularity.
	/// Once a chunk has been written to storage, it is treated as immutable.
	/// </summary>
	public abstract class FileNode : TreeNode
	{
		/// <summary>
		/// Copies the contents of this node and its children to the given output stream
		/// </summary>
		/// <param name="reader">Reader for nodes in the tree</param>
		/// <param name="outputStream">The output stream to receive the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public abstract Task CopyToStreamAsync(TreeReader reader, Stream outputStream, CancellationToken cancellationToken);

		/// <summary>
		/// Extracts the contents of this node to a file
		/// </summary>
		/// <param name="reader">Reader for nodes in the tree</param>
		/// <param name="file">File to write with the contents of this node</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async Task CopyToFileAsync(TreeReader reader, FileInfo file, CancellationToken cancellationToken)
		{
			if(file.Exists && (file.Attributes & FileAttributes.ReadOnly) != 0)
			{
				file.Attributes &= ~FileAttributes.ReadOnly;
			}
			using (FileStream stream = file.Open(FileMode.Create, FileAccess.Write, FileShare.Read))
			{
				await CopyToStreamAsync(reader, stream, cancellationToken);
			}
		}

		/// <summary>
		/// Serialize this node and its children into a byte array
		/// </summary>
		/// <param name="reader">Reader for nodes in the tree</param>
		/// <param name="cancellationToken"></param>
		/// <returns>Array of data stored by the tree</returns>
		public async Task<byte[]> ToByteArrayAsync(TreeReader reader, CancellationToken cancellationToken)
		{
			using (MemoryStream stream = new MemoryStream())
			{
				await CopyToStreamAsync(reader, stream, cancellationToken);
				return stream.ToArray();
			}
		}
	}

	/// <summary>
	/// File node that contains a chunk of data
	/// </summary>
	[TreeNode("{B27AFB68-9E20-4A4B-A4D8-788A4098D439}", 1)]
	public sealed class LeafFileNode : FileNode
	{
		/// <summary>
		/// Data for this node
		/// </summary>
		public ReadOnlyMemory<byte> Data { get; }

		/// <summary>
		/// Create an empty leaf node
		/// </summary>
		public LeafFileNode()
		{
		}

		/// <summary>
		/// Create a leaf node from the given serialized data
		/// </summary>
		public LeafFileNode(ITreeNodeReader reader)
		{
			Data = reader.ReadFixedLengthBytes(reader.Length);
		}

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			writer.WriteFixedLengthBytes(Data.Span);
		}

		/// <inheritdoc/>
		public override IEnumerable<TreeNodeRef> EnumerateRefs() => Enumerable.Empty<TreeNodeRef>();

		/// <summary>
		/// Determines how much data to append to an existing leaf node
		/// </summary>
		/// <param name="currentData">Current data in the leaf node</param>
		/// <param name="appendData">Data to be appended</param>
		/// <param name="rollingHash">Current BuzHash of the data</param>
		/// <param name="options">Options for chunking the data</param>
		/// <returns>The number of bytes to append</returns>
		internal static int AppendData(ReadOnlySpan<byte> currentData, ReadOnlySpan<byte> appendData, ref uint rollingHash, ChunkingOptionsForNodeType options)
		{
			// If the target option sizes are fixed, just chunk the data along fixed boundaries
			if (options.MinSize == options.TargetSize && options.MaxSize == options.TargetSize)
			{
				return Math.Min(appendData.Length, options.MaxSize - (int)currentData.Length);
			}

			// Cap the append data span to the maximum amount we can add
			int maxAppendLength = options.MaxSize - currentData.Length;
			if (maxAppendLength < appendData.Length)
			{
				appendData = appendData.Slice(0, maxAppendLength);
			}

			// Length of the data to be appended
			int appendLength = 0;

			// Fast path for appending data to the buffer up to the chunk window size
			int windowSize = options.MinSize;
			if (currentData.Length < windowSize)
			{
				appendLength = Math.Min(windowSize - (int)currentData.Length, appendData.Length);
				rollingHash = BuzHash.Add(rollingHash, appendData.Slice(0, appendLength));
			}

			// Get the threshold for the rolling hash
			uint rollingHashThreshold = (uint)((1L << 32) / options.TargetSize);

			// Step through the part of the data where the tail of the window is in currentData, and the head of the window is in appendData.
			if(appendLength < appendData.Length && windowSize > appendLength)
			{
				int overlap = windowSize - appendLength;
				int overlapLength = Math.Min(appendData.Length - appendLength, overlap);

				ReadOnlySpan<byte> tailSpan = currentData.Slice(currentData.Length - overlap, overlapLength);
				ReadOnlySpan<byte> headSpan = appendData.Slice(appendLength, overlapLength);

				int count = BuzHash.Update(tailSpan, headSpan, rollingHashThreshold, ref rollingHash);
				if (count != -1)
				{
					appendLength += count;
					return appendLength;
				}

				appendLength += headSpan.Length;
			}

			// Step through the rest of the data which is completely contained in appendData.
			if (appendLength < appendData.Length)
			{
				Debug.Assert(appendLength >= windowSize);
					
				ReadOnlySpan<byte> tailSpan = appendData.Slice(appendLength - windowSize, appendData.Length - windowSize);
				ReadOnlySpan<byte> headSpan = appendData.Slice(appendLength);

				int count = BuzHash.Update(tailSpan, headSpan, rollingHashThreshold, ref rollingHash);
				if (count != -1)
				{
					appendLength += count;
					return appendLength;
				}

				appendLength += headSpan.Length;
			}

			return appendLength;
		}

		/// <inheritdoc/>
		public override async Task CopyToStreamAsync(TreeReader reader, Stream outputStream, CancellationToken cancellationToken)
		{
			await outputStream.WriteAsync(Data, cancellationToken);
		}
	}

	/// <summary>
	/// An interior file node
	/// </summary>
	[TreeNode("{F4DEDDBC-70CB-4C7A-8347-F011AFCCCDB9}", 1)]
	public class InteriorFileNode : FileNode
	{
		/// <summary>
		/// Child nodes
		/// </summary>
		public IReadOnlyList<TreeNodeRef<FileNode>> Children { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="children"></param>
		public InteriorFileNode(IReadOnlyList<TreeNodeRef<FileNode>> children)
		{
			Children = children;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public InteriorFileNode(ITreeNodeReader reader)
		{
			TreeNodeRef<FileNode>[] children = new TreeNodeRef<FileNode>[reader.Length / IoHash.NumBytes];
			for (int idx = 0; idx < children.Length; idx++)
			{
				children[idx] = reader.ReadRef<FileNode>();
			}
			Children = children;
		}

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			foreach (TreeNodeRef<FileNode> child in Children)
			{
				writer.WriteRef(child);
			}
		}

		/// <inheritdoc/>
		public override IEnumerable<TreeNodeRef> EnumerateRefs() => Children;

		/// <summary>
		/// Test whether the current node is complete
		/// </summary>
		/// <param name="currentData"></param>
		/// <param name="rollingHash"></param>
		/// <param name="options"></param>
		/// <returns></returns>
		internal static bool IsComplete(ReadOnlySpan<byte> currentData, uint rollingHash, ChunkingOptionsForNodeType options)
		{
			if (currentData.Length + IoHash.NumBytes > options.MaxSize)
			{
				return true;
			}

			if (currentData.Length >= options.MinSize)
			{
				uint rollingHashThreshold = BuzHash.GetThreshold(options.TargetSize);
				if (rollingHash < rollingHashThreshold)
				{
					return true;
				}
			}

			return false;
		}

		/// <summary>
		/// Append a new hash to this interior node
		/// </summary>
		/// <param name="currentData">Current data for the node</param>
		/// <param name="hash">Hash of the child node</param>
		/// <param name="rollingHash">Current rolling hash for the node</param>
		/// <param name="options">Options for chunking the node</param>
		/// <returns>True if the hash could be appended, false otherwise</returns>
		internal static void AppendData(ReadOnlySpan<byte> currentData, IoHash hash, ref uint rollingHash, ChunkingOptionsForNodeType options)
		{
			Span<byte> hashData = stackalloc byte[IoHash.NumBytes];
			hash.CopyTo(hashData);

			rollingHash = BuzHash.Add(rollingHash, hashData);

			int windowSize = options.MinSize - (options.MinSize % IoHash.NumBytes);
			if (currentData.Length > windowSize)
			{
				ReadOnlySpan<byte> removeData = currentData.Slice(currentData.Length - windowSize, IoHash.NumBytes);
				rollingHash = BuzHash.Sub(rollingHash, removeData, windowSize + IoHash.NumBytes);
			}
		}

		/// <inheritdoc/>
		public override async Task CopyToStreamAsync(TreeReader reader, Stream outputStream, CancellationToken cancellationToken)
		{
			foreach (TreeNodeRef<FileNode> childNodeRef in Children)
			{
				FileNode childNode = await childNodeRef.ExpandAsync(reader, cancellationToken);
				await childNode.CopyToStreamAsync(reader, outputStream, cancellationToken);
			}
		}
	}
}
