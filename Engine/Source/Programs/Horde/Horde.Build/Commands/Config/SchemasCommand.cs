// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Controllers;
using HordeServer.Utilities;
using Microsoft.Extensions.Logging;
using System;
using System.Threading.Tasks;

namespace HordeServer.Commands.Config
{
	[Command("config", "schemas", "Writes JSON schemas for server settings")]
	class SchemasCommand : Command
	{
		[CommandLine]
		DirectoryReference? OutputDir = null!;

		public override Task<int> ExecuteAsync(ILogger Logger)
		{
			OutputDir ??= DirectoryReference.Combine(Program.AppDir, "Schemas");

			DirectoryReference.CreateDirectory(OutputDir);
			foreach (Type SchemaType in SchemaController.ConfigSchemas)
			{
				FileReference OutputFile = FileReference.Combine(OutputDir, $"{SchemaType.Name}.json");
				Schemas.CreateSchema(SchemaType).Write(OutputFile);
			}

			return Task.FromResult(0);
		}
	}
}
