// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.Perforce;
using EpicGames.Redis;
using Horde.Build.Server;
using Horde.Build.Streams;
using Horde.Build.Users;
using Horde.Build.Utilities;
using HordeCommon;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using StackExchange.Redis;

namespace Horde.Build.Perforce
{
	/// <summary>
	/// Provides commit information for streams
	/// </summary>
	class CommitService : ICommitService
	{
		readonly IPerforceService _perforceService;

		/// <summary>
		/// Constructor
		/// </summary>
		public CommitService(IPerforceService perforceService)
		{
			_perforceService = perforceService;
		}

		/// <inheritdoc/>
		public ICommitCollection GetCollection(IStream stream) => _perforceService.GetCommits(stream);
	}
}
