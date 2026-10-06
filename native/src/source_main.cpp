#include "kubeconfig_store.h"
#include "cli_output.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonArray>

using podlord::cli::output;
using podlord::cli::failure;
int main(int argc, char* argv[]) {
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("podlord-source");
    QCommandLineParser parser;
    parser.setApplicationDescription("Import owned kubeconfig snapshots. No authentication or cluster connections.");
    parser.addHelpOption();
    parser.addOption({"profile", "Absolute isolated profile directory (required).", "path"});
    parser.addPositionalArgument("command", "list | import FILE | import-path FILE_OR_FOLDER | rename CONTEXT DISPLAY_NAME");
    if (!parser.parse(QCoreApplication::arguments())) return failure({podlord::StoreError::InvalidInput, parser.errorText()});
    if (parser.isSet("help")) parser.showHelp(0);
    if (parser.values("profile").size() != 1) return failure({podlord::StoreError::InvalidInput, "Exactly one --profile is required."});
    const podlord::KubeconfigStore store(parser.value("profile"));
    const auto args = parser.positionalArguments();
    if (args.size() == 3 && args[0] == "rename") {
        const auto result = store.renameContext(args[1], args[2]);
        if (const auto* error = std::get_if<podlord::Failure>(&result)) return failure(*error);
        return output(podlord::sourceJson(std::get<podlord::SourceSnapshot>(result)));
    }
    if (args.size() == 2 && args[0] == "import-path") {
        const auto result = store.importPath(args[1]);
        if (const auto* error = std::get_if<podlord::Failure>(&result)) return failure(*error);
        const auto& report = std::get<podlord::SourceImportReport>(result);
        return output(podlord::sourceImportJson(report), report.errors.isEmpty() ? 0 : 1);
    }
    if (args.size() == 2 && args[0] == "import") {
        const auto result = store.importFile(args[1]);
        if (std::holds_alternative<podlord::Failure>(result)) return failure(std::get<podlord::Failure>(result));
        return output(podlord::sourceJson(std::get<podlord::SourceSnapshot>(result)));
    }
    if (args.size() == 1 && args[0] == "list") {
        const auto result = store.list();
        if (std::holds_alternative<podlord::Failure>(result)) return failure(std::get<podlord::Failure>(result));
        QJsonArray sources;
        QJsonArray errors;
        const auto& catalog = std::get<podlord::SourceCatalog>(result);
        for (const auto& source : catalog.sources) sources.append(podlord::sourceJson(source));
        for (const auto& issue : catalog.errors) errors.append(QJsonObject{{"ownedPath", issue.ownedPath},
            {"code", podlord::errorName(issue.failure.code)}, {"message", issue.failure.message}});
        return output({{"sources", sources}, {"errors", errors}});
    }
    return failure({podlord::StoreError::InvalidInput, "Expected list, import FILE, import-path FILE_OR_FOLDER, or rename CONTEXT DISPLAY_NAME."});
}
