// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Common;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace EpicGames.Horde.Compute
{
	partial class AddTasksRpcRequest
	{
		public AddTasksRpcRequest(string ChannelId, string NamespaceId, IoHash RequirementsHash, List<IoHash> TaskHashes, bool DoNotCache)
		{
			this.ChannelId = ChannelId;
			this.NamespaceId = NamespaceId;
			this.RequirementsHash = RequirementsHash;
			this.TaskHashes.Add(TaskHashes.Select(x => (IoHashWrapper)x));
			this.DoNotCache = DoNotCache;
		}
	}

	partial class GetTaskUpdatesRpcRequest
	{
		public GetTaskUpdatesRpcRequest(string ChannelId)
		{
			this.ChannelId = ChannelId;
		}
	}
}
