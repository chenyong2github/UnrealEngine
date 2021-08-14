// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Google.Protobuf;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Common
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
			this.Hash = ByteString.CopyFrom(Hash.Memory.ToArray());
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
}
