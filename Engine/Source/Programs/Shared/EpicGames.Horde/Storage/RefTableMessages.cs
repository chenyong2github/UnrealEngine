// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Common;
using EpicGames.Serialization;
using Google.Protobuf;
using Google.Protobuf.WellKnownTypes;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.Horde.Storage
{
	/// <summary>
	/// Request to update or add a reference
	/// </summary>
	partial class SetRefRequest
	{
		public SetRefRequest(string NamespaceId, string BucketId, string Name, CbObject Object)
		{
			this.NamespaceId = NamespaceId;
			this.BucketId = BucketId;
			this.Name = Name;
			this.Object = Object;
		}
	}

	/// <summary>
	/// Response from adding a reference
	/// </summary>
	partial class SetRefResponse
	{
		public SetRefResponse(IEnumerable<IoHash> MissingHashes)
		{
			this.MissingHashes.Add(MissingHashes.Select(x => (IoHashWrapper)x));
		}
	}

	/// <summary>
	/// Request to get a reference
	/// </summary>
	partial class GetRefRequest
	{
		public GetRefRequest(string NamespaceId, string BucketId, string Name)
		{
			this.NamespaceId = NamespaceId;
			this.BucketId = BucketId;
			this.Name = Name;
		}
	}

	/// <summary>
	/// Response from querying a reference
	/// </summary>
	partial class GetRefResponse
	{
		public GetRefResponse(CbObject Object, DateTime LastAccessTime, bool Finalized)
		{
			this.Object = Object;
			this.LastAccessTime = Timestamp.FromDateTime(LastAccessTime);
			this.Finalized = Finalized;
		}
	}

	/// <summary>
	/// Request to update the last access time of a reference
	/// </summary>
	partial class TouchRequest
	{
		public TouchRequest(string NamespaceId, string BucketId, string Name)
		{
			this.NamespaceId = NamespaceId;
			this.BucketId = BucketId;
			this.Name = Name;
		}
	}

	/// <summary>
	/// Request to delete a reference
	/// </summary>
	partial class DeleteRequest
	{
		public DeleteRequest(string NamespaceId, string BucketId, string Name)
		{
			this.NamespaceId = NamespaceId;
			this.BucketId = BucketId;
			this.Name = Name;
		}
	}
}
