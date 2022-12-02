// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Serialization;

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
	/// </summary>
	public class TreeNodeRef
	{
		/// <summary>
		/// Locator for the blob containing this node. Invalid for nodes in memory.
		/// </summary>
		public NodeLocator Locator { get; private set; }

		/// <summary>
		/// The target node, or null if the node is not resident in memory.
		/// </summary>
		public TreeNode? Target { get; private set; }

		/// <summary>
		/// Creates a reference to a node in memory.
		/// </summary>
		/// <param name="target">Node to reference</param>
		public TreeNodeRef(TreeNode target)
		{
			Target = target;
		}

		/// <summary>
		/// Creates a reference to a node in storage.
		/// </summary>
		/// <param name="locator">Node to reference</param>
		public TreeNodeRef(NodeLocator locator)
		{
			Locator = locator;
		}

		/// <summary>
		/// Deserialization constructor
		/// </summary>
		/// <param name="reader"></param>
		public TreeNodeRef(ITreeNodeReader reader) : this(reader.ReadNodeLocator())
		{
		}

		/// <summary>
		/// Determines whether the the referenced node has modified from the last version written to storage
		/// </summary>
		/// <returns></returns>
		public bool IsDirty() => Target != null && (Target.Revision != 0 || !Locator.IsValid());

		/// <summary>
		/// Update the reference to refer to a node in memory.
		/// </summary>
		public void MarkAsDirty()
		{
			if (Target == null)
			{
				throw new InvalidOperationException("Cannot mark a ref as dirty without having expanded it.");
			}

			Locator = default;
		}

		/// <summary>
		/// Updates the hash and revision number for the ref.
		/// </summary>
		/// <returns></returns>
		internal void MarkAsPendingWrite()
		{
			OnCollapse();
		}

		/// <summary>
		/// Update the reference to refer to a location in storage.
		/// </summary>
		/// <param name="locator">Location of the node</param>
		/// <param name="revision">Revision number that was written</param>
		internal void MarkAsWritten(NodeLocator locator, uint revision)
		{
			Locator = locator;
			if (Target != null && Target.Revision == revision)
			{
				Target = null;
			}
		}

		/// <summary>
		/// Internal helper method to allow implementations of <see cref="ITreeNodeWriter"/> to call <see cref="Serialize(IMemoryWriter)"/>.
		/// </summary>
		/// <param name="writer"></param>
		internal void SerializeInternal(IMemoryWriter writer) => Serialize(writer);

		/// <summary>
		/// Serialize the node to the given writer
		/// </summary>
		/// <param name="writer"></param>
		protected virtual void Serialize(IMemoryWriter writer)
		{
		}

		/// <summary>
		/// Resolve this reference to a concrete node
		/// </summary>
		/// <param name="reader">Reader to use for expanding this ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async ValueTask<TreeNode> ExpandAsync(TreeReader reader, CancellationToken cancellationToken = default)
		{
			if (Target == null)
			{
				Target = await reader.ReadNodeAsync(Locator, cancellationToken);
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
	/// Strongly typed reference to a <see cref="TreeNode"/>
	/// </summary>
	/// <typeparam name="T">Type of the node</typeparam>
	public class TreeNodeRef<T> : TreeNodeRef where T : TreeNode
	{
		/// <summary>
		/// Accessor for the target node
		/// </summary>
		public new T? Target => (T?)base.Target;

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
		/// <param name="target">The referenced node</param>
		public TreeNodeRef(NodeLocator target) : base(target)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="reader">The reader to deserialize from</param>
		public TreeNodeRef(ITreeNodeReader reader) : base(reader)
		{
		}

		/// <summary>
		/// Resolve this reference to a concrete node
		/// </summary>
		/// <param name="reader">Reader to use for expanding this ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public new async ValueTask<T> ExpandAsync(TreeReader reader, CancellationToken cancellationToken = default)
		{
			return (T)await base.ExpandAsync(reader, cancellationToken);
		}

		/// <summary>
		/// Resolve this reference to a concrete node
		/// </summary>
		/// <param name="reader">Reader to use for expanding this ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public async ValueTask<T> ExpandCopyAsync(TreeReader reader, CancellationToken cancellationToken = default)
		{
			return await reader.ReadNodeAsync<T>(Locator, cancellationToken);
		}
	}
}
