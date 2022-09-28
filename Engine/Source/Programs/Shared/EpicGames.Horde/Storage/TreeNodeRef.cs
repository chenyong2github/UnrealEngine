// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Stores a reference from a parent to child node. The reference may be to a node in memory, or to a node in the storage system.
	/// </summary>
	public class TreeNodeRef
	{
		/// <summary>
		/// Store containing the node data. May be null for nodes in memory.
		/// </summary>
		public IStorageClient? Store { get; private set; }

		/// <summary>
		/// Hash of the referenced node. Invalid for nodes in memory.
		/// </summary>
		public IoHash Hash { get; private set; }

		/// <summary>
		/// Locator for the blob containing this node. Invalid for nodes in memory.
		/// </summary>
		public NodeLocator Locator { get; private set; }

		/// <summary>
		/// Revision number of the target node
		/// </summary>
		private int _revision;

		/// <summary>
		/// The target node in memory
		/// </summary>
		private TreeNode? _target;

		/// <summary>
		/// The target node, or null if the node is not resident in memory.
		/// </summary>
		public TreeNode? Target
		{
			get => _target;
			set => MarkAsDirty(value);
		}

		/// <summary>
		/// Creates a reference to a node in memory.
		/// </summary>
		/// <param name="target">Node to reference</param>
		protected internal TreeNodeRef(TreeNode target)
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
			Store = store;
			Hash = hash;
			Locator = locator;
		}

		/// <summary>
		/// Deserialization constructor
		/// </summary>
		/// <param name="reader"></param>
		public TreeNodeRef(ITreeNodeReader reader)
		{
			reader.ReadRef(this);
		}

		/// <summary>
		/// Determines whether the the referenced node has modified from the last version written to storage
		/// </summary>
		/// <returns></returns>
		public bool IsDirty() => _target != null && _revision != _target.Revision;

		/// <summary>
		/// Update the reference to refer to a node in memory.
		/// </summary>
		/// <param name="target">The target node</param>
		public void MarkAsDirty(TreeNode? target)
		{
			if (target == null)
			{
				if (!Locator.IsValid())
				{
					throw new InvalidOperationException("Node has not been serialized to disk; cannot clear target reference.");
				}
			}
			else
			{
				Store = null;
				Hash = default;
				Locator = default;
				_revision = 0;
			}

			_target = target;
		}

		/// <summary>
		/// Update the reference to refer to a location in storage.
		/// </summary>
		/// <param name="store">The storage client</param>
		/// <param name="hash">Hash of the node</param>
		/// <param name="locator">Location of the node</param>
		/// <param name="revision">Revision number for the node</param>
		public bool MarkAsClean(IStorageClient store, IoHash hash, NodeLocator locator, int revision)
		{
			bool result = false;
			if (_target == null || _target.Revision == revision)
			{
				Store = store;
				Hash = hash;
				Locator = locator;
				_revision = revision;
				result = true;
			}
			return result;
		}

		/// <summary>
		/// Updates the hash and revision number for the ref.
		/// </summary>
		/// <param name="hash">Hash of the node</param>
		/// <returns></returns>
		public void MarkAsPendingWrite(IoHash hash)
		{
			Hash = hash;
			_revision = Target!.Revision;
		}

		/// <summary>
		/// Serialize the node to the given writer
		/// </summary>
		/// <param name="writer"></param>
		public void Serialize(ITreeNodeWriter writer) => writer.WriteRef(this);
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
		public async ValueTask<T> ExpandAsync(CancellationToken cancellationToken = default)
		{
			T? result = Target;
			if (result == null)
			{
				Target = await Store!.ReadNodeAsync<T>(Locator, cancellationToken);
				result = Target;
			}
			return result;
		}
	}
}
