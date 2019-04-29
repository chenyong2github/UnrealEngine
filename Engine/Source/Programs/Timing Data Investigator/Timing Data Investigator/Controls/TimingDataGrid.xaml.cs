using System.ComponentModel;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Input;

namespace Timing_Data_Investigator.Controls
{
	/// <summary>
	/// Interaction logic for TimingDataGrid.xaml
	/// </summary>
	public partial class TimingDataGrid : UserControl
    {
        public TimingDataGrid()
        {
            InitializeComponent();
        }

        private void Grid_Sorting(object sender, DataGridSortingEventArgs e)
        {
			CollectionViewSource GridModel = DataContext as CollectionViewSource;
			switch (e.Column.SortDirection)
			{
				case ListSortDirection.Ascending:
					{
						e.Column.SortDirection = ListSortDirection.Descending;
						break;
					}

				case ListSortDirection.Descending:
					{
						e.Column.SortDirection = null;
						break;
					}

				default:
					{
						e.Column.SortDirection = ListSortDirection.Ascending;
						break;
					}
			}

			GridModel?.SortDescriptions?.Clear();
			if (e.Column.SortDirection != null)
			{
				GridModel.SortDescriptions.Add(new SortDescription(e.Column.SortMemberPath, e.Column.SortDirection.Value));
			}

            e.Handled = true;
        }

        private void Grid_KeyDown(object sender, KeyEventArgs e)
        {
			TreeGridElement SelectedData = Grid.SelectedItem as TreeGridElement;
            if (SelectedData == null)
            {
                return;
            }

            if (e.Key == Key.Right && !SelectedData.IsExpanded)
            {
                SelectedData.IsExpanded = true;
                e.Handled = true;
            }
            else if (e.Key == Key.Left)
            {
                while (SelectedData != null)
                {
                    if (SelectedData.IsExpanded)
                    {
                        SelectedData.IsExpanded = false;
                        Grid.SelectedItem = SelectedData;
                        Grid.ScrollIntoView(SelectedData);
						DataGridRow Row = (DataGridRow)Grid.ItemContainerGenerator.ContainerFromItem(SelectedData);
                        Row.MoveFocus(new TraversalRequest(FocusNavigationDirection.Next));
                        e.Handled = true;
                        break;
                    }

                    SelectedData = SelectedData.Parent as TreeGridElement;
                }
            }
        }
    }
}
