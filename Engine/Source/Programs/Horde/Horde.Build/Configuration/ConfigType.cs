// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Build.Server;
using JsonObject = System.Text.Json.Nodes.JsonObject;

namespace Horde.Build.Configuration
{
	/// <summary>
	/// Attribute used to mark <see cref="Uri"/> properties that are relative to their containing file
	/// </summary>
	[AttributeUsage(AttributeTargets.Property)]
	public sealed class ConfigPathAttribute : Attribute
	{
	}

	/// <summary>
	/// Directive to merge config data from another source
	/// </summary>
	public class ConfigInclude
	{
		/// <summary>
		/// Path to the config data to be included. May be relative to the including file's location.
		/// </summary>
		[Required]
		public string Path { get; set; } = null!;
	}

	/// <summary>
	/// Exception thrown when reading config files
	/// </summary>
	public sealed class ConfigException : Exception
	{
		/// <summary>
		/// Path to the config file triggering the exception
		/// </summary>
		public Uri Path { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="context">Current parse context for the error</param>
		/// <param name="message">Description of the error</param>
		/// <param name="innerException">Inner exception details</param>
		internal ConfigException(ConfigContext context, string message, Exception? innerException = null)
			: base(message, innerException)
		{
			Path = context.CurrentFile;
		}
	}

	/// <summary>
	/// Base class for types that can be read from config files
	/// </summary>
	abstract class ConfigType
	{
		public abstract ValueTask<object?> ReadAsync(JsonNode? node, ConfigContext context, CancellationToken cancellationToken);

		static readonly ConcurrentDictionary<Type, ConfigType> s_typeToValueType = new ConcurrentDictionary<Type, ConfigType>();

		public static ConfigType FindOrAddValueType(Type type)
		{
			ConfigType? value;
			if (!s_typeToValueType.TryGetValue(type, out value))
			{
				if (!type.IsClass || type == typeof(string))
				{
					value = new ScalarConfigType(type);
				}
				else
				{
					value = ObjectConfigType.FindOrAdd(type);
				}

				lock (s_typeToValueType)
				{
					value = s_typeToValueType.GetOrAdd(type, value);
				}
			}
			return value;
		}

		/// <summary>
		/// Reads an object from a particular URL
		/// </summary>
		/// <typeparam name="T">Type of object to read</typeparam>
		/// <param name="uri">Location of the file to read</param>
		/// <param name="context">Context for reading</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns></returns>
		public static async Task<T> ReadAsync<T>(Uri uri, ConfigContext context, CancellationToken cancellationToken) where T : class, new()
		{
			ObjectConfigType type = ObjectConfigType.FindOrAdd(typeof(T));

			T target = new T();
			await type.MergeObjectAsync(target, uri, context, cancellationToken);
			return target;
		}

		/// <summary>
		/// Combine a relative path with a base URI to produce a new URI
		/// </summary>
		/// <param name="baseUri">Base uri to rebase relative to</param>
		/// <param name="path">Relative path</param>
		/// <returns>Absolute URI</returns>
		public static Uri CombinePaths(Uri baseUri, string path)
		{
			if (path.StartsWith("//", StringComparison.Ordinal))
			{
				if (baseUri.Scheme == PerforceConfigSource.Scheme)
				{
					return new Uri($"{PerforceConfigSource.Scheme}://{baseUri.Host}{path}");
				}
				else
				{
					return new Uri($"{PerforceConfigSource.Scheme}://{PerforceCluster.DefaultName}{path}");
				}
			}
			return new Uri(baseUri, path);
		}
	}

	/// <summary>
	/// Implementation of <see cref="ConfigType"/> for scalar values
	/// </summary>
	class ScalarConfigType : ConfigType
	{
		readonly Type _type;

		public ScalarConfigType(Type type)
		{
			if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(Nullable<>))
			{
				type = type.GetGenericArguments()[0];
			}
			_type = type;
		}

		public override ValueTask<object?> ReadAsync(JsonNode? node, ConfigContext context, CancellationToken cancellationToken)
		{
			if (node == null)
			{
				return new ValueTask<object?>((object?)null);
			}
			else
			{
				return new ValueTask<object?>(node.Deserialize(_type, context.JsonOptions));
			}
		}
	}

	/// <summary>
	/// Implementation of <see cref="ConfigType"/> to handle object variables
	/// </summary>
	class ObjectConfigType : ConfigType
	{
		abstract class Property
		{
			public string Name { get; }
			public PropertyInfo PropertyInfo { get; }

			public Property(string name, PropertyInfo propertyInfo)
			{
				Name = name;
				PropertyInfo = propertyInfo;
			}

			public abstract Task MergeAsync(object target, JsonNode? node, ConfigContext context, CancellationToken cancellationToken);
		}

		class ScalarProperty : Property
		{
			public ScalarProperty(string name, PropertyInfo propertyInfo)
				: base(name, propertyInfo)
			{
			}

