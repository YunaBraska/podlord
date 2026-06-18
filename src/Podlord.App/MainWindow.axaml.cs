using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.Input;
using Avalonia.Input.Platform;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Avalonia.VisualTree;
using AvaloniaEdit;
using System.Globalization;
using Podlord.Core;
using Podlord.Kubernetes;

namespace Podlord.App;

public partial class MainWindow : Window
{
    private sealed record CopyMenuAction(string Header, string Value);

    private readonly MainWindowViewModel viewModel;
    private CancellationTokenSource? contextMenuHold;
    private ContextMenu? activeContextMenu;
    private Control? activeContextMenuOwner;
    private bool isRadarPointerOver;
    private bool isRadarDragging;
    private bool suppressNextRadarClick;
    private Point? lastRadarDragPoint;
    private bool isPulseStripDragging;
    private Point? lastPulseStripDragPoint;
    private readonly HashSet<DataGrid> initializedTableLayouts = [];
    private DataGrid? headerDragGrid;
    private DataGridColumn? headerDragColumn;
    private Control? headerDragHeader;
    private Point? lastHeaderDragPoint;
    private bool isHeaderDragActive;
    private bool applyingTableLayout;
    private bool suppressNextHeaderSortClick;
    private DataGrid? resizingGrid;
    private DataGridColumn? resizingColumn;
    private DataGridColumnHeader? resizingHeader;
    private double resizeStartX;
    private double resizeStartWidth;
    private bool isResizingColumn;
    private GridLength lastOpenInspectorHeight = new(300, GridUnitType.Pixel);
    private bool isRightSidebarResizing;
    private Point? rightSidebarResizeStart;
    private double rightSidebarStartWidth;
    private bool isSyncingYamlEditor;

    public MainWindow()
        : this([])
    {
    }

    public MainWindow(IReadOnlyList<string> startupArgs)
    {
        InitializeComponent();
        var state = AppState.LoadDefault();
        viewModel = new MainWindowViewModel(state, new KubernetesResourceService(state));
        DataContext = viewModel;
        viewModel.PropertyChanged += ViewModelPropertyChanged;
        AddHandler(PointerPressedEvent, GlobalPointerPressedDismissMenus, RoutingStrategies.Tunnel);
        AddHandler(DataGridColumnHeader.PointerPressedEvent, ColumnHeaderPointerPressed, RoutingStrategies.Tunnel);
        AddHandler(DataGridColumnHeader.PointerMovedEvent, ColumnHeaderPointerMoved, RoutingStrategies.Tunnel);
        AddHandler(DataGridColumnHeader.PointerReleasedEvent, ColumnHeaderPointerReleased, RoutingStrategies.Tunnel);
        AddHandler(DataGridColumnHeader.PointerCaptureLostEvent, ColumnHeaderPointerCaptureLost, RoutingStrategies.Tunnel);
        AddHandler(DataGridCell.PointerEnteredEvent, DataGridCellPointerEntered, RoutingStrategies.Tunnel);
        PulseStripScroller.AddHandler(PointerPressedEvent, PulseStripPointerPressed, RoutingStrategies.Tunnel);
        PulseStripScroller.AddHandler(PointerMovedEvent, PulseStripPointerMoved, RoutingStrategies.Tunnel);
        PulseStripScroller.AddHandler(PointerReleasedEvent, PulseStripPointerReleased, RoutingStrategies.Tunnel);
        PulseStripScroller.AddHandler(PointerCaptureLostEvent, PulseStripPointerCaptureLost, RoutingStrategies.Tunnel);
        PulseStripScroller.AddHandler(PointerWheelChangedEvent, PulseStripPointerWheelChanged, RoutingStrategies.Tunnel);
        ConfigureYamlEditor();
        ConfigureLogEditor();
        SyncYamlEditorFromViewModel();
        SyncLogEditorFromViewModel();
        viewModel.Resources.CollectionChanged += (_, _) => Dispatcher.UIThread.Post(ApplyAutoHideEmptyColumns, DispatcherPriority.Background);
        viewModel.AutoHideEmptyColumnsRequested += (_, _) => Dispatcher.UIThread.Post(ApplyAutoHideEmptyColumns, DispatcherPriority.Background);
        viewModel.SetRadarPanelWidth(RightSidebarShell.Width);
        UpdateInspectorLayout();
        UpdateYamlEditorHeight();
        viewModel.LoadStartupKubeconfigs(startupArgs);
        Dispatcher.UIThread.Post(() =>
        {
            InitializeTableLayouts();
            UpdateSortHeaderIndicators();
        });
        Activated += (_, _) => viewModel.SetAppFocus(true);
        Deactivated += (_, _) => viewModel.SetAppFocus(false);
        SizeChanged += (_, _) => UpdateYamlEditorHeight();
        Closed += (_, _) => viewModel.Dispose();
        PropertyChanged += (_, e) =>
        {
            if (e.Property == WindowStateProperty)
            {
                viewModel.SetWindowVisible(WindowState != WindowState.Minimized);
            }
        };
    }

