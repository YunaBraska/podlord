#include "session_store.h"
#include "cli_output.h"

#include <QCommandLineParser>
#include <QCoreApplication>

namespace {
int report(const podlord::Result<podlord::SessionCatalog>& result) {
    if (std::holds_alternative<podlord::Failure>(result)) {
        return podlord::cli::failure(std::get<podlord::Failure>(result));
    }
    return podlord::cli::output(podlord::catalogJson(std::get<podlord::SessionCatalog>(result), true));
}
int invalid(QString message) {
    return report(podlord::Failure{podlord::StoreError::InvalidInput, std::move(message)});
}
} // namespace

int main(int argc, char* argv[]) {
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("podlord-session");
    QCoreApplication::setApplicationVersion("0.1.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Native local session lifecycle. No cluster connections are made.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"profile", "Absolute isolated profile directory (required).", "path"});
    parser.addOption({"context", "Context identifier for create or snapshot.", "id"});
    parser.addOption({"name", "Explicit session name; omitted means unnamed.", "name"});
    parser.addOption({"namespace", "Namespace scope; repeat for several, omit for all.", "namespace"});
    parser.addPositionalArgument("command", "list | selection | create | snapshot ID | open ID | close ID | delete ID | rename ID NAME");
    if (!parser.parse(QCoreApplication::arguments())) return invalid(parser.errorText());
    if (parser.isSet("help")) parser.showHelp(0);
    if (parser.isSet("version")) parser.showVersion();
    if (parser.values("profile").size() != 1) return invalid("Exactly one --profile is required.");
    if (parser.values("context").size() > 1 || parser.values("name").size() > 1)
        return invalid("Context and name options cannot be repeated.");
    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) return invalid("A session command is required.");
    const QString& command = args.front();
    const bool configuration = command == "create" || command == "snapshot";
    if (!configuration && (parser.isSet("context") || parser.isSet("namespace") || parser.isSet("name")))
        return invalid("Configuration options apply only to create or snapshot.");
    if (command == "snapshot" && parser.isSet("name")) return invalid("Snapshot preserves the session name.");
    const podlord::SessionStore store(parser.value("profile"));
    if (command == "list" && args.size() == 1) return report(store.list());
    if (command == "selection" && args.size() == 1) return report(store.selection());
    if (command == "create" && args.size() == 1)
        return report(store.create({parser.value("context"), parser.values("namespace")}, parser.value("name")));
    if (args.size() == 2) {
        const QUuid id(args[1]);
        if (command == "snapshot")
            return report(store.snapshot(id, {parser.value("context"), parser.values("namespace")}));
        if (command == "open") return report(store.activate(id));
        if (command == "close") return report(store.close(id));
        if (command == "delete") return report(store.remove(id));
    }
    if (command == "rename" && args.size() == 3) return report(store.rename(QUuid(args[1]), args[2]));
    return invalid("Unknown command or invalid command arguments.");
}