			public override Task MergeAsync(object target, JsonNode? node, ConfigContext context, CancellationToken cancellationToken)
			{
				context.AddProperty(Name);

				object? value;
				if (PropertyInfo.GetCustomAttribute<ConfigPathAttribute>() != null)
				{
					value = CombinePaths(context.CurrentFile, JsonSerializer.Deserialize<string>(node, context.JsonOptions) ?? String.Empty).AbsoluteUri;
				}
				else
				{
					value = JsonSerializer.Deserialize(node, PropertyInfo.PropertyType, context.JsonOptions);
				}
				PropertyInfo.SetValue(target, value);

				return Task.CompletedTask;
			}
		}

		class ListProperty : Property
		{
			readonly ConfigType _elementType;

			public ListProperty(string name, PropertyInfo propertyInfo, ConfigType elementType)
				: base(name, propertyInfo)
			{
				_elementType = elementType;
			}

			public override async Task MergeAsync(object target, JsonNode? node, ConfigContext context, CancellationToken cancellationToken)
			{
				IList? list = (IList?)PropertyInfo.GetValue(target);
				if(list == null)
				{
					object value = Activator.CreateInstance(PropertyInfo.PropertyType)!;
					PropertyInfo.SetValue(target, value);
					list = (IList)value;
				}

				JsonArray array = (JsonArray)node!;
				foreach (JsonNode? element in array)
				{
					context.EnterScope($"{Name}[{list.Count}]");

					object? elementValue = await _elementType.ReadAsync(element, context, cancellationToken);
					list.Add(elementValue);

					context.LeaveScope();
				}
			}
		}

		class ObjectProperty : Property
		{
			readonly ObjectConfigType _classConfigType;

			public ObjectProperty(string name, PropertyInfo propertyInfo, ObjectConfigType classConfigType)
				: base(name, propertyInfo)
			{
				_classConfigType = classConfigType;
			}

			public override async Task MergeAsync(object target, JsonNode? node, ConfigContext context, CancellationToken cancellationToken)
			{
				if (node is JsonObject obj)
				{
					Uri? otherFile;
					context.TryAddProperty(Name, out otherFile);

					context.EnterScope(Name);

					object? childTarget = PropertyInfo.GetValue(target);
					if (childTarget == null)
					{
						if (otherFile != null)
						{
							throw new ConfigException(context, $"Property {context.CurrentScope}{Name} conflicts with value in {otherFile}.");
						}

						childTarget = await _classConfigType.ReadAsync(node, context, cancellationToken);
						PropertyInfo.SetValue(target, childTarget);
					}
					else
					{
						await _classConfigType.MergeObjectAsync(childTarget, obj, context, cancellationToken);
					}

					context.LeaveScope();
				}
				else
				{
					context.AddProperty(Name);
					PropertyInfo.SetValue(target, JsonSerializer.Deserialize(node, PropertyInfo.PropertyType, context.JsonOptions));
				}
			}
		}

		readonly Type _type;
		readonly Dictionary<string, PropertyInfo> _nameToIncludeProperty = new Dictionary<string, PropertyInfo>(StringComparer.OrdinalIgnoreCase);
		readonly Dictionary<string, Property> _nameToProperty = new Dictionary<string, Property>(StringComparer.OrdinalIgnoreCase);
		readonly Dictionary<string, ObjectConfigType>? _knownTypes;

		static readonly ConcurrentDictionary<Type, ObjectConfigType> s_typeToObjectValueType = new ConcurrentDictionary<Type, ObjectConfigType>();

		public ObjectConfigType(Type type)
		{
			s_typeToObjectValueType.TryAdd(type, this);

			_type = type;

			foreach (PropertyInfo propertyInfo in type.GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.GetProperty))
			{
				JsonPropertyNameAttribute? attribute = propertyInfo.GetCustomAttribute<JsonPropertyNameAttribute>();
				string name = attribute?.Name ?? propertyInfo.Name;

				if (propertyInfo.PropertyType == typeof(List<ConfigInclude>))
				{
					_nameToIncludeProperty.Add(name, propertyInfo);
				}
				else
				{
					_nameToProperty.Add(name, CreateProperty(name, propertyInfo));
				}
			}

			JsonKnownTypesAttribute? knownTypes = _type.GetCustomAttribute<JsonKnownTypesAttribute>();
			if (knownTypes != null)
			{
				_knownTypes = new Dictionary<string, ObjectConfigType>(StringComparer.Ordinal);
				foreach (Type knownType in knownTypes.Types)
				{
					ObjectConfigType knownConfigType = FindOrAdd(knownType);
					foreach (JsonDiscriminatorAttribute discriminatorAttribute in knownType.GetCustomAttributes(typeof(JsonDiscriminatorAttribute), true))
					{
						_knownTypes.Add(discriminatorAttribute.Name, knownConfigType);
					}
				}
			}
		}

		public static ObjectConfigType FindOrAdd(Type type)
		{
			ObjectConfigType? value;
			if (!s_typeToObjectValueType.TryGetValue(type, out value))
			{
				lock (s_typeToObjectValueType)
				{
					if (!s_typeToObjectValueType.TryGetValue(type, out value))
					{
						value = new ObjectConfigType(type);
					}
				}
			}
			return value;
		}