    private void ViewModelPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(MainWindowViewModel.IsInspectorVisible))
        {
            UpdateInspectorLayout();
            Dispatcher.UIThread.Post(UpdateYamlEditorHeight, DispatcherPriority.Background);
        }
        else if (e.PropertyName is nameof(MainWindowViewModel.ResourceSortLabel) or nameof(MainWindowViewModel.EventSortLabel))
        {
            Dispatcher.UIThread.Post(UpdateSortHeaderIndicators);
        }
        else if (e.PropertyName == nameof(MainWindowViewModel.EditableYaml))
        {
            SyncYamlEditorFromViewModel();
        }
        else if (e.PropertyName == nameof(MainWindowViewModel.LogText))
        {
            SyncLogEditorFromViewModel();
        }
        else if (e.PropertyName == nameof(MainWindowViewModel.SelectedInspectorTabIndex)
                 && viewModel.SelectedInspectorTabIndex == 1)
        {
            Dispatcher.UIThread.Post(SyncYamlEditorFromViewModel, DispatcherPriority.Background);
            Dispatcher.UIThread.Post(UpdateYamlEditorHeight, DispatcherPriority.Background);
        }
    }

    private void ImportHomeClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.ImportHome();
    }

    private async void ImportPathClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (!string.IsNullOrWhiteSpace(viewModel.ImportPath))
        {
            viewModel.ImportPathNow();
            return;
        }

        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Import kubeconfig file(s)",
            AllowMultiple = true,
            FileTypeFilter =
            [
                new FilePickerFileType("Kubeconfig")
                {
                    Patterns = ["*.yaml", "*.yml", "*.kubeconfig", "*.conf", "config", "kubeconfig*"]
                },
                FilePickerFileTypes.All
            ]
        }).ConfigureAwait(true);

        var paths = files.Select(file => file.Path.LocalPath).ToList();
        if (paths.Count > 0)
        {
            viewModel.ImportPaths(paths);
        }
    }

    private void RemoveSourceClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Button { Tag: SourceStatusRow row })
        {
            viewModel.RemoveSource(row);
        }
    }

    private async void ImportK3dClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        await viewModel.ImportK3dNowAsync().ConfigureAwait(true);
    }

    private async void DeleteSelectedResourceClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        await viewModel.DeleteSelectedResourceAsync().ConfigureAwait(true);
    }

    private void InspectorTabClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Button { Tag: string rawIndex } && int.TryParse(rawIndex, out var index))
        {
            var wasYaml = viewModel.IsInspectorYamlActive;
            viewModel.SelectedInspectorTabIndex = index;
            Dispatcher.UIThread.Post(UpdateYamlEditorHeight, DispatcherPriority.Background);
            if (wasYaml && index == 1)
            {
                _ = viewModel.LoadFreshYamlAsync();
            }
        }
    }

    private void InspectorTabContentHostSizeChanged(object? sender, SizeChangedEventArgs e)
    {
        UpdateYamlEditorHeight();
    }

    private void YamlChromeSizeChanged(object? sender, SizeChangedEventArgs e)
    {
        UpdateYamlEditorHeight();
    }

    private void YamlEditorTextChanged(object? sender, EventArgs e)
    {
        if (isSyncingYamlEditor)
        {
            return;
        }

        if (sender is TextEditor editor)
        {
            viewModel.EditableYaml = editor.Text ?? string.Empty;
        }
    }

    private void ConfigureYamlEditor()
    {
        YamlEditor.TextArea.TextView.LineTransformers.Add(new YamlSyntaxColorizer());
        YamlEditor.Options.ConvertTabsToSpaces = true;
        YamlEditor.Options.IndentationSize = 2;
        YamlEditor.TextArea.TextView.PointerPressed += YamlEditorTextViewPointerPressed;
        YamlEditor.TextArea.TextView.PointerReleased += EditorRefPointerReleased;
        YamlEditor.TextArea.TextView.PointerCaptureLost += EditorRefPointerCaptureLost;
        YamlEditor.TextArea.TextView.PointerMoved += YamlEditorRefPointerMoved;
        YamlEditor.TextArea.TextView.Redraw();
    }

    private CancellationTokenSource? editorRefHoldTimer;

    private void YamlEditorTextViewPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        HandleEditorRefPointerPressed(e, YamlEditor, ExtractYamlRefAtPoint);
    }

    private string? ExtractYamlRefAtPoint(TextEditor editor, Avalonia.Point point)
    {
        var view = editor.TextArea.TextView;
        var position = view.GetPosition(point + view.ScrollOffset);
        if (position is not { } pos)
        {
            return null;
        }
        var document = editor.Document;
        if (document is null || pos.Line <= 0 || pos.Line > document.LineCount)
        {
            return null;
        }
        var line = document.GetLineByNumber(pos.Line);
        var lineText = document.GetText(line);
        var column = Math.Max(0, pos.Column - 1);
        foreach (var token in YamlSyntaxAnalyzer.AnalyzeLine(lineText))
        {
            if (token.Kind != YamlTokenKind.ResourceRef)
            {
                continue;
            }
            if (column < token.Start || column > token.End)
            {
                continue;
            }
            var value = lineText[token.Start..token.End].Trim();
            return string.IsNullOrEmpty(value) ? null : value;
        }
        return null;
    }

    private void HandleEditorRefPointerPressed(PointerPressedEventArgs e, TextEditor editor, Func<TextEditor, Avalonia.Point, string?> extractRef)
    {
        var localPoint = e.GetPosition(editor);
        var props = e.GetCurrentPoint(editor).Properties;
        var reference = extractRef(editor, localPoint);
        if (string.IsNullOrEmpty(reference))
        {
            return;
        }
        if (props.IsRightButtonPressed)
        {
            ShowEditorRefContextMenu(editor, reference);
            e.Handled = true;
            return;
        }
        if (!props.IsLeftButtonPressed)
        {
            return;
        }
        if ((e.KeyModifiers & (KeyModifiers.Control | KeyModifiers.Meta)) != 0)
        {
            _ = OpenResourceReferenceAsync(reference);
            e.Handled = true;
            return;
        }
        editorRefHoldTimer?.Cancel();
        editorRefHoldTimer?.Dispose();
        editorRefHoldTimer = new CancellationTokenSource();
        _ = OpenResourceReferenceAfterHold(reference, editorRefHoldTimer.Token);
    }

    private void ShowEditorRefContextMenu(Control owner, string reference)
    {
        CloseActiveContextMenu(cancelPendingHold: true);
        var menu = new ContextMenu();
        var open = new MenuItem { Header = "Open in inspector", Tag = reference };
        open.Click += ResourceLinkContextOpenClicked;
        menu.Items.Add(open);
        var copy = new MenuItem { Header = "Copy reference", Tag = reference };
        copy.Click += ResourceLinkContextCopyClicked;
        menu.Items.Add(copy);
        menu.Closed += (_, _) => ClearActiveContextMenu(menu, owner);
        owner.ContextMenu = menu;
        activeContextMenu = menu;
        activeContextMenuOwner = owner;
        menu.PlacementTarget = owner;
        menu.Open(owner);
    }

    private void SyncYamlEditorFromViewModel()
    {
        if (YamlEditor.Text == viewModel.EditableYaml)
        {
            UpdateYamlEditorHeight();
            return;
        }

        isSyncingYamlEditor = true;
        var caret = Math.Clamp(YamlEditor.CaretOffset, 0, viewModel.EditableYaml.Length);
        YamlEditor.Text = viewModel.EditableYaml;
        YamlEditor.CaretOffset = caret;
        YamlEditor.TextArea.TextView.Redraw();
        isSyncingYamlEditor = false;
        UpdateYamlEditorHeight();
    }

    private void UpdateYamlEditorHeight()
    {
        if (InspectorTabContentHost.Bounds.Height <= 0)
        {
            return;
        }

        var chromeHeight = YamlApplyBar.Bounds.Height + YamlAssistBar.Bounds.Height;
        var editorHeight = Math.Max(80, InspectorTabContentHost.Bounds.Height - chromeHeight);
        if (Math.Abs(YamlEditor.Height - editorHeight) < 1)
        {
            return;
        }

        YamlEditor.Height = editorHeight;
    }

    private void SaveFilterClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.SaveCurrentFilter();
    }

    private void RemoveFilterClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.RemoveSelectedFilter();
    }

    private void LoadFilterClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Button { Tag: FilterPreset preset })
        {
            viewModel.SelectedPreset = preset;
        }
    }

    private void DeleteSavedFilterClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Button { Tag: FilterPreset preset })
        {
            viewModel.DeleteSavedFilter(preset);
        }
    }

    private void RenameSavedFilterClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Button { DataContext: FilterPreset preset } button)
        {
            var editor = button.FindAncestorOfType<Grid>()?
                .GetVisualDescendants()
                .OfType<TextBox>()
                .FirstOrDefault();
            CommitSavedFilterRename(preset, editor?.Text ?? preset.Name);
        }
    }

    private void SavedFilterNameLostFocus(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is TextBox { DataContext: FilterPreset preset } editor)
        {
            CommitSavedFilterRename(preset, editor.Text ?? preset.Name);
        }
    }

    private void SavedFilterNameGotFocus(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is TextBox { DataContext: FilterPreset preset })
        {
            viewModel.SelectedPreset = preset;
        }
    }

    private void SavedFilterNameKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter || sender is not TextBox { DataContext: FilterPreset preset } editor)
        {
            return;
        }

        CommitSavedFilterRename(preset, editor.Text ?? preset.Name);
        e.Handled = true;
    }

    private void CommitSavedFilterRename(FilterPreset preset, string requestedName)
    {
        viewModel.RenameSavedFilter(preset, requestedName);
    }

    private void ResourcesWorkspaceClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.SelectWorkspace("resources");
    }

    private void ToggleCurrentSearchClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.ToggleSearchForCurrentWorkspace();
        FocusCurrentSearch();
    }

    private void GraphWorkspaceClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.SelectWorkspace("graph");
    }

    private void EventsWorkspaceClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.SelectWorkspace("events");
    }

    private void PortsWorkspaceClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.SelectWorkspace("ports");
    }

    private void SourcesWorkspaceClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.OpenSourcesSettings();
    }

    private void RadarSourceButtonClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Button { Flyout: { } flyout } button)
        {
            flyout.ShowAt(button);
        }
    }

    private void SettingsWorkspaceClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.SelectWorkspace("settings");
    }

    private void ImportPasteClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.ImportPasteNow();
    }

    private void RefreshSourcesClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.RefreshSourcesNow();
    }

    private void SaveSessionClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.SaveSelectedSession();
    }

    private void SaveSourceClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.SaveSelectedSource();
    }

    private void DuplicateSessionClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.DuplicateSelectedSession();
    }

    private void PortForwardRowClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Control { DataContext: FlatResourceRow row })
        {
            viewModel.PreparePortForward(row);
        }
    }

    private async void RadarResourceClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (suppressNextRadarClick)
        {
            suppressNextRadarClick = false;
            return;
        }

        if (sender is Control { DataContext: RadarBlockViewModel block })
        {
            if (block.IsPlaceholder || !block.IsClickable)
            {
                return;
            }

            await viewModel.FocusRadarResourceAsync(block.Resource).ConfigureAwait(true);
        }
        else if (sender is Control { DataContext: FlatResourceRow row })
        {
            await viewModel.FocusRadarResourceAsync(row).ConfigureAwait(true);
        }
    }

    private void RadarPointerWheelChanged(object? sender, PointerWheelEventArgs e)
    {
        viewModel.ZoomRadar(e.Delta.Y);
        e.Handled = true;
    }

    private void PulseStripPointerWheelChanged(object? sender, PointerWheelEventArgs e)
    {
        var current = PulseStripScroller.Offset;
        var nextX = current.X - e.Delta.Y * 42 - e.Delta.X * 42;
        SetPulseStripOffset(nextX);
        e.Handled = true;
    }

    private void PulseStripPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control control)
        {
            return;
        }

        var point = e.GetCurrentPoint(control);
        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        isPulseStripDragging = true;
        lastPulseStripDragPoint = point.Position;
        e.Pointer.Capture(control);
    }

    private void PulseStripPointerMoved(object? sender, PointerEventArgs e)
    {
        if (!isPulseStripDragging || sender is not Control control || lastPulseStripDragPoint is not { } previous)
        {
            return;
        }

        var point = e.GetCurrentPoint(control);
        if (!point.Properties.IsLeftButtonPressed)
        {
            EndPulseStripDrag(e);
            return;
        }

        var delta = point.Position - previous;
        if (Math.Abs(delta.X) < 0.4)
        {
            return;
        }

        SetPulseStripOffset(PulseStripScroller.Offset.X - delta.X);
        lastPulseStripDragPoint = point.Position;
        e.Handled = true;
    }

    private void PulseStripPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        EndPulseStripDrag(e);
    }

    private void PulseStripPointerCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        isPulseStripDragging = false;
        lastPulseStripDragPoint = null;
    }

    private void EndPulseStripDrag(PointerEventArgs e)
    {
        isPulseStripDragging = false;
        lastPulseStripDragPoint = null;
        e.Pointer.Capture(null);
    }

    private void SetPulseStripOffset(double x)
    {
        var maxX = Math.Max(0, PulseStripScroller.Extent.Width - PulseStripScroller.Viewport.Width);
        var clampedX = Math.Clamp(x, 0, maxX);
        PulseStripScroller.Offset = new Vector(clampedX, PulseStripScroller.Offset.Y);
    }

    private void RightSidebarResizePressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control control)
        {
            return;
        }

        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        isRightSidebarResizing = true;
        rightSidebarResizeStart = point.Position;
        rightSidebarStartWidth = RightSidebarShell.Width;
        control.Cursor = new Cursor(StandardCursorType.SizeWestEast);
        e.Pointer.Capture(control);
        e.Handled = true;
    }

    private void RightSidebarResizeMoved(object? sender, PointerEventArgs e)
    {
        if (!isRightSidebarResizing || rightSidebarResizeStart is not { } start)
        {
            return;
        }

        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsLeftButtonPressed)
        {
            EndRightSidebarResize(e);
            return;
        }

        var delta = start.X - point.Position.X;
        RightSidebarShell.Width = Math.Clamp(rightSidebarStartWidth + delta, RightSidebarShell.MinWidth, RightSidebarShell.MaxWidth);
        viewModel.SetRadarPanelWidth(RightSidebarShell.Width);
        e.Handled = true;
    }

    private void RightSidebarResizeReleased(object? sender, PointerReleasedEventArgs e)
    {
        EndRightSidebarResize(e);
    }

    private void RightSidebarResizeCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        isRightSidebarResizing = false;
        rightSidebarResizeStart = null;
    }

    private void EndRightSidebarResize(PointerEventArgs e)
    {
        isRightSidebarResizing = false;
        rightSidebarResizeStart = null;
        e.Pointer.Capture(null);
    }

    private void RadarViewportSizeChanged(object? sender, SizeChangedEventArgs e)
    {
        viewModel.SetRadarViewport(e.NewSize.Width, e.NewSize.Height);
    }

    private void RadarPointerEntered(object? sender, PointerEventArgs e)
    {
        isRadarPointerOver = true;
    }

    private void RadarPointerExited(object? sender, PointerEventArgs e)
    {
        isRadarPointerOver = false;
        if (isRadarDragging)
        {
            return;
        }

        isRadarDragging = false;
        lastRadarDragPoint = null;
    }

    private void RadarPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control control)
        {
            return;
        }

        var point = e.GetCurrentPoint(control);
        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        isRadarDragging = true;
        suppressNextRadarClick = false;
        lastRadarDragPoint = point.Position;
        e.Pointer.Capture(control);
    }

    private void RadarPointerMoved(object? sender, PointerEventArgs e)
    {
        if (!isRadarDragging || sender is not Control control || lastRadarDragPoint is not { } previous)
        {
            return;
        }

        var point = e.GetCurrentPoint(control);
        if (!point.Properties.IsLeftButtonPressed)
        {
            isRadarDragging = false;
            lastRadarDragPoint = null;
            e.Pointer.Capture(null);
            return;
        }

        var delta = point.Position - previous;
        if (Math.Abs(delta.X) < 0.4 && Math.Abs(delta.Y) < 0.4)
        {
            return;
        }

        suppressNextRadarClick = true;
        viewModel.PanRadar(delta.X, delta.Y);
        lastRadarDragPoint = point.Position;
        e.Handled = true;
    }

    private void RadarPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        isRadarDragging = false;
        lastRadarDragPoint = null;
        e.Pointer.Capture(null);
    }

    private void RadarPointerCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        isRadarDragging = false;
        lastRadarDragPoint = null;
    }

    private async void GraphFromClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Control { DataContext: RelationshipRow row })
        {
            await viewModel.FocusRelationshipEndpointAsync(row, focusTarget: false).ConfigureAwait(true);
        }
    }

    private async void GraphToClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Control { DataContext: RelationshipRow row })
        {
            await viewModel.FocusRelationshipEndpointAsync(row, focusTarget: true).ConfigureAwait(true);
        }
    }

    private async void GraphNodeClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Control { DataContext: GraphNodeViewModel node })
        {
            await viewModel.FocusGraphNodeAsync(node).ConfigureAwait(true);
        }
    }

    private void StartPortForwardClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.StartPreparedPortForward();
    }

    private void RunPortForwardActionClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.RunPreparedPortForwardAction();
    }

    private async void ApplyYamlClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        await viewModel.ApplyEditedYamlAsync().ConfigureAwait(true);
    }

    private void ResetYamlClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.ResetEditedYaml();
    }

    private void StopPortForwardClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.StopSelectedPortForward();
    }

    private void OpenPortForwardTaskClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Button { Tag: PortForwardTaskViewModel task })
        {
            viewModel.OpenPortForwardTask(task);
        }
    }

    private void ClosePortForwardToolClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.ClosePortForwardTool();
    }

    private void FilterSearchKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter && sender is Control { DataContext: FilterPickerViewModel picker })
        {
            picker.AddSearchAsCustom();
            e.Handled = true;
        }
    }

    private void GraphPreviousClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.PreviousGraphMatch();
        ScrollSelectedGraphNodeIntoView();
    }

    private void GraphNextClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.NextGraphMatch();
        ScrollSelectedGraphNodeIntoView();
    }

    private void GraphSearchKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            viewModel.NextGraphMatch();
            ScrollSelectedGraphNodeIntoView();
            e.Handled = true;
        }
        else if (e.Key == Key.Escape)
        {
            viewModel.CloseSearchForCurrentWorkspace();
            e.Handled = true;
        }
    }

    private void ToggleResourceSearchClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.ToggleResourceSearch();
        FocusResourceSearch();
    }

    private void ResourcePreviousClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.PreviousResourceMatch();
        ScrollSelectedResourceIntoView();
    }

    private void ResourceNextClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.NextResourceMatch();
        ScrollSelectedResourceIntoView();
    }

    private void ResourceSearchKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            viewModel.NextResourceMatch();
            ScrollSelectedResourceIntoView();
            e.Handled = true;
        }
        else if (e.Key == Key.Escape)
        {
            viewModel.CloseSearchForCurrentWorkspace();
            e.Handled = true;
        }
    }

    private void ResourceSearchTextChanged(object? sender, TextChangedEventArgs e)
    {
        if (viewModel.CurrentResourceSearchMatch is not null)
        {
            ResourceGrid.ScrollIntoView(viewModel.CurrentResourceSearchMatch, ResourceGrid.Columns.FirstOrDefault());
        }
    }

    private void ToggleGraphSearchClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.ToggleGraphSearch();
        FocusGraphSearch();
    }

    private void ToggleEventSearchClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.ToggleEventSearch();
        FocusEventSearch();
    }

    private void EventPreviousClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.PreviousEventMatch();
        ScrollSelectedEventIntoView();
    }

    private void EventNextClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.NextEventMatch();
        ScrollSelectedEventIntoView();
    }

    private void EventSearchKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            viewModel.NextEventMatch();
            ScrollSelectedEventIntoView();
            e.Handled = true;
        }
        else if (e.Key == Key.Escape)
        {
            viewModel.CloseSearchForCurrentWorkspace();
            e.Handled = true;
        }
    }

    private void EventSearchTextChanged(object? sender, TextChangedEventArgs e)
    {
        if (viewModel.CurrentEventSearchMatch is not null)
        {
            EventGrid.ScrollIntoView(viewModel.CurrentEventSearchMatch, EventGrid.Columns.FirstOrDefault());
        }
    }

    private void SortResourcesClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (suppressNextHeaderSortClick)
        {
            suppressNextHeaderSortClick = false;
            return;
        }

        if (sender is Button { Tag: string column })
        {
            viewModel.SortResourcesBy(column);
            UpdateSortHeaderIndicators();
        }
    }

    private void SortEventsClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (suppressNextHeaderSortClick)
        {
            suppressNextHeaderSortClick = false;
            return;
        }

        if (sender is Button { Tag: string column })
        {
            viewModel.SortEventsBy(column);
            UpdateSortHeaderIndicators();
        }
    }

    private void ColumnHeaderPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (e.Source is not Visual visual)
        {
            return;
        }

        var header = visual as DataGridColumnHeader
                     ?? visual.GetVisualAncestors().OfType<DataGridColumnHeader>().FirstOrDefault();
        if (header is null)
        {
            return;
        }

        var point = e.GetCurrentPoint(header);
        var grid = header.FindAncestorOfType<DataGrid>();
        if (grid is null)
        {
            return;
        }

        if (point.Properties.IsLeftButtonPressed)
        {
            var edge = ColumnResizeEdge(header, point.Position);
            if (edge != ColumnResizeEdgeKind.None)
            {
                var targetColumn = edge == ColumnResizeEdgeKind.Right
                    ? ColumnForHeader(grid, header)
                    : PreviousVisibleColumn(grid, ColumnForHeader(grid, header));
                if (targetColumn is null)
                {
                    return;
                }
                resizingGrid = grid;
                resizingColumn = targetColumn;
                resizingHeader = header;
                resizeStartX = e.GetCurrentPoint(grid).Position.X;
                resizeStartWidth = targetColumn.ActualWidth;
                isResizingColumn = true;
                e.Pointer.Capture(header);
                e.Handled = true;
                suppressNextHeaderSortClick = true;
                return;
            }

            var column = ColumnForHeader(grid, header);
            if (column is null || !column.CanUserReorder)
            {
                return;
            }

            suppressNextHeaderSortClick = false;
            headerDragGrid = grid;
            headerDragColumn = column;
            headerDragHeader = header;
            lastHeaderDragPoint = e.GetCurrentPoint(grid).Position;
            isHeaderDragActive = false;
            return;
        }

        if (!point.Properties.IsRightButtonPressed)
        {
            return;
        }

        OpenColumnVisibilityMenu(header, grid);
        e.Handled = true;
    }

    private void ColumnHeaderPointerMoved(object? sender, PointerEventArgs e)
    {
        if (isResizingColumn && resizingGrid is not null && resizingColumn is not null)
        {
            var current = e.GetCurrentPoint(resizingGrid).Position.X;
            var delta = current - resizeStartX;
            var newWidth = Math.Clamp(resizeStartWidth + delta, 36d, 1200d);
            resizingColumn.Width = new DataGridLength(newWidth);
            e.Handled = true;
            return;
        }


        if (headerDragGrid is null || headerDragColumn is null || lastHeaderDragPoint is not { } previous)
        {
            return;
        }

        var point = e.GetCurrentPoint(headerDragGrid);
        if (!point.Properties.IsLeftButtonPressed)
        {
            EndColumnHeaderDrag(e);
            return;
        }

        var deltaX = point.Position.X - previous.X;
        var threshold = Math.Clamp(headerDragColumn.ActualWidth * 0.35, 28, 86);
        if (Math.Abs(deltaX) < threshold)
        {
            return;
        }

        if (!isHeaderDragActive)
        {
            if (headerDragHeader is not null)
            {
                e.Pointer.Capture(headerDragHeader);
            }
            isHeaderDragActive = true;
            suppressNextHeaderSortClick = true;
        }

        var visible = headerDragGrid.Columns
            .Where(column => column.IsVisible)
            .OrderBy(column => column.DisplayIndex)
            .ToList();
        var currentIndex = visible.IndexOf(headerDragColumn);
        if (currentIndex < 0)
        {
            EndColumnHeaderDrag(e);
            return;
        }

        var targetIndex = Math.Clamp(currentIndex + (deltaX > 0 ? 1 : -1), 0, visible.Count - 1);
        if (targetIndex == currentIndex)
        {
            lastHeaderDragPoint = point.Position;
            return;
        }

        headerDragColumn.DisplayIndex = visible[targetIndex].DisplayIndex;
        SaveTableLayout(headerDragGrid);
        suppressNextHeaderSortClick = true;
        lastHeaderDragPoint = point.Position;
        e.Handled = true;
    }

    private void ColumnHeaderPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (isResizingColumn)
        {
            FinishColumnResize(e.Pointer);
            e.Handled = true;
            return;
        }
        var hadActiveDrag = isHeaderDragActive;
        EndColumnHeaderDrag(e);

        if (hadActiveDrag || suppressNextHeaderSortClick || e.InitialPressMouseButton != Avalonia.Input.MouseButton.Left)
        {
            return;
        }
        if (e.Source is not Visual visual)
        {
            return;
        }
        var header = visual as DataGridColumnHeader
                     ?? visual.GetVisualAncestors().OfType<DataGridColumnHeader>().FirstOrDefault();
        if (header is null)
        {
            return;
        }
        var pos = e.GetCurrentPoint(header).Position;
        if (ColumnResizeEdge(header, pos) != ColumnResizeEdgeKind.None)
        {
            return;
        }
        var button = header.GetVisualDescendants().OfType<Button>().FirstOrDefault(candidate => candidate.Classes.Contains("columnPlaque"));
        if (button is null)
        {
            return;
        }
        var clickTarget = visual as Button ?? visual.GetVisualAncestors().OfType<Button>().FirstOrDefault();
        if (ReferenceEquals(clickTarget, button))
        {
            return;
        }
        var args = new RoutedEventArgs(Button.ClickEvent);
        button.RaiseEvent(args);
    }

    private void ColumnHeaderPointerCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        if (isResizingColumn)
        {
            FinishColumnResize(null);
        }
        headerDragGrid = null;
        headerDragColumn = null;
        headerDragHeader = null;
        lastHeaderDragPoint = null;
        isHeaderDragActive = false;
    }

    private void FinishColumnResize(IPointer? pointer)
    {
        var grid = resizingGrid;
        pointer?.Capture(null);
        resizingGrid = null;
        resizingColumn = null;
        resizingHeader = null;
        isResizingColumn = false;
        if (grid is not null)
        {
            SaveTableLayout(grid);
        }
        Dispatcher.UIThread.Post(() => suppressNextHeaderSortClick = false, DispatcherPriority.Background);
    }

    private enum ColumnResizeEdgeKind { None, Left, Right }

    private static ColumnResizeEdgeKind ColumnResizeEdge(DataGridColumnHeader header, Point position)
    {
        const double edgeWidth = 6;
        var width = header.Bounds.Width;
        if (width <= 0)
        {
            return ColumnResizeEdgeKind.None;
        }
        if (position.X >= width - edgeWidth)
        {
            return ColumnResizeEdgeKind.Right;
        }
        if (position.X <= edgeWidth)
        {
            return ColumnResizeEdgeKind.Left;
        }
        return ColumnResizeEdgeKind.None;
    }


    private void ApplyAutoHideEmptyColumns()
    {
        var grid = this.GetVisualDescendants().OfType<DataGrid>().FirstOrDefault(candidate => candidate.Name == "ResourceGrid");
        if (grid is null)
        {
            return;
        }
        var autoHide = viewModel.AutoHideEmptyColumnsSetting;
        foreach (var column in grid.Columns)
        {
            var header = HeaderText(column.Header);
            var hasData = ColumnHasData(header, viewModel.Resources);
            var userVisible = !IsColumnHiddenByLayout(grid, column);
            var desiredVisible = userVisible;
            if (autoHide && hasData is false && userVisible)
            {
                desiredVisible = false;
            }
            if (column.IsVisible != desiredVisible)
            {
                applyingTableLayout = true;
                try
                {
                    column.IsVisible = desiredVisible;
                }
                finally
                {
                    applyingTableLayout = false;
                }
            }
        }
    }

    private bool IsColumnHiddenByLayout(DataGrid grid, DataGridColumn column)
    {
        var layout = viewModel.TableColumnLayout(TableId(grid));
        if (layout.Count == 0)
        {
            return false;
        }
        var byColumn = layout.ToDictionary(item => item.ColumnId, StringComparer.Ordinal);
        return LayoutForColumn(byColumn, grid, column) is { IsVisible: false };
    }

    private static bool? ColumnHasData(string header, IEnumerable<FlatResourceRow> rows)
    {
        return header switch
        {
            "Ready" => rows.Any(row => row.HasReadyInfo),
            "Restarts" => rows.Any(row => row.HasRestartInfo),
            "Node" => rows.Any(row => row.HasNodeInfo),
            "Image" => rows.Any(row => row.HasImageInfo),
            "Owner" => rows.Any(row => row.HasOwnerInfo),
            "CPU" => rows.Any(row => row.HasCpuMetricInfo),
            "Memory" => rows.Any(row => row.HasMemoryMetricInfo),
            "Storage" => rows.Any(row => row.HasStorageMetricInfo),
            "Namespace" => rows.Any(row => !string.IsNullOrEmpty(row.Namespace) && row.Namespace != "cluster"),
            _ => null
        };
    }

    private static DataGridColumn? PreviousVisibleColumn(DataGrid grid, DataGridColumn? column)
    {
        if (column is null)
        {
            return null;
        }
        return grid.Columns
            .Where(c => c.IsVisible && c.DisplayIndex < column.DisplayIndex)
            .OrderByDescending(c => c.DisplayIndex)
            .FirstOrDefault();
    }

    private void EndColumnHeaderDrag(PointerEventArgs e)
    {
        var hadActiveDrag = isHeaderDragActive;
        headerDragGrid = null;
        headerDragColumn = null;
        headerDragHeader = null;
        lastHeaderDragPoint = null;
        isHeaderDragActive = false;
        if (hadActiveDrag)
        {
            e.Pointer.Capture(null);
            Dispatcher.UIThread.Post(() => suppressNextHeaderSortClick = false, DispatcherPriority.Background);
        }
    }

    private void InitializeTableLayouts()
    {
        foreach (var grid in this.GetVisualDescendants().OfType<DataGrid>())
        {
            if (!initializedTableLayouts.Add(grid))
            {
                continue;
            }

            var tableId = TableId(grid);
            ApplyTableLayout(grid, tableId);
            grid.ColumnDisplayIndexChanged += (_, _) =>
            {
                if (!applyingTableLayout)
                {
                    SaveTableLayout(grid);
                }
            };
        }
    }

    private void ApplyTableLayout(DataGrid grid, string tableId)
    {
        var layout = viewModel.TableColumnLayout(tableId);
        if (layout.Count == 0)
        {
            return;
        }

        applyingTableLayout = true;
        try
        {
            var byColumn = layout.ToDictionary(item => item.ColumnId, StringComparer.Ordinal);
            foreach (var column in grid.Columns)
            {
                if (LayoutForColumn(byColumn, grid, column) is { } item)
                {
                    column.IsVisible = item.IsVisible;
                    if (item.Width > 0d)
                    {
                        column.Width = new DataGridLength(item.Width);
                    }
                }
            }

            if (!grid.Columns.Any(column => column.IsVisible) && grid.Columns.Count > 0)
            {
                grid.Columns[0].IsVisible = true;
            }

            var orderedColumns = grid.Columns
                .OrderBy(column => LayoutForColumn(byColumn, grid, column)?.DisplayIndex ?? int.MaxValue)
                .ThenBy(column => grid.Columns.IndexOf(column))
                .ToList();
            for (var index = 0; index < orderedColumns.Count; index++)
            {
                orderedColumns[index].DisplayIndex = index;
            }
        }
        finally
        {
            applyingTableLayout = false;
        }
    }

    private void SaveTableLayout(DataGrid grid)
    {
        if (applyingTableLayout)
        {
            return;
        }

        var tableId = TableId(grid);
        var layout = grid.Columns
            .Select(column => new TableColumnLayout(tableId, ColumnId(grid, column), column.DisplayIndex, column.IsVisible, column.ActualWidth))
            .OrderBy(item => item.DisplayIndex)
            .ToList();
        viewModel.SaveTableColumnLayout(tableId, layout);
    }

    private static string TableId(DataGrid grid)
    {
        if (!string.IsNullOrWhiteSpace(grid.Name))
        {
            return grid.Name;
        }

        var headers = grid.Columns
            .Select(column => HeaderText(column.Header))
            .Select((header, index) => string.IsNullOrWhiteSpace(header) ? $"column-{index.ToString(CultureInfo.InvariantCulture)}" : header)
            .ToList();
        return "table:" + string.Join("|", headers);
    }

    private static string ColumnId(DataGrid grid, DataGridColumn column)
    {
        var index = grid.Columns.IndexOf(column).ToString(CultureInfo.InvariantCulture);
        var header = HeaderText(column.Header);
        if (!string.IsNullOrWhiteSpace(header))
        {
            return header;
        }

        return $"column-{index}";
    }

    private static TableColumnLayout? LayoutForColumn(
        IReadOnlyDictionary<string, TableColumnLayout> byColumn,
        DataGrid grid,
        DataGridColumn column)
    {
        var columnId = ColumnId(grid, column);
        if (byColumn.TryGetValue(columnId, out var layout))
        {
            return layout;
        }

        var legacySuffix = $":{columnId}";
        return byColumn
            .Where(item => item.Key.EndsWith(legacySuffix, StringComparison.Ordinal))
            .Select(item => item.Value)
            .FirstOrDefault();
    }

    private static DataGridColumn? ColumnForHeader(DataGrid grid, DataGridColumnHeader header)
    {
        if (header.Content is { } content)
        {
            var exact = grid.Columns.FirstOrDefault(column => ReferenceEquals(column.Header, content));
            if (exact is not null)
            {
                return exact;
            }

            var label = HeaderText(content);
            if (!string.IsNullOrWhiteSpace(label))
            {
                return grid.Columns.FirstOrDefault(column => HeaderText(column.Header).Equals(label, StringComparison.Ordinal));
            }
        }

        return null;
    }

