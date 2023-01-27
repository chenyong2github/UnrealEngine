// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using EpicGames.Core;
using Horde.Build.Projects;
using Horde.Build.Server;
using Horde.Build.Streams;
using Horde.Build.Utilities;
using Microsoft.Extensions.Logging;

namespace Horde.Build.Commands.Config
{
	[Command("config", "docs", "Writes Markdown docs for server settings")]
	class DocsCommand : Command
	{
		[CommandLine]
		DirectoryReference? _outputDir = null!;

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			_outputDir ??= DirectoryReference.Combine(Program.AppDir, "Docs");
			DirectoryReference.CreateDirectory(_outputDir);

			JsonSchema serverSchema = Schemas.CreateSchema(typeof(ServerSettings));
			JsonSchema globalSchema = Schemas.CreateSchema(typeof(GlobalConfig));
			JsonSchema projectSchema = Schemas.CreateSchema(typeof(ProjectConfig));
			JsonSchema streamSchema = Schemas.CreateSchema(typeof(StreamConfig));

			JsonSchemaType[] allTypes = { serverSchema.RootType, globalSchema.RootType, projectSchema.RootType, streamSchema.RootType };

			await WriteDocAsync(serverSchema.RootType, "Server Config", "Config-Server.md", allTypes);
			await WriteDocAsync(globalSchema.RootType, "Global Config", "Config-Globals.md", allTypes);
			await WriteDocAsync(projectSchema.RootType, "Project Config", "Config-Projects.md", allTypes);
			await WriteDocAsync(streamSchema.RootType, "Stream Config", "Config-Streams.md", allTypes);

			return 0;
		}

		class ObjectQueue
		{
			readonly Stack<JsonSchemaType> _stack = new Stack<JsonSchemaType>();
			readonly HashSet<string> _visited = new HashSet<string>(StringComparer.Ordinal);

			public void Add(JsonSchemaType type)
			{
				if (type.Name != null && _visited.Add(type.Name))
				{
					_stack.Push(type);
				}
			}

			public void Ignore(JsonSchemaType type)
			{
				if (type.Name != null)
				{
					_visited.Add(type.Name);
				}
			}

			public bool TryPop([NotNullWhen(true)] out JsonSchemaType? obj) => _stack.TryPop(out obj);
		}

		async Task WriteDocAsync(JsonSchemaType rootType, string title, string fileName, IEnumerable<JsonSchemaType> ignoreTypes)
		{
			FileReference file = FileReference.Combine(_outputDir!, fileName);
			using (FileStream stream = FileReference.Open(file, FileMode.Create, FileAccess.Write))
			{
				using (StreamWriter writer = new StreamWriter(stream))
				{
					await writer.WriteLineAsync($"# {title}");

					HashSet<JsonSchemaType> visitedTypes = new HashSet<JsonSchemaType>(ignoreTypes);
					visitedTypes.Remove(rootType);

					List<JsonSchemaType> types = new List<JsonSchemaType>();
					FindCustomTypes(rootType, types, visitedTypes);

					foreach (JsonSchemaType schemaType in types)
					{
						await writer.WriteLineAsync();

						if (schemaType != rootType)
						{
							await writer.WriteLineAsync($"## {GetHeadingName(schemaType)}");
							await writer.WriteLineAsync();
						}

						if (schemaType.Description != null)
						{
							await writer.WriteLineAsync(schemaType.Description);
							await writer.WriteLineAsync();
						}

						if (schemaType is JsonSchemaObject schemaObj)
						{
							await writer.WriteLineAsync("Name | Type | Description");
							await writer.WriteLineAsync("---- | ---- | -----------");

							foreach (JsonSchemaProperty property in schemaObj.Properties)
							{
								string name = property.CamelCaseName;
								string type = GetType(property.Type);
								string description = GetMarkdownDescription(property.Description);
								await writer.WriteLineAsync($"`{name}` | {type} | {description}");
							}
						}
						else if (schemaType is JsonSchemaEnum schemaEnum)
						{
							await writer.WriteLineAsync("Name | Description");
							await writer.WriteLineAsync("---- | -----------");

							for(int idx = 0; idx < schemaEnum.Values.Count; idx++)
							{
								string name = schemaEnum.Values[idx];
								string description = GetMarkdownDescription(schemaEnum.Descriptions[idx]);
								await writer.WriteLineAsync($"`{name}` | {description}");
							}
						}
						else
						{
							throw new NotImplementedException();
						}
					}
				}
			}


			await Task.Delay(1);
		}

		static void FindCustomTypes(JsonSchemaType type, List<JsonSchemaType> types, HashSet<JsonSchemaType> visited)
		{
			if (visited.Add(type))
			{
				switch (type)
				{
					case JsonSchemaOneOf oneOf:
						foreach (JsonSchemaType oneOfType in oneOf.Types)
						{
							FindCustomTypes(oneOfType, types, visited);
						}
						break;
					case JsonSchemaArray array:
						FindCustomTypes(array.ItemType, types, visited);
						break;
					case JsonSchemaEnum _:
						if (type.Name != null)
						{
							types.Add(type);
						}
						break;
					case JsonSchemaObject obj:
						if (type.Name != null)
						{
							types.Add(type);
						}
						foreach (JsonSchemaProperty property in obj.Properties)
						{
							FindCustomTypes(property.Type, types, visited);
						}
						break;
				}
			}
		}

		static string GetType(JsonSchemaType type)
		{
			switch (type)
			{
				case JsonSchemaBoolean _:
					return "`boolean`";
				case JsonSchemaInteger _:
					return "`integer`";
				case JsonSchemaNumber _:
					return "`number`";
				case JsonSchemaString _:
					return "`string`";
				case JsonSchemaOneOf oneOf:
					return String.Join("/", oneOf.Types.Select(x => GetType(x)));
				case JsonSchemaArray array:
					string elementType = GetType(array.ItemType);
					if (elementType.EndsWith("`", StringComparison.Ordinal))
					{
						return elementType.Insert(elementType.Length - 1, "[]");
					}
					else
					{
						return elementType + "`[]`";
					}
				case JsonSchemaEnum _:
				case JsonSchemaObject _:
					if (type.Name == null)
					{
						return "`object`";
					}
					else
					{
						return $"[`{type.Name}`](#{GetAnchorName(type)})";
					}
				default:
					return type.GetType().Name;
			}
		}

		static string GetMarkdownDescription(string? description)
		{
			return (description ?? String.Empty).Replace("\n", "<br>", StringComparison.Ordinal);
		}

		static string GetHeadingName(JsonSchemaType type)
		{
			if (type.Name == null)
			{
				throw new NotImplementedException("Unknown type");
			}

			switch (type)
			{
				case JsonSchemaEnum _:
					return $"{type.Name} (Enum)";
				default:
					return type.Name;
			}
		}

		[SuppressMessage("Globalization", "CA1308:Normalize strings to uppercase")]
		static string GetAnchorName(JsonSchemaType type)
		{
			string anchor = GetHeadingName(type).ToLowerInvariant();
			anchor = Regex.Replace(anchor, @"[^a-z0-9]+", "-");
			return anchor.Trim('-');
		}
	}
}
