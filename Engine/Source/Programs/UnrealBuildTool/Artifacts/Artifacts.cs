// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealBuildTool.Artifacts
{

	/// <summary>
	/// Artifacts can exist in different directory roots.
	/// </summary>
	public enum ArtifactDirectoryTree : byte
	{

		/// <summary>
		/// Absolute path
		/// </summary>
		Absolute,

		/// <summary>
		/// Input/Output exists in the engine directory tree
		/// </summary>
		Engine,

		/// <summary>
		/// Input/Outputs exists in the project directory tree
		/// </summary>
		Project,
	}

	/// <summary>
	/// Type of the artifact
	/// </summary>
	public enum ArtifactType : byte
	{

		/// <summary>
		/// Artifact is a source file
		/// </summary>
		Source,

		/// <summary>
		/// Artifact is an object file
		/// </summary>
		Object,
	}

	/// <summary>
	/// Represents a single artifact
	/// </summary>
	public readonly struct Artifact
	{

		/// <summary>
		/// Directory tree containing the artifact
		/// </summary>
		public readonly ArtifactDirectoryTree Tree;

		/// <summary>
		/// Type of the artifact
		/// </summary>
		public readonly ArtifactType Type;

		/// <summary>
		/// Name of the artifact
		/// </summary>
		public readonly Utf8String Name;

		/// <summary>
		/// Hash of the artifact contents
		/// </summary>
		public readonly IoHash ContentHash;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="tree">Directory tree containing the artifact</param>
		/// <param name="type">Type of the artifact</param>
		/// <param name="name">Name of the artifact</param>
		/// <param name="contentHash">Hash of the artifact contents</param>
		public Artifact(ArtifactDirectoryTree tree, ArtifactType type, Utf8String name, IoHash contentHash)
		{
			Tree = tree;
			Type = type;
			Name = name;
			ContentHash = contentHash;
		}
	}

	/// <summary>
	/// Given a set of inputs, provide a list of outputs
	/// </summary>
	public readonly struct ArtifactMapping : IEquatable<ArtifactMapping>
	{

		/// <summary>
		/// The hash of the primary input and the environment
		/// </summary>
		public readonly IoHash Key;

		/// <summary>
		/// The unique hash for all inputs and the environment
		/// </summary>
		public readonly IoHash MappingKey;

		/// <summary>
		/// Information about all inputs
		/// </summary>
		public readonly Artifact[] Inputs;

		/// <summary>
		/// Information about all outputs
		/// </summary>
		public readonly Artifact[] Outputs;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="key">The hash of the primary input and the environment</param>
		/// <param name="mappingKey">The unique hash for all inputs and the environment</param>
		/// <param name="inputs">Information about all inputs</param>
		/// <param name="outputs">Information about all outputs</param>
		public ArtifactMapping(IoHash key, IoHash mappingKey, Artifact[] inputs, Artifact[] outputs)
		{
			Key = key;
			MappingKey = mappingKey;
			Inputs = inputs;
			Outputs = outputs;
		}

		/// <inheritdoc/>
		public bool Equals(ArtifactMapping other)
		{
			return Key == other.Key && MappingKey == other.MappingKey && Inputs.SequenceEqual(other.Inputs) && Outputs.SequenceEqual(other.Outputs);
		}

		/// <inheritdoc/>
		public override bool Equals(object? obj)
		{
			return obj is ArtifactMapping artifactMapping && Equals(artifactMapping);
		}

		/// <inheritdoc/>
		public override int GetHashCode()
		{
			return HashCode.Combine(Key.GetHashCode(), MappingKey.GetHashCode(), Inputs.GetHashCode(), Outputs.GetHashCode()); 
		}

		/// <inheritdoc/>
		public static bool operator ==(ArtifactMapping left, ArtifactMapping right)
		{
			return left.Equals(right);
		}

		/// <inheritdoc/>
		public static bool operator !=(ArtifactMapping left, ArtifactMapping right)
		{
			return !(left == right);
		}
	}

	/// <summary>
	/// Collection of helper extension methods for working with IMemoryReader/Writer
	/// </summary>
	static class ArtifactSerializationExtensions
	{

		/// <summary>
		/// Read an artifact structure
		/// </summary>
		/// <param name="reader">Source reader</param>
		/// <returns>Read artifact structure</returns>
		public static Artifact ReadArtifact(this IMemoryReader reader)
		{
			ArtifactDirectoryTree tree = (ArtifactDirectoryTree)reader.ReadUInt8();
			ArtifactType type = (ArtifactType)reader.ReadUInt8();
			Utf8String name = reader.ReadUtf8String();
			IoHash contentHash = reader.ReadIoHash();
			return new Artifact(tree, type, name, contentHash);
		}

		/// <summary>
		/// Write an artifact structure
		/// </summary>
		/// <param name="writer">Destination writer</param>
		/// <param name="artifact">Artifact to be written</param>
		public static void WriteArtifact(this IMemoryWriter writer, Artifact artifact)
		{
			writer.WriteUInt8((byte)artifact.Tree);
			writer.WriteUInt8((byte)artifact.Type);
			writer.WriteUtf8String(artifact.Name);
			writer.WriteIoHash(artifact.ContentHash);
		}

		/// <summary>
		/// Read an artifact mapping structure
		/// </summary>
		/// <param name="reader">Source reader</param>
		/// <returns>Read artifact mapping structure</returns>
		public static ArtifactMapping ReadArtifactMapping(this IMemoryReader reader)
		{
			IoHash key = reader.ReadIoHash();
			IoHash mappingKey = reader.ReadIoHash();
			Artifact[] inputs = reader.ReadVariableLengthArray(() => reader.ReadArtifact());
			Artifact[] outputs = reader.ReadVariableLengthArray(() => reader.ReadArtifact());
			return new ArtifactMapping(key, mappingKey, inputs, outputs);
		}

		/// <summary>
		/// Write an artifact mapping structure
		/// </summary>
		/// <param name="writer">Destination writer</param>
		/// <param name="artifactMapping">Artifact mapping to be written</param>
		public static void WriteArtifactMapping(this IMemoryWriter writer, ArtifactMapping artifactMapping)
		{
			writer.WriteIoHash(artifactMapping.Key);
			writer.WriteIoHash(artifactMapping.MappingKey);
			writer.WriteVariableLengthArray(artifactMapping.Inputs, x => writer.WriteArtifact(x));
			writer.WriteVariableLengthArray(artifactMapping.Outputs, x => writer.WriteArtifact(x));
		}
	}

	/// <summary>
	/// Represents the state of the cache.  It is expect that after construction, the cache
	/// can be in pending state, but this is optional. From the pending state, the cache 
	/// becomes available or unavailable.
	/// </summary>
	public enum ArtifactCacheState
	{

		/// <summary>
		/// The cache is still initializing
		/// </summary>
		Pending,

		/// <summary>
		/// There has been some form of cache failure and it is unavailable
		/// </summary>
		Unavailable,

		/// <summary>
		/// The cache is functional and ready to process requests
		/// </summary>
		Available,
	}

	/// <summary>
	/// Interface for querying and adding artifacts
	/// </summary>
	public interface IArtifactCache
	{

		/// <summary>
		/// Return true if the cache is ready to process requests
		/// </summary>
		/// <returns></returns>
		public ArtifactCacheState State { get; }

		/// <summary>
		/// Return task that waits for the cache to be ready
		/// </summary>
		/// <returns>State of the artifact cache</returns>
		public Task<ArtifactCacheState> WaitForReadyAsync();

		/// <summary>
		/// Given a collection of partial keys return all matching mappings
		/// </summary>
		/// <param name="partialKeys">Source file key</param>
		/// <param name="cancellationToken">Token to be used to cancel operations</param>
		/// <returns>Collection of all known artifacts</returns>
		public Task<ArtifactMapping[]> QueryArtifactMappingsAsync(IoHash[] partialKeys, CancellationToken cancellationToken);

		/// <summary>
		/// Query the actual contents of a group of artifacts
		/// </summary>
		/// <param name="artifactMappings">Mappings to be read</param>
		/// <param name="cancellationToken">Token to be used to cancel operations</param>
		/// <returns>Dictionary of the artifacts</returns>
		public Task<bool[]?> QueryArtifactOutputsAsync(ArtifactMapping[] artifactMappings, CancellationToken cancellationToken);

		/// <summary>
		/// Save new artifact to the cache
		/// </summary>
		/// <param name="artifactMappings">Collection of artifacts to be saved</param>
		/// <param name="cancellationToken">Token to be used to cancel operations</param>
		/// <returns>Asynchronous task objects</returns>
		public Task SaveArtifactMappingsAsync(ArtifactMapping[] artifactMappings, CancellationToken cancellationToken);

		/// <summary>
		/// Flush all updates in the cache asynchronously.
		/// </summary>
		/// <param name="cancellationToken">Token to be used to cancel operations</param>
		/// <returns>Asynchronous task objects</returns>
		public Task FlushChangesAsync(CancellationToken cancellationToken);
	}

	/// <summary>
	/// Interface for action specific support of artifacts.
	/// </summary>
	interface IActionArtifactCache
	{

		/// <summary>
		/// Underlying artifact cache
		/// </summary>
		public IArtifactCache ArtifactCache { get; }

		/// <summary>
		/// If true, reads will be serviced
		/// </summary>
		public bool EnableReads { get; set; }

		/// <summary>
		/// If true, writes will be propagated to storage
		/// </summary>
		public bool EnableWrites { get; set; }

		/// <summary>
		/// If true, log all cache misses.  Defaults to false.
		/// </summary>
		public bool LogCacheMisses { get; set; }

		/// <summary>
		/// Complete an action from existing cached data
		/// </summary>
		/// <param name="action">Action to be completed</param>
		/// <param name="cancellationToken">Token to be used to cancel operations</param>
		/// <returns>True if it has been completed, false if not</returns>
		public Task<bool> CompleteActionFromCacheAsync(LinkedAction action, CancellationToken cancellationToken);

		/// <summary>
		/// Save the output for a completed action
		/// </summary>
		/// <param name="action">Completed action</param>
		/// <param name="cancellationToken">Token to be used to cancel operations</param>
		/// <returns>Asynchronous task object</returns>
		public Task ActionCompleteAsync(LinkedAction action, CancellationToken cancellationToken);

		/// <summary>
		/// Flush all updates in the cache asynchronously.
		/// </summary>
		/// <param name="cancellationToken">Token to be used to cancel operations</param>
		/// <returns>Asynchronous task object</returns>
		public Task FlushChangesAsync(CancellationToken cancellationToken);
	}
}
