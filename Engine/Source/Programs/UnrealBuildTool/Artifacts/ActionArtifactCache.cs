// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using UnrealBuildBase;

namespace UnrealBuildTool.Artifacts
{

	/// <summary>
	/// Generic handler for artifacts
	/// </summary>
	internal class ActionArtifactCache : IActionArtifactCache
	{

		/// <inheritdoc/>
		public bool EnableReads { get; set; } = true;

		/// <inheritdoc/>
		public bool EnableWrites { get; set; } = true;

		/// <inheritdoc/>
		public bool LogCacheMisses { get; set; } = false;

		/// <inheritdoc/>
		public IArtifactCache ArtifactCache { get; init; }

		/// <summary>
		/// Logging device
		/// </summary>
		private readonly ILogger _logger;

		/// <summary>
		/// Cache of dependency files.
		/// </summary>
		private readonly CppDependencyCache _cppDependencyCache;

		/// <summary>
		/// Cache for file hashes
		/// </summary>
		private readonly FileHasher _fileHasher;

		/// <summary>
		/// Construct a new artifact handler object
		/// </summary>
		/// <param name="artifactCache">Artifact cache instance</param>
		/// <param name="cppDependencyCache">Previously created dependency cache</param>
		/// <param name="logger">Logging device</param>
		private ActionArtifactCache(IArtifactCache artifactCache, CppDependencyCache cppDependencyCache, ILogger logger)
		{
			_logger = logger;
			_cppDependencyCache = cppDependencyCache;
			ArtifactCache = artifactCache;
			_fileHasher = new(NullLogger.Instance);
		}

		/// <summary>
		/// Create a new action artifact cache using horde file based storage
		/// </summary>
		/// <param name="directory">Directory for the cache</param>
		/// <param name="cppDependencyCache">Previously created dependency cache</param>
		/// <param name="logger">Logging device</param>
		/// <returns>Action artifact cache object</returns>
		public static IActionArtifactCache CreateHordeFileCache(DirectoryReference directory, CppDependencyCache cppDependencyCache, ILogger logger)
		{
			IArtifactCache artifactCache = HordeStorageArtifactCache.CreateFileCache(directory, /*logger*/ NullLogger.Instance, false);
			return new ActionArtifactCache(artifactCache, cppDependencyCache, logger);
		}

		/// <summary>
		/// Create a new action artifact cache using horde memory based storage
		/// </summary>
		/// <param name="cppDependencyCache">Previously created dependency cache</param>
		/// <param name="logger">Logging device</param>
		/// <returns>Action artifact cache object</returns>
		public static IActionArtifactCache CreateHordeMemoryCache(CppDependencyCache cppDependencyCache, ILogger logger)
		{
			IArtifactCache artifactCache = HordeStorageArtifactCache.CreateMemoryCache(logger);
			return new ActionArtifactCache(artifactCache, cppDependencyCache, logger);
		}

		/// <inheritdoc/>
		public async Task<bool> CompleteActionFromCacheAsync(LinkedAction action, CancellationToken cancellationToken)
		{
			if (!EnableReads)
			{
				return false;
			}

			IoHash key = await GetKeyAsync(action);

			ArtifactMapping[] mappings = await ArtifactCache.QueryArtifactMappingsAsync(new IoHash[] { key }, cancellationToken);

			string actionDescription = string.Empty;
			if (LogCacheMisses)
			{
				actionDescription = $"{(action.CommandDescription ?? action.CommandPath.GetFileNameWithoutExtension())} {action.StatusDescription}".Trim();
			}

			if (mappings.Length == 0)
			{
				if (LogCacheMisses)
				{
					_logger.LogInformation("Artifact Cache Miss: No artifact mappings found for {ActionDescription}", actionDescription);
				}
				return false;
			}

			foreach (ArtifactMapping mapping in mappings)
			{
				bool match = true;

				foreach (Artifact input in mapping.Inputs)
				{
					string name = input.Name.ToString();
					FileItem item = FileItem.GetItemByPath(name);
					if (!item.Exists)
					{
						if (LogCacheMisses)
						{
							_logger.LogInformation("Artifact Cache Miss: Input file missing {ActionDescription}/{File}", actionDescription, item.FullName);
						}
						match = false;
						break;
					}
					if (input.ContentHash != await _fileHasher.GetDigestAsync(item, cancellationToken))
					{
						if (LogCacheMisses)
						{
							_logger.LogInformation("Artifact Cache Miss: Content hash different {actionDescription}/{File}", actionDescription, item.FullName);
						}
						match = false;
						break;
					}
				}

				if (match)
				{
					bool[]? readResults = await ArtifactCache.QueryArtifactOutputsAsync(new[] { mapping }, cancellationToken);

					if (readResults == null || readResults.Length == 0 || !readResults[0])
					{
						return false;
					}
					else
					{
						foreach (Artifact output in mapping.Outputs)
						{
							string outputName = output.Name.ToString();
							FileItem item = FileItem.GetItemByPath(outputName);
							item.ResetCachedInfo(); // newly created outputs need refreshing
							_fileHasher.SetDigest(item, output.ContentHash);
						}
						return true;
					}
				}
			}
			return false;
		}

