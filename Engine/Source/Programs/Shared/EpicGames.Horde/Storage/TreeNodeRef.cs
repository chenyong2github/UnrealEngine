// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Diagnostics;
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
		/// Node that owns this ref
		/// </summary>
		internal TreeNode? _owner;

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
		/// The target node, or null if the node is not resident in memory.
		/// </summary>
		public TreeNode? Target
		{
			get => _target;
			set
			{
				if (value != _target)
				{
					if (value == null)
					{
						_target = value;
					}
					else
					{
						if (value.IncomingRef != null)
						{
							throw new ArgumentException("Target node may not be part of an existing tree.");
						}
						if (_target != null)
						{
							_target.IncomingRef = null;
						}
						_target = value;
						MarkAsDirty();
					}
				}
			}
		}

		/// <summary>
		/// Node pointed to by this ref
		/// </summary>
		private TreeNode? _target;

		/// <summary>
		/// Whether this ref is dirty
		/// </summary>
		private bool _dirty;

		/// <summary>
		/// Creates a reference to a node in memory.
		/// </summary>
		/// <param name="target">Node to reference</param>
		protected internal TreeNodeRef(TreeNode target)
		{
			Debug.Assert(target != null);
			Target = target;
			_dirty = true;
		}

		/// <summary>
		/// Creates a reference to a node with the given hash
		/// </summary>
		/// <param name="owner">Node which owns the ref</param>
		/// <param name="store">Store to fetch this node from</param>
		/// <param name="hash">Hash of the referenced node</param>
		/// <param name="locator">Locator for the node</param>
		internal TreeNodeRef(TreeNode owner, IStorageClient store, IoHash hash, NodeLocator locator)
		{
			Debug.Assert(store != null);
			_owner = owner;

			Store = store;
			Hash = hash;
			Locator = locator;

			_dirty = false;
		}

		/// <summary>
		/// Deserialization constructor
		/// </summary>
		/// <param name="reader"></param>
		public TreeNodeRef(ITreeNodeReader reader)
		{
			TreeNodeRefData data = reader.ReadRef();
			Store = data.Store;
			Hash = data.Hash;
			Locator = data.Locator;

			_dirty = false;
			Debug.Assert(Hash != IoHash.Zero);
		}

		/// <summary>
		/// Determines whether the the referenced node has modified from the last version written to storage
		/// </summary>
		/// <returns></returns>
		public bool IsDirty() => _dirty;

		/// <summary>
		/// Update the reference to refer to a node in memory.
		/// </summary>
		public void MarkAsDirty()
		{
			Debug.Assert(Target != null);

			Store = null;
			Hash = default;
			Locator = default;

			if (!_dirty)
			{
				_dirty = true;
				if (_owner != null && _owner.IncomingRef != null)
				{
					_owner.IncomingRef.MarkAsDirty();
				}
			}
		}

		/// <summary>
		/// Update the reference to refer to a location in storage.
		/// </summary>
		/// <param name="store">The storage client</param>
		/// <param name="hash">Hash of the node</param>
		/// <param name="locator">Location of the node</param>
		internal void MarkAsWritten(IStorageClient store, IoHash hash, NodeLocator locator)
		{
			if (hash == Hash)
			{
				Store = store;
				Locator = locator;
				_dirty = false;
			}
		}

		/// <summary>
		/// Updates the hash and revision number for the ref.
		/// </summary>
		/// <param name="hash">Hash of the node</param>
		/// <returns></returns>
		internal void MarkAsPendingWrite(IoHash hash)
		{
			Hash = hash;
			_dirty = false;
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
		/// <param name="owner">Node which owns the ref</param>
		/// <param name="store">Storage client containing the node data</param>
		/// <param name="hash">Hash of the referenced node</param>
		/// <param name="locator">Locator for the node</param>
		internal TreeNodeRef(TreeNode owner, IStorageClient store, IoHash hash, NodeLocator locator) 
			: base(owner, store, hash, locator)
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
			if (base.Target == null)
			{
				base.Target = await Store!.ReadNodeAsync<T>(Locator, cancellationToken);
				Target!.IncomingRef = this;
			}
			return Target!;
		}

		/// <summary>
		/// Resolve this reference to a concrete node
		/// </summary>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async ValueTask<T> ExpandCopyAsync(CancellationToken cancellationToken = default)
		{
			return await Store!.ReadNodeAsync<T>(Locator, cancellationToken);
		}
	}

	/// <summary>
	/// Deserialized ref data
	/// </summary>
	public record struct TreeNodeRefData(IStorageClient Store, IoHash Hash, NodeLocator Locator);
}
