// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Reflection.Emit;
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

		/// <summary>
		/// Static instance of the serializer for a particular <see cref="TreeNode"/> type.
		/// </summary>
		static class SerializerInstance<T> where T : TreeNode
		{
			public static readonly TreeNodeSerializer<T> Serializer = CreateSerializer();

			static TreeNodeSerializer<T> CreateSerializer()
			{
				TreeSerializerAttribute? _attribute = typeof(T).GetCustomAttribute<TreeSerializerAttribute>()!;
				if (_attribute == null)
				{
					return new DefaultTreeNodeSerializer<T>();
				}
				else
				{
					return (TreeNodeSerializer<T>)Activator.CreateInstance(_attribute.Type)!;
				}
			}
		}

		/// <summary>
		/// Deserialize a node from data
		/// </summary>
		/// <typeparam name="T"></typeparam>
		/// <param name="reader">Reader to deserialize data from</param>
		/// <returns></returns>
		public static T Deserialize<T>(ITreeNodeReader reader) where T : TreeNode
		{
			T node = SerializerInstance<T>.Serializer.Deserialize(reader);
			foreach (TreeNodeRef nodeRef in node.EnumerateRefs())
			{
				Debug.Assert(nodeRef._owner == null);
				nodeRef._owner = node;
			}
			return node;
		}
	}

	/// <summary>
	/// Reader for tree nodes
	/// </summary>
	public interface ITreeNodeReader : IMemoryReader
	{
		/// <summary>
		/// Reads a reference to another node
		/// </summary>
		TreeNodeRefData ReadRef();
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
	/// Factory class for deserializing node types
	/// </summary>
	/// <typeparam name="T">The type of node returned</typeparam>
	public abstract class TreeNodeSerializer<T> where T : TreeNode
	{
		/// <summary>
		/// Deserializes data from the given data
		/// </summary>
		/// <param name="reader">Reader to deserialize data from</param>
		/// <returns>New node parsed from the data</returns>
		public abstract T Deserialize(ITreeNodeReader reader);
	}

	/// <summary>
	/// Default implementation of <see cref="TreeNodeSerializer{T}"/> which calls a constructor taking an <see cref="ITreeNodeReader"/> argument.
	/// </summary>
	/// <typeparam name="T"></typeparam>
	public class DefaultTreeNodeSerializer<T> : TreeNodeSerializer<T> where T : TreeNode
	{
		static readonly Type[] s_signature = new[] { typeof(ITreeNodeReader) };

		readonly Func<ITreeNodeReader, T> _constructor;

		/// <summary>
		/// Constructor
		/// </summary>
		public DefaultTreeNodeSerializer()
		{
			Type type = typeof(T);

			ConstructorInfo? constructorInfo = type.GetConstructor(s_signature);
			if (constructorInfo == null)
			{
				throw new InvalidOperationException($"Type {type.Name} does not have a constructor taking an {typeof(ITreeNodeReader).Name} as parameter.");
			}

			DynamicMethod method = new DynamicMethod($"Create_{type.Name}", type, s_signature, true);

			ILGenerator generator = method.GetILGenerator();
			generator.Emit(OpCodes.Ldarg_0);
			generator.Emit(OpCodes.Newobj, constructorInfo);
			generator.Emit(OpCodes.Ret);

			_constructor = (Func<ITreeNodeReader, T>)method.CreateDelegate(typeof(Func<ITreeNodeReader, T>));
		}

		/// <inheritdoc/>
		public override T Deserialize(ITreeNodeReader reader) => (T)_constructor(reader);
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
	}
}
