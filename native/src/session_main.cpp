#include "session_store.h"
#include "cli_output.h"

#include <QCommandLineParser>
#include <QCoreApplication>

namespace {
int report(const podlord::Result<podlord::SessionCatalog>& result) {
    if (std::holds_alternative<podlord::Failure>(result)) {
        return podlord::cli::failure(std::get<podlord::Failure>(result));
    }
    return podlord::cli::output(podlord::catalogJson(std::get<podlord::SessionCatalog>(result)));
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
    parser.setApplicationDescription("Native local tab lifecycle. No cluster connections are made.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"profile", "Absolute isolated profile directory (required).", "path"});
    parser.addOption({"context", "Imported context identifier for create.", "id"});
    parser.addPositionalArgument("command", "list | create --context ID | open ID | close ID");
    if (!parser.parse(QCoreApplication::arguments())) return invalid(parser.errorText());
    if (parser.isSet("help")) parser.showHelp(0);
    if (parser.isSet("version")) parser.showVersion();
    if (parser.values("profile").size() != 1) return invalid("Exactly one --profile is required.");
    if (parser.values("context").size() > 1)
        return invalid("Context and name options cannot be repeated.");
    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) return invalid("A session command is required.");
    const QString& command = args.front();
    const bool configuration = command == "create";
    if (!configuration && parser.isSet("context"))
        return invalid("Configuration options apply only to create or snapshot.");
    const podlord::SessionStore store(parser.value("profile"));
    if (command == "list" && args.size() == 1) return report(store.list());
    if (command == "create" && args.size() == 1)
        return report(store.openTab(parser.value("context")));
    if (args.size() == 2) {
        const QUuid id(args[1]);
        if (command == "open") return report(store.activate(id));
        if (command == "close") return report(store.close(id));
    }
    return invalid("Unknown command or invalid command arguments.");
}
