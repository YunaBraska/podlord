import QtQml

QtObject {
    // The result and its resource identity are sampled together when either changes.
    property var update: ({ scope: "", result: ({}) })
    property var lastAnswer: ({})
    property string lastScope: ""
    readonly property var shown: {
        const result = update.result || ({})
        if (result.summary !== undefined) return result
        return result.failed !== true && lastScope === update.scope && lastAnswer.summary !== undefined
            ? lastAnswer : result
    }
    onUpdateChanged: {
        const result = update.result || ({})
        if (result.summary !== undefined) {
            lastAnswer = result
            lastScope = update.scope
        } else if (result.failed === true || lastScope !== update.scope) {
            lastAnswer = ({})
            lastScope = ""
        }
    }
}