private void OpenColumnVisibilityMenu(Control owner, DataGrid grid)
    {
        CloseActiveContextMenu(cancelPendingHold: true);
        var menu = new ContextMenu
        {
            MaxHeight = 360
        };

        foreach (var column in grid.Columns.OrderBy(column => column.DisplayIndex))
        {
            var label = HeaderText(column.Header);
            if (string.IsNullOrWhiteSpace(label))
            {
                label = $"Column {column.DisplayIndex + 1}";
            }

            var item = new MenuItem
            {
                Header = label,
                ToggleType = MenuItemToggleType.CheckBox,
                IsChecked = column.IsVisible
            };
            item.Click += (_, _) =>
            {
                var visibleCount = grid.Columns.Count(candidate => candidate.IsVisible);
                if (column.IsVisible && visibleCount <= 1)
                {
                    item.IsChecked = true;
                    return;
                }

                column.IsVisible = !column.IsVisible;
                SaveTableLayout(grid);
                CloseActiveContextMenu(cancelPendingHold: true);
            };
            menu.Items.Add(item);
        }

        menu.Closed += (_, _) => ClearActiveContextMenu(menu, owner);
        owner.ContextMenu = menu;
        activeContextMenu = menu;
        activeContextMenuOwner = owner;
        menu.PlacementTarget = owner;
        menu.Open(owner);
    }

    private enum InspectorSortDir { None, Ascending, Descending }

    private readonly Dictionary<DataGrid, (string Column, InspectorSortDir Direction)> inspectorSortState = new();

    private void UpdateSortHeaderIndicators()
    {
        foreach (var button in this.GetVisualDescendants().OfType<Button>().Where(button => button.Classes.Contains("columnPlaque")))
        {
            if (button.Tag is not string column)
            {
                continue;
            }

            string glyph;
            if (IsVisualInside(button, ResourceGrid))
            {
                glyph = viewModel.ResourceSortGlyphFor(column);
            }
            else if (IsVisualInside(button, EventGrid))
            {
                glyph = viewModel.EventSortGlyphFor(column);
            }
            else if (button.FindAncestorOfType<DataGrid>() is { } grid && inspectorSortState.TryGetValue(grid, out var state) && state.Column == column)
            {
                glyph = state.Direction switch
                {
                    InspectorSortDir.Ascending => "▲",
                    InspectorSortDir.Descending => "▼",
                    _ => string.Empty
                };
            }
            else
            {
                glyph = string.Empty;
            }
            SetSortGlyph(button, glyph);
        }
    }

    private void SortInspectorColumnClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is not Button button || button.Tag is not string column)
        {
            return;
        }
        var grid = button.FindAncestorOfType<DataGrid>();
        if (grid is null)
        {
            return;
        }

        var current = inspectorSortState.GetValueOrDefault(grid);
        var next = current.Column == column
            ? current.Direction switch
            {
                InspectorSortDir.Ascending => InspectorSortDir.Descending,
                InspectorSortDir.Descending => InspectorSortDir.None,
                _ => InspectorSortDir.Ascending
            }
            : InspectorSortDir.Ascending;

        if (next == InspectorSortDir.None)
        {
            inspectorSortState.Remove(grid);
            ApplyInspectorSort(grid, ResolveDefaultSortPath(grid), InspectorSortDir.Ascending);
        }
        else
        {
            var path = ResolveSortPath(grid, column);
            if (path is null)
            {
                return;
            }
            inspectorSortState[grid] = (column, next);
            ApplyInspectorSort(grid, path, next);
        }
        UpdateSortHeaderIndicators();
    }

    private static string? ResolveSortPath(DataGrid grid, string column)
    {
        var match = grid.Columns.FirstOrDefault(c => HeaderText(c.Header).Equals(column, StringComparison.Ordinal));
        if (match is null)
        {
            return null;
        }
        if (!string.IsNullOrWhiteSpace(match.SortMemberPath))
        {
            return match.SortMemberPath;
        }
        if (match is DataGridBoundColumn bound && bound.Binding is Avalonia.Data.Binding binding && !string.IsNullOrWhiteSpace(binding.Path))
        {
            return binding.Path;
        }
        return null;
    }

    private static string? ResolveDefaultSortPath(DataGrid grid)
    {
        foreach (var column in grid.Columns.OrderBy(c => c.DisplayIndex))
        {
            if (!string.IsNullOrWhiteSpace(column.SortMemberPath))
            {
                return column.SortMemberPath;
            }
            if (column is DataGridBoundColumn bound && bound.Binding is Avalonia.Data.Binding binding && !string.IsNullOrWhiteSpace(binding.Path))
            {
                return binding.Path;
            }
        }
        return null;
    }

    private static void ApplyInspectorSort(DataGrid grid, string? path, InspectorSortDir direction)
    {
        if (path is null || grid.ItemsSource is not System.Collections.IList list || list.Count < 2)
        {
            return;
        }
        var items = new List<object?>();
        foreach (var item in list)
        {
            items.Add(item);
        }
        var sample = items.FirstOrDefault(i => i is not null);
        if (sample is null)
        {
            return;
        }
        var property = sample.GetType().GetProperty(path);
        if (property is null)
        {
            return;
        }
        items.Sort((a, b) =>
        {
            var av = a is null ? null : property.GetValue(a);
            var bv = b is null ? null : property.GetValue(b);
            if (av is null && bv is null) return 0;
            if (av is null) return -1;
            if (bv is null) return 1;
            if (av is IComparable comparable)
            {
                return comparable.CompareTo(bv);
            }
            return string.Compare(av.ToString(), bv.ToString(), StringComparison.OrdinalIgnoreCase);
        });
        if (direction == InspectorSortDir.Descending)
        {
            items.Reverse();
        }
        list.Clear();
        foreach (var item in items)
        {
            list.Add(item);
        }
    }

    private static bool IsVisualInside(Control control, Control ancestor)
    {
        return ReferenceEquals(control, ancestor) || control.GetVisualAncestors().Any(parent => ReferenceEquals(parent, ancestor));
    }

    private static void SetSortGlyph(Button button, string glyph)
    {
        if (button.Content is StackPanel panel)
        {
            foreach (var existing in panel.Children.OfType<TextBlock>().Where(text => text.Classes.Contains("sortGlyph")).ToList())
            {
                panel.Children.Remove(existing);
            }

            if (glyph.Length > 0)
            {
                var text = new TextBlock
                {
                    Text = glyph,
                    Margin = new Thickness(6, 0, 0, 0),
                    HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right
                };
                text.Classes.Add("sortGlyph");
                panel.Children.Add(text);
            }

            return;
        }

        if (button.Content is string && button.Tag is string tag)
        {
            button.Content = glyph.Length == 0 ? tag : $"{tag} {glyph}";
        }
    }

    private void CloseInspectorClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.CloseInspector();
        UpdateInspectorLayout();
    }

    private async void InspectorBackClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        await viewModel.GoBackInspectorAsync();
    }

    private async void InspectorForwardClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        await viewModel.GoForwardInspectorAsync();
    }

    private CancellationTokenSource? resourceLinkHoldTimer;

    internal void ResourceLinkPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Button button || button.Tag is not string reference || string.IsNullOrWhiteSpace(reference))
        {
            return;
        }

        var props = e.GetCurrentPoint(button).Properties;
        if (!props.IsLeftButtonPressed)
        {
            return;
        }

        if ((e.KeyModifiers & (KeyModifiers.Control | KeyModifiers.Meta)) != 0)
        {
            _ = OpenResourceReferenceAsync(reference);
            return;
        }

        resourceLinkHoldTimer?.Cancel();
        resourceLinkHoldTimer?.Dispose();
        resourceLinkHoldTimer = new CancellationTokenSource();
        _ = OpenResourceReferenceAfterHold(reference, resourceLinkHoldTimer.Token);
    }

    internal void ResourceLinkPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        resourceLinkHoldTimer?.Cancel();
    }

    internal void ResourceLinkPointerCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        resourceLinkHoldTimer?.Cancel();
    }

    internal void ResourceLinkPointerEntered(object? sender, PointerEventArgs e)
    {
        if (sender is not Button button || button.Tag is not string reference || string.IsNullOrWhiteSpace(reference))
        {
            return;
        }
        ToolTip.SetTip(button, BuildResourceReferenceTooltip(reference));
        ToolTip.SetShowDelay(button, 400);
    }

    private Control BuildResourceReferenceTooltip(string reference)
    {
        const string hint = "Long-press · ⌘/Ctrl+Click · Right-click → Open";
        var row = viewModel.ResolveResourceReferenceForPreview(reference);
        var goldBrush = (Avalonia.Media.IBrush)Application.Current!.FindResource("PlGoldBrightBrush")!;
        var mutedBrush = (Avalonia.Media.IBrush)Application.Current!.FindResource("PlTextMutedBrush")!;
        var textBrush = (Avalonia.Media.IBrush)Application.Current!.FindResource("PlTextBrush")!;
        var panelBrush = (Avalonia.Media.IBrush)Application.Current!.FindResource("PlBgPanelBrush")!;
        var edgeBrush = (Avalonia.Media.IBrush)Application.Current!.FindResource("PlPlaqueEdgeBrush")!;

        var stack = new Avalonia.Controls.StackPanel { Spacing = 4 };
        if (row is null)
        {
            stack.Children.Add(new Avalonia.Controls.TextBlock
            {
                Text = reference,
                Foreground = goldBrush,
                FontWeight = Avalonia.Media.FontWeight.Bold,
                TextWrapping = Avalonia.Media.TextWrapping.Wrap
            });
            stack.Children.Add(new Avalonia.Controls.TextBlock
            {
                Text = "(not in cache)",
                Foreground = mutedBrush
            });
        }
        else
        {
            stack.Children.Add(new Avalonia.Controls.TextBlock
            {
                Text = $"{row.Kind}/{row.Name}",
                Foreground = goldBrush,
                FontWeight = Avalonia.Media.FontWeight.Bold,
                TextWrapping = Avalonia.Media.TextWrapping.Wrap
            });
            stack.Children.Add(new Avalonia.Controls.TextBlock
            {
                Text = row.Namespace ?? "cluster",
                Foreground = mutedBrush
            });
            AddKvRow(stack, "Status", row.Status, textBrush, mutedBrush);
            if (row.HasReadyInfo) AddKvRow(stack, "Ready", row.Ready, textBrush, mutedBrush);
            if (row.HasRestartInfo) AddKvRow(stack, "Restarts", row.Restarts.ToString(CultureInfo.InvariantCulture), textBrush, mutedBrush);
            if (row.HasCpuMetricInfo) AddKvRow(stack, "CPU", row.CpuSummaryDisplay, textBrush, mutedBrush);
            if (row.HasMemoryMetricInfo) AddKvRow(stack, "Memory", row.MemorySummaryDisplay, textBrush, mutedBrush);
            if (row.HasNodeInfo) AddKvRow(stack, "Node", row.Node ?? "-", textBrush, mutedBrush);
            if (row.HasImageInfo) AddKvRow(stack, "Image", row.ImageSummary, textBrush, mutedBrush);
            if (row.HasOwnerInfo) AddKvRow(stack, "Owner", row.Owner ?? "-", textBrush, mutedBrush);
        }
        stack.Children.Add(new Avalonia.Controls.Border
        {
            Margin = new Avalonia.Thickness(0, 6, 0, 0),
            Padding = new Avalonia.Thickness(0, 4, 0, 0),
            BorderBrush = edgeBrush,
            BorderThickness = new Avalonia.Thickness(0, 1, 0, 0),
            Child = new Avalonia.Controls.TextBlock
            {
                Text = hint,
                Foreground = mutedBrush,
                FontSize = 11
            }
        });
        return new Avalonia.Controls.Border
        {
            MinWidth = 280,
            MaxWidth = 420,
            Padding = new Avalonia.Thickness(10),
            Background = panelBrush,
            BorderBrush = edgeBrush,
            BorderThickness = new Avalonia.Thickness(1),
            Child = stack
        };
    }

    private static void AddKvRow(Avalonia.Controls.StackPanel parent, string label, string value, Avalonia.Media.IBrush valueBrush, Avalonia.Media.IBrush labelBrush)
    {
        var grid = new Avalonia.Controls.Grid { ColumnDefinitions = new Avalonia.Controls.ColumnDefinitions("76,*") };
        var k = new Avalonia.Controls.TextBlock { Text = label, Foreground = labelBrush };
        var v = new Avalonia.Controls.TextBlock { Text = value, Foreground = valueBrush, TextTrimming = Avalonia.Media.TextTrimming.CharacterEllipsis };
        Avalonia.Controls.Grid.SetColumn(v, 1);
        grid.Children.Add(k);
        grid.Children.Add(v);
        parent.Children.Add(grid);
    }

    private async Task OpenResourceReferenceAfterHold(string reference, CancellationToken cancellationToken)
    {
        try
        {
            await Task.Delay(500, cancellationToken).ConfigureAwait(false);
            if (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            await Dispatcher.UIThread.InvokeAsync(async () => await OpenResourceReferenceAsync(reference));
        }
        catch (OperationCanceledException)
        {
        }
    }

    private async Task OpenResourceReferenceAsync(string reference)
    {
        if (viewModel.OpenKnownResourceReference(reference))
        {
            await viewModel.OpenSelectedResourceAsync();
        }
    }

    internal async void ResourceLinkContextOpenClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is MenuItem item && item.Tag is string reference && !string.IsNullOrWhiteSpace(reference))
        {
            CloseMenuItemHostMenu(item);
            await OpenResourceReferenceAsync(reference);
        }
    }

    internal async void ResourceLinkContextCopyClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is MenuItem item && item.Tag is string reference && !string.IsNullOrWhiteSpace(reference) && Clipboard is not null)
        {
            CloseMenuItemHostMenu(item);
            await Clipboard.SetTextAsync(reference);
        }
    }

    private static void CloseMenuItemHostMenu(MenuItem item)
    {
        var current = item.GetVisualAncestors().OfType<ContextMenu>().FirstOrDefault();
        current?.Close();
    }

    private void GlobalPointerPressedDismissMenus(object? sender, PointerPressedEventArgs e)
    {
        if (e.Source is not Visual visual)
        {
            return;
        }
        if (visual.GetVisualAncestors().OfType<ContextMenu>().Any()
            || visual.GetVisualAncestors().OfType<MenuItem>().Any())
        {
            return;
        }
        CloseActiveContextMenu(cancelPendingHold: false);
        foreach (var menu in this.GetVisualDescendants().OfType<ContextMenu>())
        {
            if (menu.IsOpen)
            {
                menu.Close();
            }
        }
    }

    private void PortForwardSelectedClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.PrepareSelectedResourcePortForward();
    }

    private void ToggleResourceValueRevealClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Control { DataContext: ResourceValueRow row })
        {
            viewModel.ToggleResourceValueReveal(row);
        }
    }

    private async void CopyResourceValueKeyClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Control { DataContext: ResourceValueRow row })
        {
            await CopyTextAsync(row.Key).ConfigureAwait(true);
        }
    }

    private async void CopyResourceValuePreferredClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Control { DataContext: ResourceValueRow row })
        {
            await CopyTextAsync(row.PreferredCopyValue).ConfigureAwait(true);
        }
    }

    private async void CopyResourceValueRawClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Control { DataContext: ResourceValueRow row })
        {
            await CopyTextAsync(row.RawValue).ConfigureAwait(true);
        }
    }

    private async void CopyResourceValueDecodedClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        if (sender is Control { DataContext: ResourceValueRow row })
        {
            await CopyTextAsync(row.DecodedValue).ConfigureAwait(true);
        }
    }

    private void WindowPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        CloseActiveContextMenu(cancelPendingHold: false);
    }

    private void CopyableTableCellPointerPressed(object? sender, DataGridCellPointerPressedEventArgs e)
    {
        var actions = CopyActionsForCell(e.Cell, e.Column);
        if (actions.Count == 0)
        {
            return;
        }

        var point = e.PointerPressedEventArgs.GetCurrentPoint(e.Cell);
        if (point.Properties.IsRightButtonPressed)
        {
            OpenCopyContextMenu(e.Cell, actions);
            e.PointerPressedEventArgs.Handled = true;
            return;
        }

        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        contextMenuHold?.Cancel();
        contextMenuHold?.Dispose();
        contextMenuHold = new CancellationTokenSource();
        _ = OpenCopyContextMenuAfterHold(e.Cell, actions, contextMenuHold.Token);
    }

    private void DataGridCellPointerEntered(object? sender, PointerEventArgs e)
    {
        if (e.Source is not Visual visual)
        {
            return;
        }

        var cell = visual as DataGridCell
                   ?? visual.GetVisualAncestors().OfType<DataGridCell>().FirstOrDefault();
        if (cell is null)
        {
            return;
        }

        var column = ColumnForCell(cell);
        if (column is null)
        {
            return;
        }

        var value = CopyValueForCell(cell, column);
        if (string.IsNullOrWhiteSpace(value) || !CellTextOverflows(cell))
        {
            ToolTip.SetTip(cell, null);
            return;
        }
        ToolTip.SetTip(cell, value);
        ToolTip.SetShowDelay(cell, 700);
    }

    private static bool CellTextOverflows(DataGridCell cell)
    {
        foreach (var text in cell.GetVisualDescendants().OfType<TextBlock>())
        {
            if (text.Bounds.Width <= 0 || text.DesiredSize.Width <= 0)
            {
                continue;
            }
            if (text.DesiredSize.Width > text.Bounds.Width + 0.5)
            {
                return true;
            }
        }
        return false;
    }

    private static DataGridColumn? ColumnForCell(DataGridCell? cell)
    {
        var grid = cell?.FindAncestorOfType<DataGrid>();
        if (cell is null || grid is null)
        {
            return null;
        }

        var cellPoint = cell.TranslatePoint(new Point(0, 0), grid);
        if (cellPoint is null)
        {
            return null;
        }

        var left = 0d;
        foreach (var column in grid.Columns.Where(column => column.IsVisible).OrderBy(column => column.DisplayIndex))
        {
            var width = column.ActualWidth;
            if (cellPoint.Value.X >= left && cellPoint.Value.X < left + width)
            {
                return column;
            }

            left += width;
        }

        return null;
    }

    private void CopyableTablePointerReleased(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        CopyableCellPointerReleased(sender, e);
    }

    private void CopyableCellPointerReleased(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        contextMenuHold?.Cancel();
    }

    private async Task OpenCopyContextMenuAfterHold(Control control, IReadOnlyList<CopyMenuAction> actions, CancellationToken cancellationToken)
    {
        try
        {
            await Task.Delay(550, cancellationToken).ConfigureAwait(false);
            await Dispatcher.UIThread.InvokeAsync(() => OpenCopyContextMenu(control, actions));
        }
        catch (OperationCanceledException)
        {
        }
    }

    private void OpenCopyContextMenu(Control control, IReadOnlyList<CopyMenuAction> actions)
    {
        if (actions.Count == 0)
        {
            return;
        }

        CloseActiveContextMenu(cancelPendingHold: true);
        var menu = new ContextMenu();
        foreach (var action in actions)
        {
            var item = new MenuItem { Header = action.Header };
            item.Click += async (_, _) =>
            {
                try
                {
                    if (Clipboard is not null)
                    {
                        await Clipboard.SetTextAsync(action.Value).ConfigureAwait(true);
                    }
                }
                finally
                {
                    CloseActiveContextMenu(cancelPendingHold: true);
                }
            };
            menu.Items.Add(item);
        }

        var referenceValue = actions[0].Value;
        if (viewModel.HasKnownResourceReference(referenceValue))
        {
            var openItem = new MenuItem { Header = "Open in inspector" };
            openItem.Click += (_, _) =>
            {
                try
                {
                    viewModel.OpenKnownResourceReference(referenceValue);
                }
                finally
                {
                    CloseActiveContextMenu(cancelPendingHold: true);
                }
            };
            menu.Items.Add(openItem);
        }

        menu.Closed += (_, _) =>
        {
            ClearActiveContextMenu(menu, control);
        };
        control.ContextMenu = menu;
        activeContextMenu = menu;
        activeContextMenuOwner = control;
        menu.PlacementTarget = control;
        menu.Open(control);
    }

    private async Task CopyTextAsync(string value)
    {
        CloseActiveContextMenu(cancelPendingHold: true);
        if (Clipboard is not null)
        {
            await Clipboard.SetTextAsync(value).ConfigureAwait(true);
        }
    }

    private void CloseActiveContextMenu(bool cancelPendingHold)
    {
        if (cancelPendingHold)
        {
            contextMenuHold?.Cancel();
        }

        var menu = activeContextMenu;
        var owner = activeContextMenuOwner;
        activeContextMenu = null;
        activeContextMenuOwner = null;
        if (owner is not null && ReferenceEquals(owner.ContextMenu, menu))
        {
            owner.ContextMenu = null;
        }

        menu?.Close();
    }

    private void ClearActiveContextMenu(ContextMenu menu, Control owner)
    {
        if (ReferenceEquals(owner.ContextMenu, menu))
        {
            owner.ContextMenu = null;
        }

        if (ReferenceEquals(activeContextMenu, menu))
        {
            activeContextMenu = null;
            activeContextMenuOwner = null;
        }
    }

    private IReadOnlyList<CopyMenuAction> CopyActionsForCell(DataGridCell cell, DataGridColumn column)
    {
        if (cell.DataContext is ResourceValueRow valueRow)
        {
            return CopyResourceValueActions(valueRow, HeaderText(column.Header));
        }

        var value = CopyValueForCell(cell, column);
        return string.IsNullOrWhiteSpace(value)
            ? Array.Empty<CopyMenuAction>()
            : [new CopyMenuAction("Copy value", value)];
    }

    private string CopyValueForCell(DataGridCell cell, DataGridColumn column)
    {
        return cell.DataContext switch
        {
            FlatResourceRow row => CopyResourceValue(row, HeaderText(column.Header)),
            EventTimelineRow row => CopyEventValue(row, HeaderText(column.Header)),
            PortForwardTaskViewModel row => CopyPortForwardValue(row, HeaderText(column.Header)),
            SourceStatusRow row => CopySourceValue(row, HeaderText(column.Header)),
            RequestAuditRow row => CopyRequestAuditValue(row, HeaderText(column.Header)),
            FocusMetricRow row => CopyFocusMetricValue(row, HeaderText(column.Header)),
            RelationshipRow row => CopyRelationshipValue(row, HeaderText(column.Header)),
            ResourceValueRow row => CopyResourceValueRowValue(row, HeaderText(column.Header)),
            _ => string.Empty
        };
    }

    private static IReadOnlyList<CopyMenuAction> CopyResourceValueActions(ResourceValueRow row, string header)
    {
        return header switch
        {
            "Key" => [new CopyMenuAction("Copy key", row.Key)],
            "Encoding" => [new CopyMenuAction("Copy encoding", row.Encoding)],
            "Value" when row.IsBase64Encoded && row.IsSensitive => [
                new CopyMenuAction("Copy raw base64 secret value", row.RawValue),
                new CopyMenuAction("Copy decoded secret value", row.DecodedValue)
            ],
            "Value" when row.IsBase64Encoded => [
                new CopyMenuAction("Copy decoded value", row.DecodedValue),
                new CopyMenuAction("Copy raw base64 value", row.RawValue)
            ],
            "Value" => [new CopyMenuAction(row.IsSensitive ? "Copy secret value" : "Copy value", row.RawValue)],
            _ => [new CopyMenuAction("Copy value", row.PreferredCopyValue)]
        };
    }

    private static string CopyResourceValueRowValue(ResourceValueRow row, string header)
    {
        return header switch
        {
            "Key" => row.Key,
            "Encoding" => row.Encoding,
            "Value" => row.PreferredCopyValue,
            _ => row.PreferredCopyValue
        };
    }

    private string CopyResourceValue(FlatResourceRow row, string header)
    {
        return header switch
        {
            "Port Forward" => ActivePortForwardPort(row),
            "Status" => row.Status,
            "Kind" => row.Kind,
            "Name" => row.Name,
            "Namespace" => row.Namespace ?? "cluster",
            "Cluster" => row.Cluster,
            "CPU" => row.CpuSummaryDisplay,
            "Memory" => row.MemorySummaryDisplay,
            "CPU %" => row.CpuPercentDisplay,
            "Memory %" => row.MemoryPercentDisplay,
            "Network" => row.NetworkDisplay,
            "Storage" => row.StorageDisplay,
            "Age" => row.Age,
            "Ready" => row.Ready,
            "Restarts" => row.Restarts.ToString(CultureInfo.InvariantCulture),
            "Node" => row.Node ?? string.Empty,
            "Image" => row.ImageSummary,
            "Owner" => row.Owner ?? string.Empty,
            "ID" or "Id" => row.Id,
            _ => row.Id
        };
    }

    private static string CopyEventValue(EventTimelineRow row, string header)
    {
        return header switch
        {
            "Status" or "Type" => row.Status,
            "Name" => row.Name,
            "Reason" => row.Reason,
            "Object" => row.Object,
            "Namespace" => row.Namespace,
            "Age" => row.Age,
            "Message" => row.Message,
            _ => row.Message
        };
    }

    private static string CopyPortForwardValue(PortForwardTaskViewModel row, string header)
    {
        return header switch
        {
            "Status" => row.Status,
            "Session" => row.Session,
            "Kind" => row.Kind,
            "Name" => row.Name,
            "Namespace" => row.Namespace,
            "Container Port" => row.ContainerPort.ToString(CultureInfo.InvariantCulture),
            "Local Port" => row.LocalPort.ToString(CultureInfo.InvariantCulture),
            "Command" => row.Command,
            _ => row.Id
        };
    }

    private static string CopySourceValue(SourceStatusRow row, string header)
    {
        return header switch
        {
            "Status" => row.Status,
            "Name" => row.Name,
            "Hash" => row.Hash,
            "Imported" => row.ImportedAt,
            "Context" => row.Context,
            "Filter" => row.FilterName,
            "Cluster" => row.Cluster,
            "User" => row.User,
            "Auth" => row.AuthType,
            "File" => row.Name,
            "Source" => row.Source,
            "Detail" => row.Detail,
            _ => row.Detail
        };
    }

    private static string CopyRequestAuditValue(RequestAuditRow row, string header)
    {
        return header switch
        {
            "Time" => row.StartedAt,
            "Method" => row.Method,
            "Path" => row.Path,
            "Priority" => row.Priority,
            "Status" => row.Status,
            "Duration" => row.Duration,
            "Outcome" => row.Outcome,
            _ => row.Outcome
        };
    }

    private static string CopyFocusMetricValue(FocusMetricRow row, string header)
    {
        return header switch
        {
            "Field" => row.Label,
            "Value" => row.Value,
            "Metric" => row.HasBar ? $"{row.Percent:0}%" : string.Empty,
            _ => row.Value
        };
    }

    private static string CopyRelationshipValue(RelationshipRow row, string header)
    {
        return header switch
        {
            "From" => $"{row.FromKind}/{row.FromName}",
            "" => row.Link,
            "To" => $"{row.ToKind}/{row.ToName}",
            "Namespace" => row.Namespace,
            "Status" => row.Status,
            _ => $"{row.FromKind}/{row.FromName} {row.Link} {row.ToKind}/{row.ToName}"
        };
    }

    private string ActivePortForwardPort(FlatResourceRow row)
    {
        var task = viewModel.PortForwards.FirstOrDefault(candidate =>
            candidate.Kind.Equals(row.Kind, StringComparison.Ordinal)
            && candidate.Name.Equals(row.Name, StringComparison.Ordinal)
            && candidate.Namespace.Equals(row.Namespace ?? string.Empty, StringComparison.Ordinal)
            && candidate.IsRunning);
        return task is null ? string.Empty : task.LocalPort.ToString(CultureInfo.InvariantCulture);
    }

    private static string HeaderText(object? header)
    {
        return header switch
        {
            string text => text,
            Button { Tag: string tag } => tag,
            Button { Content: string text } => text,
            _ => string.Empty
        };
    }

    private void ConfigureLogEditor()
    {
        LogEditor.TextArea.TextView.LineTransformers.Add(new LogSyntaxColorizer(viewModel.HasKnownResourceReference));
        LogEditor.TextArea.TextView.PointerPressed += LogEditorTextViewPointerPressed;
        LogEditor.TextArea.TextView.PointerReleased += EditorRefPointerReleased;
        LogEditor.TextArea.TextView.PointerCaptureLost += EditorRefPointerCaptureLost;
        LogEditor.TextArea.TextView.PointerMoved += LogEditorRefPointerMoved;
    }

    private void SyncLogEditorFromViewModel()
    {
        var newText = viewModel.LogText ?? string.Empty;
        if (LogEditor.Text == newText)
        {
            return;
        }

        LogEditor.Text = newText;
        if (!viewModel.LogsPaused)
        {
            LogEditor.CaretOffset = newText.Length;
            LogEditor.ScrollToEnd();
        }
        LogEditor.TextArea.TextView.Redraw();
    }

    private void LogEditorTextViewPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        HandleEditorRefPointerPressed(e, LogEditor, ExtractLogRefAtPoint);
    }

    private string? ExtractLogRefAtPoint(TextEditor editor, Avalonia.Point point)
    {
        var view = editor.TextArea.TextView;
        var position = view.GetPosition(point + view.ScrollOffset);
        if (position is not { } pos)
        {
            return null;
        }
        var document = editor.Document;
        if (document is null || pos.Line <= 0 || pos.Line > document.LineCount)
        {
            return null;
        }
        var line = document.GetLineByNumber(pos.Line);
        var lineText = document.GetText(line);
        var column = Math.Max(0, pos.Column - 1);
        return LogSyntaxColorizer.FindResourceRefAt(lineText, column);
    }

    private void YamlEditorRefPointerMoved(object? sender, PointerEventArgs e)
    {
        UpdateEditorRefTooltip(YamlEditor, ExtractYamlRefAtPoint, e);
    }

    private void LogEditorRefPointerMoved(object? sender, PointerEventArgs e)
    {
        UpdateEditorRefTooltip(LogEditor, ExtractLogRefAtPoint, e);
    }

    private void UpdateEditorRefTooltip(TextEditor editor, Func<TextEditor, Avalonia.Point, string?> extractRef, PointerEventArgs e)
    {
        var view = editor.TextArea.TextView;
        var reference = extractRef(editor, e.GetPosition(editor));
        if (string.IsNullOrEmpty(reference))
        {
            ToolTip.SetTip(view, null);
            return;
        }
        ToolTip.SetTip(view, BuildResourceReferenceTooltip(reference));
        ToolTip.SetShowDelay(view, 400);
    }

    private void EditorRefPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        editorRefHoldTimer?.Cancel();
    }

    private void EditorRefPointerCaptureLost(object? sender, PointerCaptureLostEventArgs e)
    {
        editorRefHoldTimer?.Cancel();
    }

    private void UpdateInspectorLayout()
    {
        if (viewModel.IsInspectorVisible)
        {
            ExpandInspectorLayout();
        }
        else
        {
            CollapseInspectorLayout();
        }
    }

    private void CollapseInspectorLayout()
    {
        if (MainContentGrid.RowDefinitions.Count >= 3)
        {
            var currentInspectorHeight = MainContentGrid.RowDefinitions[2].Height;
            if (currentInspectorHeight.Value > 0)
            {
                lastOpenInspectorHeight = currentInspectorHeight;
            }

            MainContentGrid.RowDefinitions[0].Height = new GridLength(1, GridUnitType.Star);
            MainContentGrid.RowDefinitions[1].Height = new GridLength(0, GridUnitType.Pixel);
            MainContentGrid.RowDefinitions[2].Height = new GridLength(0, GridUnitType.Pixel);
        }
    }

    private void ExpandInspectorLayout()
    {
        if (MainContentGrid.RowDefinitions.Count >= 3)
        {
            MainContentGrid.RowDefinitions[0].Height = new GridLength(1, GridUnitType.Star);
            MainContentGrid.RowDefinitions[1].Height = new GridLength(14, GridUnitType.Pixel);
            MainContentGrid.RowDefinitions[2].Height = lastOpenInspectorHeight.Value > 0
                ? lastOpenInspectorHeight
                : new GridLength(300, GridUnitType.Pixel);
        }
    }

    private void ScrollSelectedGraphNodeIntoView()
    {
        if (viewModel.SelectedGraphNode is not null)
        {
            GraphTree.ScrollIntoView(viewModel.SelectedGraphNode);
        }
    }

    private void ScrollSelectedResourceIntoView()
    {
        if (viewModel.SelectedResource is not null)
        {
            ResourceGrid.ScrollIntoView(viewModel.SelectedResource, ResourceGrid.Columns.FirstOrDefault());
        }
    }

    private void ScrollSelectedEventIntoView()
    {
        if (viewModel.SelectedEvent is not null)
        {
            EventGrid.ScrollIntoView(viewModel.SelectedEvent, EventGrid.Columns.FirstOrDefault());
        }
    }

    private async void ResourceDoubleTapped(object? sender, Avalonia.Input.TappedEventArgs e)
    {
        await viewModel.OpenSelectedResourceAsync().ConfigureAwait(true);
    }

    private void WindowKeyDown(object? sender, KeyEventArgs e)
    {
        var commandModifier = e.KeyModifiers.HasFlag(KeyModifiers.Control) || e.KeyModifiers.HasFlag(KeyModifiers.Meta);
        if (commandModifier && e.Key == Key.K)
        {
            viewModel.OpenCommandPalette();
            e.Handled = true;
            return;
        }

        if (commandModifier && e.Key == Key.F)
        {
            viewModel.OpenSearchForCurrentWorkspace();
            FocusCurrentSearch();
            e.Handled = true;
            return;
        }

        if (commandModifier && TryHandleRadarZoomKey(e))
        {
            return;
        }

        if (e.Key == Key.Escape && (viewModel.IsResourceSearchOpen || viewModel.IsGraphSearchOpen || viewModel.IsEventSearchOpen || viewModel.IsPortSearchOpen))
        {
            viewModel.CloseSearchForCurrentWorkspace();
            e.Handled = true;
            return;
        }

        if (TryHandleRadarPanKey(e))
        {
            return;
        }

        if (!viewModel.IsCommandPaletteOpen)
        {
            return;
        }

        if (e.Key == Key.Escape)
        {
            viewModel.CloseCommandPalette();
            e.Handled = true;
        }
        else if (e.Key == Key.Enter)
        {
            viewModel.ExecuteCommandText();
            e.Handled = true;
        }
    }

    private bool TryHandleRadarZoomKey(KeyEventArgs e)
    {
        switch (e.Key)
        {
            case Key.Add:
            case Key.OemPlus:
                viewModel.ZoomRadarIn();
                break;
            case Key.Subtract:
            case Key.OemMinus:
                viewModel.ZoomRadarOut();
                break;
            case Key.D0:
            case Key.NumPad0:
                viewModel.ResetRadarView();
                break;
            default:
                return false;
        }

        e.Handled = true;
        return true;
    }

    private bool TryHandleRadarPanKey(KeyEventArgs e)
    {
        if (!isRadarPointerOver ||
            viewModel.IsCommandPaletteOpen ||
            e.KeyModifiers.HasFlag(KeyModifiers.Control) ||
            e.KeyModifiers.HasFlag(KeyModifiers.Meta) ||
            e.KeyModifiers.HasFlag(KeyModifiers.Alt))
        {
            return false;
        }

        const double step = 24;
        switch (e.Key)
        {
            case Key.W:
            case Key.Up:
                viewModel.PanRadar(0, step);
                break;
            case Key.A:
            case Key.Left:
                viewModel.PanRadar(step, 0);
                break;
            case Key.S:
            case Key.Down:
                viewModel.PanRadar(0, -step);
                break;
            case Key.D:
            case Key.Right:
                viewModel.PanRadar(-step, 0);
                break;
            default:
                return false;
        }

        e.Handled = true;
        return true;
    }

    private void FocusCurrentSearch()
    {
        if (viewModel.IsResourcesWorkspace)
        {
            FocusResourceSearch();
        }
        else if (viewModel.IsGraphWorkspace)
        {
            FocusGraphSearch();
        }
        else if (viewModel.IsEventsWorkspace)
        {
            FocusEventSearch();
        }
        else if (viewModel.IsPortsWorkspace)
        {
            FocusPortSearch();
        }
    }

    private void TogglePortSearchClicked(object? sender, Avalonia.Interactivity.RoutedEventArgs e)
    {
        viewModel.TogglePortSearch();
    }

    private void FocusPortSearch()
    {
        if (viewModel.IsPortSearchOpen)
        {
            PortSearchBox.Focus();
        }
    }

    private void FocusResourceSearch()
    {
        if (viewModel.IsResourceSearchOpen)
        {
            ResourceSearchBox.Focus();
        }
    }

    private void FocusGraphSearch()
    {
        if (viewModel.IsGraphSearchOpen)
        {
            GraphSearchBox.Focus();
        }
    }

    private void FocusEventSearch()
    {
        if (viewModel.IsEventSearchOpen)
        {
            EventSearchBox.Focus();
        }
    }
}
