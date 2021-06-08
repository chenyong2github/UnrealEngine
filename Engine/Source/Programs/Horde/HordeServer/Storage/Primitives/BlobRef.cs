// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Storage.Primitives
{
	/// <summary>
	/// Reference to a blob object
	/// </summary>
	struct BlobRef
	{
		/// <summary>
		/// Type of the blob
		/// </summary>
		public IBlobType Type { get; }

		/// <summary>
		/// The blob hash
		/// </summary>
		public IoHash Hash { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Type"></param>
		/// <param name="Hash"></param>
		public BlobRef(IBlobType Type, IoHash Hash)
		{
			this.Type = Type;
			this.Hash = Hash;
		}

		/// <inheritdoc/>
		public override string ToString()
		{
			return $"{Type.Name}: {Hash}";
		}
	}

	/// <summary>
	/// Typed reference to a blob object
	/// </summary>
	/// <typeparam name="TBlob">The blob type</typeparam>
	struct BlobRef<TBlob> where TBlob : IBlobType, new()
	{
		/// <summary>
		/// Static type accessor
		/// </summary>
		public static TBlob Type => new TBlob();

		/// <summary>
		/// Hash of the referenced blob
		/// </summary>
		public IoHash Hash { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Hash">Hash of the blob</param>
		public BlobRef(IoHash Hash)
		{
			this.Hash = Hash;
		}

		/// <summary>
		/// Converts from a dynamically typed blob to this blob type
		/// </summary>
		/// <param name="Ref">The blob reference</param>
		public static explicit operator BlobRef<TBlob>(BlobRef Ref)
		{
			_ = (TBlob)Ref.Type;
			return new BlobRef<TBlob>(Ref.Hash);
		}

		/// <summary>
		/// Converts from this blob to a dynamically typed blob
		/// </summary>
		/// <param name="Ref"></param>
		public static implicit operator BlobRef(BlobRef<TBlob> Ref)
		{
			return new BlobRef(BlobRef<TBlob>.Type, Ref.Hash);
		}
	}
}
