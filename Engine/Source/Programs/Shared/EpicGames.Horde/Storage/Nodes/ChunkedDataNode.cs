// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Storage.Nodes
{
	/// <summary>
	/// Representation of a data stream, split into chunks along content-aware boundaries using a rolling hash (<see cref="BuzHash"/>).
	/// Chunks are pushed into a tree hierarchy as data is appended to the root, with nodes of the tree also split along content-aware boundaries with <see cref="IoHash.NumBytes"/> granularity.
	/// Once a chunk has been written to storage, it is treated as immutable.
	/// </summary>
	public abstract class ChunkedDataNode : Node
	{
		/// <summary>
		/// Copies the contents of this node and its children to the given output stream
		/// </summary>
		/// <param name="reader">Reader for nodes in the tree</param>
		/// <param name="outputStream">The output stream to receive the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public abstract Task CopyToStreamAsync(TreeReader reader, Stream outputStream, CancellationToken cancellationToken);

		static readonly Guid s_leafNodeGuid = Node.GetNodeType<LeafChunkedDataNode>().Guid;
		static readonly Guid s_interiorNodeGuid = Node.GetNodeType<InteriorChunkedDataNode>().Guid;

		/// <summary>
		/// Copy the contents of the node to the output stream without creating the intermediate FileNodes
		/// </summary>
		/// <param name="reader">Reader for nodes in the tree</param>
		/// <param name="locator">File node to be copied</param>
		/// <param name="outputStream">The output stream to receive the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task CopyToStreamAsync(TreeReader reader, NodeLocator locator, Stream outputStream, CancellationToken cancellationToken)
		{
			NodeData nodeData = await reader.ReadNodeDataAsync(locator, cancellationToken);
			if (nodeData.Type.Guid == s_leafNodeGuid)
			{
				await LeafChunkedDataNode.CopyToStreamAsync(nodeData, outputStream, cancellationToken);
			}
			else if (nodeData.Type.Guid == s_interiorNodeGuid)
			{
				await InteriorChunkedDataNode.CopyToStreamAsync(reader, nodeData, outputStream, cancellationToken);
			}
			else
			{
				throw new ArgumentException($"Unexpected {nameof(ChunkedDataNode)} type found.");
			}
		}

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
	[NodeType("{B27AFB68-9E20-4A4B-A4D8-788A4098D439}", 1)]
	public sealed class LeafChunkedDataNode : ChunkedDataNode
	{
		/// <summary>
		/// Data for this node
		/// </summary>
		public ReadOnlyMemory<byte> Data { get; }

		/// <summary>
		/// Create an empty leaf node
		/// </summary>
		public LeafChunkedDataNode()
		{
		}

		/// <summary>
		/// Create a leaf node from the given serialized data
		/// </summary>
		public LeafChunkedDataNode(NodeReader reader)
		{
			// Keep this code in sync with CopyToStreamAsync
			Data = reader.ReadFixedLengthBytes(reader.Length);
		}

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			writer.WriteFixedLengthBytes(Data.Span);
		}

		/// <inheritdoc/>
		public override IEnumerable<NodeRef> EnumerateRefs() => Enumerable.Empty<NodeRef>();

		/// <inheritdoc/>
		public override async Task CopyToStreamAsync(TreeReader reader, Stream outputStream, CancellationToken cancellationToken)
		{
			await outputStream.WriteAsync(Data, cancellationToken);
		}

		/// <summary>
		/// Copy the contents of the node to the output stream without creating the intermediate FileNodes
		/// </summary>
		/// <param name="nodeData">The raw node data</param>
		/// <param name="outputStream">The output stream to receive the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task CopyToStreamAsync(NodeData nodeData, Stream outputStream, CancellationToken cancellationToken)
		{
			// Keep this code in sync with the constructor
			await outputStream.WriteAsync(nodeData.Data, cancellationToken);
		}
	}

	/// <summary>
	/// An interior file node
	/// </summary>
	[NodeType("{F4DEDDBC-70CB-4C7A-8347-F011AFCCCDB9}", 1)]
	public class InteriorChunkedDataNode : ChunkedDataNode
	{
		/// <summary>
		/// Child nodes
		/// </summary>
		public IReadOnlyList<NodeRef<ChunkedDataNode>> Children { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="children"></param>
		public InteriorChunkedDataNode(IReadOnlyList<NodeRef<ChunkedDataNode>> children)
		{
			Children = children;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public InteriorChunkedDataNode(NodeReader reader)
		{
			// Keep this code in sync with CopyToStreamAsync
			NodeRef<ChunkedDataNode>[] children = new NodeRef<ChunkedDataNode>[reader.Length / IoHash.NumBytes];
			for (int idx = 0; idx < children.Length; idx++)
			{
				children[idx] = reader.ReadRef<ChunkedDataNode>();
			}
			Children = children;
		}

		/// <inheritdoc/>
		public override void Serialize(ITreeNodeWriter writer)
		{
			foreach (NodeRef<ChunkedDataNode> child in Children)
			{
				writer.WriteRef(child);
			}
		}

		/// <inheritdoc/>
		public override IEnumerable<NodeRef> EnumerateRefs() => Children;

		/// <inheritdoc/>
		public override async Task CopyToStreamAsync(TreeReader reader, Stream outputStream, CancellationToken cancellationToken)
		{
			foreach (NodeRef<ChunkedDataNode> childNodeRef in Children)
			{
				ChunkedDataNode childNode = await childNodeRef.ExpandAsync(reader, cancellationToken);
				await childNode.CopyToStreamAsync(reader, outputStream, cancellationToken);
			}
		}

		/// <summary>
		/// Copy the contents of the node to the output stream without creating the intermediate FileNodes
		/// </summary>
		/// <param name="reader">Reader for nodes in the tree</param>
		/// <param name="nodeData">Source data</param>
		/// <param name="outputStream">The output stream to receive the data</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		public static async Task CopyToStreamAsync(TreeReader reader, NodeData nodeData, Stream outputStream, CancellationToken cancellationToken)
		{
			NodeReader nodeReader = new NodeReader(nodeData);
			while (nodeReader.GetMemory(0).Length > 0)
			{
				NodeRef nodeRef = nodeReader.ReadRef();
				await ChunkedDataNode.CopyToStreamAsync(reader, nodeRef.Handle!.Locator, outputStream, cancellationToken);
			}
		}
	}
}
