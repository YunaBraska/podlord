# Multi-Select Picker Dropdown

A reusable multi-select picker dropdown for selecting, searching, filtering, and optionally managing items in a compact interface.

The component is domain-agnostic, design-system agnostic, and implementation-language agnostic. It can be adapted to any application, visual style, programming language, or UI framework.

---

## Purpose

The component provides a compact control that opens a dropdown popup containing a searchable, scrollable list of selectable items.

It is intended for applications where users may need to select one or many values from a larger list while keeping the main interface clean and stable.

The component can be used for any item type, such as:

- categories
- tags
- labels
- users
- projects
- filters
- groups
- permissions
- issue types
- environments
- custom domain objects

---

## Core Features

- Multi-select support
- Search and filter while typing
- Optional custom item creation
- Compact selected-value summary
- Scrollable item list
- Optional row capabilities
- Configurable item actions
- Keyboard navigation
- Accessible interaction states
- Localizable visible text
- Adaptable to any existing design system

---

## Main Design Rule

Selected items must not be rendered inside the search or add input.

The search input is only used for:

- typing a search query
- filtering the list
- adding a custom value when custom item creation is enabled

Selected values are represented separately as a compact summary.

This keeps the control usable when many items are selected.

---

## Collapsed State

The collapsed control shows a stable, one-line summary of the current selection.

It must never grow vertically because many items are selected.

### Examples

    Categories                         None selected ˅
    Categories                         3 selected ˅
    Categories                         Alpha, Beta, Gamma ˅
    Categories                         Alpha, Beta, Gamma +12 ˅

### Collapsed Layout

    ┌────────────────────────────────────────────────────┐
    │ Categories                  Alpha, Beta, Gamma +12 ˅ │
    └────────────────────────────────────────────────────┘

### Collapsed Display Modes

| Mode | Description | Example |
|---|---|---|
| Count only | Shows only the selected count | `15 selected` |
| First labels with overflow | Shows the first selected labels and an overflow count | `Alpha, Beta, Gamma +12` |
| Chips with overflow | Shows a limited number of selected chips and an overflow count | `[Alpha] [Beta] [Gamma] +12` |

### Collapsed State Rules

- The control remains one line high.
- Selected values do not wrap.
- Long labels are truncated.
- Overflow is summarized with `+N`.
- The dropdown indicator remains aligned to the right.
- Clicking the field opens the popup.
- The selected summary is readable but visually quiet.
- The visual style should inherit from the host application.

---

## Expanded State

The expanded popup contains:

1. Search input
2. Optional selected summary row
3. Scrollable item list
4. Optional add-custom row

### Expanded Layout

    ┌────────────────────────────────────────────────────┐
    │ Search or add...                                   │
    ├────────────────────────────────────────────────────┤
    │ 15 selected              Show selected only  Clear │
    ├────────────────────────────────────────────────────┤
    │ ≡  ☑  Alpha                             Edit Delete│
    │ ≡  ☑  Beta                              Edit Delete│
    │ ≡  ☐  Gamma                             Edit Delete│
    │ ≡  ☐  Delta                             Edit Delete│
    │ ≡  ☐  Epsilon                           Edit Delete│
    ├────────────────────────────────────────────────────┤
    │ + Add "Typed value"                                │
    └────────────────────────────────────────────────────┘

---

## Search Input

The search input appears at the top of the popup.

It contains only the currently typed query.

### Behavior

- Typing filters the visible list immediately.
- Filtering is case-insensitive by default.
- Partial matches are supported.
- Clearing the search text restores the full list.
- Filtering does not change selected state.
- Filtering does not mutate the source list.
- The search input does not contain selected items.
- The search input does not contain selected chips.

---

## Selected Summary Row

The popup may include an optional selected summary row below the search input.

### Possible Contents

- selected count
- clear all action
- show selected only toggle
- compact selected preview
- overflow count

### Example

    15 selected              Show selected only  Clear

### Rules

- The selected summary row is optional.
- It should stay compact.
- It should not duplicate the full selected item list.
- It should help users manage large selections quickly.
- It should follow the visual style of the host application.

---

## Scrollable Item List

The dropdown list displays selectable rows.

Each row represents one item and shows its current selected state.

### Basic Row

    ☑  Item label

### Row With Optional Icon

    ☑  ◇  Item label

