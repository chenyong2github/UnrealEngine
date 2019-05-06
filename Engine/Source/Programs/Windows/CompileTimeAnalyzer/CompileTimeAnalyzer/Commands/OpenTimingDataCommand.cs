// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Windows.Input;
using Timing_Data_Investigator.Models;

namespace Timing_Data_Investigator.Commands
{
    public class OpenTimingDataCommand : ICommand
    {
        private TimingDataViewModel ViewModelToOpen;

		public event EventHandler CanExecuteChanged;

        public OpenTimingDataCommand(TimingDataViewModel TimingData)
        {
			ViewModelToOpen = TimingData;
        }

        public Action<TimingDataViewModel> OpenAction { get; set; }

        public bool CanExecute(object parameter)
        {
            return true;
        }

        public void Execute(object parameter)
        {
            OpenAction?.Invoke(ViewModelToOpen);
        }
    }
}
