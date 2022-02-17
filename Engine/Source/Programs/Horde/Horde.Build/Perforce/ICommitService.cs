// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using EpicGames.Core;
using MongoDB.Bson;
using HordeServer.Utilities;

namespace HordeServer.Commits
{
	/// <summary>
	/// Provides information about commits
	/// </summary>
	public interface ICommitService
	{
		/// <summary>
		/// Registers a delegate to be called when a new commit is added
		/// </summary>
		/// <param name="OnAddCommit">Callback for a new commit being added</param>
		/// <returns>Disposable handler.</returns>
		IDisposable AddListener(Action<ICommit> OnAddCommit);
	}
}
