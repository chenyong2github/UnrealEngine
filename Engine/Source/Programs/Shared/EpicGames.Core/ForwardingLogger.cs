// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace EpicGames.Core
{
	/// <summary>
	/// Logger which forwards output to several other loggers
	/// </summary>
	public class ForwardingLogger : ILogger
	{
		/// <summary>
		/// List of IDisposable instances
		/// </summary>
		class DisposableList : List<IDisposable>, IDisposable
		{
			/// <inheritdoc/>
			public void Dispose()
			{
				foreach(IDisposable Child in this)
				{
					Child.Dispose();
				}
			}
		}

		/// <summary>
		/// The loggers that are being written to
		/// </summary>
		public ILogger[] Loggers
		{
			get;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Loggers">Array of loggers to forward to</param>
		public ForwardingLogger(params ILogger[] Loggers)
		{
			this.Loggers = Loggers;
		}

		/// <inheritdoc/>
		public IDisposable? BeginScope<TState>(TState State)
		{
			IDisposable? Disposable = null;

			DisposableList? Scope = null;
			foreach (ILogger Logger in Loggers)
			{
				IDisposable? Next = Logger.BeginScope(State);
				if(Next != null)
				{
					if (Scope != null)
					{
						Scope.Add(Next);
					}
					else if (Disposable != null)
					{
						Scope = new DisposableList();
						Scope.Add(Disposable);
						Scope.Add(Next);
						Disposable = Scope;
					}
					else
					{
						Disposable = Next;
					}
				}
			}

			return Disposable;
		}

		/// <inheritdoc/>
		public bool IsEnabled(LogLevel LogLevel)
		{
			foreach(ILogger Logger in Loggers)
			{
				if(Logger.IsEnabled(LogLevel))
				{
					return true;
				}
			}
			return false;
		}

		/// <inheritdoc/>
		public void Log<TState>(LogLevel LogLevel, EventId EventId, TState State, Exception Exception, Func<TState, Exception, string> Formatter)
		{
			foreach (ILogger Logger in Loggers)
			{
				Logger.Log(LogLevel, EventId, State, Exception, Formatter);
			}
		}
	}
}
