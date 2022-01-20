// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Base class for exceptions
	/// </summary>
	public class RefException : Exception
	{
		/// <summary>
		/// Namespace containing the ref
		/// </summary>
		public NamespaceId NamespaceId { get; }

		/// <summary>
		/// Bucket containing the ref
		/// </summary>
		public BucketId BucketId { get; }

		/// <summary>
		/// Name of the ref
		/// </summary>
		public IoHash RefId { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		public RefException(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, string Message, Exception? InnerException = null)
			: base(Message, InnerException)
		{
			this.NamespaceId = NamespaceId;
			this.BucketId = BucketId;
			this.RefId = RefId;
		}
	}

	/// <summary>
	/// Indicates that a named reference wasn't found
	/// </summary>
	public sealed class RefNotFoundException : RefException
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public RefNotFoundException(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, Exception? InnerException = null)
			: base(NamespaceId, BucketId, RefId, $"Ref {NamespaceId}/{BucketId}/{RefId} not found", InnerException)
		{
		}
	}

	/// <summary>
	/// Indicates that a named reference wasn't found
	/// </summary>
	public sealed class RefNamespaceNotFoundException : RefException
	{
		/// <summary>
		/// Constructor
		/// </summary>
		public RefNamespaceNotFoundException(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, Exception? InnerException = null)
			: base(NamespaceId, BucketId, RefId, $"Namespace not found for ref {NamespaceId}/{BucketId}/{RefId}", InnerException)
		{
		}
	}

	/// <summary>
	/// Interface for an object reference
	/// </summary>
	public interface IRef
	{
		/// <summary>
		/// Namespace identifier
		/// </summary>
		NamespaceId NamespaceId { get; }

		/// <summary>
		/// Bucket identifier
		/// </summary>
		BucketId BucketId { get; }

		/// <summary>
		/// Ref identifier
		/// </summary>
		IoHash RefId { get; }

		/// <summary>
		/// Last access time
		/// </summary>
		DateTime LastAccessTime { get; }

		/// <summary>
		/// Whether the ref has been finalized
		/// </summary>
		bool Finalized { get; }
	}

	/// <summary>
	/// An object reference with attached data
	/// </summary>
	public interface IRefData : IRef, IDisposable
	{
		/// <summary>
		/// Stream for the data
		/// </summary>
		Stream Stream { get; }
	}

	/// <summary>
	/// Interface for a collection of ref documents
	/// </summary>
	public interface IRefStore
	{
		/// <summary>
		/// Gets the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="Name">Name of the reference</param>
		/// <returns>The reference data if the ref exists</returns>
		Task<IRef> GetAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId);

		/// <summary>
		/// Gets the given reference with its data
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <returns>The reference data if the ref exists</returns>
		Task<IRefData> GetDataAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId);

		/// <summary>
		/// Gets the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <returns>The reference data if the ref exists</returns>
		Task<bool> ExistsAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId);

		/// <summary>
		/// Determines which refs are missing
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefIds">Names of the references</param>
		/// <returns>The reference data if the ref exists</returns>
		Task<List<string>> NeedsAsync(NamespaceId NamespaceId, BucketId BucketId, List<IoHash> RefIds);

		/// <summary>
		/// Sets the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <param name="Stream">New value for the reference, as a compact binary object</param>
		/// <returns>List of missing references</returns>
		Task<List<IoHash>> SetAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, Stream Stream);

		/// <summary>
		/// Attempts to finalize a reference, turning its references into hard references
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <param name="Hash">Hash of the referenced object</param>
		/// <returns></returns>
		Task<List<IoHash>> FinalizeAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, IoHash Hash);

		/// <summary>
		/// Removes the given reference
		/// </summary>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <returns>True if the ref was deleted, false if it did not exist</returns>
		Task<bool> DeleteAsync(NamespaceId NamespaceId, BucketId BucketId, IoHash RefId);
	}

	/// <summary>
	/// Extension methods for refs
	/// </summary>
	public static class RefExtensions
	{
		/// <summary>
		/// Reads a ref data as a compact binary field
		/// </summary>
		/// <param name="RefData">The ref data object</param>
		/// <returns>Deserialized field</returns>
		public static async ValueTask<CbField> AsCbFieldAsync(this IRefData RefData)
		{
			using MemoryStream MemoryStream = new MemoryStream();
			await RefData.Stream.CopyToAsync(MemoryStream);
			return new CbField(MemoryStream.ToArray());
		}
	}

	/// <summary>
	/// Extension methods for ref collections
	/// </summary>
	public static class RefCollectionExtensions
	{
		/// <summary>
		/// Sets the given reference
		/// </summary>
		/// <param name="Collection">Collection to store the ref</param>
		/// <param name="NamespaceId">Namespace identifier</param>
		/// <param name="BucketId">Bucket identifier</param>
		/// <param name="RefId">Ref identifier</param>
		/// <param name="Object">New value for the reference, as a compact binary object</param>
		/// <returns>List of missing references</returns>
		public static async Task<List<IoHash>> SetAsync(this IRefStore Collection, NamespaceId NamespaceId, BucketId BucketId, IoHash RefId, CbObject Object)
		{
			using ReadOnlyMemoryStream Stream = new ReadOnlyMemoryStream(Object.GetView());
			return await Collection.SetAsync(NamespaceId, BucketId, RefId, Stream);
		}
	}
}
