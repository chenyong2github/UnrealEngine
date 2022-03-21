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
		/// <param name="hash"></param>
		public IoHashWrapper(IoHash hash)
		{
			Hash = ByteString.CopyFrom(hash.ToByteArray());
		}

		/// <summary>
		/// Convert from an IoHashWrapper to an IoHash
		/// </summary>
		/// <param name="hash"></param>
		public static implicit operator IoHash(IoHashWrapper hash)
		{
			return new IoHash(hash.Hash.ToByteArray());
		}

		/// <summary>
		/// Convert from an IoHash to an IoHashWrapper
		/// </summary>
		/// <param name="hash"></param>
		public static implicit operator IoHashWrapper(IoHash hash)
		{
			return new IoHashWrapper(hash);
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
		/// <param name="refId"></param>
		public RefIdWrapper(RefId refId)
		{
			Hash = ByteString.CopyFrom(refId.Hash.ToByteArray());
		}

		/// <summary>
		/// Convert from an IoHashWrapper to an IoHash
		/// </summary>
		/// <param name="refId"></param>
		public static implicit operator RefId(RefIdWrapper refId) => refId.AsRefId();

		/// <summary>
		/// Convert from an IoHash to an IoHashWrapper
		/// </summary>
		/// <param name="refId"></param>
		public static implicit operator RefIdWrapper(RefId refId)
		{
			return new RefIdWrapper(refId);
		}

		/// <summary>
		/// Convert from an IoHashWrapper to an IoHash
		/// </summary>
		/// <returns></returns>
		public RefId AsRefId() => new RefId(new IoHash(Hash.ToByteArray()));
	}
}
