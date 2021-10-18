// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Collections;
using HordeServer.Collections.Impl;
using HordeServer.Models;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using MongoDB.Driver;
using Serilog.Events;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Services
{
	/// <summary>
	/// Service to perform schema upgrades on the database
	/// </summary>
	public class UpgradeService
	{
		/// <summary>
		/// The current schema version
		/// </summary>
		public const int LatestSchemaVersion = 5;

		/// <summary>
		/// The database service instance
		/// </summary>
		DatabaseService DatabaseService { get; set; }

		/// <summary>
		/// The DI service provider
		/// </summary>
		IServiceProvider ServiceProvider;

		/// <summary>
		/// Logger instance
		/// </summary>
		ILogger<UpgradeService> Logger;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service singleton</param>
		/// <param name="ServiceProvider">The DI service provider</param>
		/// <param name="Logger">Logger instance</param>
		public UpgradeService(DatabaseService DatabaseService, IServiceProvider ServiceProvider, ILogger<UpgradeService> Logger)
		{
			this.DatabaseService = DatabaseService;
			this.ServiceProvider = ServiceProvider;
			this.Logger = Logger;
		}

		/// <summary>
		/// Update the database schema
		/// </summary>
		/// <param name="FromVersion">The version to upgrade from. Automatically detected if this is null.</param>
		/// <returns>Async task</returns>
		public async Task UpgradeSchemaAsync(int? FromVersion)
		{
			Globals Globals = await DatabaseService.GetGlobalsAsync();
			int SchemaVersion = FromVersion ?? Globals.SchemaVersion ?? 0;

			while (SchemaVersion < LatestSchemaVersion)
			{
				Logger.LogInformation("Upgrading from schema version {Version}", SchemaVersion);

				// Handle the current version
				//				if (SchemaVersion == 0)
				//				{
				//					IssueCollection IssueCollection = new IssueCollection(DatabaseService);
				//					IJobCollection JobCollection = ServiceProvider.GetService<IJobCollection>();
				//					await IssueCollection.UpdateSchemaLogIdFields(JobCollection, Logger);
				//				}
				//				if(SchemaVersion == 2)
				//				{
				//					IssueCollection IssueCollection = new IssueCollection(DatabaseService);
				//					await IssueCollection.FixResolvedIssuesAsync(Logger);
				//				}

				if (SchemaVersion == 3)
				{
					IJobCollection JobCollection = ServiceProvider.GetRequiredService<IJobCollection>();
					await JobCollection.UpgradeDocumentsAsync();
				}
				if (SchemaVersion == 4)
				{
					UserCollectionV1 UserCollectionV1 = new UserCollectionV1(ServiceProvider.GetRequiredService<DatabaseService>());
					using UserCollectionV2 UserCollectionV2 = new UserCollectionV2(ServiceProvider.GetRequiredService<DatabaseService>(), ServiceProvider.GetRequiredService<ILogger<UserCollectionV2>>());
					await UserCollectionV2.ResaveDocumentsAsync(UserCollectionV1);
				}

				// Increment the version number
				SchemaVersion++;

				// Try to update the current schema version number
				while (Globals.SchemaVersion == null || Globals.SchemaVersion < SchemaVersion)
				{
					Globals.SchemaVersion = SchemaVersion;
					if (await DatabaseService.TryUpdateSingletonAsync(Globals))
					{
						break;
					}
					Globals = await DatabaseService.GetGlobalsAsync();
				}
			}
		}
	}
}
