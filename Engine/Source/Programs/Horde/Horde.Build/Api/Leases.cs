// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using Horde.Build.Models;
using Horde.Build.Utilities;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Security.Claims;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace Horde.Build.Api
{

	/// <summary>
	/// Updates an existing lease
	/// </summary>
	public class UpdateLeaseRequest
	{
		/// <summary>
		/// Mark this lease as aborted
		/// </summary>
		public bool? Aborted { get; set; }

	}

}