		/// <inheritdoc/>
		public async Task ActionCompleteAsync(LinkedAction action, CancellationToken cancellationToken)
		{
			if (!EnableWrites)
			{
				return;
			}

			ArtifactMapping[] mappings = new ArtifactMapping[] { await CreateArtifactMappingAsync(action, cancellationToken) };
			await ArtifactCache.SaveArtifactMappingsAsync(mappings, cancellationToken);
		}

		/// <inheritdoc/>
		public async Task FlushChangesAsync(CancellationToken cancellationToken)
		{
			await ArtifactCache.FlushChangesAsync(cancellationToken);
		}

		/// <summary>
		/// Create a new artifact mapping that represents the input and output of the action
		/// </summary>
		/// <param name="action">Source action</param>
		/// <param name="cancellationToken">Cancellation token</param>
		/// <returns>Artifact mapping</returns>
		private async Task<ArtifactMapping> CreateArtifactMappingAsync(LinkedAction action, CancellationToken cancellationToken)
		{
			(IoHash key, IoHash mappingKey) = await GetKeyAndMappingKeyAsync(action);

			// We gather the output files first to make sure that all the generated files (including the dependency file) gets
			// their cached FileInfo reset.
			List<Artifact> outputs = new();
			foreach (FileItem output in action.ProducedItems)
			{
				output.ResetCachedInfo();
				IoHash hash = await _fileHasher.GetDigestAsync(output, cancellationToken);
				Artifact artifact = new(ArtifactDirectoryTree.Absolute, ArtifactType.Source, new Utf8String(output.AbsolutePath), hash);
				outputs.Add(artifact);
			}

			List<Artifact> inputs = new();
			foreach (FileItem input in action.PrerequisiteItems)
			{
				IoHash hash = await _fileHasher.GetDigestAsync(input, cancellationToken);
				Artifact artifact = new(ArtifactDirectoryTree.Absolute, ArtifactType.Source, new Utf8String(input.AbsolutePath), hash);
				inputs.Add(artifact);
			}

			if (action.DependencyListFile != null)
			{
				if (_cppDependencyCache.TryGetDependencies(action.DependencyListFile, _logger, out List<FileItem>? depedencies))
				{
					foreach (FileItem dep in depedencies)
					{
						IoHash hash = await _fileHasher.GetDigestAsync(dep, cancellationToken);
						Artifact artifact = new(ArtifactDirectoryTree.Absolute, ArtifactType.Source, new Utf8String(dep.AbsolutePath), hash);
						inputs.Add(artifact);
					}
				}
			}

			return new(key, mappingKey, inputs.ToArray(), outputs.ToArray());
		}

		/// <summary>
		/// Get the key has for the action
		/// </summary>
		/// <param name="action">Source action</param>
		/// <returns>Task returning the key</returns>
		private async Task<IoHash> GetKeyAsync(LinkedAction action)
		{
			StringBuilder builder = new();
			await AppendKeyAsync(builder, action);
			IoHash key = IoHash.Compute(new Utf8String(builder.ToString()));
			return key;
		}

		/// <summary>
		/// Generate the key and mapping key hashes for the action
		/// </summary>
		/// <param name="action">Source action</param>
		/// <returns>Task object with the key and mapping key</returns>
		private async Task<(IoHash, IoHash)> GetKeyAndMappingKeyAsync(LinkedAction action)
		{
			StringBuilder builder = new();
			await AppendKeyAsync(builder, action);
			IoHash key = IoHash.Compute(new Utf8String(builder.ToString()));
			await AppendMappingKeyAsync(builder, action);
			IoHash mappingKey = IoHash.Compute(new Utf8String(builder.ToString()));
			return (key, mappingKey);
		}

		/// <summary>
		/// Generate the lookup key.  This key is generated from the action's inputs.
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="action">Source action</param>
		/// <returns>Task object</returns>
		private async Task AppendKeyAsync(StringBuilder builder, LinkedAction action)
		{
			builder.AppendLine(action.CommandVersion);
			builder.AppendLine(action.CommandArguments);

			FileItem[] inputs = action.PrerequisiteItems.ToArray();
			Task<IoHash>[] waits = new Task<IoHash>[inputs.Length];
			for (int index = 0; index < inputs.Length; index++)
			{
				builder.AppendLine(inputs[index].FullName);
				waits[index] = _fileHasher.GetDigestAsync(inputs[index]);
			}
			await Task.WhenAll(waits);
			foreach (Task<IoHash> wait in waits)
			{
				builder.AppendLine(wait.Result.ToString());
			}
		}

		/// <summary>
		/// Generate the full mapping key.  This contains the hashes for the action's inputs and the dependent files.
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="action">Source action</param>
		/// <returns>Task object</returns>
		private async Task AppendMappingKeyAsync(StringBuilder builder, LinkedAction action)
		{
			if (action.DependencyListFile != null)
			{
				if (_cppDependencyCache.TryGetDependencies(action.DependencyListFile, _logger, out List<FileItem>? dependencies))
				{
					Task<IoHash>[] waits = new Task<IoHash>[dependencies.Count];
					for (int index = 0; index < dependencies.Count; index++)
					{
						builder.AppendLine(dependencies[index].FullName);
						waits[index] = _fileHasher.GetDigestAsync(dependencies[index]);
					}
					await Task.WhenAll(waits);
					foreach (Task<IoHash> wait in waits)
					{
						builder.AppendLine(wait.Result.ToString());
					}
				}
			}
		}
	}
}
