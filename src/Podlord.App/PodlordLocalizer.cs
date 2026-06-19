using System.Globalization;

namespace Podlord.App;

public sealed record LocaleOption(string Code, string NativeName, string Flag)
{
    public string DisplayName => Code == PodlordLocalizer.SystemLanguageCode
        ? $"{Flag} {NativeName}"
        : $"{Flag} {NativeName} ({Code})";
}

public static class PodlordLocalizer
{
    public const string SystemLanguageCode = "system";
    public const string DefaultLanguageCode = "en";

    private static readonly IReadOnlyDictionary<string, string> English = new Dictionary<string, string>(StringComparer.Ordinal)
    {
        ["nav.search"] = "Search",
        ["nav.resources"] = "RESOURCES",
        ["nav.graph"] = "GRAPH",
        ["nav.events"] = "EVENTS",
        ["nav.ports"] = "PORTS",
        ["nav.settings"] = "SETTINGS",
        ["sources.title"] = "KUBECONFIG SOURCES",
        ["sources.importPlaceholder"] = "file path, folder (~ ok, scanned recursively), or pasted kubeconfig YAML; empty opens file picker",
        ["sources.importFileTip"] = "Type a file or folder path (~ supported) then IMPORT; a folder is scanned recursively for kubeconfig files. Paste YAML directly, or leave empty to pick files.",
        ["action.import"] = "IMPORT",
        ["action.manage"] = "MANAGE...",
        ["action.save"] = "SAVE",
        ["action.delete"] = "DEL",
        ["action.duplicate"] = "DUPLICATE",
        ["action.add"] = "ADD",
        ["action.clear"] = "CLEAR",
        ["action.close"] = "CLOSE",
        ["action.port"] = "PORT",
        ["action.applyServerSide"] = "APPLY SERVER-SIDE",
        ["action.reset"] = "RESET",
        ["filters.title"] = "FILTERS",
        ["filters.problems"] = "Problems",
        ["filters.activity"] = "Activity",
        ["filters.savedFilters"] = "saved filters",
        ["filters.searchSaved"] = "search saved filters",
        ["filters.namePlaceholder"] = "filter name; save overwrites same name",
        ["filters.searchOrCustom"] = "search or custom; Enter adds value",
        ["filters.customValues"] = "Custom values",
        ["filters.syntaxHelp"] = "Text: contains, \"exact\", ~starts, ends~. Numbers: =5, >5, <5, =<5, =>5.",
        ["settings.title"] = "SETTINGS",
        ["settings.alerts"] = "Alerts",
        ["settings.sources"] = "Sources",
        ["settings.appearance"] = "Appearance",
        ["settings.graphics"] = "Graphics",
        ["settings.sync"] = "Sync",
        ["settings.workspace"] = "Workspace",
        ["settings.privacy"] = "Privacy",
        ["settings.diagnostics"] = "Diagnostics",
        ["settings.about"] = "About",
        ["about.supportHeading"] = "Support & love",
        ["about.projectHeading"] = "Project",
        ["about.starRepo"] = "★ Star the repo",
        ["about.githubRepo"] = "GitHub repository",
        ["about.createIssue"] = "Create issue",
        ["about.tagline"] = "A desktop console for the kubernetes-aware human.",
        ["about.sponsors"] = "GitHub Sponsors",
        ["about.bmc"] = "Buy Me a Coffee",
        ["about.kofi"] = "Ko-fi",
        ["about.liberapay"] = "Liberapay",
        ["ref.menuOpen"] = "Open in inspector",
        ["ref.menuCopy"] = "Copy reference",
        ["ref.notInCache"] = "(not in cache)",
        ["ref.triggerHint"] = "Long-press · ⌘/Ctrl+Click · Right-click → Open",
        ["copy.value"] = "Copy value",
        ["copy.key"] = "Copy key",
        ["copy.encoding"] = "Copy encoding",
        ["copy.rawBase64Secret"] = "Copy raw base64 secret value",
        ["copy.decodedSecret"] = "Copy decoded secret value",
        ["copy.decodedValue"] = "Copy decoded value",
        ["copy.rawBase64"] = "Copy raw base64 value",
        ["copy.secretValue"] = "Copy secret value",
        ["import.dialogTitle"] = "Import kubeconfig file(s)",
        ["import.kubeconfigFilter"] = "Kubeconfig",
        ["logs.container"] = "Container",
        ["logs.pauseTail"] = "Pause tail",
        ["logs.pauseTailHelp"] = "Pause automatic pod log tailing for the focused pod.",
        ["tooltip.closeSearch"] = "Close search",
        ["tooltip.previousMatch"] = "Previous match",
        ["tooltip.nextMatch"] = "Next match",
        ["tooltip.previousResource"] = "Previous resource",
        ["tooltip.nextResource"] = "Next resource",
        ["tooltip.portForward"] = "Port forward",
        ["tooltip.preparePortForward"] = "Prepare port forward",
        ["tooltip.removeSnapshot"] = "Remove snapshot",
        ["tooltip.resizeInspector"] = "Drag to resize inspector",
        ["tooltip.renameSource"] = "Rename source",
        ["tooltip.deleteSource"] = "Delete source",
        ["tooltip.filterProblems"] = "Show resources with degraded status, readiness gaps, restarts, or RBAC issues.",
        ["tooltip.filterActivity"] = "Show recent changes, events, and active/problem processes.",
        ["tooltip.editFilterName"] = "Edit name, then press Enter or the edit icon.",
        ["tooltip.renameFilter"] = "Rename filter",
        ["tooltip.deleteFilter"] = "Delete filter",
        ["tooltip.variantHelp"] = "Dark and light variants use the same layout and readable density.",
        ["tooltip.themeIntensityHelp"] = "Subtle keeps the app professional; medium and arcade increase glow and texture.",
        ["tooltip.portForwardColumn"] = "Port forward",
        ["settings.theme"] = "Theme",
        ["settings.variant"] = "Variant",
        ["settings.themeIntensity"] = "Theme intensity",
        ["settings.themeHelp"] = "Theme changes only affect visual treatment, not Kubernetes behavior.",
        ["settings.variantHelp"] = "Dark or light surface set.",
        ["settings.themeIntensityHelp"] = "Controls retro accent strength.",
        ["settings.language"] = "Language",
        ["settings.languageHelp"] = "Changes Podlord UI labels. Kubernetes object names remain exactly as the cluster reports them.",
        ["settings.radarWater"] = "Radar water",
        ["settings.radarWaterHelp"] = "Animated radar background. Disable it for the lowest GPU/CPU cost.",
        ["settings.radarWaterSpeed"] = "Radar water speed",
        ["settings.radarWaterSpeedHelp"] = "0 disables water; higher values move faster and still scale with API/min.",
        ["settings.animationIntensity"] = "Animation intensity",
        ["settings.animationHelp"] = "Controls alert blinking, motion, and decorative transitions.",
        ["settings.radarAutoFollow"] = "Auto-follow alerts",
        ["settings.radarAutoFollowHelp"] = "Moves radar to the newest visible alert at 100% zoom.",
        ["settings.radarScreensaver"] = "Radar screensaver",
        ["settings.graphicsHelp"] = "Performance preset plus zero animation and disabled water gives the lowest graphics overhead.",
        ["settings.inactiveBackgroundSync"] = "Inactive background sync",
        ["settings.requestHardLimit"] = "Request limit",
        ["settings.requestHardLimitHelp"] = "Optional ceiling for Kubernetes request starts. Leave none unless a cluster or VPN needs stricter protection.",
        ["settings.workspaceRestore"] = "Restore workspace on launch",
        ["settings.workspaceRestoreHelp"] = "Workspace restore reopens the last operational layout after restart.",
        ["settings.telemetry"] = "Telemetry enabled",
        ["settings.telemetryHelp"] = "Telemetry should remain disabled unless a future explicit privacy design is added.",
        ["settings.requestAuditTitle"] = "REQUEST AUDIT LOG (LAST 256)",
        ["alert.active"] = "Active",
        ["alert.type"] = "Type",
        ["alert.name"] = "Name",
        ["alert.description"] = "Description",
        ["alert.when"] = "When",
        ["alert.actions"] = "Actions",
        ["alert.sound"] = "Sound",
        ["alert.matchers"] = "Matchers",
        ["alert.orMatcher"] = "or matcher",
        ["alert.matcherBlockHelp"] = "all rows inside this block must match",
        ["alert.and"] = "and",
        ["alert.removeMatcherBlock"] = "Remove matcher block",
        ["alert.removeMatcher"] = "Remove matcher",
        ["alert.color"] = "Color",
        ["alert.noColor"] = "X",
        ["alert.statusColor"] = "STATUS",
        ["alert.animation"] = "Animation",
        ["alert.zoom"] = "Zoom",
        ["alert.previewZoom"] = "Preview zoom",
        ["alert.soundSearch"] = "search sounds",
        ["alert.previewSound"] = "Preview sound",
        ["alert.author"] = "Author",
        ["alert.source"] = "Source",
        ["alert.asset"] = "Asset",
        ["alert.selectFirst"] = "Select an alert first.",
        ["alert.noSoundSelected"] = "No sound selected.",
        ["alert.soundMissing"] = "Sound asset not found: {0}.",
        ["alert.previewingSound"] = "Previewing {0}.",
        ["alert.soundPreviewFailed"] = "Could not preview {0}: {1}",
        ["alert.openedSoundSource"] = "Opened sound source: {0}.",
        ["alert.openSoundSourceFailed"] = "Could not open sound source: {0}.",
        ["alert.noZoomTarget"] = "No radar target matched this alert.",
        ["alert.previewingZoom"] = "Previewing zoom for {0}/{1}.",
        ["alert.added"] = "Added custom alert. Adjust matchers and save.",
        ["alert.duplicated"] = "Duplicated alert '{0}'.",
        ["alert.deleted"] = "Deleted alert '{0}'.",
        ["alert.builtinNoDelete"] = "Built-in alerts can be disabled but not deleted.",
        ["alert.enabled"] = "Enabled alert '{0}'.",
        ["alert.disabled"] = "Disabled alert '{0}'.",
        ["alert.saved"] = "Saved alert rules.",
        ["audio.mute"] = "Mute app audio",
        ["audio.unmute"] = "Unmute app audio",
        ["audio.muted"] = "App audio muted.",
        ["audio.enabled"] = "App audio enabled.",
        ["inspector.overview"] = "Overview",
        ["inspector.yaml"] = "YAML",
        ["inspector.events"] = "Events",
        ["inspector.links"] = "Links",
        ["inspector.logs"] = "Logs",
        ["inspector.values"] = "Values",
        ["yaml.tabIndent"] = "TAB=indent",
        ["port.title"] = "PORT FORWARD",
        ["port.containerPort"] = "Container port",
        ["port.localPort"] = "Local port",
        ["port.containerPortTip"] = "Kubernetes pod container port or service port.",
        ["port.localPortTip"] = "Local computer port exposed on 127.0.0.1.",
        ["resource.noMatchingTitle"] = "No matching resources",
        ["resource.loadingTitle"] = "Loading resources",
        ["resource.emptyTitle"] = "No resources loaded",
        ["resource.noMatchingMessage"] = "Change or clear filters to bring cached resources back into view.",
        ["resource.loadingMessage"] = "Podlord is filling the cache through the Kubernetes request queue.",
        ["resource.emptyMessage"] = "Import a kubeconfig source or wait for the first cache fill.",
        ["source.waitingQueue"] = "Waiting for the current Kubernetes request queue.",
        ["source.waitingRefresh"] = "Waiting to refresh {0}; current Kubernetes request queue is still running.",
        ["source.noSession"] = "No Kubernetes session selected.",
        ["source.showingCached"] = "Showing cached resources for {0}; refreshing in the background.",
        ["source.loadingResources"] = "Loading resources for {0} through the Kubernetes request queue.",
        ["status.settingsSaved"] = "Settings saved."
    };

