// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Threading;
using System.Threading.Tasks;
using Horde.Agent.Services;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Horde.Agent.Tests.Services;

[TestClass]
public sealed class TelemetryServiceTest : IDisposable
{
	private readonly TelemetryService _telemetryService;
	private readonly NullLoggerFactory _loggerFactory = new ();

	public TelemetryServiceTest()
	{
		AgentSettings settings = new() { Server = "Test", ServerProfiles = { new ServerProfile() { Name = "Test", Url = new Uri("http://localhost:1234") } }};
		GrpcService grpcService = new (new OptionsWrapper<AgentSettings>(settings), NullLogger<GrpcService>.Instance, _loggerFactory);
		_telemetryService = new TelemetryService(grpcService, new OptionsWrapper<AgentSettings>(settings), NullLogger<TelemetryService>.Instance);
	}

	[TestMethod]
	public async Task NormalEventLoopTiming()
	{
		DateTime now = DateTime.UtcNow;
		_telemetryService.GetUtcNow = () => now;
		Task<(bool onTime, TimeSpan diff)> task = _telemetryService.IsEventLoopOnTimeAsync(TimeSpan.FromSeconds(1), TimeSpan.FromMilliseconds(500), CancellationToken.None);
		_telemetryService.GetUtcNow = () => now + TimeSpan.FromMilliseconds(1000 + 12);
		(bool onTime, TimeSpan diff) = await task;
		Assert.IsTrue(onTime);
	}
	
	[TestMethod]
	public async Task SlowEventLoopTooEarly()
	{
		DateTime now = DateTime.UtcNow;
		_telemetryService.GetUtcNow = () => now;
		Task<(bool onTime, TimeSpan diff)> task = _telemetryService.IsEventLoopOnTimeAsync(TimeSpan.FromSeconds(1), TimeSpan.FromMilliseconds(100), CancellationToken.None);
		_telemetryService.GetUtcNow = () => now + TimeSpan.FromMilliseconds(50);
		(bool onTime, TimeSpan diff) = await task;
		Assert.IsFalse(onTime);
	}
	
	[TestMethod]
	public async Task SlowEventLoopTooLate()
	{
		DateTime now = DateTime.UtcNow;
		_telemetryService.GetUtcNow = () => now;		
		Task<(bool onTime, TimeSpan diff)> task = _telemetryService.IsEventLoopOnTimeAsync(TimeSpan.FromSeconds(1), TimeSpan.FromMilliseconds(100), CancellationToken.None);
		_telemetryService.GetUtcNow = () => now + TimeSpan.FromMilliseconds(2500);
		(bool onTime, TimeSpan diff) = await task;
		Assert.IsFalse(onTime);
	}

	public void Dispose()
	{
		_telemetryService.Dispose();
		_loggerFactory.Dispose();
	}
}
