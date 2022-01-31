// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Auth;
using EpicGames.Horde.Compute.Impl;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Options;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Settings to configure a connected Horde.Build instance
	/// </summary>
	public class ComputeOptions : HttpServiceClientOptions
	{
	}

	/// <summary>
	/// Extension methods for configuring Horde Compute
	/// </summary>
	public static class ComputeExtensions
	{
		/// <summary>
		/// Registers services for Horde Compute
		/// </summary>
		/// <param name="Services">The current service collection</param>
		public static void AddHordeCompute(this IServiceCollection Services)
		{
			Services.AddOptions<ComputeOptions>();

			Services.AddScoped<IComputeClient, HttpComputeClient>();
			Services.AddHttpClientWithAuth<IComputeClient, HttpComputeClient>(ServiceProvider => ServiceProvider.GetRequiredService<IOptions<ComputeOptions>>().Value);
		}

		/// <summary>
		/// Registers services for Horde Compute
		/// </summary>
		/// <param name="Services">The current service collection</param>
		/// <param name="Configure">Callback for configuring the storage service</param>
		public static void AddHordeCompute(this IServiceCollection Services, Action<ComputeOptions> Configure)
		{
			AddHordeCompute(Services);
			Services.Configure<ComputeOptions>(Configure);
		}
	}
}
