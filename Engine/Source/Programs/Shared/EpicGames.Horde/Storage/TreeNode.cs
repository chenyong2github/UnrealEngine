// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Base class for user-defined types that are stored in a tree
	/// </summary>
	public abstract class TreeNode
	{
		/// <summary>
		/// Cached incoming reference to the owner of this node.
		/// </summary>
		internal TreeNodeRef? IncomingRef { get; set; }

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
		/// Enumerates all the child references from this node
		/// </summary>
		/// <returns>Children of this node</returns>
		public abstract IReadOnlyList<TreeNodeRef> GetReferences();

		/// <summary>
		/// Static instance of the serializer for a particular <see cref="TreeNode"/> type.
		/// </summary>
		static class SerializerInstance<T> where T : TreeNode
		{
			static readonly TreeSerializerAttribute _attribute = typeof(T).GetCustomAttribute<TreeSerializerAttribute>()!;
			public static TreeNodeSerializer<T> Serializer { get; } = (TreeNodeSerializer<T>)Activator.CreateInstance(_attribute.Type)!;
		}

		/// <summary>
		/// Serialize a node to data
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="node"></param>
		/// <param name="writer"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public static ValueTask<ITreeBlob> SerializeAsync<T>(T node, ITreeBlobWriter writer, CancellationToken cancellationToken) where T : TreeNode
		{
			return SerializerInstance<T>.Serializer.SerializeAsync(writer, node, cancellationToken);
		}

		/// <summary>
		/// Deserialize a node from data
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="data"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public static ValueTask<T> DeserializeAsync<T>(ITreeBlob data, CancellationToken cancellationToken) where T : TreeNode
		{
			return SerializerInstance<T>.Serializer.DeserializeAsync(data, cancellationToken);
		}
	}

	/// <summary>
	/// Factory class for deserializing node types
	/// </summary>
	/// <typeparam name="T">The type of node returned</typeparam>
	public abstract class TreeNodeSerializer<T> where T : TreeNode
	{
		/// <summary>
		/// Serialize a given node
		/// </summary>
		/// <param name="writer">Writer for node data</param>
		/// <param name="node">The user type to be serialized</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Serialized node instance</returns>
		public abstract ValueTask<ITreeBlob> SerializeAsync(ITreeBlobWriter writer, T node, CancellationToken cancellationToken);

		/// <summary>
		/// Deserializes data from the given data
		/// </summary>
		/// <param name="node">Node to deserialize from</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>New node parsed from the data</returns>
		public abstract ValueTask<T> DeserializeAsync(ITreeBlob node, CancellationToken cancellationToken);
	}

	/// <summary>
	/// Attribute used to define a factory for a particular node type
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public sealed class TreeSerializerAttribute : Attribute
	{
		/// <summary>
		/// The factory type. Should be derived from <see cref="TreeNodeSerializer{T}"/>
		/// </summary>
		public Type Type { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public TreeSerializerAttribute(Type type) => Type = type;
	}
}
