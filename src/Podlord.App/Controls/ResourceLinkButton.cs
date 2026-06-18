using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.VisualTree;

namespace Podlord.App;

/// <summary>
/// Button preset for clickable Kubernetes resource references.
/// Auto-applies the <c>resourceLink</c> style class, wires the
/// open/copy/long-press/hover behaviors from <see cref="MainWindow"/>,
/// and attaches a context menu with "Open in inspector" + "Copy reference".
/// Tag must be bound to the reference string (e.g. "Pod/my-app").
/// </summary>
public sealed class ResourceLinkButton : Button
{
    public ResourceLinkButton()
    {
        Classes.Add("resourceLink");
        Padding = new Avalonia.Thickness(6, 0);
        HorizontalContentAlignment = Avalonia.Layout.HorizontalAlignment.Stretch;
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        if (this.FindAncestorOfType<MainWindow>() is not { } window)
        {
            return;
        }

        PointerPressed += window.ResourceLinkPointerPressed;
        PointerReleased += window.ResourceLinkPointerReleased;
        PointerCaptureLost += window.ResourceLinkPointerCaptureLost;
        PointerEntered += window.ResourceLinkPointerEntered;

        var menu = new ContextMenu();
        var open = new MenuItem { Header = "Open in inspector" };
        var copy = new MenuItem { Header = "Copy reference" };
        menu.Items.Add(open);
        menu.Items.Add(copy);
        open.Click += window.ResourceLinkContextOpenClicked;
        copy.Click += window.ResourceLinkContextCopyClicked;

        void SyncTags(object? _, Avalonia.AvaloniaPropertyChangedEventArgs args)
        {
            if (args.Property == TagProperty)
            {
                open.Tag = Tag;
                copy.Tag = Tag;
            }
        }
        open.Tag = Tag;
        copy.Tag = Tag;
        PropertyChanged += SyncTags;
        ContextMenu = menu;
    }
}