### Row With Optional Actions

    ☑  Item label                         Edit Delete

### Row With Optional Reorder Handle

    ≡  ☑  Item label                      Edit Delete

---

## Row Structure

Each row is built from flexible slots.

### Leading Slots

Optional elements before the label:

- reorder handle
- checkbox or selection indicator
- item icon
- avatar
- status marker

### Main Slot

The central item content:

- primary label
- optional secondary text
- optional metadata
- optional description

### Trailing Slots

Optional row actions:

- rename
- delete
- duplicate
- open
- favorite
- pin
- enable or disable
- custom action

---

## Optional Capabilities

The component supports optional capabilities.

These features are examples, not mandatory behavior.

A picker can have no optional row actions, one optional action, or many optional actions.

### Capability Examples

| Capability | Purpose |
|---|---|
| Custom item creation | Allows adding typed values |
| Reorder | Allows changing item order |
| Rename | Allows changing an item label |
| Delete | Allows removing an item |
| Clear all | Clears the current selection |
| Show selected only | Filters visible rows to selected items |
| Duplicate | Creates a copy of an item |
| Open | Opens details for an item |
| Favorite | Marks an item as preferred |
| Pin | Keeps an item visually prioritized |
| Custom actions | Allows app-specific commands |

### Capability Rules

- Disabled capabilities are not visible.
- Disabled capabilities cannot be triggered by keyboard shortcuts.
- The row layout adapts to the enabled capabilities.
- The component must remain useful even when no optional capabilities are enabled.
- Optional actions must not be hardcoded into the core component.
- Optional actions should be configurable by the host application.

---

## Reorder Behavior

Reordering is optional.

When enabled:

- a reorder handle is shown in the leading row area
- items can be reordered by dragging the handle
- selected state is preserved
- item identity is preserved
- the list order updates after reordering
- a reorder event can be emitted

When disabled:

- no reorder handle is shown
- drag reordering is unavailable

---

## Rename Behavior

Renaming is optional.

When enabled:

- a rename action may appear in the row actions
- renaming changes only the display label
- item identity remains stable
- selected state is preserved
- the selected summary updates immediately

### Validation Rules

- Empty names are rejected.
- Whitespace is trimmed.
- Duplicate labels may be rejected depending on configuration.
- Locked or read-only items cannot be renamed.

---

## Delete Behavior

Deleting is optional.

When enabled:

- a delete action may appear in the row actions
- deleting removes the item from the full list
- deleting removes the item from the visible filtered list
- deleting removes the item from the current selection
- the selected summary updates immediately

### Rules

- Locked or read-only items cannot be deleted.
- Destructive actions should be visually distinguishable.
- Confirmation may be used when appropriate.
- Deleting one item must not affect unrelated selected items.

---

## Custom Item Creation

Custom item creation is optional.

When enabled, the user can type a value that does not already exist and add it as a new item.

### Add Custom Row

    + Add "Typed value"

### Rules

- The typed value is trimmed.
- Empty values are rejected.
- Duplicate values are rejected or handled by configuration.
- Matching is case-insensitive by default.
- Original capitalization is preserved.
- The newly added item is selected automatically.
- The search input is cleared after adding.
- The popup may stay open after adding.

---

## Selection Behavior

Items can be selected or deselected from the dropdown list.

### Rules

- Multiple items can be selected.
- Selection state is independent from filter state.
- Filtering does not deselect hidden items.
- Reordering does not change selection.
- Renaming does not change selection.
- Deleting an item removes it from selection.
- The collapsed summary updates whenever selection changes.
- Selection should follow stable item identity, not the visible label.

---

## Empty States

The component should handle empty or filtered-out lists gracefully.

### No Items

    No items available

### No Search Results

    No matching items

### Add Available

    No matching items
    + Add "Typed value"

---

## Visual Adaptability

The component should adapt to the host application's existing design system.

It should not require a specific visual style, platform style, or brand style.

### Visual Principles

- compact but readable
- clear hierarchy
- stable layout
- consistent spacing
- predictable row alignment
- visible focus states
- restrained use of accent color
- subtle separation between regions
- no unnecessary visual noise
- no hardcoded design identity

### Style Integration Rules

