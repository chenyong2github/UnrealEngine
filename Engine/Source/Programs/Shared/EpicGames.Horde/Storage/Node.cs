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
		/// <param name="nodeData">Data to deserialize from</param>
		/// <returns>New node instance</returns>
		public static Node Deserialize(NodeData nodeData)
		{
			NodeReader reader = new NodeReader(nodeData);
			return s_guidToDeserializer[reader.Type.Guid](reader);
		}

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
		void WriteNodeHandle(HashedNodeHandle handle);
	}

	/// <summary>
	/// Extension methods for serializing <see cref="Node"/> objects
	/// </summary>
	public static class NodeExtensions
	{
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
		public static async Task<HashedNodeHandle> WriteNodeAsync(this IStorageClient store, RefName name, Node node, TreeOptions? options = null, Utf8String prefix = default, RefOptions? refOptions = null, CancellationToken cancellationToken = default)
		{
			using TreeWriter writer = new TreeWriter(store, options, prefix.IsEmpty ? name.Text : prefix);
			return await writer.WriteAsync(name, node, refOptions, cancellationToken);
		}
	}
}