		static Property CreateProperty(string name, PropertyInfo propertyInfo)
		{
			Type propertyType = propertyInfo.PropertyType;
			if (!propertyType.IsClass || propertyType == typeof(string))
			{
				return new ScalarProperty(name, propertyInfo);
			}
			else
			{
				if (propertyType.IsGenericType && propertyType.GetGenericTypeDefinition() == typeof(List<>))
//				if (propertyType.IsAssignableTo(typeof(IList)))
				{
	//				foreach (Type type in propertyType.GetInterfaces())
	//				{
	//					if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(IList<>))
	//					{
//							Type elementType = type.GetGenericArguments()[0];
					Type elementType = propertyType.GetGenericArguments()[0];
					return new ListProperty(name, propertyInfo, FindOrAddValueType(elementType));
//						}
//					}
				}
				return new ObjectProperty(name, propertyInfo, FindOrAdd(propertyType));
			}
		}

		public override async ValueTask<object?> ReadAsync(JsonNode? node, ConfigContext context, CancellationToken cancellationToken)
		{
			if (node == null)
			{
				throw new ConfigException(context, "Unable to deserialize object from null value");
			}
			else if (node is JsonObject obj)
			{
				return await ReadAsync(obj, context, cancellationToken);
			}
			else
			{
				return JsonSerializer.Deserialize(node, _type, context.JsonOptions);
			}
		}

		public ValueTask<object> ReadAsync(JsonObject obj, ConfigContext context, CancellationToken cancellationToken)
		{
			ObjectConfigType targetType = this;
			if (_knownTypes != null && obj.TryGetPropertyValue("Type", out JsonNode? knownTypeNode) && knownTypeNode != null)
			{
				targetType = _knownTypes[knownTypeNode.ToString()];
			}
			return targetType.ReadConcreteTypeAsync(obj, context, cancellationToken);
		}

		async ValueTask<object> ReadConcreteTypeAsync(JsonObject obj, ConfigContext context, CancellationToken cancellationToken)
		{
			object result = Activator.CreateInstance(_type)!;
			await MergeObjectAsync(result, obj, context, cancellationToken);
			return result;
		}

		static async ValueTask<JsonObject> ReadFileAsync(Uri uri, ConfigContext context, CancellationToken cancellationToken)
		{
			IConfigSource? source = context.Sources[uri.Scheme];
			if (source == null)
			{
				throw new ConfigException(context, $"Invalid/unknown scheme for config file {uri}");
			}

			IConfigData? file;
			if (!context.Files.TryGetValue(uri, out file))
			{
				file = await source.GetAsync(uri, cancellationToken);
				context.Files.Add(uri, file);
			}

			ReadOnlyMemory<byte> data = await file.ReadAsync(cancellationToken);

			JsonObject? obj = JsonSerializer.Deserialize<JsonObject>(data.Span, context.JsonOptions);
			if (obj == null)
			{
				throw new ConfigException(context, $"Config file {uri} contains a null object.");
			}

			return obj;
		}

		public async Task MergeObjectAsync(object target, Uri uri, ConfigContext context, CancellationToken cancellationToken)
		{
			if (context.IncludeStack.Contains(uri))
			{
				throw new ConfigException(context, $"Recursive include of file {uri}");
			}

			context.IncludeStack.Push(uri);

			JsonObject obj = await ReadFileAsync(uri, context, cancellationToken);
			await MergeObjectAsync(target, obj, context, cancellationToken);

			context.IncludeStack.Pop();
		}

		async Task MergeObjectAsync(object target, JsonObject obj, ConfigContext context, CancellationToken cancellationToken)
		{
			// If it's a BaseConfig object, read the parent config files recursively first
			foreach ((string name, JsonNode? node) in obj)
			{
				if (_nameToIncludeProperty.TryGetValue(name, out PropertyInfo? propertyInfo))
				{
					List<ConfigInclude>? includes = node.Deserialize<List<ConfigInclude>>(context.JsonOptions);
					if (includes != null)
					{
						List<ConfigInclude> targetIncludes = (List<ConfigInclude>)propertyInfo.GetValue(target)!;
						foreach (ConfigInclude include in includes)
						{
							Uri uri = ConfigType.CombinePaths(context.CurrentFile, include.Path);
							context.IncludeStack.Push(uri);

							JsonObject nextObj = await ReadFileAsync(uri, context, cancellationToken);
							await MergeObjectAsync(target, nextObj, context, cancellationToken);

							context.IncludeStack.Pop();
							targetIncludes.Add(include);
						}
					}
				}
			}

			// Parse all the properties into this object
			foreach ((string name, JsonNode? node) in obj)
			{
				if (_nameToProperty.TryGetValue(name, out Property? property))
				{
					await property.MergeAsync(target, node, context, cancellationToken);
				}
			}
		}
	}
}
