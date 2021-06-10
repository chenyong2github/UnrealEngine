// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Newtonsoft.Json.Linq;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Http;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	/// <summary>
	/// Provides avatar images
	/// </summary>
	public interface IAvatarService
	{
		/// <summary>
		/// Gets a users's avatar
		/// </summary>
		/// <param name="User"></param>
		/// <returns></returns>
		Task<IAvatar?> GetAvatarAsync(IUser User);
	}
}

