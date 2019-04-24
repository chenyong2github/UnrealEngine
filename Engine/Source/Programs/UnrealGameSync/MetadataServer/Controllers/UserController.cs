// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Data.SQLite;
using System.Web.Http;
using UnrealGameSyncMetadataServer.Connectors;
using UnrealGameSyncMetadataServer.Models;

namespace UnrealGameSyncMetadataServer.Controllers
{
	public class UserController : ApiController
	{
		public object Get(string Name)
		{
			long UserId = SqlConnector.FindOrAddUserId(Name);
			return new { Id = UserId };
		}
	}
}
