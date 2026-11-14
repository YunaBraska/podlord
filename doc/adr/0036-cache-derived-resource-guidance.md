# Wie werden bekannte Kubernetes-Fehler sicher erklärt?

Status: accepted
Date: 2026-10-05

## Context

Der native Inspector soll bekannte Probleme wie ein IDE-Inspektionshinweis erklären, ohne die cachebasierte UI, Clusterberechtigungen oder die Bestätigungsgrenzen mutierender Aktionen zu umgehen.

## Decision

Eine reine Funktion leitet höchstens acht feste Hinweise aus dem akzeptierten Kubernetes-Detail-Snapshot ab. Regelowner ist `resource_guidance`; Workspace reicht ausschließlich seinen bestehenden, UID-gebundenen Detail-Cache ein. Es gibt keinen weiteren Store, keine Hintergrundabfrage und keinen Diagnose-Timer.

Die API-Version und Kind grenzen jede Regel ein. Konkrete Statuscodes und Container-Scope sind Beobachtung; Erklärungen benennen mögliche Prüfschritte statt einer unbelegten Ursache. Historische Terminierungen sind ausdrücklich historisch. Freie Statusmeldungen und Secret-Daten werden nicht in Hinweise kopiert. Texte werden als PlainText dargestellt. Dokumentationslinks sind feste offizielle Kubernetes-URLs und werden nur nach Benutzeraktion extern geöffnet.

Die Diagnosegruppe beginnt eingeklappt. Detail-Controls werden erst beim Aufklappen instanziiert und beim Verlassen des Overview freigegeben. Die bestehenden Inspector-Frische-/Fehleranzeigen gelten weiter; fehlende Hinweise bedeuten nicht „gesund“.

Es gibt keine automatische Reparatur. Ein späterer mutierender Quick Fix muss dieselben Sicherheits- und Frischegrenzen wie manuelles YAML-Apply durchlaufen und darf nie eine bloße Diagnose in Schreibautorisierung verwandeln.

## Evidence and consequences

`native.guidance.*` ordnet bekannte, unbekannte, fehlerhafte und historische Statusdaten öffentlichen Ergebnissen zu. UI-Fälle verwenden den echten Workspace/Inspector gegen einen minimalen externen Kubernetes-HTTP-Testserver: Aufklappen ohne Requests, aktualisierte Hinweise nach explizitem Detail-Refresh, erhaltene Cache-Hinweise mit sichtbarem Lesefehler und erhaltener aktiver YAML-Entwurf beim Speichern eines Session-Snapshots.

Diese Hinweise ersetzen weder vollständige Ursachenanalyse noch Cluster-/Berechtigungsprüfung. Der Katalog ist bewusst kein vollständiges Diagnosesystem; keine Persistenz oder Plugin-Abstraktion ist nötig.

## References

- [Pod lifecycle and restart backoff](https://kubernetes.io/docs/concepts/workloads/pods/pod-lifecycle/)
- [Container image pull failures](https://kubernetes.io/docs/concepts/containers/images/)
- [Pod debugging](https://kubernetes.io/docs/tasks/debug/debug-application/debug-pods/)
- [Memory limits and termination](https://kubernetes.io/docs/tasks/configure-pod-container/assign-memory-resource/)
- [Node conditions](https://kubernetes.io/docs/concepts/architecture/nodes/)
- [Node pressure and eviction](https://kubernetes.io/docs/concepts/scheduling-eviction/node-pressure-eviction/)
- [PersistentVolumeClaim phases and binding](https://kubernetes.io/docs/concepts/storage/persistent-volumes/)
- [Deployment progress deadline](https://kubernetes.io/docs/concepts/workloads/controllers/deployment/)
