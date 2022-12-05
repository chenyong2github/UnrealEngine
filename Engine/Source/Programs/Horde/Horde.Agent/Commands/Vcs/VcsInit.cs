// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace Horde.Agent.Commands.Vcs
{
	[Command("Vcs", "Init", "Initialize a directory for VCS-like operations")]
	class VcsInitCommand : VcsBase
	{
		public VcsInitCommand(IOptions<AgentSettings> settings)
			: base(settings)
		{
		}

		public override async Task<int> ExecuteAsync(ILogger logger)
		{
			BaseDir ??= DirectoryReference.GetCurrentDirectory();
			await InitAsync(BaseDir);
			logger.LogInformation("Initialized in {RootDir}", BaseDir);
			return 0;
		}
	}
}
