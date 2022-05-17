// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Buffers;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;

namespace EpicGames.Horde.Bundles.Nodes
{
	/// <summary>
	/// Base class for nodes stored in a bundle
	/// </summary>
	public abstract class BundleNode
	{
		class DefaultSerializer<T> where T : BundleNode
		{
			public static readonly BundleNodeDeserializer<T> Instance = CreateInstance();

			static BundleNodeDeserializer<T> CreateInstance()
			{
				BundleNodeDeserializerAttribute? attribute = typeof(T).GetCustomAttribute<BundleNodeDeserializerAttribute>();
				if (attribute == null)
				{
					throw new InvalidOperationException($"No serializer is defined for {typeof(T).Name}");
				}
				return (BundleNodeDeserializer<T>)Activator.CreateInstance(attribute.Type)!;
			}
		}

		/// <summary>
		/// Cached incoming reference to the owner of this node.
		/// </summary>
		internal BundleNodeRef? IncomingRef { get; set; }

		/// <summary>
		/// Queries if the node in its current state is read-only. Once we know that nodes are no longer going to be modified, they are favored for spilling to persistent storage.
		/// </summary>
		/// <returns>True if the node is read-only.</returns>
		public virtual bool IsReadOnly() => false;

		/// <summary>
		/// Mark this node as dirty
		/// </summary>
		protected void MarkAsDirty() => IncomingRef?.MarkAsDirty();

		/// <summary>
		/// Serialize this node into a sequence of bytes
		/// </summary>
		/// <returns>Serialized data for this node</returns>
		public abstract ReadOnlySequence<byte> Serialize();

		/// <summary>
		/// Deserialize a node of a particular type, using the default serializer defined by an <see cref="BundleNodeDeserializerAttribute"/>.
		/// </summary>
		/// <typeparam name="T">The type to deserialize</typeparam>
		/// <param name="memory">Data to deserialize from</param>
		/// <returns>New instance of the node</returns>
		public static T Deserialize<T>(ReadOnlyMemory<byte> memory) where T : BundleNode => DefaultSerializer<T>.Instance.Deserialize(memory);

		/// <summary>
		/// Deserialize a node of a particular type, using the default serializer defined by an <see cref="BundleNodeDeserializerAttribute"/>.
		/// </summary>
		/// <typeparam name="T">The type to deserialize</typeparam>
		/// <param name="sequence">Data to deserialize from</param>
		/// <returns>New instance of the node</returns>
		public static T Deserialize<T>(ReadOnlySequence<byte> sequence) where T : BundleNode => DefaultSerializer<T>.Instance.Deserialize(sequence);

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
		/// Cached reference to the parent node
		/// </summary>
		internal BundleNode _owner;

		/// <summary>
		/// Strong reference to the current node
		/// </summary>
		internal BundleNode? _strongRef;

		/// <summary>
		/// Weak reference to the child node. Maintained after the object has been flushed.
		/// </summary>
		private WeakReference<BundleNode>? _weakRef;

		/// <summary>
		/// Last time that the node was modified
		/// </summary>
		internal long _lastModifiedTime;

		/// <summary>
		/// Hash of the node. May be set to <see cref="IoHash.Zero"/> if the node in memory has been modified.
		/// </summary>
		public IoHash Hash { get; private set; }

		/// <summary>
		/// Reference to the node.
		/// </summary>
		public BundleNode? Node
		{
			get => _strongRef;
			set
			{ 
				_strongRef = value; 
				_weakRef = null; 
			}
		}

		/// <summary>
		/// Creates a reference to a node with the given hash
		/// </summary>
		/// <param name="owner">The node which owns the reference</param>
		/// <param name="hash">Hash of the referenced node</param>
		public BundleNodeRef(BundleNode owner, IoHash hash)
		{
			_owner = owner;
			Hash = hash;
		}

