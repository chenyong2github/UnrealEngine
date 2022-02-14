// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Storage;
using Google.Protobuf;

namespace HordeCommon
{
	/// <summary>
	/// Methods for IoHashWrapper
	/// </summary>
	public partial class IoHashWrapper
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Hash"></param>
		public IoHashWrapper(IoHash Hash)
		{
			this.Hash = ByteString.CopyFrom(Hash.ToByteArray());
		}

		/// <summary>
		/// Convert from an IoHashWrapper to an IoHash
		/// </summary>
		/// <param name="Hash"></param>
		public static implicit operator IoHash(IoHashWrapper Hash)
		{
			return new IoHash(Hash.Hash.ToByteArray());
		}

		/// <summary>
		/// Convert from an IoHash to an IoHashWrapper
		/// </summary>
		/// <param name="Hash"></param>
		public static implicit operator IoHashWrapper(IoHash Hash)
		{
			return new IoHashWrapper(Hash);
		}
	}

	/// <summary>
	/// Methods for IoHashWrapper
	/// </summary>
	public partial class RefIdWrapper
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="RefId"></param>
		public RefIdWrapper(RefId RefId)
		{
			this.Hash = ByteString.CopyFrom(RefId.Hash.ToByteArray());
		}

		/// <summary>
		/// Convert from an IoHashWrapper to an IoHash
		/// </summary>
		/// <param name="RefId"></param>
		public static implicit operator RefId(RefIdWrapper RefId) => RefId.AsRefId();

		/// <summary>
		/// Convert from an IoHash to an IoHashWrapper
		/// </summary>
		/// <param name="RefId"></param>
		public static implicit operator RefIdWrapper(RefId RefId)
		{
			return new RefIdWrapper(RefId);
		}

		/// <summary>
		/// Convert from an IoHashWrapper to an IoHash
		/// </summary>
		/// <returns></returns>
		public RefId AsRefId() => new RefId(new IoHash(Hash.ToByteArray()));
	}
}
