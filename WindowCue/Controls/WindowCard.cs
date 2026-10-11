using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using WindowCue.Models;

namespace WindowCue.Controls;

internal sealed class WindowCard : Border
{
    private static readonly System.Windows.Media.Brush NormalBorder = new SolidColorBrush(Color.FromRgb(55, 57, 64));
    private static readonly System.Windows.Media.Brush SelectedBorder = new SolidColorBrush(Color.FromRgb(202, 139, 255));
    private static readonly System.Windows.Media.Brush BoundBorder = new SolidColorBrush(Color.FromRgb(145, 230, 179));

    internal WindowDescriptor WindowInfo { get; }
    internal Border PreviewHost { get; }
    internal event EventHandler? Selected;

    internal WindowCard(WindowDescriptor info)
    {
        WindowInfo = info;
        Width = 194;
        Height = 146;
        Margin = new Thickness(4);
        Padding = new Thickness(1);
        Background = new SolidColorBrush(Color.FromRgb(16, 17, 20));
        BorderBrush = NormalBorder;
        BorderThickness = new Thickness(1);
        CornerRadius = new CornerRadius(10);
        Cursor = Cursors.Hand;
        ToolTip = info.Title;

        var root = new Grid();
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(38) });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });

        var header = new Grid { Margin = new Thickness(8, 5, 8, 3) };
        header.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(23) });
        header.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

        var icon = new Image
        {
            Source = info.Icon,
            Width = 18,
            Height = 18,
            VerticalAlignment = VerticalAlignment.Top,
            Stretch = Stretch.Uniform
        };
        header.Children.Add(icon);

        var textPanel = new StackPanel { Margin = new Thickness(5, -1, 0, 0) };
        Grid.SetColumn(textPanel, 1);
        textPanel.Children.Add(new TextBlock
        {
            Text = info.Title,
            Foreground = Brushes.White,
            FontSize = 12,
            FontWeight = FontWeights.SemiBold,
            TextTrimming = TextTrimming.CharacterEllipsis,
            ToolTip = info.Title
        });
        textPanel.Children.Add(new TextBlock
        {
            Text = info.ProcessName,
            Foreground = new SolidColorBrush(Color.FromRgb(145, 148, 158)),
            FontSize = 10,
            TextTrimming = TextTrimming.CharacterEllipsis
        });
        header.Children.Add(textPanel);
        root.Children.Add(header);

        PreviewHost = new Border
        {
            Margin = new Thickness(5, 0, 5, 5),
            Background = Brushes.Black,
            CornerRadius = new CornerRadius(6),
            ClipToBounds = true,
            IsHitTestVisible = false
        };
        Grid.SetRow(PreviewHost, 1);
        root.Children.Add(PreviewHost);

        Child = root;
        MouseLeftButtonUp += (_, _) => Selected?.Invoke(this, EventArgs.Empty);
        MouseEnter += (_, _) =>
        {
            if (!IsSelected && !IsBound)
            {
                BorderBrush = new SolidColorBrush(Color.FromRgb(98, 101, 112));
            }
        };
        MouseLeave += (_, _) =>
        {
            UpdateBorder();
        };
    }

    internal bool IsSelected { get; private set; }
    internal bool IsBound { get; private set; }

    internal void SetSelected(bool selected)
    {
        IsSelected = selected;
        UpdateBorder();
    }

    internal void SetBound(bool bound)
    {
        IsBound = bound;
        UpdateBorder();
    }

    private void UpdateBorder()
    {
        bool emphasized = IsSelected || IsBound;
        BorderBrush = IsBound ? BoundBorder : IsSelected ? SelectedBorder : NormalBorder;
        BorderThickness = emphasized ? new Thickness(3) : new Thickness(1);
        Padding = emphasized ? new Thickness(0) : new Thickness(1);
    }
}
