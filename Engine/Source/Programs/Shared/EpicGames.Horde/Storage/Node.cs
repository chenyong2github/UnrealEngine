// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Reflection.Emit;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Attribute used to define a factory for a particular node type
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public sealed class NodeTypeAttribute : Attribute // TODO: THIS SHOULD BE NODEATTRIBUTE, not NODETYPEATTRIBUTE
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
		public NodeTypeAttribute(string guid, int version = 1)
		{
			Guid = guid;
			Version = version;
		}
	}

	/// <summary>
	/// Base class for user-defined types that are stored in a tree
	/// </summary>
	public abstract class Node
	{
		/// <summary>
		/// Revision number of the node. Incremented whenever the node is modified, and used to track whether nodes are modified between 
		/// writes starting and completing.
		/// </summary>
		public uint Revision { get; private set; }

		/// <summary>
		/// Hash when deserialized
		/// </summary>
		public IoHash Hash { get; internal set; }

		/// <summary>
		/// Accessor for the bundle type definition associated with this node
		/// </summary>
		public NodeType NodeType => GetNodeType(GetType());

		/// <summary>
		/// Default constructor
		/// </summary>
		protected Node()
		{
		}

		/// <summary>
		/// Serialization constructor. Leaves the revision number zeroed by default.
		/// </summary>
		/// <param name="reader"></param>
		protected Node(NodeReader reader)
		{
			Hash = reader.Hash;
		}

		/// <summary>
		/// Mark this node as dirty
		/// </summary>
		protected void MarkAsDirty()
		{
			Hash = IoHash.Zero;
			Revision++;
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
		public abstract IEnumerable<NodeRef> EnumerateRefs();

		#region Static methods

		static readonly object s_writeLock = new object();
		static readonly ConcurrentDictionary<Type, NodeType> s_typeToNodeType = new ConcurrentDictionary<Type, NodeType>();
		static readonly ConcurrentDictionary<Guid, Type> s_guidToType = new ConcurrentDictionary<Guid, Type>();
		static readonly ConcurrentDictionary<Guid, Func<NodeReader, Node>> s_guidToDeserializer = new ConcurrentDictionary<Guid, Func<NodeReader, Node>>();

		static NodeType CreateNodeType(NodeTypeAttribute attribute)
		{
			return new NodeType(Guid.Parse(attribute.Guid), attribute.Version);
		}

		/// <summary>
		/// Attempts to get the concrete type with the given node. The type must have been registered via a previous call to <see cref="RegisterType(Type)"/>.
		/// </summary>
		/// <param name="guid">Guid specified in the <see cref="NodeTypeAttribute"/></param>
		/// <param name="type">On success, receives the C# type associated with this GUID</param>
		/// <returns>True if the type was found</returns>
		public static bool TryGetConcreteType(Guid guid, [NotNullWhen(true)] out Type? type) => s_guidToType.TryGetValue(guid, out type);

		/// <summary>
		/// Gets the node type corresponding to the given C# type
		/// </summary>
		/// <param name="type"></param>
		/// <returns></returns>
		public static NodeType GetNodeType(Type type)
		{
			NodeType nodeType;
			if (!s_typeToNodeType.TryGetValue(type, out nodeType))
			{
				lock (s_writeLock)
				{
					if (!s_typeToNodeType.TryGetValue(type, out nodeType))
					{
						NodeTypeAttribute? attribute = type.GetCustomAttribute<NodeTypeAttribute>();
						if (attribute == null)
						{
							throw new InvalidOperationException($"Missing {nameof(NodeTypeAttribute)} from type {type.Name}");
						}
						nodeType = s_typeToNodeType.GetOrAdd(type, CreateNodeType(attribute));
					}
				}
			}
			return nodeType;
		}

		/// <summary>
		/// Gets the type descriptor for the given type
		/// </summary>
		/// <typeparam name="T">Type to get a <see cref="NodeType"/> for</typeparam>
		/// <returns></returns>
		public static NodeType GetNodeType<T>() where T : Node => GetNodeType(typeof(T));

		/// <summary>
		/// Deserialize a node from the given reader
		/// </summary>
		/// <param name="reader">Data to deserialize from</param>
		/// <returns>New node instance</returns>
		public static Node Deserialize(NodeReader reader)
		{
			return s_guidToDeserializer[reader.Type.Guid](reader);
		}

		/// <summary>
		/// Deserialize a node from the given reader
		/// </summary>
		/// <param name="reader">Data to deserialize from</param>
		/// <returns>New node instance</returns>
		public static TNode Deserialize<TNode>(NodeReader reader) where TNode : Node => (TNode)Deserialize(reader);

		/// <summary>
		/// Static constructor. Registers all the types in the current assembly.
		/// </summary>
		static Node()
		{
			RegisterTypesFromAssembly(Assembly.GetExecutingAssembly());
		}

		/// <summary>
		/// Registers a single deserializer
		/// </summary>
		/// <param name="type"></param>
		/// <exception cref="NotImplementedException"></exception>
		public static void RegisterType(Type type)
		{
			NodeTypeAttribute? attribute = type.GetCustomAttribute<NodeTypeAttribute>();
			if (attribute == null)
			{
				throw new InvalidOperationException($"Missing {nameof(NodeTypeAttribute)} from type {type.Name}");
			}
			RegisterType(type, attribute);
		}

		/// <summary>
		/// 
		/// </summary>
		/// <typeparam name="T"></typeparam>
		public static void RegisterType<T>() where T : Node => RegisterType(typeof(T));

		/// <summary>
		/// Register all node types with the <see cref="NodeTypeAttribute"/> from the given assembly
		/// </summary>
		/// <param name="assembly">Assembly to register types from</param>
		public static void RegisterTypesFromAssembly(Assembly assembly)
		{
			Type[] types = assembly.GetTypes();
			lock (s_writeLock)
			{
				foreach (Type type in types)
				{
					if (type.IsClass)
					{
						NodeTypeAttribute? attribute = type.GetCustomAttribute<NodeTypeAttribute>();
						if (attribute != null)
						{
							RegisterType(type, attribute);
						}
					}
				}
			}
		}

		static void RegisterType(Type type, NodeTypeAttribute attribute)
		{
			NodeType nodeType = CreateNodeType(attribute);
			s_typeToNodeType.TryAdd(type, nodeType);

			Func<NodeReader, Node> deserializer = CreateDeserializer(type);
			s_guidToType.TryAdd(nodeType.Guid, type);
			s_guidToDeserializer.TryAdd(nodeType.Guid, deserializer);
		}

		static Func<NodeReader, Node> CreateDeserializer(Type type)
		{
			Type[] signature = new[] { typeof(NodeReader) };

			ConstructorInfo? constructorInfo = type.GetConstructor(signature);
			if (constructorInfo == null)
			{
				throw new InvalidOperationException($"Type {type.Name} does not have a constructor taking an {typeof(NodeReader).Name} as parameter.");
			}

			DynamicMethod method = new DynamicMethod($"Create_{type.Name}", type, signature, true);

			ILGenerator generator = method.GetILGenerator();
			generator.Emit(OpCodes.Ldarg_0);
			generator.Emit(OpCodes.Newobj, constructorInfo);
			generator.Emit(OpCodes.Ret);

			return (Func<NodeReader, Node>)method.CreateDelegate(typeof(Func<NodeReader, Node>));
		}

		#endregion
	}

	/// <summary>
	/// Writer for tree nodes
	/// </summary>
	public interface ITreeNodeWriter : IMemoryWriter
	{
		/// <summary>
		/// Writes a reference to another node
		/// </summary>
		/// <param name="handle">Handle to the target node</param>
		void WriteNodeHandle(NodeHandle handle);
	}

	/// <summary>
	/// Extension methods for serializing <see cref="Node"/> objects
	/// </summary>
	public static class NodeExtensions
	{
		/// <summary>
		/// Reads and deserializes a node from storage
		/// </summary>
		/// <typeparam name="TNode"></typeparam>
		/// <param name="handle"></param>
		/// <param name="cancellationToken"></param>
		/// <returns></returns>
		public static async ValueTask<TNode> ReadAsync<TNode>(this NodeHandle handle, CancellationToken cancellationToken = default) where TNode : Node
		{
			NodeReader nodeData = await handle.ReadAsync(cancellationToken);
			return Node.Deserialize<TNode>(nodeData);
		}

		/// <summary>
		/// Read an untyped ref from the reader
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		/// <returns>New untyped ref</returns>
		public static NodeRef ReadRef(this NodeReader reader)
		{
			return new NodeRef(reader);
		}

		/// <summary>
		/// Read a strongly typed ref from the reader
		/// </summary>
		/// <typeparam name="T">Type of the referenced node</typeparam>
		/// <param name="reader">Reader to deserialize from</param>
		/// <returns>New strongly typed ref</returns>
		public static NodeRef<T> ReadRef<T>(this NodeReader reader) where T : Node
		{
			return new NodeRef<T>(reader);
		}

		/// <summary>
		/// Read an optional untyped ref from the reader
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		/// <returns>New untyped ref</returns>
		public static NodeRef? ReadOptionalRef(this NodeReader reader)
		{
			if (reader.ReadBoolean())
			{
				return reader.ReadRef();
			}
			else
			{
				return null;
			}
		}

		/// <summary>
		/// Read an optional strongly typed ref from the reader
		/// </summary>
		/// <param name="reader">Reader to deserialize from</param>
		/// <returns>New strongly typed ref</returns>
		public static NodeRef<T>? ReadOptionalRef<T>(this NodeReader reader) where T : Node
		{
			if (reader.ReadBoolean())
			{
				return reader.ReadRef<T>();
			}
			else
			{
				return null;
			}
		}

		/// <summary>
		/// Writes a ref to storage
		/// </summary>
		/// <param name="writer">Writer to serialize to</param>
		/// <param name="value">Value to write</param>
		public static void WriteRef(this ITreeNodeWriter writer, NodeRef value)
		{
			value.Serialize(writer);
		}

		/// <summary>
		/// Writes an optional ref value to storage
		/// </summary>
		/// <param name="writer">Writer to serialize to</param>
		/// <param name="value">Value to write</param>
		public static void WriteOptionalRef(this ITreeNodeWriter writer, NodeRef? value)
		{
			if (value == null)
			{
				writer.WriteBoolean(false);
			}
			else
			{
				writer.WriteBoolean(true);
				writer.WriteRef(value);
			}
		}

		/// <summary>
		/// Serializes a single node
		/// </summary>
		/// <param name="writer">Writer for the node data</param>
		/// <param name="node">The node to write</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Handle to the written node</returns>
		public static async Task<NodeHandle> WriteNodeAsync(this IStorageWriter writer, Node node, CancellationToken cancellationToken = default)
		{
			NodeRef nodeRef = new NodeRef(node);
			return await writer.WriteAsync(nodeRef, cancellationToken);
		}

		/// <summary>
		/// Writes a node to storage
		/// </summary>
		/// <param name="store">Store instance to write to</param>
		/// <param name="name">Name of the ref containing this node</param>
		/// <param name="node">Node to be written</param>
		/// <param name="options">Options for the node writer</param>
		/// <param name="refOptions">Options for the ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Location of node targetted by the ref</returns>
		public static async Task<NodeHandle> WriteNodeAsync(this IStorageClient store, RefName name, Node node, TreeOptions? options = null, RefOptions? refOptions = null, CancellationToken cancellationToken = default)
		{
			using IStorageWriter writer = store.CreateWriter(name, options);
			return await writer.WriteAsync(name, node, refOptions, cancellationToken);
		}

		/// <summary>
		/// Reads data for a ref from the store, along with the node's contents.
		/// </summary>
		/// <param name="store">Store instance to write to</param>
		/// <param name="name">The ref name</param>
		/// <param name="cacheTime">Minimum coherency for any cached value to be returned</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>Node for the given ref, or null if it does not exist</returns>
		public static async Task<TNode?> TryReadNodeAsync<TNode>(this IStorageClient store, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default) where TNode : Node
		{
			NodeHandle? refTarget = await store.TryReadRefTargetAsync(name, cacheTime, cancellationToken);
			if (refTarget == null)
			{
				return null;
			}

			NodeReader nodeData = await refTarget.ReadAsync(cancellationToken);
			return Node.Deserialize<TNode>(nodeData);
		}

		/// <summary>
		/// Reads a ref from the store, throwing an exception if it does not exist
		/// </summary>
		/// <param name="store">Store instance to write to</param>
		/// <param name="name">Id for the ref</param>
		/// <param name="cacheTime">Minimum coherency of any cached result</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The blob instance</returns>
		public static async Task<TNode> ReadNodeAsync<TNode>(this IStorageClient store, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default) where TNode : Node
		{
			TNode? refValue = await store.TryReadNodeAsync<TNode>(name, cacheTime, cancellationToken);
			if (refValue == null)
			{
				throw new RefNameNotFoundException(name);
			}
			return refValue;
		}
	}
}
