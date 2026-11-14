# How Do Age And UID Filters Preserve Existing Views?

Status: accepted, 2026-10-06.

The resource table owns one UID column and the existing creation-timestamp column.
Field filters, global search, sorting, copying and column controls use that model,
not a second resource catalog or an inspector request.

Age comparisons use nonnegative elapsed whole seconds from the cached creation
timestamp. Bare values mean seconds. Supported units are ms/s/m/h/d/w, their
reference aliases, and compound durations such as 1h30m. Range comparisons use
AND; exact numeric alternatives use OR. Quoted display text and patterns remain
available. Missing, malformed and future timestamps do not match numeric terms.
Malformed numeric expressions fail explicitly instead of accepting a partial
duration. Regular background publication reevaluates age filters; no new timer
or paint-time work is introduced.

UID is hidden by default, with a 280-pixel default width. Layout schema version 3
records the additional column. Version-2 resource layouts containing exactly the
previous fifteen columns retain order, widths, visibility and pins and append a
hidden UID. Loading does not rewrite the file. Saving uses the existing atomic,
conflict-aware store. Invalid or unsupported records remain intact and fail
explicitly. Version-1 six-column migration remains supported.

Verification: public model duration/UID scenarios, real Qt filter dialogs in Basic
and Fusion, preset reuse, restart restoration and old-layout read/save/rejection.
The Kubernetes HTTP server in those UI tests replaces only the external API;
stores, parsing, filtering, scheduling and rendering are the shipped code.
