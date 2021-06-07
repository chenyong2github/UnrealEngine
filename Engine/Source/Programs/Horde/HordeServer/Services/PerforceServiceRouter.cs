// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using Microsoft.Extensions.Logging;
using System.Collections.Generic;
using System.Threading.Tasks;
using Microsoft.Extensions.Options;
using System;

namespace HordeServer.Services
{
	/// <summary>
	/// A temporary router for easing in the new P4 API based perforce service
	/// which will replace the python based one
	/// </summary>
	public sealed class PerforceServiceRouter : PerforceServiceBase, IDisposable
	{

		IOptionsMonitor<ServerSettings> Settings;

		ILogger<PerforceServiceRouter> Logger;

		BridgePerforceService PythonBridgeService;

		P4APIPerforceService P4APIService;

		/// <summary>
		/// Constructor
		/// </summary>
		public PerforceServiceRouter(IOptionsMonitor<ServerSettings> Settings, ILogger<PerforceServiceRouter> Logger, ILogger<P4APIPerforceService> P4APILogger, ILogger<BridgePerforceService> BridgePerforceLogger)
		{
			this.Settings = Settings;
			this.Logger = Logger;

			this.P4APIService = new P4APIPerforceService(Settings, P4APILogger);
			this.PythonBridgeService = new BridgePerforceService(Settings.CurrentValue.PerforceBridge!, BridgePerforceLogger);
		}

		/// <inheritdoc/>
		override public Task<PerforceUserInfo?> GetUserInfoAsync(string UserName)
		{
			return P4APIService.GetUserInfoAsync(UserName);
		}

		/// <inheritdoc />
		override public Task<string> CreateTicket(string ImpersonateUser)
		{
			return P4APIService.CreateTicket(ImpersonateUser);
		}

		/// <inheritdoc/>
		override public Task<int> GetCodeChangeAsync(string StreamName, int Change)
		{
			return P4APIService.GetCodeChangeAsync(StreamName, Change);
		}

		/// <inheritdoc/>
		override public Task<List<ChangeSummary>> GetChangesAsync(string StreamName, int? MinChange, int? MaxChange, int Results, string? ImpersonateUser)
		{
			return P4APIService.GetChangesAsync(StreamName, MinChange, MaxChange, Results, ImpersonateUser);
		}

		/// <inheritdoc/>
		override public Task<int> CreateNewChangeAsync(string StreamName, string Path)
		{
			return PythonBridgeService.CreateNewChangeAsync(StreamName, Path);
		}

		/// <inheritdoc/>
		override public Task<List<ChangeDetails>> GetChangeDetailsAsync(string StreamName, IReadOnlyList<int> ChangeNumbers, string? ImpersonateUser)
		{
			return PythonBridgeService.GetChangeDetailsAsync(StreamName, ChangeNumbers, ImpersonateUser);
		}

		/// <inheritdoc/>
		override public Task<List<FileSummary>> FindFilesAsync(IEnumerable<string> Paths)
		{
			return PythonBridgeService.FindFilesAsync(Paths);
		}

		/// <inheritdoc/>
		override public Task<byte[]> PrintAsync(string Path)
		{
			return PythonBridgeService.PrintAsync(Path);
		}

		/// <inheritdoc/>
		override public Task<int> DuplicateShelvedChangeAsync(int ShelvedChange)
		{
			throw new Exception("DuplicateShelvedChangeAsync is disabled due to p4 issue: https://jira.it.epicgames.com/servicedesk/customer/portal/1/ITH-144069");	
		}

		/// <inheritdoc/>
		override public Task<(int? Change, string Message)> SubmitShelvedChangeAsync(int ShelvedChange, int OriginalChange)
		{
			return PythonBridgeService.SubmitShelvedChangeAsync(ShelvedChange, OriginalChange);
		}

		/// <inheritdoc/>
		override public Task DeleteShelvedChangeAsync(int ShelvedChange)
		{
			return PythonBridgeService.DeleteShelvedChangeAsync(ShelvedChange);
		}

		/// <inheritdoc/>
		override public Task UpdateChangelistDescription(int Change, string Description)
		{
			return PythonBridgeService.UpdateChangelistDescription(Change, Description );
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			PythonBridgeService.Dispose();
		}

	}

}