- Colors should come from the host design system.
- Fonts should come from the host design system.
- Border radius should match the host design system.
- Spacing should match the host design system.
- Icons should match the host design system.
- Hover, focus, selected, disabled, and destructive states should match the host design system.
- The component should be themeable.
- The component should support light and dark themes when the host application supports them.

---

## Suggested Dimensions

These values are suggestions only and may be adapted to the host design system.

| Element | Suggested Size |
|---|---|
| Collapsed field height | 32-40px |
| Popup search input height | 32-40px |
| Row height | 32-40px |
| Popup max height | configurable |
| Row padding | based on the host spacing system |
| Popup width | at least the width of the collapsed field |

---

## Interaction States

The component should define clear states.

### Field States

- default
- hover
- focused
- disabled
- invalid
- open

### Row States

- default
- hover
- selected
- focused
- disabled
- locked

### Action States

- default
- hover
- focused
- pressed
- disabled
- destructive

---

## Keyboard Behavior

The component should be usable without a mouse.

| Key | Behavior |
|---|---|
| Enter | Opens popup, selects highlighted item, or adds typed custom item |
| Escape | Closes popup |
| Arrow Down | Moves focus to next visible row |
| Arrow Up | Moves focus to previous visible row |
| Space | Toggles selected state of focused row |
| Backspace | Edits search text |
| Delete | Triggers delete only when delete is enabled and the delete action or row command is focused |
| Tab | Moves focus through interactive elements |

### Keyboard Rules

- Backspace must not remove selected items from the collapsed summary.
- Typing should focus the search input when the popup is open.
- Keyboard shortcuts must respect enabled capabilities.
- Disabled actions must not be reachable as active commands.

---

## Accessibility

The component should expose meaningful accessible states and labels.

### Requirements

- selected and unselected states are exposed
- disabled and locked states are exposed
- search input has a clear accessible label
- row actions have accessible labels
- destructive actions are identified clearly
- keyboard navigation is complete
- visible focus indication is provided
- long labels remain understandable through tooltip, expansion, or accessible text

---

## Internationalization

Visible text should be configurable.

The component must not hardcode language-specific labels.

### Configurable Text Examples

- placeholder text
- no selection text
- selected count text
- add item text
- clear all text
- show selected only text
- no results text
- row action labels
- accessibility labels

### Language Rules

- long labels must truncate gracefully
- overflow summaries must work with translated text
- layout should not assume word lengths from one language
- right-to-left layouts should be supported where the host environment supports them

---

## Data Model Principles

Items should have stable identity.

The display label must not be used as identity.

### Item Properties

| Property | Purpose |
|---|---|
| ID | Stable unique identity |
| Value | Original domain value or payload |
| Label | Visible display text |
| Secondary text | Optional supporting text |
| Selected | Current selected state |
| Disabled | Cannot be selected |
| Locked | Cannot be modified |
| Custom | Created by the user |
| Metadata | Optional extra information |

### Rules

- IDs remain stable.
- Labels may change.
- Selection follows identity, not label.
- Reordering preserves identity.
- Filtering never mutates the source list.
- Optional actions operate on item identity.

---

## Recommended Default Configuration

| Option | Default |
|---|---|
| Multi-select | enabled |
| Search | enabled |
| Selection display | first labels with overflow |
| Maximum visible labels | 3 |
| Custom item creation | disabled unless needed |
| Reorder | disabled unless needed |
| Rename | disabled unless needed |
| Delete | disabled unless needed |
| Clear all | optional |
| Show selected only | optional |
| Popup max height | configurable |

---

## Acceptance Criteria

- The component supports selecting multiple items.
- The component supports searching and filtering while typing.
- The search input never contains selected items.
- The collapsed field remains one stable line high.
- Many selected items are summarized compactly.
- Selected state is visible in dropdown rows.
- Optional capabilities can be enabled or disabled independently.
- The component works without reorder, rename, or delete.
- The component works with custom row actions.
- The component supports custom item creation when enabled.
- Selection survives filtering.
- Selection survives reordering.
- Selection survives renaming.
- Deleting a selected item updates the selection.
- The component is not bound to one visual style.
- The component is not bound to one programming language.
- The component is not bound to one UI framework.
- The component is not bound to one platform.
- The component is not bound to one domain.
- The component is suitable for large item lists.
- The component is keyboard accessible.
- Visible text is configurable for localization.
