// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Stores a reference from a parent to child node, which can be resurrected after the child node is flushed to storage if subsequently modified.
	/// </summary>
	public abstract class TreeNodeRef
	{
		/// <summary>
		/// Store containing the node data. May be null for nodes in memory.
		/// </summary>
		internal IStorageClient? _store;

		/// <summary>
		/// Hash of the referenced node. Invalid for nodes in memory.
		/// </summary>
		internal IoHash _hash;

		/// <summary>
		/// Locator for the blob containing this node. Invalid for nodes in memory.
		/// </summary>
		internal NodeLocator _locator;

		/// <summary>
		/// The target node, or null if the node is not resident in memory.
		/// </summary>
		public TreeNode? Target { get; set; }

		/// <summary>
		/// Creates a reference to a node in memory.
		/// </summary>
		/// <param name="target">Node to reference</param>
		protected TreeNodeRef(TreeNode target)
		{
			Target = target;
		}

		/// <summary>
		/// Creates a reference to a node with the given hash
		/// </summary>
		/// <param name="store">Storage client containing the data</param>
		/// <param name="hash">Hash of the referenced node</param>
		/// <param name="locator">Locator for the node</param>
		internal TreeNodeRef(IStorageClient store, IoHash hash, NodeLocator locator)
		{
			_store = store;
			_hash = hash;
			_locator = locator;
		}

		/// <summary>
		/// 
		/// </summary>
		/// <param name="reader"></param>
		public TreeNodeRef(ITreeNodeReader reader)
		{
			reader.ReadRef(this);
		}

		/// <summary>
		/// Serialize the node to the given writer
		/// </summary>
		/// <param name="writer"></param>
		public void Serialize(ITreeNodeWriter writer) => writer.WriteRef(this);

		/// <summary>
		/// Resolve this reference to a concrete node
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public abstract ValueTask<TreeNode> ExpandBaseAsync(CancellationToken cancellationToken = default);
	}

	/// <summary>
	/// Strongly typed reference to a <see cref="TreeNode"/>
	/// </summary>
	/// <typeparam name="T">Type of the node</typeparam>
	public class TreeNodeRef<T> : TreeNodeRef where T : TreeNode
	{
		/// <summary>
		/// Accessor for the target node
		/// </summary>
		public new T? Target
		{
			get => (T?)base.Target;
			set => base.Target = value;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="target">The referenced node</param>
		public TreeNodeRef(T target) : base(target)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="store">Storage client containing the node data</param>
		/// <param name="hash">Hash of the referenced node</param>
		/// <param name="locator">Locator for the node</param>
		internal TreeNodeRef(IStorageClient store, IoHash hash, NodeLocator locator) 
			: base(store, hash, locator)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="reader"></param>
		public TreeNodeRef(ITreeNodeReader reader)
			: base(reader)
		{
		}

		/// <summary>
		/// Resolve this reference to a concrete node
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public override async ValueTask<TreeNode> ExpandBaseAsync(CancellationToken cancellationToken = default)
		{
			return await ExpandAsync(cancellationToken);
		}

		/// <summary>
		/// Resolve this reference to a concrete node
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async ValueTask<T> ExpandAsync(CancellationToken cancellationToken = default)
		{
			T? result = Target;
			if (result == null)
			{
				Target = await _store!.ReadNodeAsync<T>(_locator, cancellationToken);
				result = Target;
			}
			return result;
		}
	}
}
