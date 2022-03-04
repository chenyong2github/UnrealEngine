// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Bundles.Nodes
{
	/// <summary>
	/// Base class for nodes stored in a bundle
	/// </summary>
	public abstract class BundleNode
	{
		/// <summary>
		/// Cached incoming reference to the owner of this node. When the contents of this node are flushed to storage, this is modified to become a weak reference
		/// and hash value. When modified, it becomes a strong reference and zero hash.
		/// </summary>
		internal BundleNodeRef? IncomingRef { get; set; }

		/// <summary>
		/// Mark this node as dirty
		/// </summary>
		protected void MarkAsDirty() => IncomingRef?.MarkAsDirty();

		/// <summary>
		/// Serialize this node into a sequence of bytes
		/// </summary>
		/// <returns>Serialized data for this node</returns>
		public abstract ReadOnlyMemory<byte> Serialize();

		/// <summary>
		/// Enumerates all the child references from this node
		/// </summary>
		/// <returns>Children of this node</returns>
		public abstract IEnumerable<BundleNodeRef> GetReferences();
	}

	/// <summary>
	/// Stores a reference from a parent to child node, which can be resurrected after the child node is flushed to storage if subsequently modified.
	/// </summary>
	public class BundleNodeRef
	{
		/// <summary>
		/// The bundle that owns this reference. Used to realize hashes into full nodes, and set when we do a full scan of the tree from the bundle.
		/// </summary>
		internal Bundle? Bundle;

		/// <summary>
		/// Cached reference to the parent node
		/// </summary>
		internal BundleNodeRef? ParentRef;

		/// <summary>
		/// Strong reference to the current node
		/// </summary>
		protected BundleNode? StrongRef;

		/// <summary>
		/// Weak reference to the child node. Maintained after the object has been flushed.
		/// </summary>
		protected WeakReference<BundleNode>? WeakRef { get; set; }

		/// <summary>
		/// Last time that the node was modified
		/// </summary>
		internal long LastModifiedTime;

		/// <summary>
		/// Hash of the node. May be set to <see cref="IoHash.Zero"/> if the node in memory has been modified.
		/// </summary>
		public IoHash Hash { get; private set; }

		/// <summary>
		/// Reference to the node.
		/// </summary>
		public BundleNode? Node
		{
			get => StrongRef;
			set { StrongRef = value; WeakRef = null; }
		}

		/// <summary>
		/// Creates a reference to a node with the given hash
		/// </summary>
		/// <param name="Bundle">The bundle that contains the reference</param>
		/// <param name="Hash">Hash of the referenced node</param>
		public BundleNodeRef(Bundle Bundle, IoHash Hash)
		{
			this.Bundle = Bundle;
			this.Hash = Hash;
		}

		/// <summary>
		/// Creates a reference to the given node
		/// </summary>
		/// <param name="Node">The referenced node</param>
		public BundleNodeRef(BundleNode Node)
		{
			this.StrongRef = Node;
			this.LastModifiedTime = Stopwatch.GetTimestamp();
		}

		/// <summary>
		/// Detach this reference from its current owner. Used in situations where a node needs to be reparented.
		/// </summary>
		public void Detach()
		{
			MarkAsDirty();
			ParentRef = null;
		}

		/// <summary>
		/// Marks this reference as dirty
		/// </summary>
		internal void MarkAsDirty()
		{
			if (LastModifiedTime == 0)
			{
				this.LastModifiedTime = Stopwatch.GetTimestamp();
				if (StrongRef == null)
				{
					if (WeakRef == null || !WeakRef.TryGetTarget(out StrongRef))
					{
						throw new InvalidOperationException("Unable to resolve weak reference to node");
					}
					this.WeakRef = null;
				}
				ParentRef?.MarkAsDirty();
			}
		}

		/// <summary>
		/// Marks this reference as clean
		/// </summary>
		internal void MarkAsClean(IoHash Hash)
		{
			this.Hash = Hash;

			if (StrongRef != null)
			{
				Collapse();

				this.WeakRef = new WeakReference<BundleNode>(StrongRef);
				this.StrongRef = null;
			}

			this.LastModifiedTime = 0;
		}

		/// <summary>
		/// Callback for when a reference is collapsed to a hash value. At the time of calling, both the hash and node data will be valid.
		/// </summary>
		protected virtual void Collapse()
		{
		}
	}

	/// <summary>
	/// Strongly typed reference to a <see cref="BundleNode"/>
	/// </summary>
	/// <typeparam name="T">Type of the node</typeparam>
	public class BundleNodeRef<T> : BundleNodeRef where T : BundleNode
	{
		static readonly BundleNodeDeserializer<T> Deserializer = CreateDeserializer();

		static BundleNodeDeserializer<T> CreateDeserializer()
		{
			BundleNodeDeserializerAttribute? Attribute = typeof(T).GetCustomAttribute<BundleNodeDeserializerAttribute>();
			if (Attribute == null)
			{
				throw new InvalidOperationException($"No serializer is defined for {typeof(T).Name}");
			}
			return (BundleNodeDeserializer<T>)Activator.CreateInstance(Attribute.Type)!;
		}

		/// <inheritdoc cref="BundleNodeRef.Node"/>
		public new T? Node
		{
			get => (T?)base.Node;
			set => base.Node = value;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Bundle">The bundle that contains the reference</param>
		/// <param name="Hash">Hash of the referenced node</param>
		public BundleNodeRef(Bundle Bundle, IoHash Hash) : base(Bundle, Hash)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Node">The referenced node</param>
		public BundleNodeRef(T Node) : base(Node)
		{
		}

		/// <summary>
		/// Gets the referenced node, loading it from storage if necessary
		/// </summary>
		/// <returns>The parsed node</returns>
		public async ValueTask<T> GetAsync()
		{
			if (StrongRef == null)
			{
				if (WeakRef != null && WeakRef.TryGetTarget(out StrongRef))
				{
					WeakRef = null;
				}
				else
				{
					StrongRef = Deserializer.Deserialize(Bundle!, await Bundle!.GetDataAsync(Hash));
				}
			}
			return (T)StrongRef;
		}
	}

	/// <summary>
	/// Attribute used to define a factory for a particular node type
	/// </summary>
	public class BundleNodeDeserializerAttribute : Attribute
	{
		/// <summary>
		/// The factory type. Should be derived from <see cref="BundleNodeDeserializer{T}"/>
		/// </summary>
		public Type Type { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleNodeDeserializerAttribute(Type Type) => this.Type = Type;
	}

	/// <summary>
	/// Factory class for deserializing node types
	/// </summary>
	/// <typeparam name="T">The type of node returned</typeparam>
	public abstract class BundleNodeDeserializer<T> where T : BundleNode
	{
		/// <summary>
		/// Deserializes data from the given bundle
		/// </summary>
		/// <param name="Bundle">The bundle containing the data</param>
		/// <param name="Data">Data to deserialize</param>
		/// <returns>New node parsed from the data</returns>
		public abstract T Deserialize(Bundle Bundle, ReadOnlyMemory<byte> Data);
	}
}
