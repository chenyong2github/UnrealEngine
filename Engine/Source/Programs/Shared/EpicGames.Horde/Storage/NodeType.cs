// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Concurrent;
using System.Globalization;
using System.Reflection;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Attribute used to define a factory for a particular node type
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public sealed class NodeTypeAttribute : Attribute
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
	/// Information about a type within a bundle
	/// </summary>
	/// <param name="Guid">Nominal identifier for the type</param>
	/// <param name="Version">Version number for the serializer</param>
	public record class NodeType(Guid Guid, int Version)
	{
		/// <summary>
		/// Parse a span of characters as a bundle type
		/// </summary>
		/// <param name="text">Text to parse</param>
		/// <returns>The parsed bundle type</returns>
		public static NodeType Parse(ReadOnlySpan<char> text)
		{
			int hashIdx = text.IndexOf('#');
			Guid guid = Guid.Parse(text.Slice(0, hashIdx));
			int version = Int32.Parse(text.Slice(hashIdx + 1), NumberStyles.None, CultureInfo.InvariantCulture);
			return new NodeType(guid, version);
		}

		/// <inheritdoc/>
		public override string ToString() => $"{Guid}#{Version}";

		#region Static Methods

		/// <summary>
		/// Cache of constructed <see cref="NodeType"/> instances.
		/// </summary>
		static readonly ConcurrentDictionary<Type, NodeType> s_typeToBundleType = new ConcurrentDictionary<Type, NodeType>();

		/// <summary>
		/// Gets the <see cref="NodeType"/> instance for a particular node type
		/// </summary>
		/// <param name="type"></param>
		/// <returns>Bundle</returns>
		public static NodeType Get(Type type)
		{
			NodeType? bundleType;
			if (!s_typeToBundleType.TryGetValue(type, out bundleType))
			{
				NodeTypeAttribute? attribute = type.GetCustomAttribute<NodeTypeAttribute>();
				if (attribute == null)
				{
					throw new InvalidOperationException($"Missing {nameof(NodeTypeAttribute)} from type {type.Name}");
				}
				bundleType = s_typeToBundleType.GetOrAdd(type, new NodeType(Guid.Parse(attribute.Guid), attribute.Version));
			}
			return bundleType;
		}

		/// <summary>
		/// Gets the <see cref="NodeType"/> instance for a particular node type
		/// </summary>
		/// <typeparam name="T">Type of the node</typeparam>
		public static NodeType Get<T>() where T : Node => Get(typeof(T));

		#endregion
	}
}
