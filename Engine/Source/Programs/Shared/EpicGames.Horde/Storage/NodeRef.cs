// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Stores a reference to a node, which may be in memory or in the storage system.
	/// 
	/// Refs may be in the following states:
	///  * In storage 
	///      * Locator is valid, Target is null, IsDirty() returns false.
	///      * Writing or calling Collapse() is a no-op.
	///  * In memory and target was expanded, has not been modified 
	///      * Locator is valid, Target is valid, IsDirty() returns false.
	///      * Writing or calling Collapse() on a ref transitions it to the "in storage" state.
	///  * In memory and target is new
	///      * Locator is invalid, Target is valid, IsDirty() returns true.
	///      * Writing a ref transitions it to the "in storage" state. Calling Collapse is a no-op.
	///  * In memory but target has been modified
	///      * Locator is set but may not reflect the current node state, Target is valid, IsDirty() returns true. 
	///      * Writing a ref transitions it to the "in storage" state. Calling Collapse is a no-op.
	///
	/// The <see cref="OnCollapse"/> and <see cref="OnExpand"/> methods allow overriden implementations to cache information about the target.
	///
	/// Each ref must have EXACTLY one owner; sharing of refs between objects is not permitted and will break change tracking.
	/// Multiple refs MAY point to the same target object.
	/// 
	/// To read an untracked object that can be added to a new ref, call <see cref="TreeReader.ReadNodeDataAsync(NodeLocator, CancellationToken)"/> 
	/// directly, or use <see cref="NodeRef{T}.ExpandCopyAsync(CancellationToken)"/>.
	/// </summary>
	public class NodeRef
	{
		/// <summary>
		/// The target node, or null if the node is not resident in memory.
		/// </summary>
		public Node? Target { get; private set; }

		/// <summary>
		/// Handle to the node if in storage (or pending write to storage)
		/// </summary>
		public HashedNodeHandle? Handle { get; private set; }

		/// <summary>
		/// Creates a reference to a node in memory.
		/// </summary>
		/// <param name="target">Node to reference</param>
		public NodeRef(Node target)
		{
			Target = target;
		}

		/// <summary>
		/// Creates a reference to a node in storage.
		/// </summary>
		/// <param name="handle">Handle to the referenced node</param>
		public NodeRef(HashedNodeHandle handle)
		{
			Handle = handle;
		}

		/// <summary>
		/// Deserialization constructor
		/// </summary>
		/// <param name="reader"></param>
		public NodeRef(NodeReader reader) : this(reader.ReadNodeHandle())
		{
		}

		/// <summary>
		/// Determines whether the the referenced node has been modified from the last version written to storage
		/// </summary>
		/// <returns></returns>
		public bool IsDirty() => Target != null && (Handle == null || Target.Hash != Handle.Hash);

		/// <summary>
		/// Update the reference to refer to a node in memory.
		/// </summary>
		public void MarkAsDirty()
		{
			if (Target == null)
			{
				throw new InvalidOperationException("Cannot mark a ref as dirty without having expanded it.");
			}

			Handle = null;
		}

		/// <summary>
		/// Updates the hash and revision number for the ref.
		/// </summary>
		/// <returns></returns>
		internal void MarkAsPendingWrite(HashedNodeHandle handle)
		{
			OnCollapse();
			Handle = handle;
		}

		/// <summary>
		/// Update the reference to refer to a location in storage.
		/// </summary>
		internal void MarkAsWritten()
		{
			if (Target != null && Handle != null && Target.Hash == Handle.Hash)
			{
				Target = null;
			}
		}

		/// <summary>
		/// Serialize the node to the given writer
		/// </summary>
		/// <param name="writer"></param>
		public virtual void Serialize(ITreeNodeWriter writer)
		{
			Debug.Assert(Handle != null);
			writer.WriteNodeHandle(Handle);
		}

		/// <summary>
		/// Resolve this reference to a concrete node
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async ValueTask<Node> ExpandAsync(CancellationToken cancellationToken = default)
		{
			if (Target == null)
			{
				NodeData nodeData = await Handle!.Handle.ReadAsync(cancellationToken);
				Target = Node.Deserialize(nodeData);
				OnExpand();
			}
			return Target;
		}

		/// <summary>
		/// Collapse the current node
		/// </summary>
		public void Collapse()
		{
			if (Target != null && !IsDirty())
			{
				OnCollapse();
				Target = null;
			}
		}

		/// <summary>
		/// Callback after a node is expanded, allowing overridden implementations to cache any information about the target
		/// </summary>
		protected virtual void OnExpand()
		{
		}

		/// <summary>
		/// Callback before a node is collapsed, allowing overridden implementations to cache any information about the target
		/// </summary>
		protected virtual void OnCollapse()
		{
		}
	}

	/// <summary>
	/// Strongly typed reference to a <see cref="Node"/>
	/// </summary>
	/// <typeparam name="T">Type of the node</typeparam>
	public class NodeRef<T> : NodeRef where T : Node
	{
		/// <summary>
		/// Accessor for the target node
		/// </summary>
		public new T? Target => (T?)base.Target;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="target">The referenced node</param>
		public NodeRef(T target) : base(target)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="handle">Handle to the referenced node</param>
		public NodeRef(HashedNodeHandle handle) : base(handle)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="reader">The reader to deserialize from</param>
		public NodeRef(NodeReader reader) : base(reader)
		{
		}

		/// <summary>
		/// Resolve this reference to a concrete node
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public new async ValueTask<T> ExpandAsync(CancellationToken cancellationToken = default)
		{
			return (T)await base.ExpandAsync(cancellationToken);
		}

		/// <summary>
		/// Resolve this reference to a concrete node
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async ValueTask<T> ExpandCopyAsync(CancellationToken cancellationToken = default)
		{
			if (Handle == null)
			{
				throw new InvalidOperationException("TreeNodeRef has not been serialized to storage");
			}

			NodeData nodeData = await Handle.Handle.ReadAsync(cancellationToken);
			return Node.Deserialize<T>(nodeData);
		}
	}

	/// <summary>
	/// Extension methods for writing node
	/// </summary>
	public static class NodeRefExtensions
	{
		// Implementation of INodeWriter that tracks refs
		class NodeWriter : ITreeNodeWriter
		{
			readonly IStorageWriter _treeWriter;

			Memory<byte> _memory;
			int _length;
			readonly IReadOnlyList<NodeHandle> _refs;
			int _refIdx;

			public int Length => _length;

			public NodeWriter(IStorageWriter treeWriter, IReadOnlyList<NodeHandle> refs)
			{
				_treeWriter = treeWriter;
				_memory = treeWriter.GetOutputBuffer(0, 256 * 1024);
				_refs = refs;
			}

			public void WriteNodeHandle(HashedNodeHandle target)
			{
				if (_refs[_refIdx] != target.Handle)
				{
					throw new InvalidOperationException("Referenced node does not match the handle returned by owner's EnumerateRefs method.");
				}

				this.WriteIoHash(target.Hash);
				_refIdx++;
			}

			public Span<byte> GetSpan(int sizeHint = 0) => GetMemory(sizeHint).Span;

			public Memory<byte> GetMemory(int sizeHint = 0)
			{
				int newLength = _length + Math.Max(sizeHint, 1);
				if (newLength > _memory.Length)
				{
					newLength = _length + Math.Max(sizeHint, 1024);
					_memory = _treeWriter.GetOutputBuffer(_length, Math.Max(_memory.Length * 2, newLength));
				}
				return _memory.Slice(_length);
			}

			public void Advance(int length) => _length += length;
		}

		/// <summary>
		/// Class used to track nodes which are pending write (and the state of the object when the write was started)
		/// </summary>
		internal class NodeWriteCallback : TreeWriter.WriteCallback
		{
			readonly NodeRef _nodeRef;
			readonly HashedNodeHandle _handle;

			public NodeWriteCallback(NodeRef nodeRef, HashedNodeHandle handle)
			{
				_nodeRef = nodeRef;
				_handle = handle;
			}

			public override void OnWrite()
			{
				if (_nodeRef.Handle == _handle)
				{
					_nodeRef.MarkAsWritten();
				}
			}
		}

		/// <summary>
		/// Writes an individual node to storage
		/// </summary>
		/// <param name="writer">Writer to serialize nodes to</param>
		/// <param name="nodeRef">Reference to the node</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>A flag indicating whether the node is dirty, and if it is, an optional bundle that contains it</returns>
		public static async ValueTask<HashedNodeHandle> WriteAsync(this IStorageWriter writer, NodeRef nodeRef, CancellationToken cancellationToken)
		{
			// Check we actually have a target node. If we don't, we don't need to write anything.
			Node? target = nodeRef.Target;
			if (target == null)
			{
				Debug.Assert(nodeRef.Handle != null);
				return nodeRef.Handle;
			}

			// Write all the nodes it references, and mark the ref as dirty if any of them change.
			List<NodeRef> nextRefs = target.EnumerateRefs().ToList();
			List<NodeHandle> nextRefHandles = new List<NodeHandle>(nextRefs.Count);
			foreach (NodeRef nextRef in nextRefs)
			{
				HashedNodeHandle nextRefHandle = await WriteAsync(writer, nextRef, cancellationToken);
				if (!nextRefHandle.Handle.Locator.IsValid())
				{
					nodeRef.MarkAsDirty();
				}
				nextRefHandles.Add(nextRefHandle.Handle);
			}

			// If the target node hasn't been modified, use the existing serialized state.
			if (!nodeRef.IsDirty())
			{
				// Make sure the locator is valid. The node may be queued for writing but not flushed to disk yet.
				Debug.Assert(nodeRef.Handle != null);
				if (nodeRef.Handle.Handle.Locator.IsValid())
				{
					nodeRef.Collapse();
				}
				return nodeRef.Handle;
			}

			// Serialize the node
			NodeWriter nodeWriter = new NodeWriter(writer, nextRefHandles);
			target.Serialize(nodeWriter);

			// Write the final data
			HashedNodeHandle handle = await writer.WriteNodeAsync(nodeWriter.Length, nextRefHandles, target.NodeType, cancellationToken);
			target.Hash = handle.Hash;

			NodeWriteCallback writeState = new NodeWriteCallback(nodeRef, handle);
			handle.Handle.AddWriteCallback(writeState);

			nodeRef.MarkAsPendingWrite(handle);
			return handle;
		}

		/// <summary>
		/// Writes a node to the given ref
		/// </summary>
		/// <param name="writer"></param>
		/// <param name="name">Name of the ref to write</param>
		/// <param name="node"></param>
		/// <param name="options"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public static async Task<HashedNodeHandle> WriteAsync(this IStorageWriter writer, RefName name, Node node, RefOptions? options = null, CancellationToken cancellationToken = default)
		{
			NodeRef nodeRef = new NodeRef(node);
			await writer.WriteAsync(nodeRef, cancellationToken);
			await writer.FlushAsync(cancellationToken);

			Debug.Assert(nodeRef.Handle != null);
			await writer.Store.WriteRefTargetAsync(name, nodeRef.Handle.Handle, options, cancellationToken);
			return nodeRef.Handle;
		}

		/// <summary>
		/// Flushes all the current nodes to storage
		/// </summary>
		/// <param name="writer"></param>
		/// <param name="root">Root for the tree</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public static async Task<HashedNodeHandle> FlushAsync(this IStorageWriter writer, Node root, CancellationToken cancellationToken = default)
		{
			NodeRef rootRef = new NodeRef(root);

			HashedNodeHandle handle = await writer.WriteAsync(rootRef, cancellationToken);
//			writer._traceLogger?.LogInformation("Written root node {Handle}", handle);

			await writer.FlushAsync(cancellationToken);
//			writer._traceLogger?.LogInformation("Flushed root node {Handle}", handle);

			return rootRef.Handle!;
		}
	}
}