		/// <summary>
		/// Creates a reference to the given node
		/// </summary>
		/// <param name="owner">The node which owns the reference</param>
		/// <param name="node">The referenced node</param>
		public BundleNodeRef(BundleNode owner, BundleNode node)
		{
			_owner = owner;
			_strongRef = node;
			_lastModifiedTime = Stopwatch.GetTimestamp();

			node.IncomingRef = this;
		}

		/// <summary>
		/// Reparent this reference to a new owner.
		/// </summary>
		public void Reparent(BundleNode newOwner)
		{
			MarkAsDirty();
			_owner = newOwner;
			MarkAsDirty();
		}

		/// <summary>
		/// Figure out whether this ref is dirty
		/// </summary>
		/// <returns></returns>
		public bool IsDirty() => _lastModifiedTime != 0;

		/// <summary>
		/// Converts the node in this reference from a weak to strong reference.
		/// </summary>
		/// <returns>True if the ref contains a valid node on return</returns>
		internal bool MakeStrongRef()
		{
			if (_strongRef != null)
			{
				return true;
			}
			if (_weakRef != null && _weakRef.TryGetTarget(out _strongRef))
			{
				_weakRef = null;
				return true;
			}

			return false;
		}

		/// <summary>
		/// Marks this reference as dirty
		/// </summary>
		internal void MarkAsDirty()
		{
			if (_lastModifiedTime == 0)
			{
				_lastModifiedTime = Stopwatch.GetTimestamp();
				if (_strongRef == null)
				{
					if (_weakRef == null || !_weakRef.TryGetTarget(out _strongRef))
					{
						throw new InvalidOperationException("Unable to resolve weak reference to node");
					}
					_weakRef = null;
				}
				_owner.IncomingRef?.MarkAsDirty();
			}
		}

		/// <summary>
		/// Marks this reference as clean
		/// </summary>
		internal void MarkAsClean(IoHash hash)
		{
			Hash = hash;

			if (_strongRef != null)
			{
				OnCollapse();

				_weakRef = new WeakReference<BundleNode>(_strongRef);
				_strongRef = null;
			}

			_lastModifiedTime = 0;
		}

		/// <summary>
		/// Callback for when a reference is collapsed to a hash value. At the time of calling, both the hash and node data will be valid.
		/// </summary>
		protected virtual void OnCollapse()
		{
		}
	}

	/// <summary>
	/// Strongly typed reference to a <see cref="BundleNode"/>
	/// </summary>
	/// <typeparam name="T">Type of the node</typeparam>
	public class BundleNodeRef<T> : BundleNodeRef where T : BundleNode
	{
		/// <inheritdoc cref="BundleNodeRef.Node"/>
		public new T? Node
		{
			get => (T?)base.Node;
			set => base.Node = value;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="owner">The node which owns the reference</param>
		/// <param name="hash">Hash of the referenced node</param>
		public BundleNodeRef(BundleNode owner, IoHash hash) : base(owner, hash)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="owner">The node which owns the reference</param>
		/// <param name="node">The referenced node</param>
		public BundleNodeRef(BundleNode owner, T node) : base(owner, node)
		{
		}
	}

	/// <summary>
	/// Attribute used to define a factory for a particular node type
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public sealed class BundleNodeDeserializerAttribute : Attribute
	{
		/// <summary>
		/// The factory type. Should be derived from <see cref="BundleNodeDeserializer{T}"/>
		/// </summary>
		public Type Type { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleNodeDeserializerAttribute(Type type) => Type = type;
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
		/// <param name="data">Data to deserialize</param>
		/// <returns>New node parsed from the data</returns>
		public abstract T Deserialize(ReadOnlyMemory<byte> data);

		/// <summary>
		/// Deserializes data from the given bundle
		/// </summary>
		/// <param name="data">Data to deserialize</param>
		/// <returns>New node parsed from the data</returns>
		public T Deserialize(ReadOnlySequence<byte> data)
		{
			ReadOnlyMemory<byte> memory;
			if (data.IsSingleSegment)
			{
				memory = data.First;
			}
			else
			{
				memory = data.ToArray();
			}
			return Deserialize(memory);
		}
	}
}