    private static readonly IReadOnlyDictionary<string, IReadOnlyDictionary<string, string>> Catalog =
        new Dictionary<string, IReadOnlyDictionary<string, string>>(StringComparer.OrdinalIgnoreCase)
        {
            ["en"] = English,
            ["de"] = WithEnglish("de", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Suche", ["nav.resources"] = "RESSOURCEN", ["nav.graph"] = "GRAPH", ["nav.events"] = "EREIGNISSE", ["nav.ports"] = "PORTS", ["nav.settings"] = "EINSTELLUNGEN",
                ["filters.title"] = "FILTER", ["filters.problems"] = "Probleme", ["filters.activity"] = "Aktivität", ["settings.title"] = "EINSTELLUNGEN", ["settings.language"] = "Sprache",
                ["resource.noMatchingTitle"] = "Keine passenden Ressourcen", ["resource.loadingTitle"] = "Ressourcen werden geladen", ["resource.emptyTitle"] = "Keine Ressourcen geladen", ["source.noSession"] = "Keine Kubernetes-Sitzung ausgewählt.", ["status.settingsSaved"] = "Einstellungen gespeichert."
            }),
            ["es"] = WithEnglish("es", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Buscar", ["nav.resources"] = "RECURSOS", ["nav.graph"] = "GRAFO", ["nav.events"] = "EVENTOS", ["nav.ports"] = "PUERTOS", ["nav.settings"] = "AJUSTES",
                ["filters.title"] = "FILTROS", ["filters.problems"] = "Problemas", ["filters.activity"] = "Actividad", ["settings.title"] = "AJUSTES", ["settings.language"] = "Idioma",
                ["resource.noMatchingTitle"] = "No hay recursos coincidentes", ["resource.loadingTitle"] = "Cargando recursos", ["source.noSession"] = "No hay sesión de Kubernetes seleccionada.", ["status.settingsSaved"] = "Ajustes guardados."
            }),
            ["fr"] = WithEnglish("fr", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Rechercher", ["nav.resources"] = "RESSOURCES", ["nav.graph"] = "GRAPHE", ["nav.events"] = "ÉVÉNEMENTS", ["nav.ports"] = "PORTS", ["nav.settings"] = "PARAMÈTRES",
                ["filters.title"] = "FILTRES", ["filters.problems"] = "Problèmes", ["filters.activity"] = "Activité", ["settings.title"] = "PARAMÈTRES", ["settings.language"] = "Langue",
                ["resource.noMatchingTitle"] = "Aucune ressource correspondante", ["resource.loadingTitle"] = "Chargement des ressources", ["source.noSession"] = "Aucune session Kubernetes sélectionnée.", ["status.settingsSaved"] = "Paramètres enregistrés."
            }),
            ["pt-BR"] = WithEnglish("pt-BR", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Buscar", ["nav.resources"] = "RECURSOS", ["nav.graph"] = "GRAFO", ["nav.events"] = "EVENTOS", ["nav.ports"] = "PORTAS", ["nav.settings"] = "CONFIGURAÇÕES",
                ["filters.title"] = "FILTROS", ["filters.problems"] = "Problemas", ["filters.activity"] = "Atividade", ["settings.title"] = "CONFIGURAÇÕES", ["settings.language"] = "Idioma",
                ["resource.noMatchingTitle"] = "Nenhum recurso encontrado", ["resource.loadingTitle"] = "Carregando recursos", ["source.noSession"] = "Nenhuma sessão Kubernetes selecionada.", ["status.settingsSaved"] = "Configurações salvas."
            }),
            ["it"] = WithEnglish("it", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Cerca", ["nav.resources"] = "RISORSE", ["nav.graph"] = "GRAFO", ["nav.events"] = "EVENTI", ["nav.ports"] = "PORTE", ["nav.settings"] = "IMPOSTAZIONI",
                ["filters.title"] = "FILTRI", ["filters.problems"] = "Problemi", ["filters.activity"] = "Attività", ["settings.title"] = "IMPOSTAZIONI", ["settings.language"] = "Lingua",
                ["resource.noMatchingTitle"] = "Nessuna risorsa corrispondente", ["resource.loadingTitle"] = "Caricamento risorse", ["source.noSession"] = "Nessuna sessione Kubernetes selezionata.", ["status.settingsSaved"] = "Impostazioni salvate."
            }),
            ["nl"] = WithEnglish("nl", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Zoeken", ["nav.resources"] = "RESOURCES", ["nav.graph"] = "GRAFIEK", ["nav.events"] = "GEBEURTENISSEN", ["nav.ports"] = "POORTEN", ["nav.settings"] = "INSTELLINGEN",
                ["filters.title"] = "FILTERS", ["filters.problems"] = "Problemen", ["filters.activity"] = "Activiteit", ["settings.title"] = "INSTELLINGEN", ["settings.language"] = "Taal",
                ["resource.noMatchingTitle"] = "Geen overeenkomende resources", ["resource.loadingTitle"] = "Resources laden", ["source.noSession"] = "Geen Kubernetes-sessie geselecteerd.", ["status.settingsSaved"] = "Instellingen opgeslagen."
            }),
            ["pl"] = WithEnglish("pl", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Szukaj", ["nav.resources"] = "ZASOBY", ["nav.graph"] = "GRAF", ["nav.events"] = "ZDARZENIA", ["nav.ports"] = "PORTY", ["nav.settings"] = "USTAWIENIA",
                ["filters.title"] = "FILTRY", ["filters.problems"] = "Problemy", ["filters.activity"] = "Aktywność", ["settings.title"] = "USTAWIENIA", ["settings.language"] = "Język",
                ["resource.noMatchingTitle"] = "Brak pasujących zasobów", ["resource.loadingTitle"] = "Ładowanie zasobów", ["source.noSession"] = "Nie wybrano sesji Kubernetes.", ["status.settingsSaved"] = "Ustawienia zapisane."
            }),
            ["ru"] = WithEnglish("ru", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Поиск", ["nav.resources"] = "РЕСУРСЫ", ["nav.graph"] = "ГРАФ", ["nav.events"] = "СОБЫТИЯ", ["nav.ports"] = "ПОРТЫ", ["nav.settings"] = "НАСТРОЙКИ",
                ["filters.title"] = "ФИЛЬТРЫ", ["filters.problems"] = "Проблемы", ["filters.activity"] = "Активность", ["settings.title"] = "НАСТРОЙКИ", ["settings.language"] = "Язык",
                ["resource.noMatchingTitle"] = "Нет подходящих ресурсов", ["resource.loadingTitle"] = "Загрузка ресурсов", ["source.noSession"] = "Сеанс Kubernetes не выбран.", ["status.settingsSaved"] = "Настройки сохранены."
            }),
            ["uk"] = WithEnglish("uk", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Пошук", ["nav.resources"] = "РЕСУРСИ", ["nav.graph"] = "ГРАФ", ["nav.events"] = "ПОДІЇ", ["nav.ports"] = "ПОРТИ", ["nav.settings"] = "НАЛАШТУВАННЯ",
                ["filters.title"] = "ФІЛЬТРИ", ["filters.problems"] = "Проблеми", ["filters.activity"] = "Активність", ["settings.title"] = "НАЛАШТУВАННЯ", ["settings.language"] = "Мова"
            }),
            ["tr"] = WithEnglish("tr", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Ara", ["nav.resources"] = "KAYNAKLAR", ["nav.graph"] = "GRAF", ["nav.events"] = "OLAYLAR", ["nav.ports"] = "PORTLAR", ["nav.settings"] = "AYARLAR",
                ["filters.title"] = "FİLTRELER", ["filters.problems"] = "Sorunlar", ["filters.activity"] = "Etkinlik", ["settings.title"] = "AYARLAR", ["settings.language"] = "Dil",
                ["resource.noMatchingTitle"] = "Eşleşen kaynak yok", ["resource.loadingTitle"] = "Kaynaklar yükleniyor", ["source.noSession"] = "Kubernetes oturumu seçilmedi.", ["status.settingsSaved"] = "Ayarlar kaydedildi."
            }),
            ["ar"] = WithEnglish("ar", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "بحث", ["nav.resources"] = "الموارد", ["nav.graph"] = "الرسم", ["nav.events"] = "الأحداث", ["nav.ports"] = "المنافذ", ["nav.settings"] = "الإعدادات",
                ["filters.title"] = "الفلاتر", ["filters.problems"] = "مشكلات", ["filters.activity"] = "نشاط", ["settings.title"] = "الإعدادات", ["settings.language"] = "اللغة",
                ["resource.noMatchingTitle"] = "لا توجد موارد مطابقة", ["resource.loadingTitle"] = "تحميل الموارد", ["source.noSession"] = "لم يتم اختيار جلسة Kubernetes.", ["status.settingsSaved"] = "تم حفظ الإعدادات."
            }),
            ["hi"] = WithEnglish("hi", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "खोज", ["nav.resources"] = "संसाधन", ["nav.graph"] = "ग्राफ", ["nav.events"] = "घटनाएं", ["nav.ports"] = "पोर्ट", ["nav.settings"] = "सेटिंग्स",
                ["filters.title"] = "फिल्टर", ["filters.problems"] = "समस्याएं", ["filters.activity"] = "गतिविधि", ["settings.title"] = "सेटिंग्स", ["settings.language"] = "भाषा",
                ["resource.noMatchingTitle"] = "कोई मिलते संसाधन नहीं", ["resource.loadingTitle"] = "संसाधन लोड हो रहे हैं", ["source.noSession"] = "कोई Kubernetes सत्र चयनित नहीं.", ["status.settingsSaved"] = "सेटिंग्स सहेजी गईं."
            }),
            ["bn"] = WithEnglish("bn", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "অনুসন্ধান", ["nav.resources"] = "রিসোর্স", ["nav.graph"] = "গ্রাফ", ["nav.events"] = "ইভেন্ট", ["nav.ports"] = "পোর্ট", ["nav.settings"] = "সেটিংস",
                ["filters.title"] = "ফিল্টার", ["filters.problems"] = "সমস্যা", ["filters.activity"] = "কার্যকলাপ", ["settings.title"] = "সেটিংস", ["settings.language"] = "ভাষা"
            }),
            ["pa"] = WithEnglish("pa", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "ਖੋਜ", ["nav.resources"] = "ਸਰੋਤ", ["nav.graph"] = "ਗ੍ਰਾਫ", ["nav.events"] = "ਘਟਨਾਵਾਂ", ["nav.ports"] = "ਪੋਰਟ", ["nav.settings"] = "ਸੈਟਿੰਗਾਂ",
                ["filters.title"] = "ਫਿਲਟਰ", ["filters.problems"] = "ਸਮੱਸਿਆਵਾਂ", ["filters.activity"] = "ਗਤੀਵਿਧੀ", ["settings.title"] = "ਸੈਟਿੰਗਾਂ", ["settings.language"] = "ਭਾਸ਼ਾ"
            }),
            ["ur"] = WithEnglish("ur", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "تلاش", ["nav.resources"] = "وسائل", ["nav.graph"] = "گراف", ["nav.events"] = "واقعات", ["nav.ports"] = "پورٹس", ["nav.settings"] = "ترتیبات",
                ["filters.title"] = "فلٹرز", ["filters.problems"] = "مسائل", ["filters.activity"] = "سرگرمی", ["settings.title"] = "ترتیبات", ["settings.language"] = "زبان"
            }),
            ["id"] = WithEnglish("id", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Cari", ["nav.resources"] = "SUMBER DAYA", ["nav.graph"] = "GRAF", ["nav.events"] = "PERISTIWA", ["nav.ports"] = "PORT", ["nav.settings"] = "PENGATURAN",
                ["filters.title"] = "FILTER", ["filters.problems"] = "Masalah", ["filters.activity"] = "Aktivitas", ["settings.title"] = "PENGATURAN", ["settings.language"] = "Bahasa",
                ["resource.noMatchingTitle"] = "Tidak ada sumber daya cocok", ["resource.loadingTitle"] = "Memuat sumber daya", ["source.noSession"] = "Sesi Kubernetes belum dipilih.", ["status.settingsSaved"] = "Pengaturan disimpan."
            }),
            ["vi"] = WithEnglish("vi", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Tìm", ["nav.resources"] = "TÀI NGUYÊN", ["nav.graph"] = "ĐỒ THỊ", ["nav.events"] = "SỰ KIỆN", ["nav.ports"] = "CỔNG", ["nav.settings"] = "CÀI ĐẶT",
                ["filters.title"] = "BỘ LỌC", ["filters.problems"] = "Sự cố", ["filters.activity"] = "Hoạt động", ["settings.title"] = "CÀI ĐẶT", ["settings.language"] = "Ngôn ngữ",
                ["resource.noMatchingTitle"] = "Không có tài nguyên phù hợp", ["resource.loadingTitle"] = "Đang tải tài nguyên", ["source.noSession"] = "Chưa chọn phiên Kubernetes.", ["status.settingsSaved"] = "Đã lưu cài đặt."
            }),
            ["th"] = WithEnglish("th", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "ค้นหา", ["nav.resources"] = "ทรัพยากร", ["nav.graph"] = "กราฟ", ["nav.events"] = "เหตุการณ์", ["nav.ports"] = "พอร์ต", ["nav.settings"] = "ตั้งค่า",
                ["filters.title"] = "ตัวกรอง", ["filters.problems"] = "ปัญหา", ["filters.activity"] = "กิจกรรม", ["settings.title"] = "ตั้งค่า", ["settings.language"] = "ภาษา"
            }),
            ["zh-Hans"] = WithEnglish("zh-Hans", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "搜索", ["nav.resources"] = "资源", ["nav.graph"] = "图谱", ["nav.events"] = "事件", ["nav.ports"] = "端口", ["nav.settings"] = "设置",
                ["filters.title"] = "过滤器", ["filters.problems"] = "问题", ["filters.activity"] = "活动", ["settings.title"] = "设置", ["settings.language"] = "语言",
                ["resource.noMatchingTitle"] = "没有匹配资源", ["resource.loadingTitle"] = "正在加载资源", ["source.noSession"] = "未选择 Kubernetes 会话。", ["status.settingsSaved"] = "设置已保存。"
            }),
            ["ja"] = WithEnglish("ja", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "検索", ["nav.resources"] = "リソース", ["nav.graph"] = "グラフ", ["nav.events"] = "イベント", ["nav.ports"] = "ポート", ["nav.settings"] = "設定",
                ["filters.title"] = "フィルター", ["filters.problems"] = "問題", ["filters.activity"] = "アクティビティ", ["settings.title"] = "設定", ["settings.language"] = "言語",
                ["resource.noMatchingTitle"] = "一致するリソースなし", ["resource.loadingTitle"] = "リソースを読み込み中", ["source.noSession"] = "Kubernetes セッションが選択されていません。", ["status.settingsSaved"] = "設定を保存しました。"
            }),
            ["ko"] = WithEnglish("ko", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "검색", ["nav.resources"] = "리소스", ["nav.graph"] = "그래프", ["nav.events"] = "이벤트", ["nav.ports"] = "포트", ["nav.settings"] = "설정",
                ["filters.title"] = "필터", ["filters.problems"] = "문제", ["filters.activity"] = "활동", ["settings.title"] = "설정", ["settings.language"] = "언어",
                ["resource.noMatchingTitle"] = "일치하는 리소스 없음", ["resource.loadingTitle"] = "리소스 로드 중", ["source.noSession"] = "Kubernetes 세션이 선택되지 않았습니다.", ["status.settingsSaved"] = "설정이 저장되었습니다."
            }),
            ["sv"] = WithEnglish("sv", new Dictionary<string, string>(StringComparer.Ordinal)
            {
                ["nav.search"] = "Sök", ["nav.resources"] = "RESURSER", ["nav.graph"] = "GRAF", ["nav.events"] = "HÄNDELSER", ["nav.ports"] = "PORTAR", ["nav.settings"] = "INSTÄLLNINGAR",
                ["filters.title"] = "FILTER", ["filters.problems"] = "Problem", ["filters.activity"] = "Aktivitet", ["settings.title"] = "INSTÄLLNINGAR", ["settings.language"] = "Språk",
                ["resource.noMatchingTitle"] = "Inga matchande resurser", ["resource.loadingTitle"] = "Laddar resurser", ["source.noSession"] = "Ingen Kubernetes-session vald.", ["status.settingsSaved"] = "Inställningar sparade."
            })
        };

    public static IReadOnlyList<LocaleOption> SupportedLocales { get; } =
    [
        new(SystemLanguageCode, "System language", "🌐"),
        new("en", "English", "🇬🇧"),
        new("zh-Hans", "简体中文", "🇨🇳"),
        new("hi", "हिन्दी", "🇮🇳"),
        new("es", "Español", "🇪🇸"),
        new("ar", "العربية", "🇸🇦"),
        new("bn", "বাংলা", "🇧🇩"),
        new("pt-BR", "Português do Brasil", "🇧🇷"),
        new("ru", "Русский", "🇷🇺"),
        new("ja", "日本語", "🇯🇵"),
        new("pa", "ਪੰਜਾਬੀ", "🇮🇳"),
        new("de", "Deutsch", "🇩🇪"),
        new("fr", "Français", "🇫🇷"),
        new("ur", "اردو", "🇵🇰"),
        new("id", "Bahasa Indonesia", "🇮🇩"),
        new("tr", "Türkçe", "🇹🇷"),
        new("ko", "한국어", "🇰🇷"),
        new("vi", "Tiếng Việt", "🇻🇳"),
        new("it", "Italiano", "🇮🇹"),
        new("pl", "Polski", "🇵🇱"),
        new("nl", "Nederlands", "🇳🇱"),
        new("sv", "Svenska", "🇸🇪")
    ];

    public static IReadOnlyList<string> LanguageOptionLabels { get; } =
        SupportedLocales.Select(option => option.DisplayName).ToArray();

    public static string LanguageOptionLabel(string? code)
    {
        var normalized = NormalizeLanguageSetting(code);
        return SupportedLocales.First(option => option.Code.Equals(normalized, StringComparison.OrdinalIgnoreCase)).DisplayName;
    }

    public static string LanguageCodeFromLabel(string? label)
    {
        return SupportedLocales
                   .FirstOrDefault(option => option.DisplayName.Equals(label, StringComparison.OrdinalIgnoreCase))
                   ?.Code
               ?? SystemLanguageCode;
    }

    public static string Text(string key, string? language)
    {
        var code = ResolveLanguage(language);
        return Catalog.TryGetValue(code, out var dictionary) && dictionary.TryGetValue(key, out var value)
            ? value
            : English.TryGetValue(key, out var fallback)
                ? fallback
                : key;
    }

    public static string NormalizeLanguageSetting(string? language)
    {
        if (string.IsNullOrWhiteSpace(language))
        {
            return SystemLanguageCode;
        }

        return SupportedLocales.Any(option => option.Code.Equals(language, StringComparison.OrdinalIgnoreCase))
            ? SupportedLocales.First(option => option.Code.Equals(language, StringComparison.OrdinalIgnoreCase)).Code
            : SystemLanguageCode;
    }

    public static string ResolveLanguage(string? language)
    {
        var normalized = NormalizeLanguageSetting(language);
        if (!normalized.Equals(SystemLanguageCode, StringComparison.OrdinalIgnoreCase))
        {
            return Catalog.ContainsKey(normalized) ? normalized : DefaultLanguageCode;
        }

        return ResolveCulture(CultureInfo.CurrentUICulture);
    }

    private static string ResolveCulture(CultureInfo culture)
    {
        foreach (var candidate in new[] { culture.Name, culture.TwoLetterISOLanguageName })
        {
            if (Catalog.ContainsKey(candidate))
            {
                return candidate;
            }
        }

        if (culture.TwoLetterISOLanguageName.Equals("zh", StringComparison.OrdinalIgnoreCase))
        {
            return "zh-Hans";
        }

        return DefaultLanguageCode;
    }

    private static IReadOnlyDictionary<string, string> WithEnglish(
        string languageCode,
        Dictionary<string, string> translations)
    {
        ApplyCommonUiPack(languageCode, translations);
        ApplyAlertUiPack(languageCode, translations);
        foreach (var (key, value) in English)
        {
            translations.TryAdd(key, value);
        }

        return translations;
    }

    private static void ApplyCommonUiPack(string languageCode, IDictionary<string, string> translations)
    {
        var packs = CommonUiPacks();
        if (!packs.TryGetValue(languageCode, out var values))
        {
            return;
        }

        var keys = CommonUiPackKeys();
        for (var index = 0; index < keys.Length && index < values.Length; index++)
        {
            translations.TryAdd(keys[index], values[index]);
        }
    }

    private static string[] CommonUiPackKeys() =>
    [
        "settings.sources",
        "settings.appearance",
        "settings.graphics",
        "settings.sync",
        "settings.workspace",
        "settings.privacy",
        "settings.diagnostics",
        "settings.theme",
        "settings.variant",
        "settings.themeIntensity",
        "settings.language",
        "settings.radarWater",
        "settings.radarWaterSpeed",
        "settings.animationIntensity",
        "settings.radarAutoFollow",
        "settings.radarScreensaver",
        "settings.inactiveBackgroundSync",
        "settings.requestHardLimit",
        "settings.workspaceRestore",
        "settings.telemetry",
        "settings.requestAuditTitle",
        "action.applyServerSide",
        "action.reset",
        "action.port",
        "inspector.overview",
        "inspector.yaml",
        "inspector.events",
        "inspector.links",
        "inspector.logs",
        "inspector.values",
        "port.title",
        "port.containerPort",
        "port.localPort"
    ];

    private static void ApplyAlertUiPack(string languageCode, IDictionary<string, string> translations)
    {
        if (!AlertUiPacks().TryGetValue(languageCode, out var values))
        {
            return;
        }

        foreach (var (key, value) in values)
        {
            translations.TryAdd(key, value);
        }
    }

    private static IReadOnlyDictionary<string, string> LabelPack(params string[] values)
    {
        var keys = new[]
        {
            "action.duplicate", "alert.active", "alert.type", "alert.name", "alert.description", "alert.when",
            "alert.actions", "alert.sound", "alert.matchers", "alert.orMatcher", "alert.matcherBlockHelp",
            "alert.and", "alert.color", "alert.noColor", "alert.statusColor", "alert.animation", "alert.zoom",
            "alert.previewZoom", "alert.soundSearch", "alert.previewSound", "alert.author", "alert.source", "alert.asset"
        };
        return keys
            .Zip(values, static (key, value) => new KeyValuePair<string, string>(key, value))
            .ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal);
    }

    private static IReadOnlyDictionary<string, IReadOnlyDictionary<string, string>> AlertUiPacks() =>
        new Dictionary<string, IReadOnlyDictionary<string, string>>(StringComparer.OrdinalIgnoreCase)
        {
            ["de"] = LabelPack("DUPLIZIEREN", "Aktiv", "Typ", "Name", "Beschreibung", "Wenn", "Aktionen", "Sound", "Matcher", "oder Matcher", "alle Zeilen in diesem Block müssen passen", "und", "Farbe", "KEINE", "STATUS", "Animation", "Zoom", "Zoom testen", "Sounds suchen", "Sound testen", "Autor", "Quelle", "Asset"),
            ["es"] = LabelPack("DUPLICAR", "Activo", "Tipo", "Nombre", "Descripción", "Cuando", "Acciones", "Sonido", "Reglas", "o regla", "todas las filas de este bloque deben coincidir", "y", "Color", "NINGUNO", "ESTADO", "Animación", "Zoom", "Probar zoom", "buscar sonidos", "Probar sonido", "Autor", "Fuente", "Archivo"),
            ["fr"] = LabelPack("DUPLIQUER", "Actif", "Type", "Nom", "Description", "Quand", "Actions", "Son", "Filtres", "ou filtre", "toutes les lignes de ce bloc doivent correspondre", "et", "Couleur", "AUCUNE", "ÉTAT", "Animation", "Zoom", "Tester zoom", "chercher sons", "Tester son", "Auteur", "Source", "Fichier"),
            ["pt-BR"] = LabelPack("DUPLICAR", "Ativo", "Tipo", "Nome", "Descrição", "Quando", "Ações", "Som", "Regras", "ou regra", "todas as linhas deste bloco devem combinar", "e", "Cor", "NENHUMA", "STATUS", "Animação", "Zoom", "Testar zoom", "buscar sons", "Testar som", "Autor", "Fonte", "Arquivo"),
            ["it"] = LabelPack("DUPLICA", "Attivo", "Tipo", "Nome", "Descrizione", "Quando", "Azioni", "Suono", "Matcher", "o matcher", "tutte le righe del blocco devono corrispondere", "e", "Colore", "NESSUNO", "STATO", "Animazione", "Zoom", "Prova zoom", "cerca suoni", "Prova suono", "Autore", "Fonte", "Asset"),
            ["nl"] = LabelPack("DUPLICEREN", "Actief", "Type", "Naam", "Beschrijving", "Wanneer", "Acties", "Geluid", "Matchers", "of matcher", "alle rijen in dit blok moeten matchen", "en", "Kleur", "GEEN", "STATUS", "Animatie", "Zoom", "Zoom testen", "geluiden zoeken", "Geluid testen", "Auteur", "Bron", "Asset"),
            ["pl"] = LabelPack("DUPLIKUJ", "Aktywny", "Typ", "Nazwa", "Opis", "Kiedy", "Akcje", "Dźwięk", "Dopasowania", "lub dopasowanie", "wszystkie wiersze w bloku muszą pasować", "i", "Kolor", "BRAK", "STATUS", "Animacja", "Zoom", "Test zoomu", "szukaj dźwięków", "Test dźwięku", "Autor", "Źródło", "Plik"),
            ["ru"] = LabelPack("ДУБЛИРОВАТЬ", "Активно", "Тип", "Имя", "Описание", "Когда", "Действия", "Звук", "Условия", "или условие", "все строки блока должны совпасть", "и", "Цвет", "НЕТ", "СТАТУС", "Анимация", "Масштаб", "Проверить масштаб", "поиск звуков", "Проверить звук", "Автор", "Источник", "Файл"),
            ["uk"] = LabelPack("ДУБЛЮВАТИ", "Активно", "Тип", "Назва", "Опис", "Коли", "Дії", "Звук", "Умови", "або умова", "усі рядки блоку мають збігатися", "і", "Колір", "НЕМАЄ", "СТАТУС", "Анімація", "Масштаб", "Тест масштабу", "шукати звуки", "Тест звуку", "Автор", "Джерело", "Файл"),
            ["tr"] = LabelPack("ÇOĞALT", "Aktif", "Tür", "Ad", "Açıklama", "Ne zaman", "Eylemler", "Ses", "Eşleşmeler", "veya eşleşme", "bu bloktaki tüm satırlar eşleşmeli", "ve", "Renk", "YOK", "DURUM", "Animasyon", "Yakınlaştırma", "Yakınlaştırmayı dene", "ses ara", "Sesi dene", "Yazar", "Kaynak", "Varlık"),
            ["ar"] = LabelPack("نسخ", "نشط", "النوع", "الاسم", "الوصف", "متى", "الإجراءات", "الصوت", "المطابقات", "أو مطابقة", "كل الصفوف داخل هذا المربع يجب أن تطابق", "و", "اللون", "لا شيء", "الحالة", "حركة", "تكبير", "اختبار التكبير", "بحث الأصوات", "اختبار الصوت", "المؤلف", "المصدر", "الملف"),
            ["hi"] = LabelPack("डुप्लिकेट", "सक्रिय", "प्रकार", "नाम", "विवरण", "कब", "क्रियाएँ", "ध्वनि", "मैचर", "या मैचर", "इस ब्लॉक की सभी पंक्तियाँ मेल खाएँ", "और", "रंग", "कोई नहीं", "स्थिति", "एनीमेशन", "ज़ूम", "ज़ूम टेस्ट", "ध्वनि खोजें", "ध्वनि टेस्ट", "लेखक", "स्रोत", "एसेट"),
            ["bn"] = LabelPack("ডুপ্লিকেট", "সক্রিয়", "ধরন", "নাম", "বিবরণ", "কখন", "অ্যাকশন", "শব্দ", "ম্যাচার", "বা ম্যাচার", "এই ব্লকের সব সারি মিলতে হবে", "এবং", "রং", "নেই", "স্ট্যাটাস", "অ্যানিমেশন", "জুম", "জুম পরীক্ষা", "শব্দ খুঁজুন", "শব্দ পরীক্ষা", "লেখক", "উৎস", "অ্যাসেট"),
            ["pa"] = LabelPack("ਡੁਪਲੀਕੇਟ", "ਸਰਗਰਮ", "ਕਿਸਮ", "ਨਾਮ", "ਵੇਰਵਾ", "ਕਦੋਂ", "ਕਾਰਵਾਈਆਂ", "ਧੁਨੀ", "ਮੈਚਰ", "ਜਾਂ ਮੈਚਰ", "ਇਸ ਬਲਾਕ ਦੀਆਂ ਸਾਰੀਆਂ ਕਤਾਰਾਂ ਮਿਲਣ", "ਅਤੇ", "ਰੰਗ", "ਕੋਈ ਨਹੀਂ", "ਹਾਲਤ", "ਐਨੀਮੇਸ਼ਨ", "ਜ਼ੂਮ", "ਜ਼ੂਮ ਟੈਸਟ", "ਧੁਨੀਆਂ ਖੋਜੋ", "ਧੁਨੀ ਟੈਸਟ", "ਲੇਖਕ", "ਸਰੋਤ", "ਐਸੈਟ"),
            ["ur"] = LabelPack("نقل", "فعال", "قسم", "نام", "تفصیل", "کب", "اعمال", "آواز", "میچر", "یا میچر", "اس بلاک کی سب قطاریں ملنی چاہئیں", "اور", "رنگ", "کوئی نہیں", "حالت", "اینیمیشن", "زوم", "زوم ٹیسٹ", "آوازیں تلاش", "آواز ٹیسٹ", "مصنف", "ماخذ", "اثاثہ"),
            ["id"] = LabelPack("DUPLIKAT", "Aktif", "Tipe", "Nama", "Deskripsi", "Saat", "Aksi", "Suara", "Pencocok", "atau pencocok", "semua baris dalam blok ini harus cocok", "dan", "Warna", "TIDAK ADA", "STATUS", "Animasi", "Zoom", "Uji zoom", "cari suara", "Uji suara", "Penulis", "Sumber", "Aset"),
            ["vi"] = LabelPack("NHÂN BẢN", "Đang bật", "Loại", "Tên", "Mô tả", "Khi", "Hành động", "Âm thanh", "Bộ khớp", "hoặc bộ khớp", "mọi dòng trong khối này phải khớp", "và", "Màu", "KHÔNG", "TRẠNG THÁI", "Hoạt ảnh", "Zoom", "Thử zoom", "tìm âm thanh", "Thử âm", "Tác giả", "Nguồn", "Tệp"),
            ["th"] = LabelPack("ทำซ้ำ", "เปิดใช้", "ชนิด", "ชื่อ", "คำอธิบาย", "เมื่อ", "การกระทำ", "เสียง", "ตัวจับคู่", "หรือตัวจับคู่", "ทุกแถวในบล็อกนี้ต้องตรงกัน", "และ", "สี", "ไม่มี", "สถานะ", "แอนิเมชัน", "ซูม", "ทดสอบซูม", "ค้นหาเสียง", "ทดสอบเสียง", "ผู้เขียน", "แหล่งที่มา", "ไฟล์"),
            ["zh-Hans"] = LabelPack("复制", "活动", "类型", "名称", "描述", "何时", "动作", "声音", "匹配器", "或匹配器", "此块中的所有行都必须匹配", "并且", "颜色", "无", "状态", "动画", "缩放", "测试缩放", "搜索声音", "测试声音", "作者", "来源", "资源"),
            ["ja"] = LabelPack("複製", "有効", "種別", "名前", "説明", "条件", "アクション", "音", "マッチャー", "またはマッチャー", "このブロックの全行が一致する必要があります", "かつ", "色", "なし", "状態", "アニメーション", "ズーム", "ズーム確認", "音を検索", "音を確認", "作者", "出典", "アセット"),
            ["ko"] = LabelPack("복제", "활성", "유형", "이름", "설명", "조건", "동작", "소리", "매처", "또는 매처", "이 블록의 모든 행이 일치해야 합니다", "그리고", "색상", "없음", "상태", "애니메이션", "줌", "줌 테스트", "소리 검색", "소리 테스트", "작성자", "출처", "자산"),
            ["sv"] = LabelPack("DUPLICERA", "Aktiv", "Typ", "Namn", "Beskrivning", "När", "Åtgärder", "Ljud", "Matchare", "eller matchare", "alla rader i blocket måste matcha", "och", "Färg", "INGEN", "STATUS", "Animation", "Zoom", "Testa zoom", "sök ljud", "Testa ljud", "Skapare", "Källa", "Tillgång")
        };

    private static IReadOnlyDictionary<string, string[]> CommonUiPacks() =>
        new Dictionary<string, string[]>(StringComparer.OrdinalIgnoreCase)
        {
            ["de"] = ["Quellen", "Darstellung", "Grafik", "Synchronisierung", "Arbeitsbereich", "Privatsphäre", "Diagnose", "Theme", "Variante", "Theme-Stärke", "Sprache", "Radarwasser", "Radarwasser-Tempo", "Animationsstärke", "Warnungen folgen", "Radar-Bildschirmschoner", "Sync im Hintergrund", "Anfragelimit", "Arbeitsbereich wiederherstellen", "Telemetrie", "ANFRAGEPROTOKOLL (LETZTE 256)", "SERVERSEITIG ANWENDEN", "ZURÜCKSETZEN", "PORT", "Übersicht", "YAML", "Ereignisse", "Links", "Logs", "Werte", "PORT-FORWARD", "Container-Port", "Lokaler Port"],
            ["es"] = ["Fuentes", "Apariencia", "Gráficos", "Sincronización", "Espacio", "Privacidad", "Diagnóstico", "Tema", "Variante", "Intensidad del tema", "Idioma", "Agua del radar", "Velocidad del agua", "Intensidad de animación", "Seguir alertas", "Salvapantallas radar", "Sync inactiva", "Límite de solicitudes", "Restaurar espacio", "Telemetría", "REGISTRO DE SOLICITUDES (ÚLTIMAS 256)", "APLICAR EN SERVIDOR", "RESTABLECER", "PUERTO", "Resumen", "YAML", "Eventos", "Enlaces", "Logs", "Valores", "REENVÍO DE PUERTO", "Puerto contenedor", "Puerto local"],
            ["fr"] = ["Sources", "Apparence", "Graphismes", "Synchro", "Espace de travail", "Confidentialité", "Diagnostic", "Thème", "Variante", "Intensité du thème", "Langue", "Eau radar", "Vitesse de l'eau", "Intensité animation", "Suivre les alertes", "Veille radar", "Synchro inactive", "Limite de requêtes", "Restaurer l'espace", "Télémétrie", "JOURNAL DES REQUÊTES (256 DERNIÈRES)", "APPLIQUER CÔTÉ SERVEUR", "RÉINITIALISER", "PORT", "Vue", "YAML", "Événements", "Liens", "Logs", "Valeurs", "REDIRECTION DE PORT", "Port conteneur", "Port local"],
            ["pt-BR"] = ["Fontes", "Aparência", "Gráficos", "Sincronização", "Área de trabalho", "Privacidade", "Diagnósticos", "Tema", "Variante", "Intensidade do tema", "Idioma", "Água do radar", "Velocidade da água", "Intensidade da animação", "Seguir alertas", "Proteção de tela radar", "Sync inativo", "Limite de requisições", "Restaurar área", "Telemetria", "LOG DE REQUISIÇÕES (ÚLTIMAS 256)", "APLICAR NO SERVIDOR", "REDEFINIR", "PORTA", "Visão geral", "YAML", "Eventos", "Links", "Logs", "Valores", "REDIRECIONAMENTO DE PORTA", "Porta do container", "Porta local"],
            ["it"] = ["Sorgenti", "Aspetto", "Grafica", "Sincronizzazione", "Workspace", "Privacy", "Diagnostica", "Tema", "Variante", "Intensità tema", "Lingua", "Acqua radar", "Velocità acqua", "Intensità animazione", "Segui avvisi", "Salvaschermo radar", "Sync inattiva", "Limite richieste", "Ripristina workspace", "Telemetria", "LOG RICHIESTE (ULTIME 256)", "APPLICA SERVER-SIDE", "REIMPOSTA", "PORTA", "Panoramica", "YAML", "Eventi", "Collegamenti", "Log", "Valori", "PORT FORWARD", "Porta container", "Porta locale"],
            ["nl"] = ["Bronnen", "Uiterlijk", "Grafisch", "Synchronisatie", "Werkruimte", "Privacy", "Diagnose", "Thema", "Variant", "Thema-intensiteit", "Taal", "Radarwater", "Watersnelheid", "Animatie-intensiteit", "Waarschuwingen volgen", "Radar screensaver", "Inactieve sync", "Aanvraaglimiet", "Werkruimte herstellen", "Telemetrie", "AANVRAAGLOG (LAATSTE 256)", "SERVER-SIDE TOEPASSEN", "RESETTEN", "POORT", "Overzicht", "YAML", "Gebeurtenissen", "Links", "Logs", "Waarden", "POORT FORWARD", "Containerpoort", "Lokale poort"],
            ["pl"] = ["Źródła", "Wygląd", "Grafika", "Synchronizacja", "Obszar roboczy", "Prywatność", "Diagnostyka", "Motyw", "Wariant", "Intensywność motywu", "Język", "Woda radaru", "Prędkość wody", "Intensywność animacji", "Śledź alerty", "Wygaszacz radaru", "Sync nieaktywny", "Limit żądań", "Przywróć obszar", "Telemetria", "DZIENNIK ŻĄDAŃ (OSTATNIE 256)", "ZASTOSUJ NA SERWERZE", "RESETUJ", "PORT", "Przegląd", "YAML", "Zdarzenia", "Linki", "Logi", "Wartości", "PRZEKIEROWANIE PORTU", "Port kontenera", "Port lokalny"],
            ["ru"] = ["Источники", "Вид", "Графика", "Синхронизация", "Рабочая область", "Приватность", "Диагностика", "Тема", "Вариант", "Интенсивность темы", "Язык", "Вода радара", "Скорость воды", "Интенсивность анимации", "Следить за тревогами", "Заставка радара", "Фоновая синхронизация", "Лимит запросов", "Восстановить область", "Телеметрия", "ЖУРНАЛ ЗАПРОСОВ (ПОСЛЕДНИЕ 256)", "ПРИМЕНИТЬ НА СЕРВЕРЕ", "СБРОС", "ПОРТ", "Обзор", "YAML", "События", "Связи", "Логи", "Значения", "ПРОБРОС ПОРТА", "Порт контейнера", "Локальный порт"],
            ["uk"] = ["Джерела", "Вигляд", "Графіка", "Синхронізація", "Робоча область", "Приватність", "Діагностика", "Тема", "Варіант", "Інтенсивність теми", "Мова", "Вода радара", "Швидкість води", "Інтенсивність анімації", "Стежити за тривогами", "Заставка радара", "Фонова синхронізація", "Ліміт запитів", "Відновити область", "Телеметрія", "ЖУРНАЛ ЗАПИТІВ (ОСТАННІ 256)", "ЗАСТОСУВАТИ НА СЕРВЕРІ", "СКИНУТИ", "ПОРТ", "Огляд", "YAML", "Події", "Зв'язки", "Логи", "Значення", "ПРОКИДАННЯ ПОРТУ", "Порт контейнера", "Локальний порт"],
            ["tr"] = ["Kaynaklar", "Görünüm", "Grafikler", "Senkron", "Çalışma alanı", "Gizlilik", "Tanı", "Tema", "Varyant", "Tema yoğunluğu", "Dil", "Radar suyu", "Su hızı", "Animasyon yoğunluğu", "Uyarıları izle", "Radar ekran koruyucu", "Pasif senkron", "İstek limiti", "Çalışma alanını geri yükle", "Telemetri", "İSTEK GÜNLÜĞÜ (SON 256)", "SUNUCUDA UYGULA", "SIFIRLA", "PORT", "Genel bakış", "YAML", "Olaylar", "Bağlantılar", "Loglar", "Değerler", "PORT YÖNLENDİRME", "Konteyner portu", "Yerel port"],
            ["ar"] = ["المصادر", "المظهر", "الرسوم", "المزامنة", "مساحة العمل", "الخصوصية", "التشخيص", "السمة", "النمط", "شدة السمة", "اللغة", "ماء الرادار", "سرعة الماء", "شدة الحركة", "متابعة التنبيهات", "حافظ شاشة الرادار", "مزامنة الخمول", "حد الطلبات", "استعادة المساحة", "القياس عن بعد", "سجل الطلبات (آخر 256)", "تطبيق على الخادم", "إعادة ضبط", "منفذ", "نظرة عامة", "YAML", "الأحداث", "الروابط", "السجلات", "القيم", "تمرير المنفذ", "منفذ الحاوية", "المنفذ المحلي"],
            ["hi"] = ["स्रोत", "रूप", "ग्राफिक्स", "सिंक", "वर्कस्पेस", "गोपनीयता", "निदान", "थीम", "वेरिएंट", "थीम तीव्रता", "भाषा", "रडार पानी", "पानी गति", "एनीमेशन तीव्रता", "अलर्ट फॉलो", "रडार स्क्रीनसेवर", "निष्क्रिय सिंक", "रिक्वेस्ट सीमा", "वर्कस्पेस पुनर्स्थापित", "टेलीमेट्री", "रिक्वेस्ट लॉग (अंतिम 256)", "सर्वर पर लागू", "रीसेट", "पोर्ट", "अवलोकन", "YAML", "घटनाएं", "लिंक", "लॉग", "मान", "पोर्ट फॉरवर्ड", "कंटेनर पोर्ट", "लोकल पोर्ट"],
            ["bn"] = ["উৎস", "চেহারা", "গ্রাফিক্স", "সিঙ্ক", "ওয়ার্কস্পেস", "গোপনীয়তা", "ডায়াগনস্টিক", "থিম", "ভ্যারিয়েন্ট", "থিম তীব্রতা", "ভাষা", "রাডার পানি", "পানির গতি", "অ্যানিমেশন তীব্রতা", "সতর্কতা অনুসরণ", "রাডার স্ক্রিনসেভার", "নিষ্ক্রিয় সিঙ্ক", "রিকোয়েস্ট সীমা", "ওয়ার্কস্পেস ফিরিয়ে আনুন", "টেলিমেট্রি", "রিকোয়েস্ট লগ (শেষ ২৫৬)", "সার্ভারে প্রয়োগ", "রিসেট", "পোর্ট", "ওভারভিউ", "YAML", "ইভেন্ট", "লিংক", "লগ", "মান", "পোর্ট ফরওয়ার্ড", "কন্টেইনার পোর্ট", "লোকাল পোর্ট"],
            ["pa"] = ["ਸਰੋਤ", "ਦਿੱਖ", "ਗ੍ਰਾਫਿਕਸ", "ਸਿੰਕ", "ਵਰਕਸਪੇਸ", "ਪਰਦੇਦਾਰੀ", "ਡਾਇਗਨੋਸਟਿਕ", "ਥੀਮ", "ਵੈਰੀਅੰਟ", "ਥੀਮ ਤੀਬਰਤਾ", "ਭਾਸ਼ਾ", "ਰਡਾਰ ਪਾਣੀ", "ਪਾਣੀ ਗਤੀ", "ਐਨੀਮੇਸ਼ਨ ਤੀਬਰਤਾ", "ਚੇਤਾਵਨੀ ਪਾਲੋ", "ਰਡਾਰ ਸਕ੍ਰੀਨਸੇਵਰ", "ਨਿਸ਼ਕ੍ਰਿਯ ਸਿੰਕ", "ਬੇਨਤੀ ਸੀਮਾ", "ਵਰਕਸਪੇਸ ਰੀਸਟੋਰ", "ਟੈਲੀਮੈਟਰੀ", "ਬੇਨਤੀ ਲਾਗ (ਆਖਰੀ 256)", "ਸਰਵਰ ਤੇ ਲਾਗੂ", "ਰੀਸੈੱਟ", "ਪੋਰਟ", "ਝਲਕ", "YAML", "ਘਟਨਾਵਾਂ", "ਲਿੰਕ", "ਲਾਗ", "ਮੁੱਲ", "ਪੋਰਟ ਫਾਰਵਰਡ", "ਕੰਟੇਨਰ ਪੋਰਟ", "ਲੋਕਲ ਪੋਰਟ"],
            ["ur"] = ["ذرائع", "ظاہری شکل", "گرافکس", "سنک", "ورک اسپیس", "رازداری", "تشخیص", "تھیم", "قسم", "تھیم شدت", "زبان", "رڈار پانی", "پانی رفتار", "اینیمیشن شدت", "الرٹ فالو", "رڈار اسکرین سیور", "غیر فعال سنک", "درخواست حد", "ورک اسپیس بحال", "ٹیلی میٹری", "درخواست لاگ (آخری 256)", "سرور پر لاگو", "ری سیٹ", "پورٹ", "جائزہ", "YAML", "واقعات", "روابط", "لاگز", "اقدار", "پورٹ فارورڈ", "کنٹینر پورٹ", "لوکل پورٹ"],
            ["id"] = ["Sumber", "Tampilan", "Grafik", "Sinkron", "Ruang kerja", "Privasi", "Diagnostik", "Tema", "Varian", "Intensitas tema", "Bahasa", "Air radar", "Kecepatan air", "Intensitas animasi", "Ikuti peringatan", "Screensaver radar", "Sinkron pasif", "Batas permintaan", "Pulihkan ruang kerja", "Telemetri", "LOG PERMINTAAN (256 TERAKHIR)", "TERAPKAN DI SERVER", "RESET", "PORT", "Ringkasan", "YAML", "Peristiwa", "Tautan", "Log", "Nilai", "PORT FORWARD", "Port container", "Port lokal"],
            ["vi"] = ["Nguồn", "Giao diện", "Đồ họa", "Đồng bộ", "Không gian làm việc", "Riêng tư", "Chẩn đoán", "Chủ đề", "Biến thể", "Cường độ chủ đề", "Ngôn ngữ", "Nước radar", "Tốc độ nước", "Cường độ hoạt ảnh", "Theo dõi cảnh báo", "Bảo vệ màn hình radar", "Đồng bộ khi rảnh", "Giới hạn yêu cầu", "Khôi phục workspace", "Telemetry", "NHẬT KÝ YÊU CẦU (256 GẦN NHẤT)", "ÁP DỤNG PHÍA MÁY CHỦ", "ĐẶT LẠI", "CỔNG", "Tổng quan", "YAML", "Sự kiện", "Liên kết", "Nhật ký", "Giá trị", "CHUYỂN TIẾP CỔNG", "Cổng container", "Cổng cục bộ"],
            ["th"] = ["แหล่งข้อมูล", "หน้าตา", "กราฟิก", "ซิงก์", "พื้นที่ทำงาน", "ความเป็นส่วนตัว", "วินิจฉัย", "ธีม", "รูปแบบ", "ความเข้มธีม", "ภาษา", "น้ำเรดาร์", "ความเร็วน้ำ", "ความเข้มแอนิเมชัน", "ติดตามการเตือน", "สกรีนเซฟเวอร์เรดาร์", "ซิงก์เมื่อว่าง", "จำกัดคำขอ", "กู้คืนพื้นที่ทำงาน", "เทเลเมทรี", "บันทึกคำขอ (256 ล่าสุด)", "ใช้บนเซิร์ฟเวอร์", "รีเซ็ต", "พอร์ต", "ภาพรวม", "YAML", "เหตุการณ์", "ลิงก์", "ล็อก", "ค่า", "ส่งต่อพอร์ต", "พอร์ตคอนเทนเนอร์", "พอร์ตเครื่อง"],
            ["zh-Hans"] = ["来源", "外观", "图形", "同步", "工作区", "隐私", "诊断", "主题", "变体", "主题强度", "语言", "雷达水面", "水面速度", "动画强度", "跟随告警", "雷达屏保", "非活动同步", "请求限制", "恢复工作区", "遥测", "请求审计日志（最近 256）", "服务器端应用", "重置", "端口", "概览", "YAML", "事件", "链接", "日志", "值", "端口转发", "容器端口", "本地端口"],
            ["ja"] = ["ソース", "外観", "グラフィック", "同期", "ワークスペース", "プライバシー", "診断", "テーマ", "バリアント", "テーマ強度", "言語", "レーダー水面", "水面速度", "アニメーション強度", "アラート追従", "レーダースクリーンセーバー", "非アクティブ同期", "リクエスト制限", "ワークスペース復元", "テレメトリ", "リクエスト監査ログ（最新 256）", "サーバー側で適用", "リセット", "ポート", "概要", "YAML", "イベント", "リンク", "ログ", "値", "ポートフォワード", "コンテナポート", "ローカルポート"],
            ["ko"] = ["소스", "모양", "그래픽", "동기화", "작업공간", "개인정보", "진단", "테마", "변형", "테마 강도", "언어", "레이더 물", "물 속도", "애니메이션 강도", "알림 따라가기", "레이더 화면보호기", "비활성 동기화", "요청 제한", "작업공간 복원", "텔레메트리", "요청 감사 로그(최근 256)", "서버 측 적용", "초기화", "포트", "개요", "YAML", "이벤트", "링크", "로그", "값", "포트 포워드", "컨테이너 포트", "로컬 포트"],
            ["sv"] = ["Källor", "Utseende", "Grafik", "Synk", "Arbetsyta", "Integritet", "Diagnostik", "Tema", "Variant", "Temastyrka", "Språk", "Radarvatten", "Vattenhastighet", "Animationsstyrka", "Följ varningar", "Radar skärmsläckare", "Inaktiv synk", "Begäransgräns", "Återställ arbetsyta", "Telemetri", "BEGÄRANSLOGG (SENASTE 256)", "TILLÄMPA PÅ SERVERN", "ÅTERSTÄLL", "PORT", "Översikt", "YAML", "Händelser", "Länkar", "Loggar", "Värden", "PORTVIDAREBEFORDRAN", "Containerport", "Lokal port"]
        };
}
