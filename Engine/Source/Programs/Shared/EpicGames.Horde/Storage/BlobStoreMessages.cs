// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Google.Protobuf;
using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Additional methods for AddBlobRequest
	/// </summary>
	public partial class AddBlobRequest
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Hash"></param>
		/// <param name="Data"></param>
		public AddBlobRequest(IoHash Hash, ReadOnlyMemory<byte> Data)
		{
			this.Hash = Hash;
			this.Data = ByteString.CopyFrom(Data.ToArray());
		}
	}

	/// <summary>
	/// Additional methods for GetBlobResponse
	/// </summary>
	partial class GetBlobResponse
	{
		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Data"></param>
		public GetBlobResponse(byte[]? Data)
		{
			if (Data != null)
			{
				this.Data = ByteString.CopyFrom(Data);
				this.Exists = true;
			}
		}
	}
}
