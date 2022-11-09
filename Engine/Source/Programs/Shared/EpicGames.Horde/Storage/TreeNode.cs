// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Base class for user-defined types that are stored in a tree
	/// </summary>
	public abstract class TreeNode
	{
		/// <summary>
		/// The ref which points to this node
		/// </summary>
		public TreeNodeRef? IncomingRef { get; internal set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		protected TreeNode()
		{
		}

		/// <summary>
		/// Serialization constructor. Leaves the revision number zeroed by default.
		/// </summary>
		/// <param name="reader"></param>
		protected TreeNode(IMemoryReader reader)
		{
			_ = reader;
		}

		/// <summary>
		/// Mark this node as dirty
		/// </summary>
		protected void MarkAsDirty()
		{
			if (IncomingRef != null)
			{
				IncomingRef.MarkAsDirty();
			}
		}

		/// <summary>
		/// Serialize the contents of this node
		/// </summary>
		/// <returns>Data for the node</returns>
		public abstract void Serialize(ITreeNodeWriter writer);

		/// <summary>
		/// Enumerate all outward references from this node
		/// </summary>
		/// <returns>References to other nodes</returns>
		public abstract IEnumerable<TreeNodeRef> EnumerateRefs();
	}

	/// <summary>
	/// Writer for tree nodes
	/// </summary>
	public interface ITreeNodeWriter : IMemoryWriter
	{
		/// <summary>
		/// Write a reference to another node
		/// </summary>
		/// <param name="target">Target of the reference</param>
		void WriteRef(TreeNodeRef target);
	}

	/// <summary>
	/// Attribute used to define a factory for a particular node type
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public sealed class TreeNodeAttribute : Attribute
	{
		/// <summary>
		/// Name of the type to store in the bundle header
		/// </summary>
		public string Guid { get; }

		/// <summary>
		/// Version number of the serializer
		/// </summary>
		public int Version { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public TreeNodeAttribute(string guid, int version = 1)
		{
			Guid = guid;
			Version = version;
		}
	}

	/// <summary>
	/// Extension methods for serializing <see cref="TreeNode"/> objects
	/// </summary>
	public static class TreeNodeExtensions
	{
		/// <summary>
		/// 
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="reader"></param>
		/// <returns></returns>
		public static TreeNodeRef<T> ReadRef<T>(this ITreeNodeReader reader) where T : TreeNode
		{
			return new TreeNodeRef<T>(reader);
		}

		/// <summary>
		/// Writes a node to storage
		/// </summary>
		/// <param name="store">Store instance to write to</param>
		/// <param name="name">Name of the ref containing this node</param>
		/// <param name="node">Node to be written</param>
		/// <param name="options">Options for the node writer</param>
		/// <param name="prefix">Prefix for uploaded blobs</param>
		/// <param name="refOptions">Options for the ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Location of node targetted by the ref</returns>
		public static async Task<NodeLocator> WriteNodeAsync(this IStorageClient store, RefName name, TreeNode node, TreeOptions? options = null, Utf8String prefix = default, RefOptions? refOptions = null, CancellationToken cancellationToken = default)
		{
			TreeWriter writer = new TreeWriter(store, options, prefix.IsEmpty ? name.Text : prefix);
			return await writer.WriteRefAsync(name, node, refOptions, cancellationToken);
		}

		/// <summary>
		/// Cache of constructed <see cref="BundleType"/> instances.
		/// </summary>
		static readonly ConcurrentDictionary<Type, BundleType> s_typeToBundleType = new ConcurrentDictionary<Type, BundleType>();

		/// <summary>
		/// Gets the bundle type object for a particular node
		/// </summary>
		/// <param name="node"></param>
		/// <returns></returns>
		public static BundleType GetBundleType(this TreeNode node) => GetBundleType(node.GetType());

		/// <summary>
		/// Gets the <see cref="BundleType"/> instance for a particular node type
		/// </summary>
		/// <param name="type"></param>
		/// <returns>Bundle</returns>
		public static BundleType GetBundleType(Type type)
		{
			BundleType? bundleType;
			if (!s_typeToBundleType.TryGetValue(type, out bundleType))
			{
				TreeNodeAttribute? attribute = type.GetCustomAttribute<TreeNodeAttribute>();
				if (attribute == null)
				{
					throw new InvalidOperationException($"Missing {nameof(TreeNodeAttribute)} from type {type.Name}");
				}
				bundleType = s_typeToBundleType.GetOrAdd(type, new BundleType(Guid.Parse(attribute.Guid), attribute.Version));
			}
			return bundleType;
		}
	}
}
