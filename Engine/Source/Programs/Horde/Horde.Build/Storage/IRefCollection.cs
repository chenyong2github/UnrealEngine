// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using Amazon.Runtime.Internal.Util;
using EpicGames.Horde.Storage;
using Horde.Build.Server;
using Microsoft.Extensions.Caching.Memory;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;

namespace Horde.Build.Storage
{
	/// <summary>
	/// Collection of ref name to blob mappings
	/// </summary>
	public interface IRefCollection
	{
		/// <summary>
		/// Read a ref from storage
		/// </summary>
		/// <param name="namespaceId">Namespace for the ref</param>
		/// <param name="name">The ref name</param>
		/// <param name="cacheTime">Minimum age for a cached value to be returned</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		/// <returns>The ref target</returns>
		Task<RefTarget?> TryReadRefTargetAsync(NamespaceId namespaceId, RefName name, DateTime cacheTime = default, CancellationToken cancellationToken = default);

		/// <summary>
		/// Writes a ref to storage
		/// </summary>
		/// <param name="namespaceId">Namespace for the ref</param>
		/// <param name="name">The ref name</param>
		/// <param name="target">Target for the ref</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		Task WriteRefTargetAsync(NamespaceId namespaceId, RefName name, RefTarget target, CancellationToken cancellationToken = default);

		/// <summary>
		/// Delete a ref from storage
		/// </summary>
		/// <param name="namespaceId">Namespace for the ref</param>
		/// <param name="name">The ref name</param>
		/// <param name="cancellationToken">Cancellation token for the operation</param>
		Task DeleteRefAsync(NamespaceId namespaceId, RefName name, CancellationToken cancellationToken = default);
	}
}
