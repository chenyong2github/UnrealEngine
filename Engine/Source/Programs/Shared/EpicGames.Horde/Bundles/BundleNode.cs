// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
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
		/// The bundle that owns this node
		/// </summary>
		public Bundle Owner { get; }

		/// <summary>
		/// The node's parent. Each expanded instance of the node in the tree will have a unique object, so each will have a singular parent.
		/// </summary>
		protected BundleNode? Parent { get; set; }

		/// <summary>
		/// Hash of this data. Set to zero once modified.
		/// </summary>
		private IoHash Hash { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleNode(Bundle Owner, BundleNode? Parent)
			: this(Owner, Parent, IoHash.Zero)
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleNode(Bundle Owner, BundleNode? Parent, IoHash Hash)
		{
			this.Owner = Owner;
			this.Parent = Parent;
			this.Hash = Hash;
		}

		/// <summary>
		/// Checks if the current node is dirty
		/// </summary>
		/// <returns>True if the node is dirty</returns>
		public bool IsDirty() => Hash == IoHash.Zero;

		/// <summary>
		/// Mark this node as dirty
		/// </summary>
		protected void MarkAsDirty()
		{
			if (!IsDirty())
			{
				Hash = IoHash.Zero;
				Parent?.MarkAsDirty();
			}
		}

		/// <summary>
		/// Serialize this node to a buffer
		/// </summary>
		/// <returns>Hash of the serialized node</returns>
		public IoHash Serialize()
		{
			if (IsDirty())
			{
				Hash = SerializeDirty();
			}
			return Hash;
		}

		/// <summary>
		/// Serialize this node to the bundle
		/// </summary>
		/// <returns>Hash of the serialized node</returns>
		protected abstract IoHash SerializeDirty();
	}

	/// <summary>
	/// Attribute used to define a factory for a particular node type
	/// </summary>
	public class BundleNodeFactoryAttribute : Attribute
	{
		/// <summary>
		/// The factory type. Should be derived from <see cref="BundleNodeFactory{T}"/>
		/// </summary>
		public Type Type { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public BundleNodeFactoryAttribute(Type Type) => this.Type = Type;
	}

	/// <summary>
	/// Factory class for creating node types
	/// </summary>
	/// <typeparam name="T">The type of node returned</typeparam>
	public abstract class BundleNodeFactory<T> where T : BundleNode
	{
		/// <summary>
		/// Creates a default value for the factory
		/// </summary>
		/// <param name="Bundle"></param>
		/// <returns>New instance of the node type</returns>
		public abstract T CreateRoot(Bundle Bundle);

		/// <summary>
		/// Parse an object from data
		/// </summary>
		/// <param name="Bundle">The owning bundle</param>
		/// <param name="Hash">Hash of the data</param>
		/// <param name="Data"></param>
		/// <returns>New instance of the node type</returns>
		public abstract T ParseRoot(Bundle Bundle, IoHash Hash, ReadOnlyMemory<byte> Data);
	}
}


