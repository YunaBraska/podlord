namespace Podlord.Core;

public enum PodlordErrorKind
{
    ReadFile,
    WriteFile,
    KubeconfigParse,
    EmptyKubeconfig,
    MissingHomeDirectory,
    MissingConfigDirectory,
    NoActiveSession,
    SessionNotFound,
    ContextNotFound,
    KubernetesConfig,
    KubernetesApi,
    UnsupportedResourceKind,
    InvalidInput,
    StoreLock
}

public sealed class PodlordException : Exception
{
    public PodlordException(PodlordErrorKind kind, string message, string nextAction, Exception? innerException = null)
        : base(message, innerException)
    {
        Kind = kind;
        NextAction = nextAction;
    }

    public PodlordErrorKind Kind { get; }

    public string NextAction { get; }

    public CommandError ToCommandError() => new(Message, NextAction);

    public static PodlordException ReadFile(string path, Exception inner)
    {
        return new PodlordException(
            PodlordErrorKind.ReadFile,
            $"Could not read {path}: {inner.Message}",
            "Check that the file exists and Podlord can read it.",
            inner);
    }

    public static PodlordException WriteFile(string path, Exception inner)
    {
        return new PodlordException(
            PodlordErrorKind.WriteFile,
            $"Could not write {path}: {inner.Message}",
            "Check app config directory permissions.",
            inner);
    }

    public static PodlordException KubeconfigParse(string path, string reason, Exception? inner = null)
    {
        return new PodlordException(
            PodlordErrorKind.KubeconfigParse,
            $"Could not parse kubeconfig from {path}: {reason}",
            "Validate the kubeconfig structure or import a different file.",
            inner);
    }

    public static PodlordException EmptyKubeconfig(string path)
    {
        return new PodlordException(
            PodlordErrorKind.EmptyKubeconfig,
            $"Kubeconfig {path} did not contain any contexts",
            "Import a kubeconfig that contains contexts.");
    }

    public static PodlordException MissingHomeDirectory()
    {
        return new PodlordException(
            PodlordErrorKind.MissingHomeDirectory,
            "No home directory is available",
            "Import a kubeconfig by explicit path.");
    }

    public static PodlordException MissingConfigDirectory()
    {
        return new PodlordException(
            PodlordErrorKind.MissingConfigDirectory,
            "No app config directory is available",
            "Start Podlord from a normal desktop session.");
    }

    public static PodlordException NoActiveSession()
    {
        return new PodlordException(
            PodlordErrorKind.NoActiveSession,
            "No active Kubernetes session is selected",
            "Import a kubeconfig and select a session.");
    }

    public static PodlordException SessionNotFound(string sessionId)
    {
        return new PodlordException(
            PodlordErrorKind.SessionNotFound,
            $"Session not found: {sessionId}",
            "Select or create a session first.");
    }

    public static PodlordException ContextNotFound(string sessionId, string contextId)
    {
        return new PodlordException(
            PodlordErrorKind.ContextNotFound,
            $"Context not found for session {sessionId}: {contextId}",
            "Re-import the kubeconfig for this session.");
    }

    public static PodlordException KubernetesConfig(string context, string reason, Exception? inner = null)
    {
        return new PodlordException(
            PodlordErrorKind.KubernetesConfig,
            $"Could not create Kubernetes client for context {context}: {reason}",
            "Validate the kubeconfig, auth plugin, and selected context.",
            inner);
    }

    public static PodlordException KubernetesApi(string context, string kind, string reason, Exception? inner = null)
    {
        return new PodlordException(
            PodlordErrorKind.KubernetesApi,
            $"Could not list {kind} in context {context}: {reason}",
            "Check cluster connectivity and RBAC for this context.",
            inner);
    }

    public static PodlordException UnsupportedResourceKind(string kind)
    {
        return new PodlordException(
            PodlordErrorKind.UnsupportedResourceKind,
            $"Unsupported resource kind: {kind}",
            "Open a supported MVP kind or add a typed handler for this kind.");
    }

    public static PodlordException InvalidInput(string message, string nextAction)
    {
        return new PodlordException(PodlordErrorKind.InvalidInput, message, nextAction);
    }
}

public sealed record CommandError(string Message, string NextAction);
